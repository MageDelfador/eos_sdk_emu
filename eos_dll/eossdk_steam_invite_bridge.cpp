/*
 * Copyright (C) 2020 Nemirtingas
 * Steam overlay invite bridge — uses upstream gbe_fork via GetProcAddress only.
 */

#include "eossdk_lobby.h"
#include "eossdk_platform.h"
#include "eossdk_sessions.h"
#include "eos_client_api.h"
#include "settings.h"
#include "steam_bridge_runtime.h"

#include <cctype>
#include <cstring>

#if defined(__WINDOWS__)
#include <windows.h>
#endif

namespace sdk
{
namespace
{
constexpr char kLobbyConnectPrefix[] = "GSEEpicEmu:";
constexpr char kEOSInvitePrefix[] = "-EOSInvite=";
constexpr char kEOSLobbyInviteTag[] = "01L";
constexpr char kEOSSessionInviteTag[] = "01S";
constexpr char kSteamConnectLobbyPrefix[] = "+connect_lobby";

enum class eos_invite_kind_t
{
    none,
    lobby,
    session,
};

struct parsed_eos_invite_t
{
    eos_invite_kind_t kind = eos_invite_kind_t::none;
    std::string id;
};

#if defined(__WINDOWS__)

struct SteamCallbackBase
{
    enum { k_ECallbackFlagsRegistered = 0x01, k_ECallbackFlagsGameServer = 0x02 };

    SteamCallbackBase() = default;
    virtual void Run(void* pvParam) = 0;
    virtual void Run(void* pvParam, bool bIOFailure, uint64_t hSteamAPICall) = 0;
    int GetICallback() { return m_iCallback; }
    virtual int GetCallbackSizeBytes() = 0;

protected:
    uint8_t m_nCallbackFlags{};
    int m_iCallback{};
};

struct SteamGameLobbyJoinRequested_t
{
    uint64_t m_steamIDLobby{};
    uint64_t m_steamIDFriend{};
};

struct SteamGameRichPresenceJoinRequested_t
{
    uint64_t m_steamIDFriend{};
    char m_rgchConnect[256]{};
};

constexpr int kSteamCallbackGameLobbyJoinRequested = 333;
constexpr int kSteamCallbackGameRichPresenceJoinRequested = 337;

class SteamLobbyJoinBridgeCallback final : public SteamCallbackBase
{
public:
    int callback_id{};

    explicit SteamLobbyJoinBridgeCallback(int id)
        : callback_id(id)
    {
        m_iCallback = id;
    }

    void Run(void* pvParam) override
    {
        if (pvParam == nullptr)
            return;

        if (callback_id == kSteamCallbackGameLobbyJoinRequested)
        {
            auto const* ev = static_cast<SteamGameLobbyJoinRequested_t const*>(pvParam);
            APP_LOG(Log::LogLevel::INFO,
                "Steam invite bridge: GameLobbyJoinRequested steam_lobby=%llu steam_friend=%llu",
                static_cast<unsigned long long>(ev->m_steamIDLobby),
                static_cast<unsigned long long>(ev->m_steamIDFriend));
            if (!EOSSDK_Platform::Inst().is_platform_ready())
                return;
            GetEOS_Lobby().OnSteamLobbyJoinRequested(ev->m_steamIDLobby, ev->m_steamIDFriend, nullptr);
        }
        else if (callback_id == kSteamCallbackGameRichPresenceJoinRequested)
        {
            auto const* ev = static_cast<SteamGameRichPresenceJoinRequested_t const*>(pvParam);
            APP_LOG(Log::LogLevel::INFO,
                "Steam invite bridge: GameRichPresenceJoinRequested steam_friend=%llu connect='%s'",
                static_cast<unsigned long long>(ev->m_steamIDFriend),
                ev->m_rgchConnect);
            if (!EOSSDK_Platform::Inst().is_platform_ready())
                return;
            GetEOS_Lobby().OnSteamLobbyJoinRequested(0, ev->m_steamIDFriend, ev->m_rgchConnect);
        }
    }

    void Run(void*, bool, uint64_t) override
    {}

