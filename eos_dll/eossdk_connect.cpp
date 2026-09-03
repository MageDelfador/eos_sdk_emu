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

#include "eossdk_connect.h"
#include "eossdk_platform.h"
#include "eos_client_api.h"
#include "settings.h"

#include <cstring>

namespace sdk
{

namespace
{
constexpr char STEAM64_SESSION_KEY[] = "steam64";

char* dup_cstr(std::string const& s)
{
    if (s.empty())
        return nullptr;
    char* out = new char[s.size() + 1];
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

void set_connect_steam64(Connect_Infos_pb& infos, std::string const& steam64)
{
    if (!steam64.empty())
        (*infos.mutable_sessions())[STEAM64_SESSION_KEY] = steam64;
}

std::string get_connect_steam64(Connect_Infos_pb const& infos)
{
    auto it = infos.sessions().find(STEAM64_SESSION_KEY);
    if (it != infos.sessions().end())
        return it->second;
    return {};
}

EOS_Connect_ExternalAccountInfo* make_connect_external_account(
    EOS_ProductUserId product_user_id,
    std::string const& display_name,
    std::string const& account_id,
    EOS_EExternalAccountType account_type)
{
    EOS_Connect_ExternalAccountInfo* info = new EOS_Connect_ExternalAccountInfo();
    info->ApiVersion = EOS_CONNECT_EXTERNALACCOUNTINFO_API_LATEST;
    info->ProductUserId = product_user_id;
    info->DisplayName = dup_cstr(display_name);
    info->AccountId = dup_cstr(account_id);
    info->AccountIdType = account_type;
    info->LastLoginTime = EOS_CONNECT_TIME_UNDEFINED;
    return info;
}

Connect_Infos_pb const* get_connect_infos_for_product_user(EOS_ProductUserId product_user_id)
{
    if (product_user_id == Settings::Inst().productuserid)
        return &GetEOS_Connect().get_myself()->second.infos;

    auto user = GetEOS_Connect().get_user_by_productid(product_user_id);
    if (user == GetEOS_Connect().get_end_users())
        return nullptr;

    return &user->second.infos;
}
}

decltype(EOSSDK_Connect::user_infos_rate)      EOSSDK_Connect::user_infos_rate;

EOSSDK_Connect::EOSSDK_Connect()
{
    auto userProductId = Settings::Inst().productuserid;
    auto userid = Settings::Inst().userid;
    if (userProductId == nullptr || userid == nullptr)
        return;

    auto& myself = _users[userProductId];
    myself.connected = false;
    myself.infos.set_userid(userid->to_string());
    myself.infos.set_displayname(Settings::Inst().username);
    set_connect_steam64(myself.infos, Settings::Inst().steam64);

    APP_LOG(Log::LogLevel::DEBUG, "Userid: %s, Productid: %s", userid->to_string().c_str(), userProductId->to_string().c_str());
    try
    {
        GetNetwork().set_default_channel(userProductId->to_string(), 0);
        GetNetwork().advertise_peer_id(userProductId->to_string());
        GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kNetworkAdvertise);
        GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kConnect);
        GetNetwork().advertise(true);
    }
    catch (...)
    {
        APP_LOG(Log::LogLevel::WARN, "Connect network setup failed, continuing offline");
    }

    GetCB_Manager().register_callbacks(this);
    GetCB_Manager().register_frame(this);
}

EOSSDK_Connect::~EOSSDK_Connect()
{
    GetNetwork().advertise(false);
    GetNetwork().remove_advertise_peer_id(get_myself()->first->to_string());

    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kConnect);
    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kNetworkAdvertise);

    GetCB_Manager().unregister_frame(this);
    GetCB_Manager().unregister_callbacks(this);

    GetCB_Manager().remove_all_notifications(this);
}

//void EOSSDK_Connect::add_session(EOS_ProductUserId session_id, std::string const& session_name)
//{
//    auto& sessions = *get_myself()->second.infos.mutable_sessions();
//    sessions[session_name] = session_id->to_string();
//}
//
//void EOSSDK_Connect::remove_session(EOS_ProductUserId session_id, std::string const& session_name)
//{
//    auto& sessions = *get_myself()->second.infos.mutable_sessions();
//    auto it = sessions.find(session_name);
//    if (it != sessions.end())
//    {
//        sessions.erase(it);
//    }
//}

/**
 * The Connect Interface is used to manage local user permissions and access to backend services through the verification of various forms of credentials.
 * It creates an association between third party providers and an internal mapping that allows Epic Online Services to represent a user agnostically
 * All Connect Interface calls take a handle of type EOS_HConnect as the first parameter.
 * This handle can be retrieved from a EOS_HPlatform handle by using the EOS_Platform_GetConnectInterface function.
 *
 * NOTE: At this time, this feature is only available for products that are part of the Epic Games store.
 *
 * @see EOS_Platform_GetConnectInterface
 */

/**
  * Login/Authenticate given a valid set of external auth credentials.
  *
  * @param Options structure containing the external account credentials and type to use during the login operation
  * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
  * @param CompletionDelegate a callback that is fired when the login operation completes, either successfully or in error
  */
void EOSSDK_Connect::Login(const EOS_Connect_LoginOptions* Options, void* ClientData, const EOS_Connect_OnLoginCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_LoginCallbackInfo& lci = res->CreateCallback<EOS_Connect_LoginCallbackInfo>((CallbackFunc)CompletionDelegate);

    lci.ClientData = ClientData;
    lci.ContinuanceToken = nullptr;
    lci.LocalUserId = Settings::Inst().productuserid;
    lci.ResultCode = EOS_EResult::EOS_Success;
    
    res->done = true;
    GetCB_Manager().add_callback(this, res);

    get_myself()->second.connected = true;
}

