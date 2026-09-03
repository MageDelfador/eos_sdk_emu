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

#include "eossdk_userinfo.h"
#include "eossdk_platform.h"
#include "eos_client_api.h"
#include "settings.h"

#include <cstring>

namespace sdk
{

namespace
{
char* dup_cstr(std::string const& s)
{
    if (s.empty())
        return nullptr;
    char* out = new char[s.size() + 1];
    std::memcpy(out, s.c_str(), s.size() + 1);
    return out;
}

void fill_userinfo_strings(EOS_UserInfo002* infos, UserInfo_Info_pb const* userinfo)
{
    infos->Country = dup_cstr(userinfo->country());
    infos->PreferredLanguage = dup_cstr(userinfo->preferredlanguage());
    infos->DisplayName = dup_cstr(userinfo->displayname());
    infos->Nickname = dup_cstr(userinfo->nickname());
}

void merge_userinfo_pb(UserInfo_Info_pb& dst, UserInfo_Info_pb const& src)
{
    if (!src.displayname().empty())
    {
        dst.set_displayname(src.displayname());
        if (dst.nickname().empty())
            dst.set_nickname(src.displayname());
    }
    if (!src.nickname().empty())
        dst.set_nickname(src.nickname());
    if (!src.country().empty())
        dst.set_country(src.country());
    if (!src.preferredlanguage().empty())
        dst.set_preferredlanguage(src.preferredlanguage());
}

std::string get_connect_steam64(Connect_Infos_pb const& infos)
{
    auto it = infos.sessions().find("steam64");
    return it != infos.sessions().end() ? it->second : std::string();
}

EOS_UserInfo_ExternalUserInfo* make_external_userinfo(
    EOS_EExternalAccountType account_type,
    std::string const& account_id,
    std::string const& display_name)
{
    EOS_UserInfo_ExternalUserInfo* info = new EOS_UserInfo_ExternalUserInfo();
    info->ApiVersion = EOS_USERINFO_EXTERNALUSERINFO_API_LATEST;
    info->AccountType = account_type;
    info->AccountId = dup_cstr(account_id);
    info->DisplayName = dup_cstr(display_name);
    info->DisplayNameSanitized = dup_cstr(display_name);
    return info;
}

Connect_Infos_pb const* get_connect_infos_for_epic_user(EOS_EpicAccountId userid)
{
    if (userid == nullptr || !userid->IsValid())
        return nullptr;

    auto user = GetEOS_Connect().get_user_by_userid(userid);
    if (user == GetEOS_Connect().get_end_users())
        return nullptr;

    return &user->second.infos;
}
}

decltype(EOSSDK_UserInfo::userinfo_query_timeout) EOSSDK_UserInfo::userinfo_query_timeout;

EOSSDK_UserInfo::EOSSDK_UserInfo()
{
    try
    {
        GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kUserinfo);
    }
    catch (...)
    {
        APP_LOG(Log::LogLevel::WARN, "UserInfo network listener registration failed");
    }
    GetCB_Manager().register_callbacks(this);
    GetCB_Manager().register_frame(this);
}

EOSSDK_UserInfo::~EOSSDK_UserInfo()
{
    GetCB_Manager().unregister_frame(this);
    GetCB_Manager().unregister_callbacks(this);
    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kUserinfo);
}

void EOSSDK_UserInfo::setup_myself()
{
    auto& userinfo = get_myself();

    userinfo.set_country("");
    userinfo.set_displayname(Settings::Inst().username);
    userinfo.set_nickname(Settings::Inst().username);
    userinfo.set_preferredlanguage(Settings::Inst().language);
}

UserInfo_Info_pb& EOSSDK_UserInfo::get_myself()
{
    return _userinfos[Settings::Inst().userid];
}

UserInfo_Info_pb* EOSSDK_UserInfo::get_userinfo(EOS_EpicAccountId userid)
{
    auto it = _userinfos.find(userid);
    if (it != _userinfos.end())
        return &it->second;

    return nullptr;
}

void EOSSDK_UserInfo::cache_userinfo_from_connect(EOS_EpicAccountId userid, Connect_Infos_pb const& infos)
{
    if (userid == nullptr || !userid->IsValid())
        return;

    UserInfo_Info_pb& cached = _userinfos[userid];
    if (!infos.displayname().empty())
    {
        cached.set_displayname(infos.displayname());
        if (cached.nickname().empty())
            cached.set_nickname(infos.displayname());
    }
}

