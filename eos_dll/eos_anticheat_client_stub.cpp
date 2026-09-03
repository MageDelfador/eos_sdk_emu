#include "common_includes.h"
#include "eos_anticheatclient.h"
#include "eos_api_trace.h"
#include "settings.h"

#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef Options
#undef Options
#endif

namespace sdk
{

namespace
{
struct ac_session_t
{
    bool active = false;
    EOS_ProductUserId local_user = nullptr;
    EOS_EAntiCheatClientMode mode = EOS_EAntiCheatClientMode::EOS_ACCM_Invalid;
};

using peer_auth_callback_t = EOS_AntiCheatClient_OnPeerAuthStatusChangedCallback;
using message_to_peer_callback_t = EOS_AntiCheatClient_OnMessageToPeerCallback;
using peer_action_callback_t = EOS_AntiCheatClient_OnPeerActionRequiredCallback;

std::mutex g_ac_mutex;
ac_session_t g_ac_session;
EOS_NotificationId g_ac_next_notification_id = 500;

std::unordered_map<EOS_NotificationId, std::pair<void*, peer_auth_callback_t>> g_ac_peer_auth_notifs;
std::unordered_map<EOS_NotificationId, std::pair<void*, message_to_peer_callback_t>> g_ac_message_to_peer_notifs;
std::unordered_map<EOS_NotificationId, std::pair<void*, peer_action_callback_t>> g_ac_peer_action_notifs;

std::unordered_map<EOS_AntiCheatCommon_ClientHandle, EOS_EAntiCheatCommonClientAuthStatus> g_ac_peer_auth_status;
std::deque<std::function<void()>> g_ac_pending_tick;

void ac_fire_peer_auth(EOS_AntiCheatCommon_ClientHandle peer, EOS_EAntiCheatCommonClientAuthStatus status)
{
    g_ac_peer_auth_status[peer] = status;

    EOS_AntiCheatCommon_OnClientAuthStatusChangedCallbackInfo info = {};
    info.ClientHandle = peer;
    info.ClientAuthStatus = status;

    for (auto const& entry : g_ac_peer_auth_notifs)
    {
        info.ClientData = entry.second.first;
        entry.second.second(&info);
    }
}

void ac_queue_peer_auth(EOS_AntiCheatCommon_ClientHandle peer, EOS_EAntiCheatCommonClientAuthStatus status)
{
    g_ac_pending_tick.emplace_back([peer, status]() { ac_fire_peer_auth(peer, status); });
}

void ac_queue_peer_auth_sequence(EOS_AntiCheatCommon_ClientHandle peer)
{
    ac_queue_peer_auth(peer, EOS_EAntiCheatCommonClientAuthStatus::EOS_ACCCAS_LocalAuthComplete);
    ac_queue_peer_auth(peer, EOS_EAntiCheatCommonClientAuthStatus::EOS_ACCCAS_RemoteAuthComplete);
}

void ac_replay_peer_auth_for_new_listener(void* client_data, peer_auth_callback_t callback)
{
    for (auto const& peer : g_ac_peer_auth_status)
    {
        if (peer.second == EOS_EAntiCheatCommonClientAuthStatus::EOS_ACCCAS_Invalid)
            continue;

        EOS_AntiCheatCommon_OnClientAuthStatusChangedCallbackInfo info = {};
        info.ClientData = client_data;
        info.ClientHandle = peer.first;
        info.ClientAuthStatus = peer.second;
        callback(&info);
    }
}

bool ac_session_allows_peer_ops()
{
    return g_ac_session.active &&
        (g_ac_session.mode == EOS_EAntiCheatClientMode::EOS_ACCM_PeerToPeer ||
         g_ac_session.mode == EOS_EAntiCheatClientMode::EOS_ACCM_ClientServer);
}
}

void dispatch_anticheat_client_tick()
{
    std::deque<std::function<void()>> pending;
    {
        std::lock_guard<std::mutex> lock(g_ac_mutex);
        pending.swap(g_ac_pending_tick);
    }

    for (auto& fn : pending)
        fn();
}

} // namespace sdk

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyMessageToPeer(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_AddNotifyMessageToPeerOptions* Options,
    void* ClientData,
    EOS_AntiCheatClient_OnMessageToPeerCallback NotificationFn)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    EOS_NotificationId const id = sdk::g_ac_next_notification_id++;
    sdk::g_ac_message_to_peer_notifs[id] = { ClientData, NotificationFn };
    return id;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyPeerActionRequired(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_AddNotifyPeerActionRequiredOptions* Options,
    void* ClientData,
    EOS_AntiCheatClient_OnPeerActionRequiredCallback NotificationFn)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    EOS_NotificationId const id = sdk::g_ac_next_notification_id++;
    sdk::g_ac_peer_action_notifs[id] = { ClientData, NotificationFn };
    return id;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_AntiCheatClient_AddNotifyPeerAuthStatusChanged(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_AddNotifyPeerAuthStatusChangedOptions* Options,
    void* ClientData,
    EOS_AntiCheatClient_OnPeerAuthStatusChangedCallback NotificationFn)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    EOS_NotificationId const id = sdk::g_ac_next_notification_id++;
    sdk::g_ac_peer_auth_notifs[id] = { ClientData, NotificationFn };
    sdk::ac_replay_peer_auth_for_new_listener(ClientData, NotificationFn);
    return id;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_BeginSession(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_BeginSessionOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    if (Options == nullptr || Options->LocalUserId == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    sdk::g_ac_session.active = true;
    sdk::g_ac_session.local_user = Options->LocalUserId;
    sdk::g_ac_session.mode = Options->Mode;
    sdk::g_ac_peer_auth_status.clear();

    APP_LOG(Log::LogLevel::INFO,
        "AntiCheatClient BeginSession: user=%s mode=%d",
        Options->LocalUserId->to_string().c_str(),
        static_cast<int>(Options->Mode));

    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_EndSession(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_EndSessionOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    sdk::g_ac_session = {};
    sdk::g_ac_peer_auth_status.clear();
    sdk::g_ac_pending_tick.clear();
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_PollStatus(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_PollStatusOptions* Options,
    EOS_EAntiCheatClientViolationType* OutViolationType,
    char* OutMessage)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)OutViolationType;
    (void)OutMessage;
    return EOS_EResult::EOS_NotFound;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_RegisterPeer(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_RegisterPeerOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    if (Options == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    if (!sdk::ac_session_allows_peer_ops())
        return EOS_EResult::EOS_InvalidState;

    sdk::ac_queue_peer_auth_sequence(Options->PeerHandle);

    APP_LOG(Log::LogLevel::INFO,
        "AntiCheatClient RegisterPeer: handle=%p account=%s",
        Options->PeerHandle,
        Options->AccountId != nullptr ? Options->AccountId : "(null)");

    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_UnregisterPeer(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_UnregisterPeerOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    if (Options == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    sdk::g_ac_peer_auth_status.erase(Options->PeerHandle);
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_AntiCheatClient_ReceiveMessageFromPeer(
    EOS_HAntiCheatClient Handle,
    const EOS_AntiCheatClient_ReceiveMessageFromPeerOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyMessageToPeer(
    EOS_HAntiCheatClient Handle,
    EOS_NotificationId NotificationId)
{
    EOS_API_TRACE();
    (void)Handle;
    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    sdk::g_ac_message_to_peer_notifs.erase(NotificationId);
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyPeerActionRequired(
    EOS_HAntiCheatClient Handle,
    EOS_NotificationId NotificationId)
{
    EOS_API_TRACE();
    (void)Handle;
    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    sdk::g_ac_peer_action_notifs.erase(NotificationId);
}

EOS_DECLARE_FUNC(void) EOS_AntiCheatClient_RemoveNotifyPeerAuthStatusChanged(
    EOS_HAntiCheatClient Handle,
    EOS_NotificationId NotificationId)
{
    EOS_API_TRACE();
    (void)Handle;
    std::lock_guard<std::mutex> lock(sdk::g_ac_mutex);
    sdk::g_ac_peer_auth_notifs.erase(NotificationId);
}