    int GetCallbackSizeBytes() override
    {
        if (callback_id == kSteamCallbackGameLobbyJoinRequested)
            return sizeof(SteamGameLobbyJoinRequested_t);
        return sizeof(SteamGameRichPresenceJoinRequested_t);
    }
};

SteamLobbyJoinBridgeCallback g_steam_lobby_join_cb(kSteamCallbackGameLobbyJoinRequested);
SteamLobbyJoinBridgeCallback g_steam_rich_join_cb(kSteamCallbackGameRichPresenceJoinRequested);

#endif // __WINDOWS__

uint64_t parse_steam_connect_lobby_id(char const* connect_string)
{
    if (connect_string == nullptr)
        return 0;

    char const* p = connect_string;
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
        ++p;

    size_t const prefix_len = std::strlen(kSteamConnectLobbyPrefix);
    if (std::strncmp(p, kSteamConnectLobbyPrefix, prefix_len) != 0)
        return 0;

    p += prefix_len;
    while (*p != '\0' && std::isspace(static_cast<unsigned char>(*p)))
        ++p;

    if (!std::isdigit(static_cast<unsigned char>(*p)))
        return 0;

    return std::strtoull(p, nullptr, 10);
}

parsed_eos_invite_t parse_eos_connect_invite(char const* connect_string)
{
    parsed_eos_invite_t parsed;
    if (connect_string == nullptr)
        return parsed;

    size_t const prefix_len = std::strlen(kLobbyConnectPrefix);
    if (std::strncmp(connect_string, kLobbyConnectPrefix, prefix_len) == 0)
    {
        char const* lobby_id = connect_string + prefix_len;
        if (*lobby_id == '\0')
            return parsed;

        parsed.kind = eos_invite_kind_t::lobby;
        parsed.id = lobby_id;
        return parsed;
    }

    char const* eos_invite = std::strstr(connect_string, kEOSInvitePrefix);
    if (eos_invite == nullptr)
        return parsed;

    eos_invite += std::strlen(kEOSInvitePrefix);
    if (std::strncmp(eos_invite, kEOSLobbyInviteTag, std::strlen(kEOSLobbyInviteTag)) == 0)
    {
        parsed.kind = eos_invite_kind_t::lobby;
        eos_invite += std::strlen(kEOSLobbyInviteTag);
    }
    else if (std::strncmp(eos_invite, kEOSSessionInviteTag, std::strlen(kEOSSessionInviteTag)) == 0)
    {
        parsed.kind = eos_invite_kind_t::session;
        eos_invite += std::strlen(kEOSSessionInviteTag);
    }
    else
    {
        return parsed;
    }

    while (*eos_invite != '\0' && !std::isspace(static_cast<unsigned char>(*eos_invite)))
    {
        parsed.id.push_back(*eos_invite);
        ++eos_invite;
    }

    if (parsed.id.empty())
        parsed.kind = eos_invite_kind_t::none;
    return parsed;
}

std::string parse_eos_connect_lobby_id(char const* connect_string)
{
    parsed_eos_invite_t parsed = parse_eos_connect_invite(connect_string);
    return parsed.kind == eos_invite_kind_t::lobby ? parsed.id : std::string{};
}

bool lobby_is_game_invite_target(Lobby_Infos_pb const& infos)
{
    auto it = infos.attributes().find("Redpoint:EOS:NamespaceFilter");
    return it != infos.attributes().end() &&
        it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kS &&
        it->second.value().s() == "GAME";
}

static bool lobby_is_presence_invite_target(Lobby_Infos_pb const& infos)
{
    auto it = infos.attributes().find("IsCrossPlatformPresence");
    return it != infos.attributes().end() &&
        it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kB &&
        it->second.value().b();
}

static bool steam_bridge_should_session_search_first(Lobby_Infos_pb const& infos)
{
    if (lobby_is_game_invite_target(infos) || lobby_is_presence_invite_target(infos))
        return true;

    auto it = infos.attributes().find("AdvertisedPartyId");
    return it != infos.attributes().end() &&
        it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kS &&
        !it->second.value().s().empty();
}

static bool try_steam_bridge_session_search_first(Network::peer_t const& host_peer, Lobby_Infos_pb const& infos)
{
    if (!steam_bridge_should_session_search_first(infos))
        return false;

    if (GetEOS_Sessions().send_steam_bridge_session_search(host_peer, infos.lobby_id(), infos, 0))
        return true;

    APP_LOG(Log::LogLevel::WARN,
        "Steam invite bridge: session search failed for lobby=%s host=%s; falling back to JoinLobbyAccepted",
        infos.lobby_id().c_str(),
        host_peer.c_str());
    return false;
}

static bool session_infos_has_version(Session_Infos_pb const& infos)
{
    auto version_it = infos.attributes().find("Version");
    if (version_it != infos.attributes().end() &&
        version_it->second.value().value_case() == Session_Attr_Value::ValueCase::kS &&
        !version_it->second.value().s().empty())
    {
        return true;
    }

    auto mm_it = infos.attributes().find("MM_NETWORK_VERSION");
    return mm_it != infos.attributes().end() &&
        mm_it->second.value().value_case() == Session_Attr_Value::ValueCase::kS &&
        !mm_it->second.value().s().empty();
}

void notify_steam_bridge_session_join(std::string const& session_id)
{
    Session_Infos_pb infos;
    if (!GetEOS_Sessions().try_get_session_infos_for_search(session_id, infos))
    {
        APP_LOG(Log::LogLevel::INFO,
            "Steam invite bridge: defer JoinSessionAccepted until host session search for '%s'",
            session_id.c_str());
        return;
    }

    if (!session_infos_has_version(infos))
    {
        APP_LOG(Log::LogLevel::INFO,
            "Steam invite bridge: defer JoinSessionAccepted until host session search (no Version) session='%s'",
            session_id.c_str());
        return;
    }

    GetEOS_Sessions().notify_join_session_accepted(infos);
}

} // namespace

void EOSSDK_Lobby::try_register_steam_bridge_callbacks()
{
#if defined(__WINDOWS__)
    if (Settings::Inst().steam_passthrough)
        return;

    if (_steam_bridge_callbacks_registered)
        return;

    auto const now = std::chrono::steady_clock::now();
    if (_steam_bridge_last_register_try.time_since_epoch().count() != 0 &&
        (now - _steam_bridge_last_register_try) < std::chrono::milliseconds(250))
        return;
    _steam_bridge_last_register_try = now;

    if (!steam_bridge::ensure_module())
    {
        static std::chrono::steady_clock::time_point last_wait_log{};
        if (last_wait_log.time_since_epoch().count() == 0 ||
            (now - last_wait_log) >= std::chrono::seconds(15))
        {
            APP_LOG(Log::LogLevel::INFO, "Steam invite bridge: waiting for steam_api module");
            last_wait_log = now;
        }
        return;
    }

    steam_bridge::try_init();

    HMODULE const steam = static_cast<HMODULE>(steam_bridge::module_handle());
    if (steam == nullptr)
    {
        APP_LOG(Log::LogLevel::WARN, "Steam invite bridge: steam_api handle unavailable after ensure_module");
        return;
    }

    using register_callback_t = void(__cdecl*)(SteamCallbackBase*, int);
    auto reg = reinterpret_cast<register_callback_t>(GetProcAddress(steam, "SteamAPI_RegisterCallback"));
    if (reg == nullptr)
    {
        reg = reinterpret_cast<register_callback_t>(steam_bridge::get_export("SteamAPI_RegisterCallback"));
    }
    if (reg == nullptr)
    {
        APP_LOG(Log::LogLevel::WARN, "Steam invite bridge: SteamAPI_RegisterCallback not found");
        return;
    }

    reg(&g_steam_lobby_join_cb, kSteamCallbackGameLobbyJoinRequested);
    reg(&g_steam_rich_join_cb, kSteamCallbackGameRichPresenceJoinRequested);
    _steam_bridge_callbacks_registered = true;

    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: registered Steam callbacks GameLobbyJoinRequested=%d GameRichPresenceJoinRequested=%d",
        kSteamCallbackGameLobbyJoinRequested,
        kSteamCallbackGameRichPresenceJoinRequested);
#endif
}

