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
#include "eossdk_platform.h"
#include "eos_client_api.h"
#include "eos_memory.h"

namespace sdk
{
namespace
{
Lobby_Infos_pb patched_lobby_infos_for_read(Lobby_Infos_pb const& infos)
{
    Lobby_Infos_pb copy = infos;
    GetEOS_Lobby().prepare_lobby_infos_for_unity(copy);
    GetEOS_Lobby().patch_crossplatform_joinable_lobby(copy);
    return copy;
}

static Lobby_Member_Infos_pb const* find_lobby_member_by_product_user_id(
    google::protobuf::Map<std::string, Lobby_Member_Infos_pb> const& members,
    EOS_ProductUserId target_user_id)
{
    if (target_user_id == nullptr)
        return nullptr;

    for (auto const& member : members)
    {
        if (GetProductUserId(member.first) == target_user_id)
            return &member.second;
    }
    return nullptr;
}

template<typename AttrEntry>
EOS_Lobby_Attribute* allocate_lobby_attribute_from_entry(AttrEntry const& attr_entry)
{
    EOS_Lobby_Attribute* pAttribute = eos_allocate_struct<EOS_Lobby_Attribute>();
    EOS_Lobby_AttributeData* pData = eos_allocate_struct<EOS_Lobby_AttributeData>();
    if (pAttribute == nullptr || pData == nullptr)
    {
        eos_release_bytes(pData);
        eos_release_bytes(pAttribute);
        return nullptr;
    }

    pData->Key = eos_allocate_c_string(attr_entry.first);
    if (pData->Key == nullptr)
    {
        eos_release_bytes(pData);
        eos_release_bytes(pAttribute);
        return nullptr;
    }

    switch (attr_entry.second.value().value_case())
    {
        case Lobby_Attr_Value::ValueCase::kB:
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_BOOLEAN;
            pData->Value.AsBool = attr_entry.second.value().b() ? EOS_TRUE : EOS_FALSE;
            break;

        case Lobby_Attr_Value::ValueCase::kD:
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_DOUBLE;
            pData->Value.AsDouble = attr_entry.second.value().d();
            break;

        case Lobby_Attr_Value::ValueCase::kI:
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_INT64;
            pData->Value.AsInt64 = attr_entry.second.value().i();
            break;

        case Lobby_Attr_Value::ValueCase::kS:
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
            pData->ValueType = EOS_ESessionAttributeType::EOS_AT_STRING;
            pData->Value.AsUtf8 = eos_allocate_c_string("");
            if (pData->Value.AsUtf8 == nullptr)
            {
                eos_release_bytes(const_cast<char*>(pData->Key));
                eos_release_bytes(pData);
                eos_release_bytes(pAttribute);
                return nullptr;
            }
            break;
    }

    pAttribute->ApiVersion = EOS_LOBBY_ATTRIBUTE_API_LATEST;
    pAttribute->Data = pData;
    pAttribute->Visibility = static_cast<EOS_ELobbyAttributeVisibility>(attr_entry.second.visibility_type());
    return pAttribute;
}
}

EOSSDK_LobbyDetails::EOSSDK_LobbyDetails()
{}

EOSSDK_LobbyDetails::~EOSSDK_LobbyDetails()
{}

/**
 * A "read only" representation of an existing lobby that games interact with externally.
 * Both the lobby and lobby search interfaces interface use this common class for lobby management and search results
 */

/**
  * Get the product user id of the current owner for a given lobby
  *
  * @param Options Structure containing the input parameters
  *
  * @return the product user id for the lobby owner or null if the input parameters are invalid
  */
EOS_ProductUserId EOSSDK_LobbyDetails::GetLobbyOwner(const EOS_LobbyDetails_GetLobbyOwnerOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr)
        return GetInvalidProductUserId();

    return GetProductUserId(_state.infos.owner_id());
}

