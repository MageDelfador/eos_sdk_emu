/*
 * Copyright (C) 2020 Nemirtingas
 * This file is part of the Nemirtingas's Epic Emulator
 *
 * The Nemirtingas's Epic Emulator is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * The Nemirtingas's Epic Emulator is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the Nemirtingas's Epic Emulator; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "eossdk_sessions.h"
#include "eossdk_platform.h"
#include "eos_client_api.h"
#include "eos_memory.h"
#include "settings.h"

namespace sdk
{

Session_Infos_pb EOSSDK_ActiveSession::resolve_live_infos() const
{
    session_state_t* live = nullptr;
    if (!_infos.session_id().empty())
        live = GetEOS_Sessions().get_primary_session_for_id(_infos.session_id());
    if (live == nullptr)
        live = GetEOS_Sessions().get_session_by_name(_session_name);
    if (live == nullptr)
        return _infos;

    Session_Infos_pb infos = live->infos;
    std::string const local_id = Settings::Inst().productuserid->to_string();

    if (live->state == session_state_t::state_e::joined &&
        infos.state() != utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress))
    {
        infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress));
    }
    else if (live->state == session_state_t::state_e::joining &&
        infos.state() == utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_NoSession))
    {
        infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending));
    }

    if (live->state == session_state_t::state_e::joined)
    {
        bool local_registered = false;
        for (auto const& player : infos.registered_players())
        {
            if (player == local_id)
            {
                local_registered = true;
                break;
            }
        }
        if (!local_registered)
            *infos.add_registered_players() = local_id;
    }

    return infos;
}

/**
 * Representation of an existing session some local players are actively involved in (via Create/Join)
 */

 /**
  * EOS_ActiveSession_CopyInfo is used to immediately retrieve a copy of active session information
  * If the call returns an EOS_Success result, the out parameter, OutActiveSessionInfo, must be passed to EOS_ActiveSession_Info_Release to release the memory associated with it.
  *
  * @param Options Structure containing the input parameters
  * @param OutActiveSessionInfo Out parameter used to receive the EOS_ActiveSession_Info structure.
  *
  * @return EOS_Success if the information is available and passed out in OutActiveSessionInfo
  *         EOS_InvalidParameters if you pass a null pointer for the out parameter
  *         EOS_IncompatibleVersion if the API version passed in is incorrect
  *
  * @see EOS_ActiveSession_Info
  * @see EOS_ActiveSession_CopyInfoOptions
  * @see EOS_ActiveSession_Info_Release
  */
