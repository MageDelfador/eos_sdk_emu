#include "common_includes.h"
#include "eos_types.h"
#include "settings.h"
#include "eos_stub_handles.h"
#include "eos_rtc_audio.h"
#include "eos_client_api.h"
#include "steam_bridge_runtime.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "eos_api_trace.h"

#ifdef Options
#undef Options
#endif

#ifdef Options
#undef Options
#endif

namespace sdk
{
    struct IntegratedPlatformOptionsEntry
    {
        std::string type;
        EOS_EIntegratedPlatformManagementFlags flags = EOS_EIntegratedPlatformManagementFlags::EOS_IPMF_Disabled;
    };

    struct StubIntegratedPlatformOptionsContainer final
    {
        uint32_t tag = 12;
        std::vector<IntegratedPlatformOptionsEntry> entries;
    };
}

using namespace sdk;

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainer(
    const EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainerOptions* Options,
    EOS_HIntegratedPlatformOptionsContainer* OutIntegratedPlatformOptionsContainerHandle)
{
    (void)Options;
    if (OutIntegratedPlatformOptionsContainerHandle == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    static StubIntegratedPlatformOptionsContainer container;
    *OutIntegratedPlatformOptionsContainerHandle =
        reinterpret_cast<EOS_HIntegratedPlatformOptionsContainer>(&container);
    return EOS_EResult::EOS_Success;
}

namespace
{
    static const char g_jwt[] = "eyJhbGciOiJub25lIn0.eyJzdWIiOiJlbXUifQ.";

    static EOS_Auth_IdToken g_auth_id_token = {
        EOS_AUTH_IDTOKEN_API_LATEST,
        nullptr,
        g_jwt,
    };

    static EOS_Connect_IdToken g_connect_id_token = {
        EOS_CONNECT_IDTOKEN_API_LATEST,
        nullptr,
        g_jwt,
    };
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Auth_CopyIdToken(
    EOS_HAuth Handle,
    const EOS_Auth_CopyIdTokenOptions* Options,
    EOS_Auth_IdToken** OutIdToken)
{
    (void)Handle;
    (void)Options;
    if (OutIdToken == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    g_auth_id_token.AccountId = Settings::Inst().userid;
    *OutIdToken = &g_auth_id_token;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Connect_CopyIdToken(
    EOS_HConnect Handle,
    const EOS_Connect_CopyIdTokenOptions* Options,
    EOS_Connect_IdToken** OutIdToken)
{
    (void)Handle;
    (void)Options;
    if (OutIdToken == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    g_connect_id_token.ProductUserId = Settings::Inst().productuserid;
    *OutIdToken = &g_connect_id_token;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Auth_QueryIdToken(
    EOS_HAuth Handle,
    const EOS_Auth_QueryIdTokenOptions* Options,
    void* ClientData,
    const EOS_Auth_OnQueryIdTokenCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_Auth_QueryIdTokenCallbackInfo info = {};
    info.ClientData = ClientData;
    info.ResultCode = EOS_EResult::EOS_Success;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().userid;
    info.TargetAccountId = Options != nullptr ? Options->TargetAccountId : Settings::Inst().userid;
    CompletionDelegate(&info);
}

namespace
{
    std::mutex g_rtc_mutex;
    std::unordered_set<std::string> g_rtc_connected_lobbies;
    EOS_NotificationId g_rtc_next_notification_id = 100;
    std::unordered_map<EOS_NotificationId, std::pair<void*, EOS_Lobby_OnRTCRoomConnectionChangedCallback>> g_rtc_connection_notifications;

    uint32_t g_rtc_active_query_id = 0;
    static const char g_rtc_token[] = "emu_rtc_token";
    static const char g_rtc_client_base_url[] = "emu://rtc";

    static EOS_RTCAdmin_UserToken g_rtc_user_token = {
        EOS_RTCADMIN_USERTOKEN_API_LATEST,
        nullptr,
        g_rtc_token,
    };

    static std::string rtc_room_name(EOS_LobbyId lobby_id)
    {
        if (lobby_id == nullptr || lobby_id[0] == '\0')
            return "emu_rtc_room";
        return std::string(lobby_id);
    }

    static void fire_rtc_connection_notifications(EOS_LobbyId lobby_id, EOS_ProductUserId local_user_id, EOS_Bool connected)
    {
        for (const auto& entry : g_rtc_connection_notifications)
        {
            EOS_Lobby_RTCRoomConnectionChangedCallbackInfo info = {};
            info.ClientData = entry.second.first;
            info.LobbyId = lobby_id;
            info.LocalUserId = local_user_id;
            info.bIsConnected = connected;
            info.DisconnectReason = connected == EOS_TRUE ? EOS_EResult::EOS_Success : EOS_EResult::EOS_NoConnection;
            entry.second.second(&info);
        }
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_Lobby_AddNotifyRTCRoomConnectionChanged(
    EOS_HLobby Handle,
    const EOS_Lobby_AddNotifyRTCRoomConnectionChangedOptions* Options,
    void* ClientData,
    const EOS_Lobby_OnRTCRoomConnectionChangedCallback NotificationFn)
{
    (void)Handle;
    (void)Options;
    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    std::lock_guard<std::mutex> lock(g_rtc_mutex);
    const EOS_NotificationId id = g_rtc_next_notification_id++;
    g_rtc_connection_notifications[id] = { ClientData, NotificationFn };
    return id;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_RemoveNotifyRTCRoomConnectionChanged(EOS_HLobby Handle, EOS_NotificationId InId)
{
    EOS_API_TRACE();
    (void)Handle;
    std::lock_guard<std::mutex> lock(g_rtc_mutex);
    g_rtc_connection_notifications.erase(InId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_GetRTCRoomName(
    EOS_HLobby Handle,
    const EOS_Lobby_GetRTCRoomNameOptions* Options,
    char* OutBuffer,
    uint32_t* InOutBufferLength)
{
    (void)Handle;
    if (OutBuffer == nullptr || InOutBufferLength == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    const std::string room_name = rtc_room_name(Options != nullptr ? Options->LobbyId : nullptr);
    const uint32_t required_length = static_cast<uint32_t>(room_name.size() + 1);
    if (*InOutBufferLength < required_length)
    {
        *InOutBufferLength = required_length;
        return EOS_EResult::EOS_LimitExceeded;
    }

    memcpy(OutBuffer, room_name.c_str(), required_length);
    *InOutBufferLength = required_length;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Lobby_IsRTCRoomConnected(
    EOS_HLobby Handle,
    const EOS_Lobby_IsRTCRoomConnectedOptions* Options,
    EOS_Bool* bOutIsConnected)
{
    (void)Handle;
    if (bOutIsConnected == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Options == nullptr || Options->LobbyId == nullptr)
    {
        *bOutIsConnected = EOS_FALSE;
        return EOS_EResult::EOS_InvalidParameters;
    }

    std::lock_guard<std::mutex> lock(g_rtc_mutex);
    *bOutIsConnected = g_rtc_connected_lobbies.count(Options->LobbyId) > 0 ? EOS_TRUE : EOS_FALSE;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_Lobby_JoinRTCRoom(
    EOS_HLobby Handle,
    const EOS_Lobby_JoinRTCRoomOptions* Options,
    void* ClientData,
    const EOS_Lobby_OnJoinRTCRoomCallback CompletionDelegate)
{
    (void)Handle;
    APP_LOG(Log::LogLevel::DEBUG, "EOS_Lobby_JoinRTCRoom lobby=%s", Options != nullptr && Options->LobbyId != nullptr ? Options->LobbyId : "(null)");
    if (CompletionDelegate == nullptr)
        return;

    EOS_Lobby_JoinRTCRoomCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LobbyId = Options != nullptr ? Options->LobbyId : nullptr;

    if (Options == nullptr || Options->LobbyId == nullptr)
    {
        info.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        info.ResultCode = EOS_EResult::EOS_Success;
        {
            std::lock_guard<std::mutex> lock(g_rtc_mutex);
            g_rtc_connected_lobbies.insert(Options->LobbyId);
        }
        fire_rtc_connection_notifications(Options->LobbyId, Options->LocalUserId, EOS_TRUE);
    }

    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_Lobby_LeaveRTCRoom(
    EOS_HLobby Handle,
    const EOS_Lobby_LeaveRTCRoomOptions* Options,
    void* ClientData,
    const EOS_Lobby_OnLeaveRTCRoomCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_Lobby_LeaveRTCRoomCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LobbyId = Options != nullptr ? Options->LobbyId : nullptr;

    if (Options == nullptr || Options->LobbyId == nullptr)
    {
        info.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        info.ResultCode = EOS_EResult::EOS_Success;
        {
            std::lock_guard<std::mutex> lock(g_rtc_mutex);
            g_rtc_connected_lobbies.erase(Options->LobbyId);
        }
        fire_rtc_connection_notifications(Options->LobbyId, Options->LocalUserId, EOS_FALSE);
    }

    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAdmin_QueryJoinRoomToken(
    EOS_HRTCAdmin Handle,
    const EOS_RTCAdmin_QueryJoinRoomTokenOptions* Options,
    void* ClientData,
    const EOS_RTCAdmin_OnQueryJoinRoomTokenCompleteCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAdmin_QueryJoinRoomTokenCompleteCallbackInfo info = {};
    info.ClientData = ClientData;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.ClientBaseUrl = g_rtc_client_base_url;
    info.TokenCount = Options != nullptr && Options->TargetUserIdsCount > 0 ? Options->TargetUserIdsCount : 1;

    if (Options == nullptr || Options->RoomName == nullptr)
    {
        info.ResultCode = EOS_EResult::EOS_InvalidParameters;
        info.QueryId = 0;
    }
    else
    {
        info.ResultCode = EOS_EResult::EOS_Success;
        std::lock_guard<std::mutex> lock(g_rtc_mutex);
        g_rtc_active_query_id++;
        info.QueryId = g_rtc_active_query_id;
    }

    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAdmin_CopyUserTokenByIndex(
    EOS_HRTCAdmin Handle,
    const EOS_RTCAdmin_CopyUserTokenByIndexOptions* Options,
    EOS_RTCAdmin_UserToken** OutUserToken)
{
    (void)Handle;
    if (OutUserToken == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Options == nullptr || Options->QueryId != g_rtc_active_query_id || g_rtc_active_query_id == 0)
        return EOS_EResult::EOS_NotFound;

    g_rtc_user_token.ProductUserId = Settings::Inst().productuserid;
    *OutUserToken = &g_rtc_user_token;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAdmin_CopyUserTokenByUserId(
    EOS_HRTCAdmin Handle,
    const EOS_RTCAdmin_CopyUserTokenByUserIdOptions* Options,
    EOS_RTCAdmin_UserToken** OutUserToken)
{
    (void)Handle;
    if (OutUserToken == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Options == nullptr || Options->QueryId != g_rtc_active_query_id || g_rtc_active_query_id == 0)
        return EOS_EResult::EOS_NotFound;

    g_rtc_user_token.ProductUserId = Options->TargetUserId != nullptr ? Options->TargetUserId : Settings::Inst().productuserid;
    *OutUserToken = &g_rtc_user_token;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_RTCAdmin_UserToken_Release(EOS_RTCAdmin_UserToken* UserToken)
{
    EOS_API_TRACE();
    (void)UserToken;
}

EOS_DECLARE_FUNC(void) EOS_RTC_JoinRoom(
    EOS_HRTC Handle,
    const EOS_RTC_JoinRoomOptions* Options,
    void* ClientData,
    const EOS_RTC_OnJoinRoomCallback CompletionDelegate)
{
    (void)Handle;
    APP_LOG(Log::LogLevel::DEBUG, "EOS_RTC_JoinRoom room=%s", Options != nullptr && Options->RoomName != nullptr ? Options->RoomName : "(null)");
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTC_JoinRoomCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.ResultCode = (Options == nullptr || Options->RoomName == nullptr)
        ? EOS_EResult::EOS_InvalidParameters
        : EOS_EResult::EOS_Success;

    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTC_LeaveRoom(
    EOS_HRTC Handle,
    const EOS_RTC_LeaveRoomOptions* Options,
    void* ClientData,
    const EOS_RTC_OnLeaveRoomCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTC_LeaveRoomCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.ResultCode = (Options == nullptr || Options->RoomName == nullptr)
        ? EOS_EResult::EOS_InvalidParameters
        : EOS_EResult::EOS_Success;

    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(EOS_HRTCAudio) EOS_RTC_GetAudioInterface(EOS_HRTC Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    APP_LOG(Log::LogLevel::DEBUG, "EOS_RTC_GetAudioInterface");
    return reinterpret_cast<EOS_HRTCAudio>(&StubRTCAudio());
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_RegisterPlatformUser(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_RegisterPlatformUserOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnRegisterPlatformUserCallback CompletionDelegate)
{
    (void)Handle;
    APP_LOG(Log::LogLevel::DEBUG, "EOS_RTCAudio_RegisterPlatformUser");
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_OnRegisterPlatformUserCallbackInfo info = {};
    info.ClientData = ClientData;
    info.PlatformUserId = Options != nullptr ? Options->PlatformUserId : nullptr;
    info.ResultCode = (Options == nullptr || Options->PlatformUserId == nullptr)
        ? EOS_EResult::EOS_InvalidParameters
        : EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_QueryInputDevicesInformation(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_QueryInputDevicesInformationOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnQueryInputDevicesInformationCallback CompletionDelegate)
{
    (void)Handle;
    (void)Options;
    APP_LOG(Log::LogLevel::DEBUG, "EOS_RTCAudio_QueryInputDevicesInformation");
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_OnQueryInputDevicesInformationCallbackInfo info = {};
    info.ClientData = ClientData;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_QueryOutputDevicesInformation(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_QueryOutputDevicesInformationOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnQueryOutputDevicesInformationCallback CompletionDelegate)
{
    (void)Handle;
    (void)Options;
    APP_LOG(Log::LogLevel::DEBUG, "EOS_RTCAudio_QueryOutputDevicesInformation");
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_OnQueryOutputDevicesInformationCallbackInfo info = {};
    info.ClientData = ClientData;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateReceiving(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_UpdateReceivingOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnUpdateReceivingCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_UpdateReceivingCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.ParticipantId = Options != nullptr ? Options->ParticipantId : nullptr;
    info.bAudioEnabled = Options != nullptr ? Options->bAudioEnabled : EOS_TRUE;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateSending(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_UpdateSendingOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnUpdateSendingCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_UpdateSendingCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.AudioStatus = Options != nullptr ? Options->AudioStatus : EOS_ERTCAudioStatus::EOS_RTCAS_Enabled;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateReceivingVolume(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_UpdateReceivingVolumeOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnUpdateReceivingVolumeCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_UpdateReceivingVolumeCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.Volume = Options != nullptr ? Options->Volume : 50.0f;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_UpdateSendingVolume(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_UpdateSendingVolumeOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnUpdateSendingVolumeCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_UpdateSendingVolumeCallbackInfo info = {};
    info.ClientData = ClientData;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.Volume = Options != nullptr ? Options->Volume : 50.0f;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_SetInputDeviceSettings(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_SetInputDeviceSettingsOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnSetInputDeviceSettingsCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_OnSetInputDeviceSettingsCallbackInfo info = {};
    info.ClientData = ClientData;
    info.RealDeviceId = Options != nullptr ? Options->RealDeviceId : nullptr;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCAudio_SetOutputDeviceSettings(
    EOS_HRTCAudio Handle,
    const EOS_RTCAudio_SetOutputDeviceSettingsOptions* Options,
    void* ClientData,
    const EOS_RTCAudio_OnSetOutputDeviceSettingsCallback CompletionDelegate)
{
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCAudio_OnSetOutputDeviceSettingsCallbackInfo info = {};
    info.ClientData = ClientData;
    info.RealDeviceId = Options != nullptr ? Options->RealDeviceId : nullptr;
    info.ResultCode = EOS_EResult::EOS_Success;
    CompletionDelegate(&info);
}

namespace
{
    std::mutex g_integrated_platform_mutex;
    EOS_NotificationId g_integrated_platform_next_notification_id = 600;
    std::unordered_map<EOS_NotificationId, std::pair<void*, EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback>>
        g_integrated_platform_login_notifications;
    std::unordered_set<std::string> g_integrated_platform_login_notified;

    static char const* integrated_platform_steam_type()
    {
        return EOS_IPT_Steam;
    }

    static void fire_integrated_platform_login_status(
        EOS_IntegratedPlatformType platform_type,
        char const* platform_user_id,
        EOS_ELoginStatus previous_status,
        EOS_ELoginStatus current_status,
        void* client_data,
        EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback callback)
    {
        if (callback == nullptr)
            return;

        EOS_IntegratedPlatform_UserLoginStatusChangedCallbackInfo info = {};
        info.ClientData = client_data;
        info.PlatformType = platform_type;
        info.LocalPlatformUserId = platform_user_id;
        info.AccountId = Settings::Inst().userid;
        info.ProductUserId = Settings::Inst().productuserid;
        info.PreviousLoginStatus = previous_status;
        info.CurrentLoginStatus = current_status;
        callback(&info);
    }

    static void notify_integrated_platform_login_if_needed(
        EOS_IntegratedPlatformType platform_type,
        char const* platform_user_id,
        EOS_ELoginStatus current_status)
    {
        if (platform_user_id == nullptr || platform_user_id[0] == '\0')
            return;

        std::string const dedupe_key = std::string(platform_type != nullptr ? platform_type : "") + ':' + platform_user_id +
            ':' + std::to_string(static_cast<int>(current_status));
        {
            std::lock_guard<std::mutex> lock(g_integrated_platform_mutex);
            if (!g_integrated_platform_login_notified.insert(dedupe_key).second)
                return;
        }

        APP_LOG(Log::LogLevel::INFO,
            "IntegratedPlatform login status platform=%s user=%s status=%d",
            platform_type != nullptr ? platform_type : "(null)",
            platform_user_id,
            static_cast<int>(current_status));

        std::lock_guard<std::mutex> lock(g_integrated_platform_mutex);
        for (auto const& entry : g_integrated_platform_login_notifications)
        {
            fire_integrated_platform_login_status(
                platform_type,
                platform_user_id,
                EOS_ELoginStatus::EOS_LS_NotLoggedIn,
                current_status,
                entry.second.first,
                entry.second.second);
        }
    }
}

namespace sdk
{

void dispatch_integrated_platform_tick()
{
    std::string const steam_id = steam_bridge::local_steam_id();
    if (!steam_id.empty())
    {
        notify_integrated_platform_login_if_needed(
            integrated_platform_steam_type(),
            steam_id.c_str(),
            EOS_ELoginStatus::EOS_LS_LoggedIn);
    }
}

} // namespace sdk

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatformOptionsContainer_Add(
    EOS_HIntegratedPlatformOptionsContainer Handle,
    const EOS_IntegratedPlatformOptionsContainer_AddOptions* InOptions)
{
    EOS_API_TRACE();
    if (Handle == nullptr || InOptions == nullptr || InOptions->Options == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    auto* container = reinterpret_cast<StubIntegratedPlatformOptionsContainer*>(Handle);
    IntegratedPlatformOptionsEntry entry;
    if (InOptions->Options->Type != nullptr)
        entry.type = InOptions->Options->Type;
    entry.flags = InOptions->Options->Flags;
    container->entries.push_back(std::move(entry));

    APP_LOG(Log::LogLevel::INFO,
        "IntegratedPlatform OptionsContainer_Add type=%s flags=0x%x entries=%zu",
        InOptions->Options->Type != nullptr ? InOptions->Options->Type : "(null)",
        static_cast<unsigned>(InOptions->Options->Flags),
        container->entries.size());
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatformOptionsContainer_Release(
    EOS_HIntegratedPlatformOptionsContainer IntegratedPlatformOptionsContainerHandle)
{
    EOS_API_TRACE();
    if (IntegratedPlatformOptionsContainerHandle == nullptr)
        return;

    auto* container = reinterpret_cast<StubIntegratedPlatformOptionsContainer*>(IntegratedPlatformOptionsContainerHandle);
    container->entries.clear();
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_IntegratedPlatform_AddNotifyUserLoginStatusChanged(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_AddNotifyUserLoginStatusChangedOptions* Options,
    void* ClientData,
    const EOS_IntegratedPlatform_OnUserLoginStatusChangedCallback CallbackFunction)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    if (CallbackFunction == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    std::lock_guard<std::mutex> lock(g_integrated_platform_mutex);
    EOS_NotificationId const id = g_integrated_platform_next_notification_id++;
    g_integrated_platform_login_notifications[id] = { ClientData, CallbackFunction };

    std::string const steam_id = steam_bridge::local_steam_id();
    if (!steam_id.empty())
    {
        fire_integrated_platform_login_status(
            integrated_platform_steam_type(),
            steam_id.c_str(),
            EOS_ELoginStatus::EOS_LS_NotLoggedIn,
            EOS_ELoginStatus::EOS_LS_LoggedIn,
            ClientData,
            CallbackFunction);
        g_integrated_platform_login_notified.insert(
            std::string(integrated_platform_steam_type()) + ':' + steam_id + ':' +
            std::to_string(static_cast<int>(EOS_ELoginStatus::EOS_LS_LoggedIn)));
    }

    return id;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_RemoveNotifyUserLoginStatusChanged(
    EOS_HIntegratedPlatform Handle,
    EOS_NotificationId NotificationId)
{
    EOS_API_TRACE();
    (void)Handle;
    std::lock_guard<std::mutex> lock(g_integrated_platform_mutex);
    g_integrated_platform_login_notifications.erase(NotificationId);
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserLoginStatus(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_SetUserLoginStatusOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    if (Options == nullptr || Options->LocalPlatformUserId == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    notify_integrated_platform_login_if_needed(
        Options->PlatformType,
        Options->LocalPlatformUserId,
        Options->CurrentLoginStatus);
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_SetUserPreLogoutCallback(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_SetUserPreLogoutCallbackOptions* Options,
    void* ClientData,
    EOS_IntegratedPlatform_OnUserPreLogoutCallback CallbackFunction)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)ClientData;
    (void)CallbackFunction;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_IntegratedPlatform_ClearUserPreLogoutCallback(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_ClearUserPreLogoutCallbackOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_IntegratedPlatform_FinalizeDeferredUserLogout(
    EOS_HIntegratedPlatform Handle,
    const EOS_IntegratedPlatform_FinalizeDeferredUserLogoutOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    return EOS_EResult::EOS_Success;
}