void EOSSDK_Lobby::sync_steam_rich_presence_for_lobby(lobby_state_t* lobby)
{
    if (Settings::Inst().steam_passthrough)
        return;

    if (lobby == nullptr || !i_am_owner(lobby))
        return;

    if (lobby->infos.max_lobby_member() <= 1)
        return;

    std::string const active = get_active_join_lobby_id();
    if (!active.empty() && lobby->infos.lobby_id() != active && !lobby_is_game_invite_target(lobby->infos))
        return;

    char const* tag = lobby_is_game_invite_target(lobby->infos) ? kEOSSessionInviteTag : kEOSLobbyInviteTag;
    std::string const connect_value = std::string(kEOSInvitePrefix) + tag + lobby->infos.lobby_id();
    steam_bridge::set_rich_presence_connect(connect_value.c_str());
}

void EOSSDK_Lobby::clear_steam_rich_presence_if_no_owned_lobby()
{
    if (Settings::Inst().steam_passthrough)
        return;

    std::string const my_id = GetEOS_Connect().get_myself()->first->to_string();
    for (auto const& kv : _lobbies)
    {
        if (kv.second._state.infos.owner_id() == my_id)
            return;
    }
    steam_bridge::clear_rich_presence_connect();
}

std::string EOSSDK_Lobby::resolve_peer_from_steam_friend_id(uint64_t steam_friend_id)
{
    if (steam_friend_id == 0)
        return {};

    return GetEOS_Connect().peer_id_for_steam_id(std::to_string(steam_friend_id));
}