UserInfo_Info_pb* EOSSDK_UserInfo::resolve_userinfo(EOS_EpicAccountId userid)
{
    UserInfo_Info_pb* userinfo = get_userinfo(userid);
    if (userinfo != nullptr && !userinfo->displayname().empty())
        return userinfo;

    auto user = GetEOS_Connect().get_user_by_userid(userid);
    if (user != GetEOS_Connect().get_end_users())
    {
        cache_userinfo_from_connect(userid, user->second.infos);
        userinfo = get_userinfo(userid);
        if (userinfo != nullptr && !userinfo->displayname().empty())
            return userinfo;

        if (!user->second.infos.displayname().empty())
        {
            UserInfo_Info_pb& cached = _userinfos[userid];
            cached.set_displayname(user->second.infos.displayname());
            cached.set_nickname(user->second.infos.displayname());
            return &cached;
        }
    }

    return userinfo;
}

/**
 * The UserInfo Interface is used to receive user information for Epic account IDs from the backend services and to retrieve that information once it is cached.
 * All UserInfo Interface calls take a handle of type EOS_HUserInfo as the first parameter.
 * This handle can be retrieved from a EOS_HPlatform handle by using the EOS_Platform_GetUserInfoInterface function.
 *
 * NOTE: At this time, this feature is only available for products that are part of the Epic Games store.
 *
 * @see EOS_Platform_GetUserInfoInterface
 */

/**
  * EOS_UserInfo_QueryUserInfo is used to start an asynchronous query to retrieve information, such as display name, about another account.
  * Once the callback has been fired with a successful ResultCode, it is possible to call EOS_UserInfo_CopyUserInfo to receive an EOS_UserInfo containing the available information.
  *
  * @param Options structure containing the input parameters
  * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
  * @param CompletionDelegate a callback that is fired when the async operation completes, either successfully or in error
  *
  * @see EOS_UserInfo
  * @see EOS_UserInfo_CopyUserInfo
  * @see EOS_UserInfo_QueryUserInfoOptions
  * @see EOS_UserInfo_OnQueryUserInfoCallback
  */
void EOSSDK_UserInfo::QueryUserInfo(const EOS_UserInfo_QueryUserInfoOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    APP_LOG(Log::LogLevel::DEBUG, "Query infos of %s", Options->TargetUserId->to_string().c_str());

    pFrameResult_t res(new FrameResult);
    EOS_UserInfo_QueryUserInfoCallbackInfo& quici = res->CreateCallback<EOS_UserInfo_QueryUserInfoCallbackInfo>((CallbackFunc)CompletionDelegate);
    quici.ClientData = ClientData;
    quici.LocalUserId = Settings::Inst().userid;

    if (Options == nullptr || Options->TargetUserId == nullptr)
    {
        quici.TargetUserId = GetEpicUserId(sdk::NULL_USER_ID);
        quici.ResultCode = EOS_EResult::EOS_InvalidParameters;

        res->done = true;
    }
    else
    {
        quici.TargetUserId = Options->TargetUserId;

        auto user = GetEOS_Connect().get_user_by_userid(Options->TargetUserId);
        if (user == GetEOS_Connect().get_end_users())
        {
            quici.ResultCode = EOS_EResult::EOS_NotFound;
            res->done = true;
        }
        else if (user->first == Settings::Inst().productuserid)
        {
            quici.ResultCode = EOS_EResult::EOS_Success;
            res->done = true;
        }
        else if(user->second.connected)
        {
            cache_userinfo_from_connect(Options->TargetUserId, user->second.infos);

            if (get_userinfo(Options->TargetUserId) != nullptr &&
                !get_userinfo(Options->TargetUserId)->displayname().empty())
            {
                quici.ResultCode = EOS_EResult::EOS_Success;
                res->done = true;
            }
            else
            {
                _userinfos_queries[Options->TargetUserId].push_back(res);

                UserInfo_Info_Request_pb* request = new UserInfo_Info_Request_pb;
                send_userinfo_request(user->first->to_string(), request);
            }
        }
        else
        {
            quici.ResultCode = EOS_EResult::EOS_NotFound;
            res->done = true;
        }
    }

    GetCB_Manager().add_callback(this, res);
}