/**
 * Create an account association with the Epic Online Service as a product user given their external auth credentials
 *
 * @param Options structure containing a continuance token from a "user not found" response during Login (always try login first)
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the create operation completes, either successfully or in error
 */
void EOSSDK_Connect::CreateUser(const EOS_Connect_CreateUserOptions* Options, void* ClientData, const EOS_Connect_OnCreateUserCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_CreateUserCallbackInfo& cuci = res->CreateCallback<EOS_Connect_CreateUserCallbackInfo>((CallbackFunc)CompletionDelegate);

    cuci.ClientData = ClientData;
    cuci.LocalUserId = Settings::Inst().productuserid;
    cuci.ResultCode = EOS_EResult::EOS_Connect_UserAlreadyExists;

    res->done = true;
    GetCB_Manager().add_callback(this, res);

    get_myself()->second.connected = true;
}

/**
 * Link a set of external auth credentials with an existing product user on the Epic Online Service
 *
 * @param Options structure containing a continuance token from a "user not found" response during Login (always try login first) and a currently logged in user not already associated with this external auth provider
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the link operation completes, either successfully or in error
 */
void EOSSDK_Connect::LinkAccount(const EOS_Connect_LinkAccountOptions* Options, void* ClientData, const EOS_Connect_OnLinkAccountCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_LinkAccountCallbackInfo& laci = res->CreateCallback<EOS_Connect_LinkAccountCallbackInfo>((CallbackFunc)CompletionDelegate);

    laci.ClientData = ClientData;
    laci.LocalUserId = get_myself()->first;

    if (Options == nullptr)
    {
        laci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        laci.ResultCode = EOS_EResult::EOS_Connect_LinkAccountFailed;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Unlink external auth credentials from the owning keychain of a logged in product user.
 *
 * This function allows recovering the user from scenarios where they have accidentally proceeded to creating
 * a new product user for the local native user account, instead of linking it with an existing keychain that
 * they have previously created by playing the game (or another game owned by the organization) on another platform.
 *
 * In such scenario, after the initial platform login and a new product user creation, the user wishes to re-login
 * using other set of external auth credentials to connect with their existing game progression data. In order to
 * allow automatic login also on the current platform, they will need to unlink the accidentally created new keychain
 * and product user and then use the EOS_Connect_Login and EOS_Connect_LinkAccount APIs to link the local native platform
 * account with that previously created existing product user and its owning keychain.
 *
 * In another secnario, the user may simply want to disassociate the account that they have logged in with from the current
 * keychain that it is linked with, perhaps to link it against another keychain or to separate the game progressions again.
 *
 * In order to protect against account theft, it is only possible to unlink user accounts that have been authenticated
 * and logged in to the product user in the current session. This prevents a malicious actor from gaining access to one
 * of the linked accounts and using it to remove all other accounts linked with the keychain. This also prevents a malicious
 * actor from replacing the unlinked account with their own corresponding account on the same platform, as the unlinking
 * operation will ensure that any existing authentication session cannot be used to re-link and overwrite the entry without
 * authenticating with one of the other linked accounts in the keychain. These restrictions limit the potential attack surface
 * related to account theft scenarios.
 *
 * @param Options structure containing operation input parameters
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the unlink operation completes, either successfully or in error
 */
void EOSSDK_Connect::UnlinkAccount(const EOS_Connect_UnlinkAccountOptions* Options, void* ClientData, const EOS_Connect_OnUnlinkAccountCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_UnlinkAccountCallbackInfo& uaci = res->CreateCallback<EOS_Connect_UnlinkAccountCallbackInfo>((CallbackFunc)CompletionDelegate);

    uaci.ClientData = ClientData;
    uaci.LocalUserId = GetProductUserId(Settings::Inst().productuserid->to_string());

    if (Options == nullptr)
    {
        uaci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        uaci.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Create a new unique pseudo-account that can be used to identify the current user profile on the local device.
 *
 * This function is intended to be used by mobile games and PC games that wish to allow
 * a new user to start playing without requiring to login to the game using any user identity.
 * In addition to this, the Device ID feature is used to automatically login the local user
 * also when they have linked at least one external user account(s) with the local Device ID.
 *
 * It is possible to link many devices with the same user's account keyring using the Device ID feature.
 *
 * Linking a device later or immediately with a real user account will ensure that the player
 * will not lose their progress if they switch devices or lose the device at some point,
 * as they will be always able to login with one of their linked real accounts and also link
 * another new device with the user account associations keychain. Otherwise, without having
 * at least one permanent user account linked to the Device ID, the player would lose all of their
 * game data and progression permanently should something happen to their device or the local
 * user profile on the device.
 *
 * After a successful one-time CreateDeviceId operation, the game can login the local user
 * automatically on subsequent game starts with EOS_Connect_Login using the EOS_ECT_DEVICEID_ACCESS_TOKEN
 * credentials type. If a Device ID already exists for the local user on the device then EOS_DuplicateNotAllowed
 * error result is returned and the caller should proceed to calling EOS_Connect_Login directly.
 *
 * @param Options structure containing operation input parameters
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the create operation completes, either successfully or in error
 */
void EOSSDK_Connect::CreateDeviceId(const EOS_Connect_CreateDeviceIdOptions* Options, void* ClientData, const EOS_Connect_OnCreateDeviceIdCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_CreateDeviceIdCallbackInfo& cdici = res->CreateCallback<EOS_Connect_CreateDeviceIdCallbackInfo>((CallbackFunc)CompletionDelegate);

    cdici.ClientData = ClientData;

    if (Options == nullptr || Options->DeviceModel == nullptr)
    {
        cdici.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        _device_id = Options->DeviceModel;
        cdici.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Delete any existing Device ID access credentials for the current user profile on the local device.
 *
 * The deletion is permanent and it is not possible to recover lost game data and progression
 * if the Device ID had not been linked with at least one real external user account.
 *
 * @param Options structure containing operation input parameters
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the delete operation completes, either successfully or in error
 */
void EOSSDK_Connect::DeleteDeviceId(const EOS_Connect_DeleteDeviceIdOptions* Options, void* ClientData, const EOS_Connect_OnDeleteDeviceIdCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_DeleteDeviceIdCallbackInfo& ddici = res->CreateCallback<EOS_Connect_DeleteDeviceIdCallbackInfo>((CallbackFunc)CompletionDelegate);

    ddici.ClientData = ClientData;

    if (Options == nullptr || _device_id.empty())
    {
        ddici.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        _device_id.clear();
        ddici.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Transfer a Device ID pseudo-account and the product user associated with it into another
 * keychain linked with real user accounts (such as Epic Games, Playstation, Xbox, and other).
 *
 * This function allows transferring a product user, i.e. the local user's game progression
 * backend data from a Device ID owned keychain into a keychain with real user accounts
 * linked to it. The transfer of Device ID owned product user into a keychain of real user
 * accounts allows persisting the user's game data on the backend in the event that they
 * would lose access to the local device or otherwise switch to another device or platform.
 *
 * This function is only applicable in the situation of where the local user first plays
 * the game using the anonymous Device ID login, then later logs in using a real user
 * account that they have also already used to play the same game or another game under the
 * same organization within Epic Online Services. In such situation, while normally the login
 * attempt with a real user account would return EOS_InvalidUser and an EOS_ContinuanceToken
 * and allow calling the EOS_Connect_LinkAccount API to link it with the Device ID's keychain,
 * instead the login operation succeeds and finds an existing user because the association
 * already exists. Because the user cannot have two product users simultaneously to play with,
 * the game should prompt the user to choose which profile to keep and which one to discard
 * permanently. Based on the user choice, the game may then proceed to transfer the Device ID
 * login into the keychain that is persistent and backed by real user accounts, and if the user
 * chooses so, move the product user as well into the destination keychain and overwrite the
 * existing previous product user with it. To clarify, moving the product user with the Device ID
 * login in this way into a persisted keychain allows to preserve the so far only locally persisted
 * game progression and thus protect the user against a case where they lose access to the device.
 *
 * On success, the completion callback will return the preserved EOS_ProductUserId that remains
 * logged in while the discarded EOS_ProductUserId has been invalidated and deleted permanently.
 * Consecutive logins using the existing Device ID login type or the external account will
 * connect the user to the same backend data belonging to the preserved EOS_ProductUserId.
 *
 * Example walkthrough: Cross-platform mobile game using the anonymous Device ID login.
 *
 * For onboarding new users, the game will attempt to always automatically login the local user
 * by calling EOS_Connect_Login using the EOS_ECT_DEVICEID_ACCESS_TOKEN login type. If the local
 * Device ID credentials are not found, and the game wants a frictionless entry for the first time
 * user experience, the game will automatically call EOS_Connect_CreateDeviceId to create new
 * Device ID pseudo-account and then login the local user into it. Consecutive game starts will
 * thus automatically login the user to their locally persisted Device ID account.
 *
 * The user starts playing anonymously using the Device ID login type and makes significant game progress.
 * Later, they login using an external account that they have already used previously for the
 * same game perhaps on another platform, or another game owned by the same organization.
 * In such case, EOS_Connect_Login will automatically login the user to their existing account
 * linking keychain and create automatically a new empty product user for this product.
 *
 * In order for the user to use their existing previously created keychain and have the locally
 * created Device ID login reference to that keychain instead, the user's current product user
 * needs to be moved to be under that keychain so that their existing game progression will be
 * preserved. To do so, the game can call EOS_Connect_TransferDeviceIdAccount to transfer the
 * Device ID login and the product user associated with it into the other keychain that has real
 * external user account(s) linked to it. Note that it is important that the game either automatically
 * checks that the other product user does not have any meaningful progression data, or otherwise
 * will prompt the user to make the choice on which game progression to preserve and which can
 * be discarded permanently. The other product user will be discarded permanently and cannot be
 * recovered, so it is very important that the user is guided to make the right choice to avoid
 * accidental loss of all game progression.
 *
 * @see EOS_Connect_Login
 * @see EOS_Connect_CreateDeviceId
 *
 * @param Options structure containing the logged in product users and specifying which one will be preserved
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the transfer operation completes, either successfully or in error
 */
void EOSSDK_Connect::TransferDeviceIdAccount(const EOS_Connect_TransferDeviceIdAccountOptions* Options, void* ClientData, const EOS_Connect_OnTransferDeviceIdAccountCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_TransferDeviceIdAccountCallbackInfo& tdiaci = res->CreateCallback<EOS_Connect_TransferDeviceIdAccountCallbackInfo>((CallbackFunc)CompletionDelegate);

    tdiaci.LocalUserId = GetProductUserId(Settings::Inst().productuserid->to_string());
    tdiaci.ClientData = ClientData;

    if (Options == nullptr)
    {
        tdiaci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        tdiaci.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Retrieve the equivalent product user ids from a list of external account ids from supported account providers.  The values will be cached and retrievable via EOS_Connect_GetExternalAccountMapping
 *
 * @param Options structure containing a list of external account ids, in string form, to query for the product user id representation
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the query operation completes, either successfully or in error
 */
void EOSSDK_Connect::QueryExternalAccountMappings(const EOS_Connect_QueryExternalAccountMappingsOptions* Options, void* ClientData, const EOS_Connect_OnQueryExternalAccountMappingsCallback
    CompletionDelegate)
{
    if (CompletionDelegate == nullptr)
        return;

    TRACE_FUNC();
    
    pFrameResult_t res(new FrameResult);
    EOS_Connect_QueryExternalAccountMappingsCallbackInfo& qeamci = res->CreateCallback<EOS_Connect_QueryExternalAccountMappingsCallbackInfo>((CallbackFunc)CompletionDelegate);
    qeamci.ClientData = ClientData;
    qeamci.LocalUserId = Settings::Inst().productuserid;

    if (Options == nullptr || Options->ExternalAccountIds == nullptr)
    {
        qeamci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        switch (Options->AccountIdType)
        {
            case EOS_EExternalAccountType::EOS_EAT_EPIC:
            case EOS_EExternalAccountType::EOS_EAT_STEAM:
            {
                qeamci.ResultCode = EOS_EResult::EOS_Success;
            }
            break;
            default:
            {
                qeamci.ResultCode = EOS_EResult::EOS_Connect_ExternalServiceUnavailable;
            }
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Retrieve the equivalent account id mappings from a list of product user ids.  The values will be cached and retrievable via EOS_Connect_GetProductUserIdMapping
 *
 * @param Options structure containing a list of product user ids to query for the external account representation
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the query operation completes, either successfully or in error
 */
void EOSSDK_Connect::QueryProductUserIdMappings(const EOS_Connect_QueryProductUserIdMappingsOptions* Options, void* ClientData, const EOS_Connect_OnQueryProductUserIdMappingsCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Connect_QueryProductUserIdMappingsCallbackInfo& qpuimci = res->CreateCallback<EOS_Connect_QueryProductUserIdMappingsCallbackInfo>((CallbackFunc)CompletionDelegate);

    qpuimci.ClientData = ClientData;
    qpuimci.LocalUserId = get_myself()->first;

    if (Options == nullptr)
    {
        qpuimci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        qpuimci.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Fetch a product user id that maps to an external account id
 *
 * @param Options structure containing the local user and target external account id
 *
 * @return the product user id, previously retrieved from the backend service, for the given target external account
 */
EOS_ProductUserId EOSSDK_Connect::GetExternalAccountMapping(const EOS_Connect_GetExternalAccountMappingsOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->TargetExternalUserId == nullptr)
        return GetInvalidProductUserId();

    if (Options->AccountIdType == EOS_EExternalAccountType::EOS_EAT_EPIC)
    {
        for (auto const& user : _users)
        {
            if (user.second.infos.userid() == Options->TargetExternalUserId)
                return user.first;
        }
        return GetInvalidProductUserId();
    }

    if (Options->AccountIdType == EOS_EExternalAccountType::EOS_EAT_STEAM)
    {
        for (auto const& user : _users)
        {
            if (get_connect_steam64(user.second.infos) == Options->TargetExternalUserId)
                return user.first;
        }
        return GetInvalidProductUserId();
    }

    return GetInvalidProductUserId();
}

/**
 * Fetch an external account id, in string form, that maps to a given product user id
 *
 * @param Options structure containing the local user and target product user id
 * @param OutBuffer The buffer into which the account id data should be written.  The buffer must be long enough to hold a string of EOS_CONNECT_EXTERNAL_ACCOUNT_ID_MAX_LENGTH.
 * @param InOutBufferLength The size of the OutBuffer in characters.
 *                          The input buffer should include enough space to be null-terminated.
 *                          When the function returns, this parameter will be filled with the length of the string copied into OutBuffer.
 *
 * @return An EOS_EResult that indicates the external account id was copied into the OutBuffer
 *         EOS_Success if the information is available and passed out in OutUserInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the mapping doesn't exist or hasn't been queried yet
 *         EOS_LimitExceeded - The OutBuffer is not large enough to receive the external account id. InOutBufferLength contains the required minimum length to perform the operation successfully.
 */
EOS_EResult EOSSDK_Connect::GetProductUserIdMapping(const EOS_Connect_GetProductUserIdMappingOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
    TRACE_FUNC();

    if(Options == nullptr || Options->TargetProductUserId == nullptr || InOutBufferLength == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (OutBuffer != nullptr)
        *OutBuffer = 0;

    if (Options->AccountIdType == EOS_EExternalAccountType::EOS_EAT_EPIC)
    {
        for (auto const& user : _users)
        {
            if (user.first == Options->TargetProductUserId)
            {
                std::string const epic_id = user.second.infos.userid();
                if (epic_id.empty())
                    break;

                if (*InOutBufferLength < (epic_id.length() + 1))
                {
                    *InOutBufferLength = static_cast<int32_t>(epic_id.length() + 1);
                    return EOS_EResult::EOS_LimitExceeded;
                }

                if (OutBuffer != nullptr)
                    strncpy(OutBuffer, epic_id.c_str(), epic_id.length() + 1);

                return EOS_EResult::EOS_Success;
            }
        }
    }
    else if (Options->AccountIdType == EOS_EExternalAccountType::EOS_EAT_STEAM)
    {
        Connect_Infos_pb const* infos = get_connect_infos_for_product_user(Options->TargetProductUserId);
        if (infos != nullptr)
        {
            std::string const steam_id = !get_connect_steam64(*infos).empty()
                ? get_connect_steam64(*infos)
                : Settings::Inst().steam64;
            if (!steam_id.empty())
            {
                if (*InOutBufferLength < (steam_id.length() + 1))
                {
                    *InOutBufferLength = static_cast<int32_t>(steam_id.length() + 1);
                    return EOS_EResult::EOS_LimitExceeded;
                }

                if (OutBuffer != nullptr)
                    strncpy(OutBuffer, steam_id.c_str(), steam_id.length() + 1);

                return EOS_EResult::EOS_Success;
            }
        }
    }
    else
    {
        *InOutBufferLength = 1;
        return EOS_EResult::EOS_NotFound;
    }

    *InOutBufferLength = 1;
    return EOS_EResult::EOS_NotFound;
}

/**
 * Fetch the number of product users that are logged in.
 *
 * @return the number of product users logged in.
 */
int32_t EOSSDK_Connect::GetLoggedInUsersCount()
{
    TRACE_FUNC();

    return 1;
}

/**
 * Fetch a product user id that is logged in. This product user id is in the Epic Online Services namespace
 *
 * @param Index an index into the list of logged in users. If the index is out of bounds, the returned product user id will be invalid.
 *
 * @return the product user id associated with the index passed
 */
EOS_ProductUserId EOSSDK_Connect::GetLoggedInUserByIndex(int32_t Index)
{
    TRACE_FUNC();

    if (Index == 0)
        return Settings::Inst().productuserid;

    return GetInvalidProductUserId();
}

/**
 * Fetches the login status for an product user id.  This product user id is considered logged in as long as the underlying access token has not expired.
 *
 * @param LocalUserId the product user id of the user being queried
 *
 * @return the enum value of a user's login status
 */
EOS_ELoginStatus EOSSDK_Connect::GetLoginStatus(EOS_ProductUserId LocalUserId)
{
    TRACE_FUNC();

    if (LocalUserId == Settings::Inst().productuserid)
        return (get_myself()->second.connected ? EOS_ELoginStatus::EOS_LS_LoggedIn : EOS_ELoginStatus::EOS_LS_NotLoggedIn);

    return EOS_ELoginStatus::EOS_LS_NotLoggedIn;
}

/**
 * Register to receive upcoming authentication expiration notifications.
 * Notification is approximately 10 minutes prior to expiration.
 * Call EOS_Connect_Login again with valid third party credentials to refresh access
 *
 * @note must call RemoveNotifyAuthExpiration to remove the notification
 *
 * @param Options structure containing the API version of the callback to use
 * @param ClientData arbitrary data that is passed back to you in the callback
 * @param Notification a callback that is fired when the authentication is about to expire
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Connect::AddNotifyAuthExpiration(const EOS_Connect_AddNotifyAuthExpirationOptions* Options, void* ClientData, const EOS_Connect_OnAuthExpirationCallback Notification)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Notification == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Connect_AuthExpirationCallbackInfo& aeci = res->CreateCallback<EOS_Connect_AuthExpirationCallbackInfo>((CallbackFunc)Notification);

    aeci.ClientData = ClientData;
    aeci.LocalUserId = Settings::Inst().productuserid;

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving expiration notifications.
 *
 * @param InId handle representing the registered callback
 */
void EOSSDK_Connect::RemoveNotifyAuthExpiration(EOS_NotificationId InId)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive user login status updates.
 * @note must call RemoveNotifyLoginStatusChanged to remove the notification
 *
 * @param Options structure containing the API version of the callback to use
 * @param ClientData arbitrary data that is passed back to you in the callback
 * @param Notification a callback that is fired when the login status for a user changes
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Connect::AddNotifyLoginStatusChanged(const EOS_Connect_AddNotifyLoginStatusChangedOptions* Options, void* ClientData, const EOS_Connect_OnLoginStatusChangedCallback Notification)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Notification == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Connect_LoginStatusChangedCallbackInfo& lscci = res->CreateCallback<EOS_Connect_LoginStatusChangedCallbackInfo>((CallbackFunc)Notification);

    lscci.ClientData = ClientData;
    lscci.PreviousStatus = EOS_ELoginStatus::EOS_LS_LoggedIn;
    lscci.CurrentStatus = EOS_ELoginStatus::EOS_LS_LoggedIn;
    lscci.LocalUserId = Settings::Inst().productuserid;

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving user login status updates.
 *
 * @param InId handle representing the registered callback
 */
void EOSSDK_Connect::RemoveNotifyLoginStatusChanged(EOS_NotificationId InId)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    GetCB_Manager().remove_notification(this, InId);
}


/**
 * Fetch the number of linked external accounts for a product user id.
 *
 * @param Options The Options associated with retrieving the external account info count.
 *
 * @see EOS_Connect_CopyProductUserExternalAccountByIndex
 *
 * @return Number of external accounts or 0 otherwise
 */
uint32_t EOSSDK_Connect::GetProductUserExternalAccountCount(const EOS_Connect_GetProductUserExternalAccountCountOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->TargetUserId == nullptr)
        return 0;

    Connect_Infos_pb const* infos = get_connect_infos_for_product_user(Options->TargetUserId);
    if (infos == nullptr)
        return 0;

    uint32_t count = 0;
    if (!infos->userid().empty())
        ++count;
    if (!get_connect_steam64(*infos).empty() || (Options->TargetUserId == Settings::Inst().productuserid && !Settings::Inst().steam64.empty()))
        ++count;
    return count;
}

/**
 * Fetch information about an external account linked to a product user id.
 * On a successful call, the caller must release the returned structure using the EOS_Connect_ExternalAccountInfo_Release API.
 *
 * @param Options Structure containing the target index.
 * @param OutExternalAccountInfo The external account info data for the user with given index.
 *
 * @see EOS_Connect_ExternalAccountInfo_Release
 *
 * @return An EOS_EResult that indicates the external account data was copied into the OutExternalAccountInfo
 *         EOS_Success if the information is available and passed out in OutExternalAccountInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the account data doesn't exist or hasn't been queried yet
 */
EOS_EResult EOSSDK_Connect::CopyProductUserExternalAccountByIndex(const EOS_Connect_CopyProductUserExternalAccountByIndexOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutExternalAccountInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr)
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions type_opts{};
    type_opts.ApiVersion = EOS_CONNECT_COPYPRODUCTUSEREXTERNALACCOUNTBYACCOUNTTYPE_API_LATEST;
    type_opts.TargetUserId = Options->TargetUserId;

    if (Options->ExternalAccountInfoIndex == 0)
    {
        type_opts.AccountIdType = EOS_EExternalAccountType::EOS_EAT_STEAM;
        return CopyProductUserExternalAccountByAccountType(&type_opts, OutExternalAccountInfo);
    }

    if (Options->ExternalAccountInfoIndex == 1)
    {
        type_opts.AccountIdType = EOS_EExternalAccountType::EOS_EAT_EPIC;
        return CopyProductUserExternalAccountByAccountType(&type_opts, OutExternalAccountInfo);
    }

    set_nullptr(OutExternalAccountInfo);
    return EOS_EResult::EOS_NotFound;
}

/**
 * Fetch information about an external account of a specific type linked to a product user id.
 * On a successful call, the caller must release the returned structure using the EOS_Connect_ExternalAccountInfo_Release API.
 *
 * @param Options Structure containing the target external account type.
 * @param OutExternalAccountInfo The external account info data for the user with given external account type.
 *
 * @see EOS_Connect_ExternalAccountInfo_Release
 *
 * @return An EOS_EResult that indicates the external account data was copied into the OutExternalAccountInfo
 *         EOS_Success if the information is available and passed out in OutExternalAccountInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the account data doesn't exist or hasn't been queried yet
 */
EOS_EResult EOSSDK_Connect::CopyProductUserExternalAccountByAccountType(const EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutExternalAccountInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr)
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Connect_Infos_pb const* infos = get_connect_infos_for_product_user(Options->TargetUserId);
    if (infos == nullptr)
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_NotFound;
    }

    switch (Options->AccountIdType)
    {
        case EOS_EExternalAccountType::EOS_EAT_STEAM:
        {
            std::string const steam_id = !get_connect_steam64(*infos).empty()
                ? get_connect_steam64(*infos)
                : (Options->TargetUserId == Settings::Inst().productuserid ? Settings::Inst().steam64 : std::string());
            if (steam_id.empty())
            {
                set_nullptr(OutExternalAccountInfo);
                return EOS_EResult::EOS_NotFound;
            }

            std::string const display_name = !infos->displayname().empty() ? infos->displayname() : Settings::Inst().username;
            *OutExternalAccountInfo = make_connect_external_account(
                Options->TargetUserId,
                display_name,
                steam_id,
                EOS_EExternalAccountType::EOS_EAT_STEAM);
            return EOS_EResult::EOS_Success;
        }
        case EOS_EExternalAccountType::EOS_EAT_EPIC:
        {
            if (infos->userid().empty())
            {
                set_nullptr(OutExternalAccountInfo);
                return EOS_EResult::EOS_NotFound;
            }

            *OutExternalAccountInfo = make_connect_external_account(
                Options->TargetUserId,
                infos->displayname().empty() ? Settings::Inst().username : infos->displayname(),
                infos->userid(),
                EOS_EExternalAccountType::EOS_EAT_EPIC);
            return EOS_EResult::EOS_Success;
        }
        default:
            break;
    }

    set_nullptr(OutExternalAccountInfo);
    return EOS_EResult::EOS_NotFound;
}

/**
 * Fetch information about an external account linked to a product user id.
 * On a successful call, the caller must release the returned structure using the EOS_Connect_ExternalAccountInfo_Release API.
 *
 * @param Options Structure containing the target external account id.
 * @param OutExternalAccountInfo The external account info data for the user with given external account id.
 *
 * @see EOS_Connect_ExternalAccountInfo_Release
 *
 * @return An EOS_EResult that indicates the external account data was copied into the OutExternalAccountInfo
 *         EOS_Success if the information is available and passed out in OutExternalAccountInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the account data doesn't exist or hasn't been queried yet
 */
EOS_EResult EOSSDK_Connect::CopyProductUserExternalAccountByAccountId(const EOS_Connect_CopyProductUserExternalAccountByAccountIdOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutExternalAccountInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr || Options->AccountId == nullptr)
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Connect_Infos_pb const* infos = get_connect_infos_for_product_user(Options->TargetUserId);
    if (infos == nullptr)
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_NotFound;
    }

    std::string const account_id = Options->AccountId;
    EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions type_opts{};
    type_opts.ApiVersion = EOS_CONNECT_COPYPRODUCTUSEREXTERNALACCOUNTBYACCOUNTTYPE_API_LATEST;
    type_opts.TargetUserId = Options->TargetUserId;

    if (account_id == get_connect_steam64(*infos) ||
        (Options->TargetUserId == Settings::Inst().productuserid && account_id == Settings::Inst().steam64))
    {
        type_opts.AccountIdType = EOS_EExternalAccountType::EOS_EAT_STEAM;
    }
    else if (account_id == infos->userid())
    {
        type_opts.AccountIdType = EOS_EExternalAccountType::EOS_EAT_EPIC;
    }
    else
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_NotFound;
    }

    return CopyProductUserExternalAccountByAccountType(&type_opts, OutExternalAccountInfo);
}

/**
 * Fetch information about a Product User, using the external account that they most recently logged in with as the reference.
 * On a successful call, the caller must release the returned structure using the EOS_Connect_ExternalAccountInfo_Release API.
 *
 * @param Options Structure containing the target external account id.
 * @param OutExternalAccountInfo The external account info data last logged in for the user.
 *
 * @see EOS_Connect_ExternalAccountInfo_Release
 *
 * @return An EOS_EResult that indicates the external account data was copied into the OutExternalAccountInfo
 *         EOS_Success if the information is available and passed out in OutExternalAccountInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the account data doesn't exist or hasn't been queried yet
 */
EOS_EResult EOSSDK_Connect::CopyProductUserInfo(const EOS_Connect_CopyProductUserInfoOptions* Options, EOS_Connect_ExternalAccountInfo** OutExternalAccountInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->TargetUserId == nullptr)
    {
        set_nullptr(OutExternalAccountInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOS_Connect_CopyProductUserExternalAccountByAccountTypeOptions steam_opts{};
    steam_opts.ApiVersion = EOS_CONNECT_COPYPRODUCTUSEREXTERNALACCOUNTBYACCOUNTTYPE_API_LATEST;
    steam_opts.TargetUserId = Options->TargetUserId;
    steam_opts.AccountIdType = EOS_EExternalAccountType::EOS_EAT_STEAM;
    EOS_EResult const steam_result = CopyProductUserExternalAccountByAccountType(&steam_opts, OutExternalAccountInfo);
    if (steam_result == EOS_EResult::EOS_Success)
        return steam_result;

    steam_opts.AccountIdType = EOS_EExternalAccountType::EOS_EAT_EPIC;
    return CopyProductUserExternalAccountByAccountType(&steam_opts, OutExternalAccountInfo);
}

///////////////////////////////////////////////////////////////////////////////
//                           Network Send messages                           //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Connect::send_connect_infos_request(Network::peer_t const& peerid, Connect_Request_Info_pb* req)
{
    //TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Connect_Message_pb* conn = new Connect_Message_pb;

    conn->set_allocated_request(req);
    msg.set_allocated_connect(conn);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_Connect::send_connect_infos(Network::peer_t const& peerid, Connect_Infos_pb* infos)
{
    //TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Connect_Message_pb* conn = new Connect_Message_pb;

    conn->set_allocated_infos(infos);
    msg.set_allocated_connect(conn);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().TCPSendTo(msg);
}

///////////////////////////////////////////////////////////////////////////////
//                          Network Receive messages                         //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Connect::on_peer_connect(Network_Message_pb const& msg, Network_Peer_Connect_pb const& peer)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    EOS_ProductUserId product_id = GetProductUserId(msg.source_id());
    GetProductUserId(msg.source_id());
    auto& user = _users[product_id];
    user.connected = true;
    if (!user.authentified)
    {
        user.infos = Connect_Infos_pb{};
        user.last_infos = std::chrono::steady_clock::time_point{};
    }

    Connect_Request_Info_pb* req = new Connect_Request_Info_pb;
    send_connect_infos_request(product_id->to_string(), req);

    // Early presence bootstrap before Connect auth handshake completes.
    GetEOS_Presence().send_my_presence_info(msg.source_id());

    return true;
}

bool EOSSDK_Connect::on_peer_disconnect(Network_Message_pb const& msg, Network_Peer_Disconnect_pb const& peer)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    // Presence need to know when a user disconnects
    GetEOS_Presence().on_peer_disconnect(msg, peer);
    GetEOS_Lobby().on_peer_disconnect(msg, peer);
    GetEOS_Sessions().on_peer_disconnect(msg, peer);
    GetEOS_P2P().on_peer_disconnect(msg, peer);

    EOS_ProductUserId product_id = GetProductUserId(msg.source_id());
    _users[product_id].connected = false;
    _users[product_id].authentified = false;

    return true;
}

bool EOSSDK_Connect::on_connect_infos_request(Network_Message_pb const& msg, Connect_Request_Info_pb const& req)
{
    //TRACE_FUNC();
    GLOBAL_LOCK();

    Connect_Infos_pb* infos = new Connect_Infos_pb;

    infos->set_userid(Settings::Inst().userid->to_string());
    infos->set_displayname(Settings::Inst().username);
    set_connect_steam64(*infos, Settings::Inst().steam64);

    return send_connect_infos(msg.source_id(), infos);
}

bool EOSSDK_Connect::on_connect_infos(Network_Message_pb const& msg, Connect_Infos_pb const& infos)
{
    //TRACE_FUNC();
    GLOBAL_LOCK();

    auto& user = _users[GetProductUserId(msg.source_id())];

    if (!user.connected)
    {
        APP_LOG(Log::LogLevel::INFO, "Connect infos received before peer_connect from %s", msg.source_id().c_str());
        user.connected = true;
        user.infos = Connect_Infos_pb{};
        user.last_infos = std::chrono::steady_clock::time_point{};
    }

    user.infos = infos;
    user.last_infos = std::chrono::steady_clock::now();

    EOS_EpicAccountId account_id = GetEpicUserId(user.infos.userid());
    if (account_id->IsValid())
        GetEOS_UserInfo().cache_userinfo_from_connect(account_id, user.infos);

    if (!user.authentified)
    {
        user.authentified = true;

        if (account_id->IsValid())
        {
            GetEOS_Friends().add_friend(account_id);
            GetEOS_Presence().ensure_default_peer_presence(account_id);
        }

        GetEOS_Friends().sync_friends_from_connect();
        GetEOS_Friends().complete_pending_query_friends();

        APP_LOG(Log::LogLevel::INFO, "Connect peer authenticated: product=%s epic=%s from=%s",
            msg.source_id().c_str(),
            user.infos.userid().c_str(),
            msg.source_id().c_str());

        // Bootstrap social layer whenever a peer becomes authenticated.
        // Do not gate on first_connect: peer_connect may arrive before connect_infos and skip this path.
        {
            Network_Peer_Connect_pb connect;
            GetEOS_Presence().on_peer_connect(msg, connect);
            GetEOS_P2P().on_peer_connect(msg, connect);
            GetEOS_Lobby().on_peer_authenticated(msg.source_id());
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//                                 IRunFrame                                 //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Connect::CBRunFrame()
{
    GLOBAL_LOCK();

    if (!get_myself()->second.connected)
        return true;

    auto now = std::chrono::steady_clock::now();

    auto user_it = _users.begin();
    ++user_it;
    for (; user_it != _users.end(); ++user_it)
    {
        if (!user_it->second.connected)
            continue;

        if ((now - user_it->second.last_infos) > user_infos_rate)
        {
            Connect_Request_Info_pb* req = new Connect_Request_Info_pb;
            send_connect_infos_request(user_it->first->to_string(), req);
            user_it->second.last_infos = now;
        }
    }

    return true;
}

std::string EOSSDK_Connect::peer_id_for_steam_id(std::string const& steam_id)
{
    if (steam_id.empty())
        return {};

    for (auto it = get_all_users(); it != get_end_users(); ++it)
    {
        std::string const peer_steam = get_connect_steam64(it->second.infos);
        if (!peer_steam.empty() && peer_steam == steam_id)
            return it->first->to_string();
    }
    return {};
}

void EOSSDK_Connect::request_infos_from_all_peers()
{
    for (auto it = get_other_users(); it != get_end_users(); ++it)
    {
        if (!it->second.connected)
            continue;

        Connect_Request_Info_pb* req = new Connect_Request_Info_pb;
        send_connect_infos_request(it->first->to_string(), req);
    }
}

bool EOSSDK_Connect::RunNetwork(Network_Message_pb const& msg)
{
    if (GetProductUserId(msg.source_id()) == Settings::Inst().productuserid)
        return true;

    switch (msg.messages_case())
    {
        case Network_Message_pb::MessagesCase::kNetworkAdvertise:
        {
            Network_Advertise_pb const& adv = msg.network_advertise();
            switch (adv.message_case())
            {
                case Network_Advertise_pb::MessageCase::kPeerConnect   : return on_peer_connect(msg, adv.peer_connect());
                case Network_Advertise_pb::MessageCase::kPeerDisconnect: return on_peer_disconnect(msg, adv.peer_disconnect());
            }
        }
        break;

        case Network_Message_pb::MessagesCase::kConnect:
        {
            Connect_Message_pb const& conn = msg.connect();
            switch (conn.message_case())
            {
                case Connect_Message_pb::MessageCase::kRequest  : return on_connect_infos_request(msg, conn.request());
                case Connect_Message_pb::MessageCase::kInfos    : return on_connect_infos(msg, conn.infos());
                default: APP_LOG(Log::LogLevel::WARN, "Unhandled network message %d", conn.message_case());
            }
        }
        break;
    }

    return true;
}

bool EOSSDK_Connect::RunCallbacks(pFrameResult_t res)
{
    GLOBAL_LOCK();

    return res->done;
}

void EOSSDK_Connect::FreeCallback(pFrameResult_t res)
{
    GLOBAL_LOCK();

    //switch (res->res.m_iCallback)
    //{
    //    /////////////////////////////
    //    //        Callbacks        //
    //    /////////////////////////////
    //    case EOS_Connect_LoginCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_LoginCallbackInfo& callback = res->GetCallback<EOS_Connect_LoginCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_CreateUserCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_CreateUserCallbackInfo& callback = res->GetCallback<EOS_Connect_CreateUserCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_LinkAccountCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_LinkAccountCallbackInfo& callback = res->GetCallback<EOS_Connect_LinkAccountCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_CreateDeviceIdCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_CreateDeviceIdCallbackInfo& callback = res->GetCallback<EOS_Connect_CreateDeviceIdCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_DeleteDeviceIdCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_DeleteDeviceIdCallbackInfo& callback = res->GetCallback<EOS_Connect_DeleteDeviceIdCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_QueryExternalAccountMappingsCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_QueryExternalAccountMappingsCallbackInfo& callback = res->GetCallback<EOS_Connect_QueryExternalAccountMappingsCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_QueryProductUserIdMappingsCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_QueryProductUserIdMappingsCallbackInfo& callback = res->GetCallback<EOS_Connect_QueryProductUserIdMappingsCallbackInfo>();
    //    }
    //    break;
    //    /////////////////////////////
    //    //      Notifications      //
    //    /////////////////////////////
    //    case EOS_Connect_AuthExpirationCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_AuthExpirationCallbackInfo& callback = res->GetCallback<EOS_Connect_AuthExpirationCallbackInfo>();
    //    }
    //    break;
    //    
    //    case EOS_Connect_LoginStatusChangedCallbackInfo::k_iCallback:
    //    {
    //        EOS_Connect_LoginStatusChangedCallbackInfo& callback = res->GetCallback<EOS_Connect_LoginStatusChangedCallbackInfo>();
    //    }
    //    break;
    //}
}

}