void EOSSDK_Lobby::offer_steam_bridge_join(std::string const& eos_lobby_id, std::string const& owner_peer_id)
{
    (void)owner_peer_id;
    if (eos_lobby_id.empty())
        return;

    // Never raise JoinLobbyAccepted from a stub snapshot: wait for lobby search
    // response with full member/attribute data (avoids game crash on accept).
    APP_LOG(Log::LogLevel::DEBUG,
        "Steam invite bridge: defer JoinLobbyAccepted until search resolves lobby=%s",
        eos_lobby_id.c_str());
}

void EOSSDK_Lobby::broadcast_steam_bridge_join_attempt(std::string const& eos_lobby_id)
{
    if (eos_lobby_id.empty())
        return;

    int sent = 0;
    for (auto it = GetEOS_Connect().get_other_users(); it != GetEOS_Connect().get_end_users(); ++it)
    {
        if (!it->second.connected)
            continue;

        Lobby_Join_Request_pb* request = new Lobby_Join_Request_pb;
        request->set_lobby_id(eos_lobby_id);
        request->set_join_id(join_id++);
        if (send_lobby_join_request(it->first->to_string(), request))
            ++sent;
    }

    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: broadcast join lobby_id='%s' sent_to=%d",
        eos_lobby_id.c_str(),
        sent);
}

void EOSSDK_Lobby::initiate_steam_bridge_network_join(Lobby_Infos_pb const& infos)
{
    if (infos.lobby_id().empty() || infos.owner_id().empty())
        return;

    std::string const local_id = Settings::Inst().productuserid->to_string();
    if (infos.owner_id() == local_id)
        return;

    lobby_state_t* existing = get_lobby_by_id(infos.lobby_id());
    if (existing != nullptr && existing->state == lobby_state_t::joined)
        return;

    Lobby_Join_Request_pb* request = new Lobby_Join_Request_pb;
    request->set_lobby_id(infos.lobby_id());
    int32_t const current_join_id = join_id++;
    request->set_join_id(current_join_id);

    lobby_join_t pending;
    pending.cb = nullptr;
    pending.ignore_non_success = false;
    pending.kind = lobby_join_kind_t::join_lobby;
    _joins_requests[current_join_id] = std::move(pending);

    if (!send_lobby_join_request(infos.owner_id(), request))
    {
        _joins_requests.erase(current_join_id);
        APP_LOG(Log::LogLevel::WARN,
            "Steam invite bridge: network join failed lobby=%s owner=%s",
            infos.lobby_id().c_str(),
            infos.owner_id().c_str());
        return;
    }

    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: network join sent lobby=%s owner=%s join_id=%d",
        infos.lobby_id().c_str(),
        infos.owner_id().c_str(),
        current_join_id);
}