/**
 * EOS_UserInfo_QueryUserInfoByDisplayName is used to start an asynchronous query to retrieve user information by display name. This can be useful for getting the EOS_EpicAccountId for a display name.
 * Once the callback has been fired with a successful ResultCode, it is possible to call EOS_UserInfo_CopyUserInfo to receive an EOS_UserInfo containing the available information.
 *
 * @param Options structure containing the input parameters
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the async operation completes, either successfully or in error
 *
 * @see EOS_UserInfo
 * @see EOS_UserInfo_CopyUserInfo
 * @see EOS_UserInfo_QueryUserInfoByDisplayNameOptions
 * @see EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback
 */
void EOSSDK_UserInfo::QueryUserInfoByDisplayName(const EOS_UserInfo_QueryUserInfoByDisplayNameOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    APP_LOG(Log::LogLevel::DEBUG, "Query infos of %s", Options->DisplayName);

    pFrameResult_t res(new FrameResult);
    EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo& quibdnci = res->CreateCallback<EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo>((CallbackFunc)CompletionDelegate);
    quibdnci.ClientData = ClientData;
    quibdnci.LocalUserId = Settings::Inst().userid;

    if (Options == nullptr || Options->DisplayName == nullptr)
    {
        quibdnci.TargetUserId = GetEpicUserId(sdk::NULL_USER_ID);
        quibdnci.ResultCode = EOS_EResult::EOS_InvalidParameters;

        res->done = true;
    }
    else
    {
        auto user = GetEOS_Connect().get_user_by_name(Options->DisplayName);
        if (user == GetEOS_Connect().get_end_users())
        {
            quibdnci.ResultCode = EOS_EResult::EOS_NotFound;
            res->done = true;
        }
        else if (user->first == Settings::Inst().productuserid)
        {
            quibdnci.ResultCode = EOS_EResult::EOS_Success;
            res->done = true;
        }
        else if(user->second.connected)
        {
            quibdnci.TargetUserId = GetEpicUserId(user->second.infos.userid());
            _userinfos_queries[quibdnci.TargetUserId].push_back(res);

            UserInfo_Info_Request_pb* request = new UserInfo_Info_Request_pb;
            send_userinfo_request(user->first->to_string(), request);
        }
        else
        {
            quibdnci.ResultCode = EOS_EResult::EOS_NotFound;
            res->done = true;
        }
    }

    GetCB_Manager().add_callback(this, res);
}

/**
 * EOS_UserInfo_QueryUserInfoByExternalAccount is used to start an asynchronous query to retrieve user information by external accounts.
 * This can be useful for getting the EOS_EpicAccountIds for external accounts.
 * Once the callback has been fired with a successful ResultCode, it is possible to call CopyUserInfo to receive an EOS_UserInfo containing the available information.
 *
 * @param Options structure containing the input parameters
 * @param ClientData arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate a callback that is fired when the async operation completes, either successfully or in error
 *
 * @see EOS_UserInfo
 * @see EOS_UserInfo_QueryUserInfoByExternalAccountOptions
 * @see EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback
 */