EOS_EResult EOSSDK_ActiveSession::CopyInfo(const EOS_ActiveSession_CopyInfoOptions* Options, EOS_ActiveSession_Info** OutActiveSessionInfo)
{
    TRACE_FUNC();
    (void)Options;

    if (!is_valid() || OutActiveSessionInfo == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    *OutActiveSessionInfo = nullptr;

    // Game-facing snapshot: live session state only. Do not run network
    // advertisement prep here — prepare_session_infos_for_network resets remote
    // joiners back to Pending/owner-only for discovery broadcasts.
    Session_Infos_pb infos_copy = resolve_live_infos();

    EOS_ActiveSession_Info* copy_session_info = eos_allocate_struct<EOS_ActiveSession_Info>();
    EOS_SessionDetails_Info* session_details_info = eos_allocate_struct<EOS_SessionDetails_Info>();
    EOS_SessionDetails_Settings* session_details_settings = eos_allocate_struct<EOS_SessionDetails_Settings>();
    if (copy_session_info == nullptr || session_details_info == nullptr || session_details_settings == nullptr)
    {
        eos_release_bytes(session_details_settings);
        eos_release_bytes(session_details_info);
        eos_release_bytes(copy_session_info);
        return EOS_EResult::EOS_UnexpectedError;
    }

    copy_session_info->ApiVersion = EOS_ACTIVESESSION_INFO_API_LATEST;
    copy_session_info->SessionName = eos_allocate_c_string(_session_name);
    copy_session_info->LocalUserId = Settings::Inst().productuserid;
    copy_session_info->State = static_cast<EOS_EOnlineSessionState>(infos_copy.state());
    copy_session_info->SessionDetails = session_details_info;

    session_details_info->ApiVersion = EOS_SESSIONDETAILS_INFO_API_LATEST;
    {
        int const open = static_cast<int>(infos_copy.max_players()) - infos_copy.players_size();
        session_details_info->NumOpenPublicConnections = open > 0 ? static_cast<uint32_t>(open) : 0u;
    }
    session_details_info->SessionId = eos_allocate_c_string(infos_copy.session_id());
    session_details_info->HostAddress = eos_allocate_c_string(
        GetEOS_Sessions().resolve_session_host_address_for_copy(infos_copy));
    session_details_info->Settings = session_details_settings;
    session_details_info->OwnerUserId = owner_user_id_for_session_infos(infos_copy);
    session_details_info->OwnerServerClientId = nullptr;

    session_details_settings->ApiVersion = EOS_SESSIONDETAILS_SETTINGS_API_LATEST;
    session_details_settings->BucketId = eos_allocate_c_string(infos_copy.bucket_id());
    session_details_settings->NumPublicConnections = infos_copy.max_players();
    session_details_settings->bAllowJoinInProgress = infos_copy.join_in_progress_allowed() ? EOS_TRUE : EOS_FALSE;
    session_details_settings->PermissionLevel = static_cast<EOS_EOnlineSessionPermissionLevel>(infos_copy.permission_level());
    session_details_settings->bInvitesAllowed = infos_copy.invites_allowed() ? EOS_TRUE : EOS_FALSE;
    session_details_settings->bSanctionsEnabled = EOS_FALSE;
    session_details_settings->AllowedPlatformIds = nullptr;
    session_details_settings->AllowedPlatformIdsCount = 0;

    if (copy_session_info->SessionName == nullptr ||
        session_details_info->SessionId == nullptr ||
        session_details_info->HostAddress == nullptr ||
        session_details_settings->BucketId == nullptr)
    {
        EOS_SessionDetails_Info_Release(session_details_info);
        eos_release_bytes(const_cast<char*>(copy_session_info->SessionName));
        eos_release_bytes(copy_session_info);
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutActiveSessionInfo = copy_session_info;

    APP_LOG(Log::LogLevel::INFO,
        "ActiveSession CopyInfo: name=%s state=%d session_id=%s host=%s bucket=%s players=%d registered=%d owner=%p",
        _session_name.c_str(),
        static_cast<int>(infos_copy.state()),
        infos_copy.session_id().c_str(),
        session_details_info->HostAddress != nullptr ? session_details_info->HostAddress : "(null)",
        session_details_settings->BucketId != nullptr ? session_details_settings->BucketId : "(null)",
        infos_copy.players_size(),
        infos_copy.registered_players_size(),
        static_cast<void*>(session_details_info->OwnerUserId));

    return EOS_EResult::EOS_Success;
}

/**
 * Get the number of registered players associated with this active session
 *
 * @param Options the Options associated with retrieving the registered player count
 *
 * @return number of registered players in the active session or 0 if there is an error
 */
uint32_t EOSSDK_ActiveSession::GetRegisteredPlayerCount(const EOS_ActiveSession_GetRegisteredPlayerCountOptions* Options)
{
    TRACE_FUNC();
    (void)Options;

    return resolve_live_infos().registered_players_size();
}

/**
 * EOS_ActiveSession_GetRegisteredPlayerByIndex is used to immediately retrieve individual players registered with the active session.
 *
 * @param Options Structure containing the input parameters
 *
 * @return the product user id for the registered player at a given index or null if that index is invalid
 *
 * @see EOS_ActiveSession_GetRegisteredPlayerCount
 * @see EOS_ActiveSession_GetRegisteredPlayerByIndexOptions
 */
EOS_ProductUserId EOSSDK_ActiveSession::GetRegisteredPlayerByIndex(const EOS_ActiveSession_GetRegisteredPlayerByIndexOptions* Options)
{
    TRACE_FUNC();

    Session_Infos_pb const infos = resolve_live_infos();
    if (Options->PlayerIndex >= static_cast<uint32_t>(infos.registered_players_size()))
        return GetInvalidProductUserId();

    return GetProductUserId(infos.registered_players()[Options->PlayerIndex]);
}

/**
 * Release the memory associated with an active session.
 * This must be called on data retrieved from EOS_Sessions_CopyActiveSessionHandle
 *
 * @param ActiveSessionHandle - The active session handle to release
 *
 * @see EOS_Sessions_CopyActiveSessionHandle
 */
void EOSSDK_ActiveSession::Release()
{
    TRACE_FUNC();

    _magic = 0;
    delete this;
}

}