bool EOSSDK_Lobby::send_steam_bridge_lobby_search(uint64_t steam_lobby_id, uint64_t steam_friend_id, std::string const& target_peer_id, std::string const& lobby_id, bool session_invite)
{
    int32_t const search_id = _next_steam_bridge_search_id++;
    steam_bridge_join_t pending;
    pending.session_invite = session_invite;
    pending.steam_lobby_id = steam_lobby_id;
    pending.steam_friend_id = steam_friend_id;
    pending.target_peer_id = target_peer_id;
    pending.lobby_id = lobby_id;
    pending.created = std::chrono::steady_clock::now();
    _steam_bridge_joins[search_id] = pending;

    Lobbies_Search_pb* req = new Lobbies_Search_pb;
    req->set_search_id(search_id);
    if (!target_peer_id.empty())
        req->set_target_id(target_peer_id);
    if (!lobby_id.empty())
        req->set_lobby_id(lobby_id);

    Network_Message_pb msg;
    Lobbies_Search_Message_pb* search_msg = new Lobbies_Search_Message_pb;
    search_msg->set_allocated_search(req);
    msg.set_allocated_lobbies_search(search_msg);
    msg.set_source_id(Settings::Inst().productuserid->to_string());
    msg.set_game_id(Settings::Inst().network_game_id());

    bool sent = false;
    bool broadcast_sent = false;
    if (!target_peer_id.empty())
    {
        msg.set_dest_id(target_peer_id);
        sent = GetNetwork().TCPSendTo(msg);
    }
    else
    {
        std::set<Network::peer_t> const peers_sent = GetNetwork().TCPSendToAllPeers(msg);
        sent = !peers_sent.empty();
    }

    if (!sent)
    {
        msg.clear_dest_id();
        if (GetNetwork().SendBroadcast(msg))
        {
            sent = true;
            broadcast_sent = true;
        }
    }

    (void)search_msg->release_search();
    delete req;

    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: search_id=%d steam_lobby=%llu steam_friend=%llu target='%s' eos_lobby='%s' sent=%d broadcast=%d",
        search_id,
        static_cast<unsigned long long>(steam_lobby_id),
        static_cast<unsigned long long>(steam_friend_id),
        target_peer_id.c_str(),
        lobby_id.c_str(),
        sent ? 1 : 0,
        broadcast_sent ? 1 : 0);

    if (!sent)
        _steam_bridge_joins.erase(search_id);
    return sent;
}

bool EOSSDK_Lobby::on_steam_bridge_lobbies_search_response(Network_Message_pb const& msg, Lobbies_Search_response_pb const& resp)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    auto pending_it = _steam_bridge_joins.find(resp.search_id());
    if (pending_it == _steam_bridge_joins.end())
        return true;

    if (resp.lobbies_size() == 0)
    {
        APP_LOG(Log::LogLevel::INFO,
            "Steam invite bridge: search_id=%d response from=%s no lobbies",
            resp.search_id(),
            msg.source_id().c_str());
        if (!pending_it->second.lobby_id.empty())
            broadcast_steam_bridge_join_attempt(pending_it->second.lobby_id);
        return true;
    }

    Lobby_Infos_pb const& infos = resp.lobbies(0);
    Lobby_Infos_pb const join_lobby = resolve_join_party_lobby(infos);
    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: search_id=%d resolved steam_lobby=%llu -> eos_lobby='%s' owner='%s'%s",
        resp.search_id(),
        static_cast<unsigned long long>(pending_it->second.steam_lobby_id),
        join_lobby.lobby_id().c_str(),
        join_lobby.owner_id().c_str(),
        join_lobby.lobby_id() != infos.lobby_id() ? " (party lobby)" : "");

    std::string const session_host = resolve_steam_bridge_session_host_peer(join_lobby, msg.source_id());

    if (pending_it->second.session_invite)
    {
        if (!GetEOS_Sessions().send_steam_bridge_session_search(session_host, join_lobby.lobby_id(), join_lobby, 0))
        {
            APP_LOG(Log::LogLevel::WARN,
                "Steam invite bridge: session search failed for lobby=%s host=%s responder=%s; trying local session snapshot",
                join_lobby.lobby_id().c_str(),
                session_host.c_str(),
                msg.source_id().c_str());

            Session_Infos_pb session_infos;
            if (GetEOS_Sessions().try_get_session_infos_for_search(join_lobby.lobby_id(), session_infos) &&
                session_infos_has_version(session_infos))
            {
                GetEOS_Sessions().notify_join_session_accepted(session_infos);
                notify_join_lobby_accepted(join_lobby);
                initiate_steam_bridge_network_join(join_lobby);
            }
            else
            {
                notify_join_lobby_accepted(join_lobby);
                initiate_steam_bridge_network_join(join_lobby);
            }
        }
    }
    else
    {
        if (try_steam_bridge_session_search_first(session_host, join_lobby))
        {
            _steam_bridge_joins.erase(pending_it);
            return true;
        }

        notify_join_lobby_accepted(join_lobby);
        initiate_steam_bridge_network_join(join_lobby);
    }
    _steam_bridge_joins.erase(pending_it);
    return true;
}