void EOSSDK_UserInfo::QueryUserInfoByExternalAccount(const EOS_UserInfo_QueryUserInfoByExternalAccountOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo& quibeaci = res->CreateCallback<EOS_UserInfo_QueryUserInfoByExternalAccountCallbackInfo>((CallbackFunc)CompletionDelegate);
    
    quibeaci.ClientData = ClientData;
    quibeaci.AccountType = Options->AccountType;
    quibeaci.ExternalAccountId = "";
    quibeaci.LocalUserId = Settings::Inst().userid;
    quibeaci.TargetUserId = GetInvalidEpicUserId();
    quibeaci.ResultCode = EOS_EResult::EOS_UnexpectedError;

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * EOS_UserInfo_CopyUserInfo is used to immediately retrieve a copy of user information for an account ID, cached by a previous call to EOS_UserInfo_QueryUserInfo.
 * If the call returns an EOS_Success result, the out parameter, OutUserInfo, must be passed to EOS_UserInfo_Release to release the memory associated with it.
 *
 * @param Options structure containing the input parameters
 * @param OutUserInfo out parameter used to receive the EOS_UserInfo structure.
 *
 * @return EOS_Success if the information is available and passed out in OutUserInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound if the user info is not locally cached. The information must have been previously cached by a call to EOS_UserInfo_QueryUserInfo
 *
 * @see EOS_UserInfo
 * @see EOS_UserInfo_CopyUserInfoOptions
 * @see EOS_UserInfo_Release
 */
EOS_EResult EOSSDK_UserInfo::CopyUserInfo(const EOS_UserInfo_CopyUserInfoOptions* Options, void** OutUserInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutUserInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr || OutUserInfo == nullptr)
    {
        set_nullptr(OutUserInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    APP_LOG(Log::LogLevel::DEBUG, "Copy infos of %s", Options->TargetUserId->to_string().c_str());

    UserInfo_Info_pb* userinfo = resolve_userinfo(Options->TargetUserId);

    switch (Options->ApiVersion) {
    case EOS_USERINFO_COPYUSERINFO_API_003: {
            EOS_UserInfo003* infos = new EOS_UserInfo003();
            *OutUserInfo = infos;

            if (userinfo == nullptr)
            {
                memset(infos, 0, sizeof(*infos));
                return EOS_EResult::EOS_NotFound;
            }
            infos->ApiVersion = Options->ApiVersion;
            fill_userinfo_strings(reinterpret_cast<EOS_UserInfo002*>(infos), userinfo);
            infos->UserId = Options->TargetUserId;
            infos->DisplayNameSanitized = dup_cstr(userinfo->displayname());
            break;
        }
        case EOS_USERINFO_COPYUSERINFO_API_002:
        case EOS_USERINFO_COPYUSERINFO_API_001:
        {
            EOS_UserInfo002* infos = new EOS_UserInfo002();
            *OutUserInfo = infos;

            if (userinfo == nullptr)
            {
                memset(infos, 0, sizeof(*infos));
                return EOS_EResult::EOS_NotFound;
            }
            infos->ApiVersion = Options->ApiVersion;
            fill_userinfo_strings(reinterpret_cast<EOS_UserInfo002*>(infos), userinfo);
            infos->UserId = Options->TargetUserId;
        }
    }


    if (userinfo != nullptr)
    {
        APP_LOG(Log::LogLevel::DEBUG, "CopyUserInfo %s displayname='%s'",
            Options->TargetUserId->to_string().c_str(),
            userinfo->displayname().c_str());
    }

    return EOS_EResult::EOS_Success;
}

EOS_EResult EOSSDK_UserInfo::CopyBestDisplayName(const EOS_UserInfo_CopyBestDisplayNameOptions* Options, EOS_UserInfo_BestDisplayName** OutBestDisplayName)
{
    if (Options == nullptr)
    {
        set_nullptr(OutBestDisplayName);
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions platform_opts{};
    platform_opts.ApiVersion = EOS_USERINFO_COPYBESTDISPLAYNAMEWITHPLATFORM_API_LATEST;
    platform_opts.LocalUserId = Options->LocalUserId;
    platform_opts.TargetUserId = Options->TargetUserId;
    platform_opts.TargetPlatformType = EOS_OPT_Steam;
    return CopyBestDisplayNameWithPlatform(&platform_opts, OutBestDisplayName);
}

EOS_EResult EOSSDK_UserInfo::CopyBestDisplayNameWithPlatform(const EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions* Options, EOS_UserInfo_BestDisplayName** OutBestDisplayName)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutBestDisplayName == nullptr || Options == nullptr || Options->TargetUserId == nullptr)
    {
        set_nullptr(OutBestDisplayName);
        return EOS_EResult::EOS_InvalidParameters;
    }

    UserInfo_Info_pb* userinfo = resolve_userinfo(Options->TargetUserId);
    if (userinfo == nullptr || userinfo->displayname().empty())
    {
        set_nullptr(OutBestDisplayName);
        return EOS_EResult::EOS_NotFound;
    }

    EOS_UserInfo_BestDisplayName* best = new EOS_UserInfo_BestDisplayName();
    best->ApiVersion = EOS_USERINFO_BESTDISPLAYNAME_API_LATEST;
    best->UserId = Options->TargetUserId;
    best->DisplayName = dup_cstr(userinfo->displayname());
    best->DisplayNameSanitized = dup_cstr(userinfo->displayname());
    best->Nickname = dup_cstr(userinfo->nickname().empty() ? userinfo->displayname() : userinfo->nickname());
    best->PlatformType = Options->TargetPlatformType != EOS_OPT_Unknown ? Options->TargetPlatformType : EOS_OPT_Steam;

    *OutBestDisplayName = best;
    APP_LOG(Log::LogLevel::DEBUG, "CopyBestDisplayName for %s -> %s",
        Options->TargetUserId->to_string().c_str(), userinfo->displayname().c_str());
    return EOS_EResult::EOS_Success;
}

EOS_OnlinePlatformType EOSSDK_UserInfo::GetLocalPlatformType(const EOS_UserInfo_GetLocalPlatformTypeOptions* Options)
{
    TRACE_FUNC();
    (void)Options;
    return EOS_OPT_Steam;
}

/**
 * Fetch the number of external user infos that are cached locally.
 *
 * @param Options The options associated with retrieving the external user info count
 *
 * @see EOS_UserInfo_CopyExternalUserInfoByIndex
 *
 * @return The number of external user infos, or 0 if there is an error
 */
uint32_t EOSSDK_UserInfo::GetExternalUserInfoCount(const EOS_UserInfo_GetExternalUserInfoCountOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->TargetUserId == nullptr)
        return 0;

    Connect_Infos_pb const* infos = get_connect_infos_for_epic_user(Options->TargetUserId);
    if (infos == nullptr)
        return 0;

    uint32_t count = infos->userid().empty() ? 0 : 1;
    if (!get_connect_steam64(*infos).empty())
        ++count;
    return count;
}

