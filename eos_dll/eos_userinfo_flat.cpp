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
#include "eos_api_trace.h"

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfo(EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    pInst->QueryUserInfo(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfoByDisplayName(EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoByDisplayNameOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoByDisplayNameCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    pInst->QueryUserInfoByDisplayName(Options, ClientData, CompletionDelegate);
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
EOS_DECLARE_FUNC(void) EOS_UserInfo_QueryUserInfoByExternalAccount(EOS_HUserInfo Handle, const EOS_UserInfo_QueryUserInfoByExternalAccountOptions* Options, void* ClientData, const EOS_UserInfo_OnQueryUserInfoByExternalAccountCallback CompletionDelegate)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    pInst->QueryUserInfoByExternalAccount(Options, ClientData, CompletionDelegate);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyUserInfo(EOS_HUserInfo Handle, const EOS_UserInfo_CopyUserInfoOptions* Options, void** OutUserInfo)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->CopyUserInfo(Options, OutUserInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyBestDisplayName(EOS_HUserInfo Handle, const EOS_UserInfo_CopyBestDisplayNameOptions* Options, EOS_UserInfo_BestDisplayName** OutBestDisplayName)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->CopyBestDisplayName(Options, OutBestDisplayName);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyBestDisplayNameWithPlatform(EOS_HUserInfo Handle, const EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions* Options, EOS_UserInfo_BestDisplayName** OutBestDisplayName)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->CopyBestDisplayNameWithPlatform(Options, OutBestDisplayName);
}

EOS_DECLARE_FUNC(EOS_OnlinePlatformType) EOS_UserInfo_GetLocalPlatformType(EOS_HUserInfo Handle, const EOS_UserInfo_GetLocalPlatformTypeOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_OPT_Unknown;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->GetLocalPlatformType(Options);
}

namespace
{
void release_cstr(const char* value)
{
    delete[] value;
}

void release_userinfo002(EOS_UserInfo002* infos)
{
    if (infos == nullptr)
        return;

    release_cstr(infos->Country);
    release_cstr(infos->PreferredLanguage);
    release_cstr(infos->DisplayName);
    release_cstr(infos->Nickname);
}

void release_userinfo003(EOS_UserInfo003* infos)
{
    if (infos == nullptr)
        return;

    release_userinfo002(reinterpret_cast<EOS_UserInfo002*>(infos));
    release_cstr(infos->DisplayNameSanitized);
}
}

EOS_DECLARE_FUNC(uint32_t) EOS_UserInfo_GetExternalUserInfoCount(EOS_HUserInfo Handle, const EOS_UserInfo_GetExternalUserInfoCountOptions* Options)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return 0;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->GetExternalUserInfoCount(Options);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByIndex(EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByIndexOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->CopyExternalUserInfoByIndex(Options, OutExternalUserInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByAccountType(EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByAccountTypeOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->CopyExternalUserInfoByAccountType(Options, OutExternalUserInfo);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_UserInfo_CopyExternalUserInfoByAccountId(EOS_HUserInfo Handle, const EOS_UserInfo_CopyExternalUserInfoByAccountIdOptions* Options, EOS_UserInfo_ExternalUserInfo** OutExternalUserInfo)
{
    EOS_API_TRACE();
    if (Handle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto pInst = reinterpret_cast<sdk::EOSSDK_UserInfo*>(Handle);
    return pInst->CopyExternalUserInfoByAccountId(Options, OutExternalUserInfo);
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_Release(EOS_UserInfo* UserInfo)
{
    EOS_API_TRACE();
    TRACE_FUNC();

    if (UserInfo != nullptr)
    {
        switch (UserInfo->ApiVersion)
        {
            case EOS_USERINFO_COPYUSERINFO_API_003:
                release_userinfo003(reinterpret_cast<EOS_UserInfo003*>(UserInfo));
                delete reinterpret_cast<EOS_UserInfo003*>(UserInfo);
                break;
            case EOS_USERINFO_COPYUSERINFO_API_002:
            case EOS_USERINFO_COPYUSERINFO_API_001:
                release_userinfo002(reinterpret_cast<EOS_UserInfo002*>(UserInfo));
                delete reinterpret_cast<EOS_UserInfo002*>(UserInfo);
                break;
            default:
                delete UserInfo;
                break;
        }
    }
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_BestDisplayName_Release(EOS_UserInfo_BestDisplayName* BestDisplayName)
{
    EOS_API_TRACE();
    TRACE_FUNC();

    if (BestDisplayName != nullptr)
    {
        release_cstr(BestDisplayName->DisplayName);
        release_cstr(BestDisplayName->DisplayNameSanitized);
        release_cstr(BestDisplayName->Nickname);
        delete BestDisplayName;
    }
}

EOS_DECLARE_FUNC(void) EOS_UserInfo_ExternalUserInfo_Release(EOS_UserInfo_ExternalUserInfo* ExternalUserInfo)
{
    EOS_API_TRACE();
    TRACE_FUNC();

    if (ExternalUserInfo != nullptr)
    {
        release_cstr(ExternalUserInfo->AccountId);
        release_cstr(ExternalUserInfo->DisplayName);
        release_cstr(ExternalUserInfo->DisplayNameSanitized);
        delete ExternalUserInfo;
    }
}