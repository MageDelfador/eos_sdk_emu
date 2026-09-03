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
#include "eos_api_trace.h"

namespace sdk
{

template<typename AttrEntry>
EOS_SessionDetails_Attribute* allocate_session_attribute_from_entry(AttrEntry const& attr_entry)
{
    EOS_SessionDetails_Attribute* pAttribute = eos_allocate_struct<EOS_SessionDetails_Attribute>();
    EOS_Sessions_AttributeData* pData = eos_allocate_struct<EOS_Sessions_AttributeData>();
    if (pAttribute == nullptr || pData == nullptr)
    {
        eos_release_bytes(pData);
        eos_release_bytes(pAttribute);
        return nullptr;
    }

    pData->ApiVersion = EOS_SESSIONS_SESSIONATTRIBUTEDATA_API_LATEST;
    pData->Key = eos_allocate_c_string(attr_entry.first);
    if (pData->Key == nullptr)
    {
        eos_release_bytes(pData);
        eos_release_bytes(pAttribute);
        return nullptr;
    }

    switch (attr_entry.second.value().value_case())
    {
        case Session_Attr_Value::ValueCase::kB:
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_BOOLEAN;
            pData->Value.AsBool = attr_entry.second.value().b() ? EOS_TRUE : EOS_FALSE;
            break;

        case Session_Attr_Value::ValueCase::kD:
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_DOUBLE;
            pData->Value.AsDouble = attr_entry.second.value().d();
            break;

        case Session_Attr_Value::ValueCase::kI:
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_INT64;
            pData->Value.AsInt64 = attr_entry.second.value().i();
            break;

        case Session_Attr_Value::ValueCase::kS:
        {
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
            pData->Value.AsUtf8 = eos_allocate_c_string(attr_entry.second.value().s());
            if (pData->Value.AsUtf8 == nullptr)
            {
                eos_release_bytes(const_cast<char*>(pData->Key));
                eos_release_bytes(pData);
                eos_release_bytes(pAttribute);
                return nullptr;
            }
        }
        break;

        default:
            eos_release_bytes(const_cast<char*>(pData->Key));
            eos_release_bytes(pData);
            eos_release_bytes(pAttribute);
            return nullptr;
    }

    pAttribute->ApiVersion = EOS_SESSIONDETAILS_ATTRIBUTE_API_LATEST;
    pAttribute->Data = pData;
    pAttribute->AdvertisementType = static_cast<EOS_ESessionAttributeAdvertisementType>(attr_entry.second.advertisement_type());
    return pAttribute;
}

/**
 * This class represents the details of a session, including its session properties and the attribution associated with it
 * Locally created or joined active sessions will contain this information as will search results.
 * A handle to a session is required to join a session via search or invite
 */