/**
 * Fetches an external user info from a given index.
 *
 * @param Options Structure containing the index being accessed
 * @param OutExternalUserInfo The external user info. If it exists and is valid, use EOS_UserInfo_ExternalUserInfo_Release when finished
 *
 * @see EOS_UserInfo_ExternalUserInfo_Release
 *
 * @return EOS_Success if the information is available and passed out in OutExternalUserInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the external user info is not found
 */
EOS_EResult EOSSDK_UserInfo::CopyExternalUserInfoByIndex(const EOS_UserInfo_CopyExternalUserInfoByIndexOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutExternalUserInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr)
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions type_opts{};
    type_opts.ApiVersion = EOS_USERINFO_COPYEXTERNALUSERINFOBYACCOUNTTYPE_API_LATEST;
    type_opts.LocalUserId = Options->LocalUserId;
    type_opts.TargetUserId = Options->TargetUserId;

    Connect_Infos_pb const* infos = get_connect_infos_for_epic_user(Options->TargetUserId);
    if (infos == nullptr)
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_NotFound;
    }

    if (Options->Index == 0 && !get_connect_steam64(*infos).empty())
    {
        type_opts.AccountType = EOS_EExternalAccountType::EOS_EAT_STEAM;
        return CopyExternalUserInfoByAccountType(&type_opts, OutExternalUserInfo);
    }

    if ((Options->Index == 0 && get_connect_steam64(*infos).empty()) ||
        (Options->Index == 1 && !get_connect_steam64(*infos).empty()))
    {
        type_opts.AccountType = EOS_EExternalAccountType::EOS_EAT_EPIC;
        return CopyExternalUserInfoByAccountType(&type_opts, OutExternalUserInfo);
    }

    set_nullptr(OutExternalUserInfo);
    return EOS_EResult::EOS_NotFound;
}

/**
 * Fetches an external user info for a given external account type.
 *
 * @param Options Structure containing the account type being accessed
 * @param OutExternalUserInfo The external user info. If it exists and is valid, use EOS_UserInfo_ExternalUserInfo_Release when finished
 *
 * @see EOS_UserInfo_ExternalUserInfo_Release
 *
 * @return EOS_Success if the information is available and passed out in OutExternalUserInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the external user info is not found
 */