/**
 * EOS_LobbyDetails_CopyInfo is used to immediately retrieve a copy of lobby information from a given source such as a existing lobby or a search result.
 * If the call returns an EOS_Success result, the out parameter, OutLobbyDetailsInfo, must be passed to EOS_LobbyDetails_Info_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutLobbyDetailsInfo Out parameter used to receive the EOS_LobbyDetails_Info structure.
 *
 * @return EOS_Success if the information is available and passed out in OutLobbyDetailsInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_LobbyDetails_Info
 * @see EOS_LobbyDetails_CopyInfoOptions
 * @see EOS_LobbyDetails_Info_Release
 */
EOS_EResult EOSSDK_LobbyDetails::CopyInfo(const EOS_LobbyDetails_CopyInfoOptions* Options, EOS_LobbyDetails_Info** OutLobbyDetailsInfo)
{
    TRACE_FUNC();

    if (Options == nullptr)
    {
        *OutLobbyDetailsInfo = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    EOS_LobbyDetails_Info* pLobbyDetailsInfos = eos_allocate_struct<EOS_LobbyDetails_Info>();
    if (pLobbyDetailsInfos == nullptr)
    {
        *OutLobbyDetailsInfo = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    pLobbyDetailsInfos->ApiVersion = EOS_LOBBYDETAILS_INFO_API_LATEST;
    pLobbyDetailsInfos->LobbyId = eos_allocate_c_string(infos.lobby_id());
    pLobbyDetailsInfos->BucketId = eos_allocate_c_string(infos.bucket_id());
    pLobbyDetailsInfos->LobbyOwnerUserId = GetProductUserId(infos.owner_id());
    pLobbyDetailsInfos->PermissionLevel = static_cast<EOS_ELobbyPermissionLevel>(infos.permission_level());
    {
        uint32_t const member_count = GetEOS_Lobby().lobby_member_count_for_read(infos);
        pLobbyDetailsInfos->AvailableSlots = infos.max_lobby_member() > member_count
            ? infos.max_lobby_member() - member_count
            : 0;
    }
    pLobbyDetailsInfos->MaxMembers = infos.max_lobby_member();
    pLobbyDetailsInfos->bAllowInvites = infos.invites_allowed() ? EOS_TRUE : EOS_FALSE;
    pLobbyDetailsInfos->bAllowHostMigration = infos.ballowhostmigration() ? EOS_TRUE : EOS_FALSE;
    pLobbyDetailsInfos->bRTCRoomEnabled = infos.brtcroomenabled() ? EOS_TRUE : EOS_FALSE;
    pLobbyDetailsInfos->bAllowJoinById = infos.ballowjoinbyid() ? EOS_TRUE : EOS_FALSE;
    pLobbyDetailsInfos->bRejoinAfterKickRequiresInvite = infos.brejoinafterkickrequiresinvite() ? EOS_TRUE : EOS_FALSE;
    pLobbyDetailsInfos->bPresenceEnabled = infos.bpresenceenabled() ? EOS_TRUE : EOS_FALSE;
    pLobbyDetailsInfos->AllowedPlatformIds = NULL;
    pLobbyDetailsInfos->AllowedPlatformIdsCount = 0;

    if (pLobbyDetailsInfos->LobbyId == nullptr || pLobbyDetailsInfos->BucketId == nullptr)
    {
        EOS_LobbyDetails_Info_Release(pLobbyDetailsInfos);
        *OutLobbyDetailsInfo = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutLobbyDetailsInfo = pLobbyDetailsInfos;
    return EOS_EResult::EOS_Success;
}

/**
 * Get the number of attributes associated with this lobby
 *
 * @param Options the Options associated with retrieving the attribute count
 *
 * @return number of attributes on the lobby or 0 if there is an error
 */
uint32_t EOSSDK_LobbyDetails::GetAttributeCount(const EOS_LobbyDetails_GetAttributeCountOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr)
        return 0;

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    return infos.attributes_size();
}

/**
 * EOS_LobbyDetails_CopyAttributeByIndex is used to immediately retrieve a copy of a lobby attribute from a given source such as a existing lobby or a search result.
 * If the call returns an EOS_Success result, the out parameter, OutAttribute, must be passed to EOS_Lobby_Attribute_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutAttribute Out parameter used to receive the EOS_Lobby_Attribute structure.
 *
 * @return EOS_Success if the information is available and passed out in OutAttribute
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_Lobby_Attribute
 * @see EOS_LobbyDetails_CopyAttributeByIndexOptions
 * @see EOS_Lobby_Attribute_Release
 */
EOS_EResult EOSSDK_LobbyDetails::CopyAttributeByIndex(const EOS_LobbyDetails_CopyAttributeByIndexOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    TRACE_FUNC();

    if (Options == nullptr || OutAttribute == nullptr)
    {
        set_nullptr(OutAttribute);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);

    if (Options->AttrIndex >= static_cast<uint32_t>(infos.attributes_size()))
    {
        set_nullptr(OutAttribute);
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto attr_it = infos.attributes().begin();
    std::advance(attr_it, Options->AttrIndex);

    EOS_Lobby_Attribute* pAttribute = allocate_lobby_attribute_from_entry(*attr_it);
    if (pAttribute == nullptr)
    {
        set_nullptr(OutAttribute);
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutAttribute = pAttribute;

    return EOS_EResult::EOS_Success;
}

/**
 * EOS_LobbyDetails_CopyAttributeByKey is used to immediately retrieve a copy of a lobby attribute from a given source such as a existing lobby or a search result.
 * If the call returns an EOS_Success result, the out parameter, OutAttribute, must be passed to EOS_Lobby_Attribute_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutAttribute Out parameter used to receive the EOS_Lobby_Attribute structure.
 *
 * @return EOS_Success if the information is available and passed out in OutAttribute
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_Lobby_Attribute
 * @see EOS_LobbyDetails_CopyAttributeByKeyOptions
 * @see EOS_Lobby_Attribute_Release
 */
EOS_EResult EOSSDK_LobbyDetails::CopyAttributeByKey(const EOS_LobbyDetails_CopyAttributeByKeyOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->AttrKey == nullptr || OutAttribute == nullptr)
    {
        set_nullptr(OutAttribute);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    auto attr_it = infos.attributes().find(Options->AttrKey);
    if (attr_it == infos.attributes().end())
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOS_Lobby_Attribute* pAttribute = allocate_lobby_attribute_from_entry(*attr_it);
    if (pAttribute == nullptr)
    {
        set_nullptr(OutAttribute);
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutAttribute = pAttribute;

    return EOS_EResult::EOS_Success;
}

/**
 * Get the number of members associated with this lobby
 *
 * @param Options the Options associated with retrieving the member count
 *
 * @return number of members in the existing lobby or 0 if there is an error
 */
uint32_t EOSSDK_LobbyDetails::GetMemberCount(const EOS_LobbyDetails_GetMemberCountOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr)
        return 0;

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    return GetEOS_Lobby().lobby_member_count_for_read(infos);
}

/**
 * EOS_LobbyDetails_GetMemberByIndex is used to immediately retrieve individual members registered with a lobby.
 *
 * @param Options Structure containing the input parameters
 *
 * @return the product user id for the registered member at a given index or null if that index is invalid
 *
 * @see EOS_LobbyDetails_GetMemberCount
 * @see EOS_LobbyDetails_GetMemberByIndexOptions
 */
EOS_ProductUserId EOSSDK_LobbyDetails::GetMemberByIndex(const EOS_LobbyDetails_GetMemberByIndexOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr)
        return GetInvalidProductUserId();

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    return GetEOS_Lobby().lobby_member_by_index(infos, Options->MemberIndex);
}

/**
 * EOS_LobbyDetails_GetMemberAttributeCount is used to immediately retrieve the attribute count for members in a lobby.
 *
 * @param Options Structure containing the input parameters
 *
 * @return the number of attributes associated with a given lobby member or 0 if that member is invalid
 *
 * @see EOS_LobbyDetails_GetMemberCount
 * @see EOS_LobbyDetails_GetMemberAttributeCountOptions
 */
uint32_t EOSSDK_LobbyDetails::GetMemberAttributeCount(const EOS_LobbyDetails_GetMemberAttributeCountOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->TargetUserId == nullptr)
        return 0;

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    Lobby_Member_Infos_pb const* member = find_lobby_member_by_product_user_id(infos.members(), Options->TargetUserId);
    if (member == nullptr)
        return 0;

    return member->attributes_size();
}

/**
 * EOS_LobbyDetails_CopyMemberAttributeByIndex is used to immediately retrieve a copy of a lobby member attribute from an existing lobby.
 * If the call returns an EOS_Success result, the out parameter, OutAttribute, must be passed to EOS_Lobby_Attribute_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutAttribute Out parameter used to receive the EOS_Lobby_Attribute structure.
 *
 * @return EOS_Success if the information is available and passed out in OutAttribute
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_Lobby_Attribute
 * @see EOS_LobbyDetails_CopyMemberAttributeByIndexOptions
 * @see EOS_Lobby_Attribute_Release
 */
EOS_EResult EOSSDK_LobbyDetails::CopyMemberAttributeByIndex(const EOS_LobbyDetails_CopyMemberAttributeByIndexOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->TargetUserId == nullptr)
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    Lobby_Member_Infos_pb const* member = find_lobby_member_by_product_user_id(infos.members(), Options->TargetUserId);
    if (member == nullptr)
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    if (Options->AttrIndex >= static_cast<uint32_t>(member->attributes_size()))
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto attr_it = member->attributes().begin();
    std::advance(attr_it, Options->AttrIndex);

    EOS_Lobby_Attribute* pAttribute = allocate_lobby_attribute_from_entry(*attr_it);
    if (pAttribute == nullptr)
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutAttribute = pAttribute;

    return EOS_EResult::EOS_Success;
}

/**
 * EOS_LobbyDetails_CopyMemberAttributeByKey is used to immediately retrieve a copy of a lobby member attribute from an existing lobby.
 * If the call returns an EOS_Success result, the out parameter, OutAttribute, must be passed to EOS_Lobby_Attribute_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutAttribute Out parameter used to receive the EOS_Lobby_Attribute structure.
 *
 * @return EOS_Success if the information is available and passed out in OutAttribute
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *
 * @see EOS_Lobby_Attribute
 * @see EOS_LobbyDetails_CopyMemberAttributeByKeyOptions
 * @see EOS_Lobby_Attribute_Release
 */
EOS_EResult EOSSDK_LobbyDetails::CopyMemberAttributeByKey(const EOS_LobbyDetails_CopyMemberAttributeByKeyOptions* Options, EOS_Lobby_Attribute** OutAttribute)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->AttrKey == nullptr || Options->TargetUserId == nullptr)
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    Lobby_Infos_pb infos = patched_lobby_infos_for_read(_state.infos);
    Lobby_Member_Infos_pb const* member = find_lobby_member_by_product_user_id(infos.members(), Options->TargetUserId);
    if (member == nullptr)
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto attr_it = member->attributes().find(Options->AttrKey);
    if (attr_it == member->attributes().end())
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOS_Lobby_Attribute* pAttribute = allocate_lobby_attribute_from_entry(*attr_it);
    if (pAttribute == nullptr)
    {
        *OutAttribute = nullptr;
        return EOS_EResult::EOS_UnexpectedError;
    }

    *OutAttribute = pAttribute;

    return EOS_EResult::EOS_Success;
}

/**
 * Release the memory associated with a single lobby. This must be called on data retrieved from EOS_LobbySearch_CopySearchResultByIndex.
 *
 * @param LobbyHandle - The lobby handle to release
 *
 * @see EOS_LobbySearch_CopySearchResultByIndex
 */
void EOSSDK_LobbyDetails::Release()
{
    TRACE_FUNC();

    delete this;
}

}