 /**
  * EOS_SessionDetails_CopyInfo is used to immediately retrieve a copy of session information from a given source such as a active session or a search result.
  * If the call returns an EOS_Success result, the out parameter, OutSessionInfo, must be passed to EOS_SessionDetails_Info_Release to release the memory associated with it.
  *
  * @param Options Structure containing the input parameters
  * @param OutSessionInfo Out parameter used to receive the EOS_SessionDetails_Info structure.
  *
  * @return EOS_Success if the information is available and passed out in OutSessionInfo
  *         EOS_InvalidParameters if you pass a null pointer for the out parameter
  *         EOS_IncompatibleVersion if the API version passed in is incorrect
  *
  * @see EOS_SessionDetails_Info
  * @see EOS_SessionDetails_CopyInfoOptions
  * @see EOS_SessionDetails_Info_Release
  */
EOS_EResult EOSSDK_SessionDetails::CopyInfo(const EOS_SessionDetails_CopyInfoOptions* Options, EOS_SessionDetails_Info** OutSessionInfo)
{
    TRACE_FUNC();
    
    if (Options == nullptr || OutSessionInfo == nullptr)
    {
        *OutSessionInfo = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    *OutSessionInfo = nullptr;

    Session_Infos_pb infos_copy = _infos;
    GetEOS_Sessions().prepare_session_infos_for_network(infos_copy);

    EOS_SessionDetails_Info* pDetails = eos_allocate_struct<EOS_SessionDetails_Info>();
    EOS_SessionDetails_Settings* pSettings = eos_allocate_struct<EOS_SessionDetails_Settings>();
    if (pDetails == nullptr || pSettings == nullptr)
    {
        eos_release_bytes(pSettings);
        eos_release_bytes(pDetails);
        *OutSessionInfo = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    pSettings->ApiVersion = EOS_SESSIONDETAILS_SETTINGS_API_LATEST;
    pSettings->BucketId = eos_allocate_c_string(infos_copy.bucket_id());
    pSettings->NumPublicConnections = infos_copy.max_players();
    pSettings->bAllowJoinInProgress = infos_copy.join_in_progress_allowed() ? EOS_TRUE : EOS_FALSE;
    pSettings->PermissionLevel = static_cast<EOS_EOnlineSessionPermissionLevel>(infos_copy.permission_level());
    pSettings->bInvitesAllowed = infos_copy.invites_allowed() ? EOS_TRUE : EOS_FALSE;
    pSettings->bSanctionsEnabled = EOS_FALSE;
    pSettings->AllowedPlatformIds = nullptr;
    pSettings->AllowedPlatformIdsCount = 0;

    pDetails->ApiVersion = EOS_SESSIONDETAILS_INFO_API_LATEST;
    pDetails->SessionId = eos_allocate_c_string(infos_copy.session_id());
    pDetails->HostAddress = eos_allocate_c_string(
        GetEOS_Sessions().resolve_session_host_address_for_copy(infos_copy));
    {
        int const open = static_cast<int>(infos_copy.max_players()) - infos_copy.players_size();
        pDetails->NumOpenPublicConnections = open > 0 ? static_cast<uint32_t>(open) : 0u;
    }
    pDetails->Settings = pSettings;
    pDetails->OwnerUserId = owner_user_id_for_session_infos(infos_copy);
    pDetails->OwnerServerClientId = nullptr;

    if (pSettings->BucketId == nullptr || pDetails->SessionId == nullptr || pDetails->HostAddress == nullptr)
    {
        EOS_SessionDetails_Info_Release(pDetails);
        *OutSessionInfo = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutSessionInfo = pDetails;
    
    return EOS_EResult::EOS_Success;
}

/**
 * Get the number of attributes associated with this session
 *
 * @param Options the Options associated with retrieving the attribute count
 *
 * @return number of attributes on the session or 0 if there is an error
 */
uint32_t EOSSDK_SessionDetails::GetSessionAttributeCount(const EOS_SessionDetails_GetSessionAttributeCountOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr)
        return 0;

    return _infos.attributes_size();
}

/**
 * EOS_SessionDetails_CopySessionAttributeByIndex is used to immediately retrieve a copy of session attribution from a given source such as a active session or a search result.
 * If the call returns an EOS_Success result, the out parameter, OutSessionAttribute, must be passed to EOS_SessionDetails_Attribute_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutSessionAttribute Out parameter used to receive the EOS_SessionDetails_Attribute structure.
 *
 * @return EOS_Success if the information is available and passed out in OutSessionAttribute
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_SessionDetails_Attribute
 * @see EOS_SessionDetails_CopySessionAttributeByIndexOptions
 * @see EOS_SessionDetails_Attribute_Release
 */
EOS_EResult EOSSDK_SessionDetails::CopySessionAttributeByIndex(const EOS_SessionDetails_CopySessionAttributeByIndexOptions* Options, EOS_SessionDetails_Attribute** OutSessionAttribute)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->AttrIndex >= static_cast<uint32_t>(_infos.attributes_size()) || OutSessionAttribute == nullptr)
    {
        *OutSessionAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto it = _infos.attributes().begin();
    std::advance(it, Options->AttrIndex);

    if (sdk::g_eos_in_game_callback != 0)
    {
        APP_LOG(Log::LogLevel::DEBUG, "SessionDetails attr[%u] key=%s type=%d",
            Options->AttrIndex,
            it->first.c_str(),
            static_cast<int>(it->second.value().value_case()));
    }

    EOS_SessionDetails_Attribute* pAttr = allocate_session_attribute_from_entry(*it);
    if (pAttr == nullptr)
    {
        *OutSessionAttribute = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutSessionAttribute = pAttr;
    return EOS_EResult::EOS_Success;
}

/**
 * EOS_SessionDetails_CopySessionAttributeByKey is used to immediately retrieve a copy of session attribution from a given source such as a active session or a search result.
 * If the call returns an EOS_Success result, the out parameter, OutSessionAttribute, must be passed to EOS_SessionDetails_Attribute_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutSessionAttribute Out parameter used to receive the EOS_SessionDetails_Attribute structure.
 *
 * @return EOS_Success if the information is available and passed out in OutSessionAttribute
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_SessionDetails_Attribute
 * @see EOS_SessionDetails_CopySessionAttributeByKeyOptions
 * @see EOS_SessionDetails_Attribute_Release
 */
EOS_EResult EOSSDK_SessionDetails::CopySessionAttributeByKey(const EOS_SessionDetails_CopySessionAttributeByKeyOptions* Options, EOS_SessionDetails_Attribute** OutSessionAttribute)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->AttrKey == nullptr || OutSessionAttribute == nullptr)
    {
        *OutSessionAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }
    
    auto it = _infos.attributes().find(Options->AttrKey);
    if (it == _infos.attributes().end())
    {
        *OutSessionAttribute = nullptr;
        return EOS_EResult::EOS_NotFound;
    }

    EOS_SessionDetails_Attribute* pAttr = allocate_session_attribute_from_entry(*it);
    if (pAttr == nullptr)
    {
        *OutSessionAttribute = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutSessionAttribute = pAttr;
    return EOS_EResult::EOS_Success;
}

/**
 * Release the memory associated with a single session. This must be called on data retrieved from EOS_SessionSearch_CopySearchResultByIndex.
 *
 * @param SessionHandle - The session handle to release
 *
 * @see EOS_SessionSearch_CopySearchResultByIndex
 */
void EOSSDK_SessionDetails::Release()
{
    TRACE_FUNC();
    
    delete this;
}

}