EOS_EResult EOSSDK_UserInfo::CopyExternalUserInfoByAccountType(const EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutExternalUserInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr)
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Connect_Infos_pb const* infos = get_connect_infos_for_epic_user(Options->TargetUserId);
    UserInfo_Info_pb* userinfo = resolve_userinfo(Options->TargetUserId);
    std::string const display_name = userinfo != nullptr && !userinfo->displayname().empty()
        ? userinfo->displayname()
        : (infos != nullptr ? infos->displayname() : std::string());

    if (infos == nullptr)
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_NotFound;
    }

    switch (Options->AccountType)
    {
        case EOS_EExternalAccountType::EOS_EAT_STEAM:
        {
            std::string const steam_id = get_connect_steam64(*infos);
            if (steam_id.empty())
            {
                set_nullptr(OutExternalUserInfo);
                return EOS_EResult::EOS_NotFound;
            }

            *OutExternalUserInfo = make_external_userinfo(EOS_EExternalAccountType::EOS_EAT_STEAM, steam_id, display_name);
            APP_LOG(Log::LogLevel::DEBUG, "CopyExternalUserInfo Steam %s -> %s",
                Options->TargetUserId->to_string().c_str(), display_name.c_str());
            return EOS_EResult::EOS_Success;
        }
        case EOS_EExternalAccountType::EOS_EAT_EPIC:
        {
            if (infos->userid().empty())
            {
                set_nullptr(OutExternalUserInfo);
                return EOS_EResult::EOS_NotFound;
            }

            *OutExternalUserInfo = make_external_userinfo(EOS_EExternalAccountType::EOS_EAT_EPIC, infos->userid(), display_name);
            return EOS_EResult::EOS_Success;
        }
        default:
            break;
    }

    set_nullptr(OutExternalUserInfo);
    return EOS_EResult::EOS_NotFound;
}

/**
 * Fetches an external user info for a given external account id.
 *
 * @param Options Structure containing the account id being accessed
 * @param OutExternalUserInfo The external user info. If it exists and is valid, use EOS_UserInfo_ExternalUserInfo_Release when finished
 *
 * @see EOS_UserInfo_ExternalUserInfo_Release
 *
 * @return EOS_Success if the information is available and passed out in OutExternalUserInfo
 *         EOS_InvalidParameters if you pass a null pointer for the out parameter
 *         EOS_NotFound if the external user info is not found
 */
EOS_EResult EOSSDK_UserInfo::CopyExternalUserInfoByAccountId(const EOS_UserInfo_CopyExternalUserInfoByAccountIdOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (OutExternalUserInfo == nullptr || Options == nullptr || Options->TargetUserId == nullptr || Options->AccountId == nullptr)
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Connect_Infos_pb const* infos = get_connect_infos_for_epic_user(Options->TargetUserId);
    if (infos == nullptr)
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_NotFound;
    }

    std::string const account_id = Options->AccountId;
    EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions type_opts{};
    type_opts.ApiVersion = EOS_USERINFO_COPYEXTERNALUSERINFOBYACCOUNTTYPE_API_LATEST;
    type_opts.LocalUserId = Options->LocalUserId;
    type_opts.TargetUserId = Options->TargetUserId;

    if (account_id == get_connect_steam64(*infos))
        type_opts.AccountType = EOS_EExternalAccountType::EOS_EAT_STEAM;
    else if (account_id == infos->userid())
        type_opts.AccountType = EOS_EExternalAccountType::EOS_EAT_EPIC;
    else
    {
        set_nullptr(OutExternalUserInfo);
        return EOS_EResult::EOS_NotFound;
    }

    return CopyExternalUserInfoByAccountType(&type_opts, OutExternalUserInfo);
}