void EOSSDK_Lobby::OnSteamLobbyJoinRequested(uint64_t steam_lobby_id, uint64_t steam_friend_id, char const* connect_string)
{
    GLOBAL_LOCK();

    if (!EOSSDK_Platform::Inst().is_platform_ready())
        return;

    if (Settings::Inst().steam_passthrough)
    {
        APP_LOG(Log::LogLevel::INFO, "Steam invite bridge: ignored in steam_passthrough mode");
        return;
    }

    std::string resolved_connect;
    if (connect_string != nullptr && connect_string[0] != '\0')
        resolved_connect = connect_string;

    steam_bridge::FriendJoinHints hints;
    if (steam_friend_id != 0 && resolved_connect.empty())
        hints = steam_bridge::friend_join_hints(steam_friend_id);
    if (resolved_connect.empty() && !hints.connect_string.empty())
        resolved_connect = hints.connect_string;

    parsed_eos_invite_t eos_invite = parse_eos_connect_invite(resolved_connect.empty() ? nullptr : resolved_connect.c_str());
    std::string eos_lobby_id = eos_invite.kind == eos_invite_kind_t::lobby ? eos_invite.id : std::string{};
    std::string eos_session_id = eos_invite.kind == eos_invite_kind_t::session ? eos_invite.id : std::string{};
    uint64_t const rich_steam_lobby_id = parse_steam_connect_lobby_id(resolved_connect.empty() ? nullptr : resolved_connect.c_str());
    if (steam_lobby_id == 0)
        steam_lobby_id = (rich_steam_lobby_id != 0) ? rich_steam_lobby_id : hints.steam_lobby_id;

    std::string const dedupe_key = std::to_string(steam_friend_id) + ':' + eos_lobby_id + ':' + eos_session_id;
    auto const now = std::chrono::steady_clock::now();
    if (steam_friend_id != 0 && steam_friend_id == _steam_bridge_last_steam_friend
        && (now - _steam_bridge_last_join_time) < std::chrono::seconds(5))
    {
        APP_LOG(Log::LogLevel::INFO,
            "Steam invite bridge: duplicate join suppressed steam_friend=%llu (follow-up Steam callback)",
            static_cast<unsigned long long>(steam_friend_id));
        return;
    }
    if (!dedupe_key.empty() && dedupe_key == _steam_bridge_last_join_key
        && (now - _steam_bridge_last_join_time) < std::chrono::seconds(3))
    {
        APP_LOG(Log::LogLevel::INFO,
            "Steam invite bridge: duplicate join suppressed steam_friend=%llu eos_lobby='%s' eos_session='%s'",
            static_cast<unsigned long long>(steam_friend_id),
            eos_lobby_id.c_str(),
            eos_session_id.c_str());
        return;
    }
    _steam_bridge_last_join_key = dedupe_key;
    _steam_bridge_last_join_time = now;
    _steam_bridge_last_steam_friend = steam_friend_id;

    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: Steam join callback steam_lobby=%llu steam_friend=%llu connect='%s' eos_lobby='%s' eos_session='%s'",
        static_cast<unsigned long long>(steam_lobby_id),
        static_cast<unsigned long long>(steam_friend_id),
        resolved_connect.c_str(),
        eos_lobby_id.c_str(),
        eos_session_id.c_str());

    std::string target_peer_id = resolve_peer_from_steam_friend_id(steam_friend_id);
    if (target_peer_id.empty() && steam_friend_id != 0)
    {
        GetEOS_Connect().request_infos_from_all_peers();
        target_peer_id = resolve_peer_from_steam_friend_id(steam_friend_id);
    }

    if (target_peer_id.empty() && steam_friend_id != 0)
    {
        APP_LOG(Log::LogLevel::WARN,
            "Steam invite bridge: steam_friend=%llu not mapped to EOS peer yet; will broadcast search/join",
            static_cast<unsigned long long>(steam_friend_id));
    }

    if (!eos_lobby_id.empty())
    {
        if (!target_peer_id.empty())
            offer_steam_bridge_join(eos_lobby_id, target_peer_id);
    }

    if (!eos_session_id.empty())
        notify_steam_bridge_session_join(eos_session_id);

    if (!send_steam_bridge_lobby_search(steam_lobby_id, steam_friend_id, target_peer_id, eos_lobby_id.empty() ? eos_session_id : eos_lobby_id, !eos_session_id.empty()))
    {
        APP_LOG(Log::LogLevel::WARN,
            "Steam invite bridge: failed to send lobby search for steam_lobby=%llu steam_friend=%llu",
            static_cast<unsigned long long>(steam_lobby_id),
            static_cast<unsigned long long>(steam_friend_id));

        std::string const fallback_lobby = !eos_lobby_id.empty() ? eos_lobby_id : eos_session_id;
        if (!fallback_lobby.empty())
            broadcast_steam_bridge_join_attempt(fallback_lobby);
    }
}

} // namespace sdk
