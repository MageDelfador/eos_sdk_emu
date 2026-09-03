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

#include "eossdk_lobby.h"
#include "eos_api_trace.h"
#include "eos_memory.h"

using namespace sdk;

EOS_DECLARE_FUNC(void) EOS_Lobby_CreateLobby(EOS_HLobby Handle, const EOS_Lobby_CreateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnCreateLobbyCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->CreateLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_DestroyLobby(EOS_HLobby Handle, const EOS_Lobby_DestroyLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnDestroyLobbyCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->DestroyLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinLobby(EOS_HLobby Handle, const EOS_Lobby_JoinLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->JoinLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinLobbyById(EOS_HLobby Handle, const EOS_Lobby_JoinLobbyByIdOptions002* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyByIdCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->JoinLobbyById(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_LeaveLobby(EOS_HLobby Handle, const EOS_Lobby_LeaveLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveLobbyCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->LeaveLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_UpdateLobbyModification(EOS_HLobby Handle, const EOS_Lobby_UpdateLobbyModificationOptions* Options, EOS_HLobbyModification* OutLobbyModificationHandle)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->UpdateLobbyModification(Options, OutLobbyModificationHandle);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_UpdateLobby(EOS_HLobby Handle, const EOS_Lobby_UpdateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnUpdateLobbyCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->UpdateLobby(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_PromoteMember(EOS_HLobby Handle, const EOS_Lobby_PromoteMemberOptions* Options, void* ClientData, const EOS_Lobby_OnPromoteMemberCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->PromoteMember(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_KickMember(EOS_HLobby Handle, const EOS_Lobby_KickMemberOptions* Options, void* ClientData, const EOS_Lobby_OnKickMemberCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->KickMember(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyUpdateReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyUpdateReceivedCallback NotificationFn)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->AddNotifyLobbyUpdateReceived(Options, ClientData, NotificationFn);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyUpdateReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->RemoveNotifyLobbyUpdateReceived(InId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyMemberUpdateReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberUpdateReceivedCallback NotificationFn)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->AddNotifyLobbyMemberUpdateReceived(Options, ClientData, NotificationFn);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyMemberUpdateReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->RemoveNotifyLobbyMemberUpdateReceived(InId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyMemberStatusReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberStatusReceivedCallback NotificationFn)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->AddNotifyLobbyMemberStatusReceived(Options, ClientData, NotificationFn);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->RemoveNotifyLobbyMemberStatusReceived(InId);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_SendInvite(EOS_HLobby Handle, const EOS_Lobby_SendInviteOptions* Options, void* ClientData, const EOS_Lobby_OnSendInviteCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->SendInvite(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RejectInvite(EOS_HLobby Handle, const EOS_Lobby_RejectInviteOptions* Options, void* ClientData, const EOS_Lobby_OnRejectInviteCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->RejectInvite(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_QueryInvites(EOS_HLobby Handle, const EOS_Lobby_QueryInvitesOptions* Options, void* ClientData, const EOS_Lobby_OnQueryInvitesCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->QueryInvites(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(uint32_t) EOS_Lobby_GetInviteCount(EOS_HLobby Handle, const EOS_Lobby_GetInviteCountOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return 0;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->GetInviteCount(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetInviteIdByIndex(EOS_HLobby Handle, const EOS_Lobby_GetInviteIdByIndexOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->GetInviteIdByIndex(Options, OutBuffer, InOutBufferLength);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CreateLobbySearch(EOS_HLobby Handle, const EOS_Lobby_CreateLobbySearchOptions* Options, EOS_HLobbySearch* OutLobbySearchHandle)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->CreateLobbySearch(Options, OutLobbySearchHandle);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteReceived(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteReceivedCallback NotificationFn)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->AddNotifyLobbyInviteReceived(Options, ClientData, NotificationFn);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteReceived(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    pInst->RemoveNotifyLobbyInviteReceived(InId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyLobbyInviteAccepted(EOS_HLobby Handle, const EOS_Lobby_AddNotifyLobbyInviteAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteAcceptedCallback NotificationFn)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->AddNotifyLobbyInviteAccepted(Options, ClientData, NotificationFn);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyLobbyInviteAccepted(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->RemoveNotifyLobbyInviteAccepted(InId);
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyJoinLobbyAccepted(EOS_HLobby Handle, const EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyAcceptedCallback NotificationFn)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->AddNotifyJoinLobbyAccepted(Options, ClientData, NotificationFn);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyJoinLobbyAccepted(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->RemoveNotifyJoinLobbyAccepted(InId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandleByInviteId(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->CopyLobbyDetailsHandleByInviteId(Options, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandleByUiEventId(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->CopyLobbyDetailsHandleByUiEventId(Options, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_CopyLobbyDetailsHandle(EOS_HLobby Handle, const EOS_Lobby_CopyLobbyDetailsHandleOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_Lobby*>(Handle);
    return pInst->CopyLobbyDetailsHandle(Options, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetBucketId(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetBucketIdOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->SetBucketId(Options);
}


EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetPermissionLevel(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetPermissionLevelOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->SetPermissionLevel(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetMaxMembers(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetMaxMembersOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->SetMaxMembers(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_SetInvitesAllowed(EOS_HLobbyModification Handle, const EOS_LobbyModification_SetInvitesAllowedOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->SetInvitesAllowed(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_AddAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_AddAttributeOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->AddAttribute(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_RemoveAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_RemoveAttributeOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->RemoveAttribute(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_AddMemberAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_AddMemberAttributeOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->AddMemberAttribute(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyModification_RemoveMemberAttribute(EOS_HLobbyModification Handle, const EOS_LobbyModification_RemoveMemberAttributeOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(Handle);
    return pInst->RemoveMemberAttribute(Options);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_LobbyDetails_GetLobbyOwner(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetLobbyOwnerOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return nullptr;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->GetLobbyOwner(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyInfo(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyInfoOptions* Options, EOS_LobbyDetails_Info** OutLobbyDetailsInfo)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->CopyInfo(Options, OutLobbyDetailsInfo);
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetAttributeCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetAttributeCountOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return 0;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->GetAttributeCount(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyAttributeByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyAttributeByIndexOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->CopyAttributeByIndex(Options, OutAttribute);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyAttributeByKey(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyAttributeByKeyOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->CopyAttributeByKey(Options, OutAttribute);
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetMemberCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberCountOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return 0;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->GetMemberCount(Options);
}

EOS_DECLARE_FUNC(EOS_ProductUserId) EOS_LobbyDetails_GetMemberByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberByIndexOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return nullptr;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->GetMemberByIndex(Options);
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbyDetails_GetMemberAttributeCount(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_GetMemberAttributeCountOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return 0;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->GetMemberAttributeCount(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberAttributeByIndex(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberAttributeByIndexOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->CopyMemberAttributeByIndex(Options, OutAttribute);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbyDetails_CopyMemberAttributeByKey(EOS_HLobbyDetails Handle, const EOS_LobbyDetails_CopyMemberAttributeByKeyOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(Handle);
    return pInst->CopyMemberAttributeByKey(Options, OutAttribute);
}

EOS_DECLARE_FUNC(void) EOS_LobbySearch_Find(EOS_HLobbySearch Handle, const EOS_LobbySearch_FindOptions* Options, void* ClientData, const EOS_LobbySearch_OnFindCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    pInst->Find(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetLobbyId(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetLobbyIdOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->SetLobbyId(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetTargetUserId(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetTargetUserIdOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->SetTargetUserId(Options);
}

/** NYI */
EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetParameter(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetParameterOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->SetParameter(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_RemoveParameter(EOS_HLobbySearch Handle, const EOS_LobbySearch_RemoveParameterOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->RemoveParameter(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_SetMaxResults(EOS_HLobbySearch Handle, const EOS_LobbySearch_SetMaxResultsOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->SetMaxResults(Options);
}

EOS_DECLARE_FUNC(uint32_t) EOS_LobbySearch_GetSearchResultCount(EOS_HLobbySearch Handle, const EOS_LobbySearch_GetSearchResultCountOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return 0;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->GetSearchResultCount(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_LobbySearch_CopySearchResultByIndex(EOS_HLobbySearch Handle, const EOS_LobbySearch_CopySearchResultByIndexOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(Handle);
    return pInst->CopySearchResultByIndex(Options, OutLobbyDetailsHandle);
}

EOS_DECLARE_FUNC(void) EOS_LobbyModification_Release(EOS_HLobbyModification LobbyModificationHandle)
{
    EOS_API_TRACE();
    if (LobbyModificationHandle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_LobbyModification*>(LobbyModificationHandle);
    pInst->Release();
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_Release(EOS_HLobbyDetails LobbyHandle)
{
    EOS_API_TRACE();
    if (LobbyHandle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_LobbyDetails*>(LobbyHandle);
    pInst->Release();
}

EOS_DECLARE_FUNC(void) EOS_LobbySearch_Release(EOS_HLobbySearch LobbySearchHandle)
{
    EOS_API_TRACE();
    if (LobbySearchHandle == nullptr)
        return;

    auto pInst = reinterpret_cast<EOSSDK_LobbySearch*>(LobbySearchHandle);
    pInst->Release();
}

EOS_DECLARE_FUNC(void) EOS_LobbyDetails_Info_Release(EOS_LobbyDetails_Info* LobbyDetailsInfo)
{
    EOS_API_TRACE();
    TRACE_FUNC();
    if (LobbyDetailsInfo == nullptr)
        return;

    eos_release_bytes(const_cast<char*>(LobbyDetailsInfo->LobbyId));
    eos_release_bytes(const_cast<char*>(LobbyDetailsInfo->BucketId));
    eos_release_bytes(LobbyDetailsInfo);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_Attribute_Release(EOS_Lobby_Attribute* LobbyAttribute)
{
    EOS_API_TRACE();
    TRACE_FUNC();

    if (LobbyAttribute == nullptr)
        return;

    if (LobbyAttribute->Data != nullptr)
    {
        if (LobbyAttribute->Data->ValueType == EOS_ESessionAttributeType::EOS_AT_STRING)
            eos_release_bytes(const_cast<char*>(LobbyAttribute->Data->Value.AsUtf8));

        eos_release_bytes(const_cast<char*>(LobbyAttribute->Data->Key));
        eos_release_bytes(LobbyAttribute->Data);
    }

    eos_release_bytes(LobbyAttribute);
}