///////////////////////////////////////////////////////////////////////////////
//                           Network Send messages                           //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_UserInfo::send_userinfo_request(Network::peer_t const& peerid, UserInfo_Info_Request_pb* req)
{
    Network_Message_pb msg;
    UserInfo_Message_pb* userinfo = new UserInfo_Message_pb;

    std::string const& userid = Settings::Inst().productuserid->to_string();

    userinfo->set_allocated_userinfo_info_request(req);
    msg.set_allocated_userinfo(userinfo);

    msg.set_source_id(userid);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_UserInfo::send_my_userinfo(Network::peer_t const& peerid)
{
    Network_Message_pb msg;
    UserInfo_Message_pb* userinfo = new UserInfo_Message_pb;

    std::string const& userid = Settings::Inst().productuserid->to_string();

    userinfo->set_allocated_userinfo_info(&get_myself());
    msg.set_allocated_userinfo(userinfo);

    msg.set_source_id(userid);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    auto res = GetNetwork().TCPSendTo(msg);
    (void)userinfo->release_userinfo_info();

    return res;
}

///////////////////////////////////////////////////////////////////////////////
//                          Network Receive messages                         //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_UserInfo::on_userinfo_request(Network_Message_pb const& msg, UserInfo_Info_Request_pb const& req)
{
    GLOBAL_LOCK();

    return send_my_userinfo(msg.source_id());
}

bool EOSSDK_UserInfo::on_userinfo(Network_Message_pb const& msg, UserInfo_Info_pb const& infos)
{
    GLOBAL_LOCK();

    auto user = GetEOS_Connect().get_user_by_productid(GetProductUserId(msg.source_id()));
    if (user != GetEOS_Connect().get_end_users())
    {
        EOS_EpicAccountId user_id = GetEpicUserId(user->second.infos.userid());
        merge_userinfo_pb(_userinfos[user_id], infos);
        auto it = _userinfos_queries.find(user_id);
        if (it != _userinfos_queries.end())
        {
            auto result_it = it->second.begin();
            if (result_it != it->second.end())
            {
                switch ((*result_it)->ICallback())
                {
                    case EOS_UserInfo_QueryUserInfoCallbackInfo::k_iCallback:
                    {
                        EOS_UserInfo_QueryUserInfoCallbackInfo& quici = (*result_it)->GetCallback<EOS_UserInfo_QueryUserInfoCallbackInfo>();
                        quici.ResultCode = EOS_EResult::EOS_Success;
                    }
                    break;
                    case EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo::k_iCallback:
                    {
                        EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo& quibdnci = (*result_it)->GetCallback<EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo>();
                        quibdnci.ResultCode = EOS_EResult::EOS_Success;
                    }
                    break;
                }

                (*result_it)->done = true;

                it->second.erase(result_it);
            }
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//                                 IRunFrame                                 //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_UserInfo::CBRunFrame()
{
    GLOBAL_LOCK();

    for (auto& queries : _userinfos_queries)
    {
        for (auto query_it = queries.second.begin(); query_it != queries.second.end();)
        {
            if ((std::chrono::steady_clock::now() - (*query_it)->created_time) > userinfo_query_timeout)
            {
                switch ((*query_it)->ICallback())
                {
                    case EOS_UserInfo_QueryUserInfoCallbackInfo::k_iCallback:
                    {
                        EOS_UserInfo_QueryUserInfoCallbackInfo& quici = (*query_it)->GetCallback<EOS_UserInfo_QueryUserInfoCallbackInfo>();
                        quici.ResultCode = EOS_EResult::EOS_TimedOut;
                    }
                    break;
                    case EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo::k_iCallback:
                    {
                        EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo& quibdnci = (*query_it)->GetCallback<EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo>();
                        quibdnci.ResultCode = EOS_EResult::EOS_TimedOut;
                    }
                    break;
                }

                (*query_it)->done = true;
                query_it = queries.second.erase(query_it);
            }
            else
                ++query_it;
        }
    }

    return true;
}

bool EOSSDK_UserInfo::RunNetwork(Network_Message_pb const& msg)
{
    if (msg.source_id() == Settings::Inst().userid->to_string())
        return true;

    UserInfo_Message_pb const& userinfo = msg.userinfo();

    switch (userinfo.message_case())
    {
        case UserInfo_Message_pb::MessageCase::kUserinfoInfoRequest: return on_userinfo_request(msg, userinfo.userinfo_info_request());
        case UserInfo_Message_pb::MessageCase::kUserinfoInfo       : return on_userinfo(msg, userinfo.userinfo_info());
    }

    return true;
}

bool EOSSDK_UserInfo::RunCallbacks(pFrameResult_t res)
{
    GLOBAL_LOCK();

    return res->done;
}

void EOSSDK_UserInfo::FreeCallback(pFrameResult_t res)
{
    GLOBAL_LOCK();

    //switch (res->res.m_iCallback)
    {
        /////////////////////////////
        //        Callbacks        //
        /////////////////////////////
        //case EOS_UserInfo_QueryUserInfoCallbackInfo::k_iCallback:
        //{
        //    EOS_UserInfo_QueryUserInfoCallbackInfo& quici = res->GetCallback<EOS_UserInfo_QueryUserInfoCallbackInfo>();
        //}
        //break;
        //case EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo::k_iCallback:
        //{
        //    EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo& quibdnci = res->GetCallback<EOS_UserInfo_QueryUserInfoByDisplayNameCallbackInfo>();
        //}
        //break;
        /////////////////////////////
        //      Notifications      //
        /////////////////////////////
    }
}

}