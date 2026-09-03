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
#include "settings.h"
#include "os_funcs.h"

#include <algorithm>
#include <vector>

namespace
{
void append_unique_peer(std::vector<std::string>& peers, std::string const& peer)
{
    if (peer.empty())
        return;

    if (std::find(peers.begin(), peers.end(), peer) == peers.end())
        peers.push_back(peer);
}

std::string session_owner_product_id(Session_Infos_pb const& infos)
{
    std::string const& host = infos.host_address();
    size_t const dot = host.find('.');
    if (dot == 32 && sdk::looks_like_hex_product_user_id(host.substr(0, dot)))
        return host.substr(0, dot);

    if (infos.registered_players_size() > 0)
        return infos.registered_players(0);

    if (infos.players_size() > 0)
        return infos.players(0);

    return {};
}

static bool session_owned_locally(Session_Infos_pb const& infos)
{
    std::string const owner = session_owner_product_id(infos);
    return !owner.empty() && owner == Settings::Inst().productuserid->to_string();
}

static bool local_player_in_session(Session_Infos_pb const& infos)
{
    std::string const local_id = Settings::Inst().productuserid->to_string();
    for (auto const& player : infos.registered_players())
    {
        if (player == local_id)
            return true;
    }
    for (auto const& player : infos.players())
    {
        if (player == local_id)
            return true;
    }
    return false;
}

static void reset_remote_game_session_join_state(Session_Infos_pb& infos)
{
    if (session_owned_locally(infos))
        return;

    if (local_player_in_session(infos))
        return;

    std::string const owner = session_owner_product_id(infos);
    infos.mutable_registered_players()->Clear();
    if (!owner.empty())
        *infos.add_registered_players() = owner;

    infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending));
}

std::vector<std::string> session_member_peers(Session_Infos_pb const& infos)
{
    std::vector<std::string> peers;
    append_unique_peer(peers, session_owner_product_id(infos));
    for (auto const& player : infos.registered_players())
        append_unique_peer(peers, player);
    for (auto const& player : infos.players())
        append_unique_peer(peers, player);
    return peers;
}

bool host_address_uses_loopback(Session_Infos_pb const& infos)
{
    std::string const& host = infos.host_address();
    if (host.empty())
        return true;

    std::string lower = host;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower.find("127.0.0.1") != std::string::npos || lower.find("localhost") != std::string::npos)
        return true;

    if (lower.rfind("eosp2p:127", 0) == 0)
        return true;

    std::string const port = session_redpoint_eosp2p_port(lower);
    if (session_redpoint_eosp2p_port_is_loopback(port))
        return true;

    return false;
}

static std::string configured_p2p_port_string()
{
    return std::to_string(GetEOS_P2P().p2p_port());
}

static void fix_session_host_loopback_for_lan(Session_Infos_pb& infos, std::string const& peer_ip)
{
    if (peer_ip.empty() || !host_address_uses_loopback(infos))
        return;

    std::string const& host = infos.host_address();
    size_t const eosp2p = host.rfind(".eosp2p:");
    if (eosp2p != std::string::npos && eosp2p >= 32)
    {
        std::string const port = session_redpoint_eosp2p_port(host);
        if (port.empty() || session_redpoint_eosp2p_port_is_loopback(port))
            infos.set_host_address(host.substr(0, eosp2p) + ".eosp2p:" + configured_p2p_port_string());
        else
            infos.set_host_address("eosp2p:" + peer_ip + ":" + port);
    }
    else if (host.rfind("eosp2p:", 0) == 0)
    {
        std::string const rest = host.substr(7);
        size_t const colon = rest.find(':');
        infos.set_host_address(colon != std::string::npos ? "eosp2p:" + peer_ip + rest.substr(colon) : "eosp2p:" + peer_ip);
    }
    else
    {
        infos.set_host_address(peer_ip);
    }
}

static bool resolve_session_host_ipv4(Session_Infos_pb const& infos, std::string const& peer_id_hint, std::string& out_ip)
{
    auto try_peer = [&](std::string const& peer_id) -> bool
    {
        return !peer_id.empty() && GetNetwork().try_get_peer_ipv4(peer_id, out_ip);
    };

    if (try_peer(peer_id_hint))
        return true;
    if (try_peer(session_owner_product_id(infos)))
        return true;

    for (auto const& player : infos.registered_players())
    {
        if (try_peer(player))
            return true;
    }
    for (auto const& player : infos.players())
    {
        if (try_peer(player))
            return true;
    }

    std::string const local_id = Settings::Inst().productuserid->to_string();
    std::string const owner = session_owner_product_id(infos);
    if (owner == local_id || (!infos.host_address().empty() && infos.host_address().substr(0, 32) == local_id))
    {
        out_ip = get_preferred_lan_ipv4();
        return !out_ip.empty();
    }

    return false;
}

static void apply_session_host_network_fix(Session_Infos_pb& infos, std::string const& peer_id_hint = {})
{
    if (!host_address_uses_loopback(infos))
    {
        infos.set_host_address(normalize_session_host_address(infos.host_address()));
        return;
    }

    std::string host_ip;
    if (resolve_session_host_ipv4(infos, peer_id_hint, host_ip))
    {
        std::string const before = infos.host_address();
        fix_session_host_loopback_for_lan(infos, host_ip);
        APP_LOG(Log::LogLevel::INFO, "Session host_address rewritten: %s -> %s (peer_hint=%s)",
            before.c_str(), infos.host_address().c_str(), peer_id_hint.c_str());
    }
    else
    {
        APP_LOG(Log::LogLevel::WARN, "Session host_address resolve failed for %s (peer_hint=%s)",
            infos.host_address().c_str(), peer_id_hint.c_str());
        infos.set_host_address(normalize_session_host_address(infos.host_address()));
    }
}

static bool session_infos_attr_bool(Session_Infos_pb const& infos, char const* key)
{
    auto it = infos.attributes().find(key);
    if (it == infos.attributes().end())
        return false;

    if (it->second.value().value_case() == Session_Attr_Value::ValueCase::kB)
        return it->second.value().b() != 0;

    return false;
}

static bool session_infos_has_version(Session_Infos_pb const& infos)
{
    return infos.attributes().find("Version") != infos.attributes().end() ||
           infos.attributes().find("MM_NETWORK_VERSION") != infos.attributes().end();
}

static void reconcile_session_state_for_network(Session_Infos_pb& infos)
{
    bool const local_member = local_player_in_session(infos);

    auto const state = static_cast<EOS_EOnlineSessionState>(infos.state());
    if (state == EOS_EOnlineSessionState::EOS_OSS_InProgress)
    {
        if (!session_owned_locally(infos) && !local_member)
            infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending));
        return;
    }

    if (!session_owned_locally(infos) && !local_member)
    {
        infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending));
        return;
    }

    if (session_infos_attr_bool(infos, "__EOS_bListening") ||
        (state == EOS_EOnlineSessionState::EOS_OSS_Ended && session_infos_has_version(infos)))
    {
        infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress));
    }
}

static void set_session_attr_bool(Session_Infos_pb& infos, char const* key, bool value)
{
    Session_Attribute attr;
    auto existing = infos.attributes().find(key);
    if (existing != infos.attributes().end())
        attr = existing->second;
    attr.mutable_value()->set_b(value ? 1 : 0);
    (*infos.mutable_attributes())[key] = std::move(attr);
}

static void set_session_attr_string(Session_Infos_pb& infos, char const* key, std::string const& value)
{
    Session_Attribute attr;
    auto existing = infos.attributes().find(key);
    if (existing != infos.attributes().end())
        attr = existing->second;
    attr.mutable_value()->set_s(value);
    (*infos.mutable_attributes())[key] = std::move(attr);
}

static bool session_infos_is_game_namespace(Session_Infos_pb const& infos)
{
    auto it = infos.attributes().find("Redpoint:EOS:NamespaceFilter");
    return it != infos.attributes().end() &&
        it->second.value().value_case() == Session_Attr_Value::ValueCase::kS &&
        it->second.value().s() == "GAME";
}

static void ensure_session_advertisement_ready(Session_Infos_pb& infos)
{
    std::string const owner = session_owner_product_id(infos);
    if (!owner.empty())
        set_session_attr_string(infos, "UserId", owner);

    if (!session_infos_is_game_namespace(infos) && !session_infos_has_version(infos))
        return;

    auto const state = static_cast<EOS_EOnlineSessionState>(infos.state());
    if (state == EOS_EOnlineSessionState::EOS_OSS_InProgress ||
        session_infos_has_version(infos) ||
        session_infos_attr_bool(infos, "Redpoint:EOS:Ready"))
    {
        if (!session_infos_attr_bool(infos, "__EOS_bListening"))
        {
            set_session_attr_bool(infos, "__EOS_bListening", true);
            APP_LOG(Log::LogLevel::INFO, "Session %s: forced __EOS_bListening=true for join/travel",
                infos.session_id().c_str());
        }
    }
}

static void fix_redpoint_eosp2p_host_suffix(Session_Infos_pb& infos)
{
    std::string const& host = infos.host_address();
    size_t const eosp2p = host.rfind(".eosp2p:");
    if (eosp2p == std::string::npos || eosp2p < 32)
        return;

    std::string const suffix = host.substr(eosp2p + 8);
    if (suffix.empty() || suffix.find('.') == std::string::npos)
        return;

    std::string const fixed = host.substr(0, eosp2p) + ".eosp2p:" + configured_p2p_port_string();
    APP_LOG(Log::LogLevel::INFO, "Session host_address eosp2p suffix fixed: %s -> %s",
        host.c_str(), fixed.c_str());
    infos.set_host_address(fixed);
}

static void restore_session_host_eosp2p_format(Session_Infos_pb& infos)
{
    std::string const& host = infos.host_address();
    if (host.find("eosp2p") != std::string::npos)
    {
        fix_redpoint_eosp2p_host_suffix(infos);
        return;
    }

    std::string const owner = session_owner_product_id(infos);
    if (owner.empty())
        return;

    infos.set_host_address(owner + ".default.eosp2p:" + configured_p2p_port_string());
}

}

static bool session_attr_bool(Session_Infos_pb const& infos, char const* key)
{
    auto it = infos.attributes().find(key);
    if (it == infos.attributes().end())
        return false;

    auto const& val = it->second.value();
    if (val.value_case() == Session_Attr_Value::ValueCase::kB)
        return val.b() != 0;

    return false;
}

static void promote_session_if_listen_server_ready(sdk::session_state_t* session)
{
    if (session == nullptr)
        return;

    if (!session_attr_bool(session->infos, "__EOS_bListening"))
        return;

    if (session->infos.state() != utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending))
        return;

    session->infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress));
    APP_LOG(Log::LogLevel::INFO, "Session %s promoted to InProgress (listen server ready)",
        session->infos.session_id().c_str());
}

static void EOS_CALL noop_register_players_cb(EOS_Sessions_RegisterPlayersCallbackInfo const*)
{
}

static void EOS_CALL noop_join_session_cb(void*)
{
}

namespace sdk
{

decltype(EOSSDK_Sessions::join_timeout) EOSSDK_Sessions::join_timeout;

EOS_ProductUserId owner_user_id_for_session_infos(Session_Infos_pb const& infos)
{
    std::string owner;
    std::string const& host = infos.host_address();
    size_t const dot = host.find('.');
    if (dot == 32 && sdk::looks_like_hex_product_user_id(host.substr(0, dot)))
        owner = host.substr(0, dot);
    else if (infos.registered_players_size() > 0)
        owner = infos.registered_players(0);
    else if (infos.players_size() > 0)
        owner = infos.players(0);

    return owner.empty() ? nullptr : GetProductUserId(owner);
}

EOSSDK_Sessions::EOSSDK_Sessions()
{
    GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kSession);
    GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kSessionsSearch);

    GetCB_Manager().register_callbacks(this);

    GetCB_Manager().register_frame(this);
    
}

EOSSDK_Sessions::~EOSSDK_Sessions()
{
    GetCB_Manager().unregister_frame(this);

    GetCB_Manager().unregister_callbacks(this);

    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kSessionsSearch);
    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kSession);

    GetCB_Manager().remove_all_notifications(this);
}

static void split_search_delimited_values(std::string const& value, std::vector<std::string>& out)
{
    out.clear();
    size_t start = 0;
    for (size_t i = 0; i <= value.size(); ++i)
    {
        if (i == value.size() || value[i] == ';' || value[i] == ',')
        {
            if (i > start)
            {
                std::string token = value.substr(start, i - start);
                if (!token.empty())
                    out.push_back(std::move(token));
            }
            start = i + 1;
        }
    }
}

static bool session_value_in_search_tokens(std::string const& session_value, std::string const& search_value)
{
    if (search_value.find(';') == std::string::npos && search_value.find(',') == std::string::npos)
    {
        if (session_value == search_value)
            return true;

        if (session_value.find(';') != std::string::npos || session_value.find(',') != std::string::npos)
        {
            std::vector<std::string> session_tokens;
            split_search_delimited_values(session_value, session_tokens);
            for (auto const& token : session_tokens)
            {
                if (token == search_value)
                    return true;
            }
        }
        return false;
    }

    std::vector<std::string> tokens;
    split_search_delimited_values(search_value, tokens);
    for (auto const& token : tokens)
    {
        if (session_value == token)
            return true;
    }
    return false;
}

static int count_session_value_in_search_tokens(std::string const& session_value, std::string const& search_value)
{
    if (search_value.find(';') == std::string::npos && search_value.find(',') == std::string::npos)
        return session_value == search_value ? 1 : 0;

    int matches = 0;
    std::vector<std::string> tokens;
    split_search_delimited_values(search_value, tokens);
    for (auto const& token : tokens)
    {
        if (session_value == token)
            ++matches;
    }
    return matches;
}

static bool parse_attr_bool_string(std::string const& s)
{
    return s == "true" || s == "True" || s == "1" || s == "EOS_TRUE";
}

static bool try_parse_attr_int64(std::string const& s, int64_t& out)
{
    if (s.empty())
        return false;

    char* end = nullptr;
    long long const parsed = std::strtoll(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0')
        return false;

    out = static_cast<int64_t>(parsed);
    return true;
}

static bool session_search_missing_attribute_matches(Session_Search_Parameter const& param)
{
    if (param.param().empty())
        return false;

    for (auto& comparisons : param.param())
    {
        EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
        switch (comparisons.second.value_case())
        {
            case Session_Attr_Value::ValueCase::kB:
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_EQUAL && !comparisons.second.b())
                    continue;
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_NOTEQUAL && comparisons.second.b())
                    continue;
                return false;
            case Session_Attr_Value::ValueCase::kI:
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_EQUAL && comparisons.second.i() == 0)
                    continue;
                return false;
            case Session_Attr_Value::ValueCase::kS:
            {
                std::string const& s = comparisons.second.s();
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_EQUAL &&
                    (s.empty() || s == "0" || s == "false" || s == "False"))
                    continue;
                return false;
            }
            default:
                return false;
        }
    }
    return true;
}

template<typename T>
bool compare_attribute_values(T const& v1, EOS_EOnlineComparisonOp op, T const& v2, std::string const& attr_name)
{
    bool res = false;
    try
    {
        switch (op)
        {
            case EOS_EOnlineComparisonOp::EOS_CO_EQUAL             : res = v1 == v2; break;
            case EOS_EOnlineComparisonOp::EOS_CO_NOTEQUAL          : res = v1 != v2; break;
            case EOS_EOnlineComparisonOp::EOS_CO_GREATERTHAN       : res = v1 >  v2; break;
            case EOS_EOnlineComparisonOp::EOS_CO_GREATERTHANOREQUAL: res = v1 >= v2; break;
            case EOS_EOnlineComparisonOp::EOS_CO_LESSTHAN          : res = v1 <  v2; break;
            case EOS_EOnlineComparisonOp::EOS_CO_LESSTHANOREQUAL   : res = v1 <= v2; break;
            default: res = true;
        }
    }
    catch (...)
    {}

    return res;
}

static bool compare_string_search_op(std::string const& session_value, EOS_EOnlineComparisonOp op, std::string const& search_value, std::string const& attr_name)
{
    switch (op)
    {
        case EOS_EOnlineComparisonOp::EOS_CO_EQUAL:
            return session_value == search_value;
        case EOS_EOnlineComparisonOp::EOS_CO_NOTEQUAL:
            return session_value != search_value;
        case EOS_EOnlineComparisonOp::EOS_CO_CONTAINS:
            return session_value.find(search_value) != std::string::npos;
        case EOS_EOnlineComparisonOp::EOS_CO_ANYOF:
            return session_value_in_search_tokens(session_value, search_value);
        case EOS_EOnlineComparisonOp::EOS_CO_NOTANYOF:
            return !session_value_in_search_tokens(session_value, search_value);
        case EOS_EOnlineComparisonOp::EOS_CO_ONEOF:
            return count_session_value_in_search_tokens(session_value, search_value) == 1;
        case EOS_EOnlineComparisonOp::EOS_CO_NOTONEOF:
            return count_session_value_in_search_tokens(session_value, search_value) != 1;
        default:
            return compare_attribute_values(session_value, op, search_value, attr_name);
    }
}

static bool compare_session_attr_to_search(Session_Attribute const& session_attr, EOS_EOnlineComparisonOp comp, Session_Attr_Value const& search_value, std::string const& attr_name)
{
    auto const& session_value = session_attr.value();
    if (search_value.value_case() == session_value.value_case())
    {
        switch (search_value.value_case())
        {
            case Session_Attr_Value::ValueCase::kB:
                return compare_attribute_values(session_value.b(), comp, search_value.b(), attr_name);
            case Session_Attr_Value::ValueCase::kI:
                return compare_attribute_values(session_value.i(), comp, search_value.i(), attr_name);
            case Session_Attr_Value::ValueCase::kD:
                return compare_attribute_values(session_value.d(), comp, search_value.d(), attr_name);
            case Session_Attr_Value::ValueCase::kS:
                return compare_string_search_op(session_value.s(), comp, search_value.s(), attr_name);
            default:
                return false;
        }
    }

    if (search_value.value_case() == Session_Attr_Value::ValueCase::kB && session_value.value_case() == Session_Attr_Value::ValueCase::kS)
    {
        bool const session_bool = parse_attr_bool_string(session_value.s());
        bool const search_bool = search_value.b();
        return compare_attribute_values(session_bool, comp, search_bool, attr_name);
    }
    if (search_value.value_case() == Session_Attr_Value::ValueCase::kS && session_value.value_case() == Session_Attr_Value::ValueCase::kB)
    {
        bool const search_bool = parse_attr_bool_string(search_value.s());
        bool const session_bool = session_value.b();
        return compare_attribute_values(session_bool, comp, search_bool, attr_name);
    }
    if (search_value.value_case() == Session_Attr_Value::ValueCase::kI && session_value.value_case() == Session_Attr_Value::ValueCase::kB)
        return compare_attribute_values(static_cast<int64_t>(session_value.b() ? 1 : 0), comp, search_value.i(), attr_name);
    if (search_value.value_case() == Session_Attr_Value::ValueCase::kB && session_value.value_case() == Session_Attr_Value::ValueCase::kI)
        return compare_attribute_values(session_value.i(), comp, static_cast<int64_t>(search_value.b() ? 1 : 0), attr_name);

    if (search_value.value_case() == Session_Attr_Value::ValueCase::kI && session_value.value_case() == Session_Attr_Value::ValueCase::kS)
    {
        int64_t session_i = 0;
        if (try_parse_attr_int64(session_value.s(), session_i))
        {
            int64_t const search_i = search_value.i();
            return compare_attribute_values(session_i, comp, search_i, attr_name);
        }
    }
    if (search_value.value_case() == Session_Attr_Value::ValueCase::kS && session_value.value_case() == Session_Attr_Value::ValueCase::kI)
    {
        int64_t search_i = 0;
        if (try_parse_attr_int64(search_value.s(), search_i))
        {
            int64_t const session_i = session_value.i();
            return compare_attribute_values(session_i, comp, search_i, attr_name);
        }
    }

    if (search_value.value_case() == Session_Attr_Value::ValueCase::kS && session_value.value_case() == Session_Attr_Value::ValueCase::kD)
        return compare_string_search_op(std::to_string(session_value.d()), comp, search_value.s(), attr_name);
    if (search_value.value_case() == Session_Attr_Value::ValueCase::kD && session_value.value_case() == Session_Attr_Value::ValueCase::kS)
        return compare_string_search_op(session_value.s(), comp, std::to_string(search_value.d()), attr_name);

    return false;
}

static bool session_has_version(Session_Infos_pb const& session);

bool EOSSDK_Sessions::local_player_in_game_session(std::string const& session_id)
{
    session_state_t* game_session = get_session_by_name("GameSession");
    if (game_session == nullptr)
        return false;

    if (game_session->state != session_state_t::state_e::joined)
        return false;

    Session_Infos_pb const& infos = game_session->infos;
    if (!session_id.empty() && infos.session_id() != session_id)
        return false;

    std::string const local_id = Settings::Inst().productuserid->to_string();
    for (auto const& player : infos.registered_players())
    {
        if (player == local_id)
            return true;
    }
    return false;
}
static bool session_storage_is_lobby_shadow(std::string const& storage_key);
static void merge_session_attributes(Session_Infos_pb& target, Session_Infos_pb const& source, bool source_overwrites);

static session_state_t* find_primary_session_for_id(
    std::unordered_map<std::string, session_state_t>& sessions,
    std::string const& session_id)
{
    if (session_id.empty())
        return nullptr;

    auto prefer = [&](char const* name) -> session_state_t*
    {
        auto it = sessions.find(name);
        if (it != sessions.end() && it->second.infos.session_id() == session_id)
            return &it->second;
        return nullptr;
    };

    if (session_state_t* session = prefer("GameSession"))
        return session;
    if (session_state_t* session = prefer("Default"))
        return session;

    auto by_key = sessions.find(session_id);
    if (by_key != sessions.end())
        return &by_key->second;

    for (auto& entry : sessions)
    {
        if (entry.second.infos.session_id() == session_id)
            return &entry.second;
    }

    return nullptr;
}

static void replicate_session_to_all_copies(
    std::unordered_map<std::string, session_state_t>& sessions,
    session_state_t const& source)
{
    std::string const& session_id = source.infos.session_id();
    if (session_id.empty())
        return;

    for (auto& entry : sessions)
    {
        if (entry.second.infos.session_id() == session_id)
        {
            entry.second.state = source.state;
            entry.second.infos = source.infos;
        }
    }
}

session_state_t* EOSSDK_Sessions::get_session_by_id(std::string const& session_id)
{
    if (session_id.empty())
        return nullptr;

    auto by_key = _sessions.find(session_id);
    if (by_key != _sessions.end())
        return &by_key->second;

    auto it = std::find_if(_sessions.begin(), _sessions.end(), [&session_id]( std::pair<std::string const, session_state_t>& infos)
    {
        return session_id == infos.second.infos.session_id();
    });
    if (it == _sessions.end())
        return nullptr;

    return &it->second;
}

session_state_t* EOSSDK_Sessions::get_primary_session_for_id(std::string const& session_id)
{
    return find_primary_session_for_id(_sessions, session_id);
}

session_state_t* EOSSDK_Sessions::get_session_by_name(std::string const& session_name)
{
    auto it = _sessions.find(session_name);
    if (it != _sessions.end())
        return &it->second;

    if (session_name == "GameSession")
    {
        session_state_t* best = nullptr;
        size_t best_score = 0;
        for (auto& entry : _sessions)
        {
            if (!session_has_version(entry.second.infos))
                continue;

            // Party/presence lobby shadows must not satisfy CopyActiveSessionHandle("GameSession").
            if (session_storage_is_lobby_shadow(entry.first))
                continue;

            // Skip lobby shadow entries keyed by lobby id while pointing at a different session id.
            if (entry.first != "GameSession" && entry.first != entry.second.infos.session_id())
                continue;

            size_t score = entry.second.infos.attributes_size();
            if (entry.first == "GameSession")
                score += 10000;
            else if (entry.first == entry.second.infos.session_id())
                score += 1000;

            if (best == nullptr || score > best_score)
            {
                best = &entry.second;
                best_score = score;
            }
        }
        return best;
    }

    return nullptr;
}

bool EOSSDK_Sessions::session_match_from_attributes(session_state_t* session, google::protobuf::Map<std::string, Session_Search_Parameter> const& parameters)
{
    session_state_t match_state = *session;
    if (!session_has_version(match_state.infos))
    {
        if (session_state_t* game_session = get_session_by_name("GameSession"))
        {
            if (session_has_version(game_session->infos))
                merge_session_attributes(match_state.infos, game_session->infos, false);
        }
    }

    for (auto& param : parameters)
    {
        if (param.first == "bucket")
        {
            auto& comparison = *param.second.param().begin();
            EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparison.first);

            switch (comparison.second.value_case())
            {
                case Session_Attr_Value::ValueCase::kS:
                {
                    std::string const& s_session = match_state.infos.bucket_id();
                    std::string const& s_search = comparison.second.s();
                    if (!compare_string_search_op(s_session, comp, s_search, param.first))
                        return false;
                }
                break;
                default: return false;
            }
        }
        else if (param.first == "is_matchmaking")
        {
            continue;
        }
        else if (param.first == "UserId")
        {
            std::string user_id;
            auto uit = match_state.infos.attributes().find("UserId");
            if (uit != match_state.infos.attributes().end() &&
                uit->second.value().value_case() == Session_Attr_Value::ValueCase::kS)
            {
                user_id = uit->second.value().s();
            }
            if (user_id.empty() && match_state.infos.registered_players_size() > 0)
                user_id = match_state.infos.registered_players(0);
            if (user_id.empty() && match_state.infos.players_size() > 0)
                user_id = match_state.infos.players(0);

            bool matched = false;
            if (!user_id.empty())
            {
                for (auto& comparisons : param.second.param())
                {
                    if (comparisons.second.value_case() != Session_Attr_Value::ValueCase::kS)
                        continue;

                    EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
                    if (compare_string_search_op(user_id, comp, comparisons.second.s(), param.first))
                    {
                        matched = true;
                        break;
                    }
                }
            }

            if (!matched && !session_search_missing_attribute_matches(param.second))
                return false;
        }
        else
        {
            auto it = match_state.infos.attributes().find(param.first);
            if (it == match_state.infos.attributes().end())
            {
                if (!session_search_missing_attribute_matches(param.second))
                    return false;
            }
            else
            {
                for (auto& comparisons : param.second.param())
                {
                    EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
                    if (!compare_session_attr_to_search(it->second, comp, comparisons.second, param.first))
                        return false;
                }
            }
        }
    }

    return true;
}

std::vector<session_state_t*> EOSSDK_Sessions::get_sessions_from_attributes(google::protobuf::Map<std::string, Session_Search_Parameter> const& parameters)
{
    std::vector<session_state_t*> res;
    for (auto& session : _sessions)
    {
        bool found = session_match_from_attributes(&session.second, parameters);
        if (found)
        {
            res.emplace_back(&session.second);
        }
        else
        {
            APP_LOG(Log::LogLevel::DEBUG, "This session didn't match: %s(%s)", session.second.infos.session_id().c_str(), session.first.c_str());
        }
    }

    return res;
}

static Session_Attr_Value lobby_attr_value_to_session(Lobby_Attr_Value const& lobby_value)
{
    Session_Attr_Value session_value;
    switch (lobby_value.value_case())
    {
        case Lobby_Attr_Value::ValueCase::kB: session_value.set_b(lobby_value.b()); break;
        case Lobby_Attr_Value::ValueCase::kI: session_value.set_i(lobby_value.i()); break;
        case Lobby_Attr_Value::ValueCase::kD: session_value.set_d(lobby_value.d()); break;
        case Lobby_Attr_Value::ValueCase::kS: session_value.set_s(lobby_value.s()); break;
        default: break;
    }
    return session_value;
}

static Session_Infos_pb make_session_infos_from_lobby(Lobby_Infos_pb const& lobby, std::string const& session_id)
{
    Session_Infos_pb session;
    session.set_session_id(session_id);
    session.set_bucket_id(lobby.bucket_id());
    session.set_max_players(lobby.max_lobby_member());
    session.set_permission_level(lobby.permission_level());
    session.set_join_in_progress_allowed(true);
    session.set_invites_allowed(lobby.invites_allowed());
    session.set_presence_allowed(lobby.bpresenceenabled());
    session.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending));

    auto add_player = [&](std::string const& player)
    {
        if (player.empty() || !sdk::looks_like_hex_product_user_id(player))
            return;

        for (auto const& existing : session.players())
        {
            if (existing == player)
                return;
        }

        *session.add_players() = player;
    };

    add_player(lobby.owner_id());
    for (auto const& member : lobby.members())
        add_player(member.first);

    if (!lobby.owner_id().empty())
        *session.add_registered_players() = lobby.owner_id();

    for (auto const& attr : lobby.attributes())
    {
        if (attr.second.value().value_case() == Lobby_Attr_Value::ValueCase::VALUE_NOT_SET)
            continue;

        Session_Attribute session_attr;
        session_attr.set_advertisement_type(attr.second.visibility_type());
        *session_attr.mutable_value() = lobby_attr_value_to_session(attr.second.value());
        (*session.mutable_attributes())[attr.first] = std::move(session_attr);
    }

    return session;
}

static bool session_attr_string_nonempty(Session_Infos_pb const& session, char const* key)
{
    auto it = session.attributes().find(key);
    return it != session.attributes().end() &&
        it->second.value().value_case() == Session_Attr_Value::ValueCase::kS &&
        !it->second.value().s().empty();
}

static bool session_has_version(Session_Infos_pb const& session)
{
    return session_attr_string_nonempty(session, "Version") ||
           session_attr_string_nonempty(session, "MM_NETWORK_VERSION");
}

static bool session_storage_is_lobby_shadow(std::string const& storage_key)
{
    return !storage_key.empty() && GetEOS_Lobby().get_lobby_by_id(storage_key) != nullptr;
}

static std::string resolve_advertised_game_session_id(Lobby_Infos_pb const& lobby);

static std::string resolve_canonical_session_id_for_lobby(std::string const& lobby_id)
{
    if (lobby_state_t* lobby = GetEOS_Lobby().get_lobby_by_id(lobby_id))
    {
        std::string const advertised = resolve_advertised_game_session_id(lobby->infos);
        if (!advertised.empty())
            return advertised;
    }

    if (session_state_t* game_session = GetEOS_Sessions().get_session_by_name("Default"))
    {
        if (session_has_version(game_session->infos) &&
            !session_storage_is_lobby_shadow(game_session->infos.session_id()))
            return game_session->infos.session_id();
    }

    if (session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession"))
    {
        if (session_has_version(game_session->infos) &&
            !session_storage_is_lobby_shadow(game_session->infos.session_id()))
            return game_session->infos.session_id();
    }

    return {};
}

static void normalize_session_id_for_response(Session_Infos_pb& out, std::string const& search_key)
{
    if (search_key.empty())
        return;

    bool const search_is_lobby = GetEOS_Lobby().get_lobby_by_id(search_key) != nullptr;
    if (!search_is_lobby && !session_storage_is_lobby_shadow(search_key))
    {
        out.set_session_id(search_key);
        return;
    }

    std::string canonical = resolve_canonical_session_id_for_lobby(search_key);
    if (canonical.empty() && session_has_version(out) && !session_storage_is_lobby_shadow(out.session_id()))
        canonical = out.session_id();

    if (!canonical.empty())
        out.set_session_id(canonical);
}

static bool session_is_canonical_game_host(std::string const& storage_key, Session_Infos_pb const& infos)
{
    if (!session_has_version(infos))
        return false;

    if (session_storage_is_lobby_shadow(infos.session_id()))
        return false;

    if (storage_key == "GameSession" || storage_key == "Default")
        return true;

    if (session_storage_is_lobby_shadow(storage_key))
        return false;

    return storage_key == infos.session_id();
}

static bool local_user_hosts_active_game_session()
{
    session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession");
    if (game_session == nullptr)
        return false;

    if (!session_owned_locally(game_session->infos))
        return false;

    switch (game_session->state)
    {
        case session_state_t::state_e::created:
        case session_state_t::state_e::joined:
        case session_state_t::state_e::joining:
            return true;
        default:
            return false;
    }
}

static bool should_alias_discovered_session(Session_Infos_pb const& discovered)
{
    if (!session_has_version(discovered))
        return false;

    if (local_user_hosts_active_game_session())
    {
        APP_LOG(Log::LogLevel::INFO,
            "Discovered session %s not aliased to GameSession: local user hosts active GameSession",
            discovered.session_id().c_str());
        return false;
    }

    std::string const local_id = Settings::Inst().productuserid->to_string();
    std::string const owner = session_owner_product_id(discovered);
    if (!owner.empty() && owner == local_id)
    {
        session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession");
        if (game_session != nullptr &&
            game_session->infos.session_id() != discovered.session_id() &&
            session_owned_locally(game_session->infos))
        {
            APP_LOG(Log::LogLevel::INFO,
                "Discovered session %s not aliased to GameSession: local GameSession %s takes precedence",
                discovered.session_id().c_str(),
                game_session->infos.session_id().c_str());
            return false;
        }
    }

    return true;
}

static bool session_matches_expected_host(Session_Infos_pb const& infos, std::string const& expected_host)
{
    if (expected_host.empty())
        return true;

    std::string const owner = session_owner_product_id(infos);
    if (owner.empty())
        return true;

    return owner == expected_host;
}

static void sync_game_session_alias(
    std::unordered_map<std::string, session_state_t>& sessions,
    std::string const& storage_key,
    session_state_t const& source)
{
    if (!session_is_canonical_game_host(storage_key, source.infos))
        return;

    auto existing_it = sessions.find("GameSession");
    if (existing_it != sessions.end() && session_owned_locally(existing_it->second.infos))
    {
        APP_LOG(Log::LogLevel::INFO,
            "GameSession alias sync blocked: preserving local host session %s (would overwrite with %s from %s)",
            existing_it->second.infos.session_id().c_str(),
            source.infos.session_id().c_str(),
            storage_key.c_str());
        return;
    }

    session_state_t& alias = sessions["GameSession"];
    alias.state = source.state;
    alias.infos = source.infos;

    APP_LOG(Log::LogLevel::INFO,
        "GameSession alias synced: session_id=%s host=%s (from %s)",
        alias.infos.session_id().c_str(),
        alias.infos.host_address().c_str(),
        storage_key.c_str());
}

static void merge_session_attributes(Session_Infos_pb& target, Session_Infos_pb const& source, bool source_overwrites)
{
    for (auto const& attr : source.attributes())
    {
        if (attr.second.value().value_case() == Session_Attr_Value::ValueCase::VALUE_NOT_SET)
            continue;

        if (attr.first.find("PublicMemberList") != std::string::npos)
            continue;

        auto existing = target.attributes().find(attr.first);
        if (existing != target.attributes().end() && !source_overwrites)
        {
            if (attr.first == "Version" || attr.first == "MM_NETWORK_VERSION" || attr.first == "MODDED")
                continue;
            if (existing->second.value().value_case() != Session_Attr_Value::ValueCase::VALUE_NOT_SET)
                continue;
        }

        (*target.mutable_attributes())[attr.first] = attr.second;
    }
}

static session_state_t* find_richest_session_for_search(
    std::unordered_map<std::string, session_state_t> const& sessions,
    std::string const& session_id,
    std::string const& owner_id)
{
    session_state_t* best = nullptr;
    size_t best_score = 0;

    auto game_it = sessions.find("GameSession");
    if (game_it != sessions.end() && session_has_version(game_it->second.infos))
        return const_cast<session_state_t*>(&game_it->second);

    for (auto const& entry : sessions)
    {
        if (entry.first == session_id)
            continue;

        bool id_match = entry.second.infos.session_id() == session_id;
        bool owner_match = !owner_id.empty() &&
            entry.second.infos.registered_players_size() > 0 &&
            entry.second.infos.registered_players(0) == owner_id;

        if (!id_match && !owner_match)
            continue;

        size_t score = entry.second.infos.attributes_size();
        if (session_has_version(entry.second.infos))
            score += 1000;

        if (score > best_score)
        {
            best = const_cast<session_state_t*>(&entry.second);
            best_score = score;
        }
    }

    return best;
}

static bool lobby_is_game_namespace(Lobby_Infos_pb const& lobby)
{
    auto it = lobby.attributes().find("Redpoint:EOS:NamespaceFilter");
    return it != lobby.attributes().end() &&
        it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kS &&
        it->second.value().s() == "GAME";
}

static bool lobby_is_presence_namespace(Lobby_Infos_pb const& lobby)
{
    auto it = lobby.attributes().find("IsCrossPlatformPresence");
    if (it != lobby.attributes().end() &&
        it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kB &&
        it->second.value().b())
        return true;

    it = lobby.attributes().find("Redpoint:EOS:NamespaceFilter");
    return it != lobby.attributes().end() &&
        it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kS &&
        it->second.value().s() == "CROSSPLATFORMPRESENCE";
}

static std::string lobby_attr_string_value(Lobby_Infos_pb const& lobby, char const* key)
{
    auto it = lobby.attributes().find(key);
    if (it == lobby.attributes().end() || it->second.value().value_case() != Lobby_Attr_Value::ValueCase::kS)
        return {};

    return it->second.value().s();
}

static std::string resolve_advertised_game_session_id(Lobby_Infos_pb const& lobby)
{
    std::string advertised = lobby_attr_string_value(lobby, "AdvertisedSessionId");
    if (!advertised.empty())
    {
        static char const kPrefix[] = "Session:";
        if (advertised.rfind(kPrefix, 0) == 0)
            advertised = advertised.substr(sizeof(kPrefix) - 1);
        if (!advertised.empty())
            return advertised;
    }

    std::string game_session_id = lobby_attr_string_value(lobby, "Redpoint:EOS:GameSessionId");
    if (!game_session_id.empty())
        return game_session_id;

    return {};
}

static bool session_is_game_namespace(Session_Infos_pb const& infos)
{
    auto it = infos.attributes().find("Redpoint:EOS:NamespaceFilter");
    return it != infos.attributes().end() &&
        it->second.value().value_case() == Session_Attr_Value::ValueCase::kS &&
        it->second.value().s() == "GAME";
}

static void split_csv_members(std::string const& value, std::vector<std::string>& out)
{
    size_t start = 0;
    while (start < value.size())
    {
        size_t end = value.find(',', start);
        if (end == std::string::npos)
            end = value.size();

        std::string part = value.substr(start, end - start);
        if (!part.empty())
            out.push_back(part);

        start = end + 1;
    }
}

static void clear_public_member_list_attrs(Session_Infos_pb& infos)
{
    for (auto it = infos.mutable_attributes()->begin(); it != infos.mutable_attributes()->end();)
    {
        if (it->first.find("PublicMemberList") != std::string::npos)
            it = infos.mutable_attributes()->erase(it);
        else
            ++it;
    }
}

static void patch_session_public_member_list(Session_Infos_pb& infos);
static uint32_t session_join_capacity(Session_Infos_pb const& infos, std::string const& session_id);
static void restore_session_capacity_from_lobby(Session_Infos_pb& infos);

static void merge_session_player_lists(Session_Infos_pb& target, Session_Infos_pb const& preserved)
{
    auto append_unique = [](std::string const& player, google::protobuf::RepeatedPtrField<std::string>* list)
    {
        if (player.empty() || !sdk::looks_like_hex_product_user_id(player))
            return;

        for (auto const& existing : *list)
        {
            if (existing == player)
                return;
        }

        *list->Add() = player;
    };

    for (auto const& player : preserved.registered_players())
        append_unique(player, target.mutable_registered_players());
    for (auto const& player : preserved.players())
        append_unique(player, target.mutable_players());
}

static Session_Infos_pb build_clean_session_infos_for_join_search(std::string const& lobby_id)
{
    Session_Infos_pb out;
    if (lobby_id.empty())
        return out;

    lobby_state_t* pLobby = GetEOS_Lobby().get_lobby_by_id(lobby_id);
    if (pLobby == nullptr)
        return out;

    out = make_session_infos_from_lobby(pLobby->infos, lobby_id);

    std::string session_id_for_join = lobby_id;
    if (session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession"))
    {
        if (session_has_version(game_session->infos))
        {
            merge_session_attributes(out, game_session->infos, false);
            if (!game_session->infos.host_address().empty())
                out.set_host_address(game_session->infos.host_address());
            if (!game_session->infos.bucket_id().empty())
                out.set_bucket_id(game_session->infos.bucket_id());
            if (!game_session->infos.session_id().empty())
                session_id_for_join = game_session->infos.session_id();
        }
    }

    out.set_session_id(session_id_for_join);
    out.mutable_players()->Clear();
    out.mutable_registered_players()->Clear();

    auto add_member = [&](std::string const& player, bool register_member)
    {
        if (!sdk::looks_like_hex_product_user_id(player))
            return;
        if (std::find(out.players().begin(), out.players().end(), player) == out.players().end())
            *out.add_players() = player;
        if (register_member &&
            std::find(out.registered_players().begin(), out.registered_players().end(), player) == out.registered_players().end())
            *out.add_registered_players() = player;
    };

    add_member(pLobby->infos.owner_id(), true);
    for (auto const& member : pLobby->infos.members())
        add_member(member.first, false);

    clear_public_member_list_attrs(out);
    patch_session_public_member_list(out);
    restore_session_capacity_from_lobby(out);
    ensure_session_advertisement_ready(out);
    return out;
}

static void upgrade_session_infos_for_join(Session_Infos_pb& infos)
{
    std::string lobby_id = infos.session_id();
    if (lobby_id.empty())
        lobby_id = GetEOS_Lobby().get_active_join_lobby_id();

    lobby_state_t* pLobby = nullptr;
    if (!lobby_id.empty())
        pLobby = GetEOS_Lobby().find_lobby_for_session_id(lobby_id);
    if (pLobby == nullptr && !lobby_id.empty())
        pLobby = GetEOS_Lobby().get_lobby_by_id(lobby_id);

    if (pLobby != nullptr && lobby_is_game_namespace(pLobby->infos))
    {
        Session_Infos_pb upgraded = build_clean_session_infos_for_join_search(pLobby->infos.lobby_id());
        if (!upgraded.session_id().empty())
        {
            std::string const host_hint = infos.host_address();
            infos = std::move(upgraded);
            if (!host_hint.empty() && infos.host_address().empty())
                infos.set_host_address(host_hint);
            return;
        }
    }

    if (!session_has_version(infos))
    {
        if (session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession"))
        {
            merge_session_attributes(infos, game_session->infos, false);
            if (infos.host_address().empty() && !game_session->infos.host_address().empty())
                infos.set_host_address(game_session->infos.host_address());
        }
    }
}

static uint32_t session_join_capacity(Session_Infos_pb const& infos, std::string const& session_id)
{
    uint32_t capacity = infos.max_players();
    lobby_state_t* pLobby = GetEOS_Lobby().get_lobby_by_id(session_id);
    if (pLobby == nullptr)
        pLobby = GetEOS_Lobby().find_lobby_for_session_id(session_id);
    if (pLobby != nullptr && pLobby->infos.max_lobby_member() > 0)
        capacity = std::max(capacity, static_cast<uint32_t>(pLobby->infos.max_lobby_member()));
    return capacity;
}

static void restore_session_capacity_from_lobby(Session_Infos_pb& infos)
{
    if (infos.session_id().empty())
        return;

    lobby_state_t* pLobby = GetEOS_Lobby().get_lobby_by_id(infos.session_id());
    if (pLobby == nullptr)
        pLobby = GetEOS_Lobby().find_lobby_for_session_id(infos.session_id());
    if (pLobby != nullptr && pLobby->infos.max_lobby_member() > 0)
    {
        uint32_t const lobby_capacity = static_cast<uint32_t>(pLobby->infos.max_lobby_member());
        if (infos.max_players() < lobby_capacity)
            infos.set_max_players(lobby_capacity);
    }
}

static void patch_session_public_member_list(Session_Infos_pb& infos)
{
    static char const kCurrentMembersKey[] = "Redpoint:EOS:PublicMemberList:CurrentMembers";
    static char const kMaxMembersKey[] = "Redpoint:EOS:PublicMemberList:MaxMembers";
    static char const kIndexedPrefix[] = "Redpoint:EOS:PublicMemberList:CurrentMembers:";

    std::vector<std::string> member_ids;
    auto add_member = [&](std::string const& player)
    {
        if (!sdk::looks_like_hex_product_user_id(player))
            return;

        if (std::find(member_ids.begin(), member_ids.end(), player) != member_ids.end())
            return;

        GetProductUserId(player);
        member_ids.push_back(player);
    };

    for (auto const& player : infos.players())
        add_member(player);
    for (auto const& player : infos.registered_players())
        add_member(player);

    if (member_ids.empty())
    {
        auto existing = infos.attributes().find(kCurrentMembersKey);
        if (existing != infos.attributes().end() &&
            existing->second.value().value_case() == Session_Attr_Value::ValueCase::kS)
        {
            split_csv_members(existing->second.value().s(), member_ids);
            for (auto const& player : member_ids)
                GetProductUserId(player);
        }
    }

    if (member_ids.empty())
    {
        for (auto const& attr : infos.attributes())
        {
            if (attr.first.rfind(kIndexedPrefix, 0) != 0)
                continue;
            if (attr.second.value().value_case() != Session_Attr_Value::ValueCase::kS)
                continue;
            add_member(attr.second.value().s());
        }
    }

    clear_public_member_list_attrs(infos);

    if (member_ids.empty())
        return;

    APP_LOG(Log::LogLevel::INFO, "Session %s PublicMemberList: %zu member(s) [%s]",
        infos.session_id().c_str(),
        member_ids.size(),
        member_ids[0].c_str());

    std::string members;
    for (size_t i = 0; i < member_ids.size(); ++i)
    {
        if (!members.empty())
            members += ',';
        members += member_ids[i];

        std::string indexed_key = kIndexedPrefix + std::to_string(i);
        Session_Attribute indexed_attr;
        auto indexed_existing = infos.attributes().find(indexed_key);
        if (indexed_existing != infos.attributes().end())
            indexed_attr = indexed_existing->second;
        indexed_attr.mutable_value()->set_s(member_ids[i]);
        (*infos.mutable_attributes())[indexed_key] = std::move(indexed_attr);
    }

    {
        Session_Attribute attr;
        auto existing = infos.attributes().find(kCurrentMembersKey);
        if (existing != infos.attributes().end())
            attr = existing->second;
        attr.mutable_value()->set_s(members);
        (*infos.mutable_attributes())[kCurrentMembersKey] = std::move(attr);
    }

    int const member_count = static_cast<int>(member_ids.size());
    {
        Session_Attribute max_members;
        auto mm = infos.attributes().find(kMaxMembersKey);
        if (mm != infos.attributes().end())
            max_members = mm->second;
        max_members.mutable_value()->set_i(static_cast<int64_t>(member_count));
        (*infos.mutable_attributes())[kMaxMembersKey] = std::move(max_members);
    }

    {
        Session_Attribute player_count;
        auto pc = infos.attributes().find("PlayerCount");
        if (pc != infos.attributes().end())
            player_count = pc->second;
        player_count.mutable_value()->set_i(static_cast<int64_t>(member_count));
        (*infos.mutable_attributes())["PlayerCount"] = std::move(player_count);
    }
}

void EOSSDK_Sessions::prepare_session_infos_for_network(Session_Infos_pb& infos)
{
    restore_session_host_eosp2p_format(infos);
    reconcile_session_state_for_network(infos);
    infos.set_host_address(normalize_session_host_address(infos.host_address()));

    {
        std::string const& host = infos.host_address();
        size_t const dot = host.find('.');
        if (dot == 32 && sdk::looks_like_hex_product_user_id(host.substr(0, dot)))
        {
            std::string const owner = host.substr(0, dot);
            GetProductUserId(owner);
            if (infos.registered_players_size() == 0)
                *infos.add_registered_players() = owner;
        }
    }

    std::string const owner = session_owner_product_id(infos);
    if (!owner.empty())
    {
        GetProductUserId(owner);
        if (infos.registered_players_size() == 0)
            *infos.add_registered_players() = owner;
        if (infos.players_size() == 0)
            *infos.add_players() = owner;
    }

    for (auto const& player : infos.registered_players())
        GetProductUserId(player);
    for (auto const& player : infos.players())
        GetProductUserId(player);

    for (auto const& attr : infos.attributes())
    {
        if (attr.second.value().value_case() != Session_Attr_Value::ValueCase::kS)
            continue;

        std::string const& value = attr.second.value().s();
        if (sdk::looks_like_hex_product_user_id(value))
            GetProductUserId(value);
    }

    patch_session_public_member_list(infos);
    restore_session_capacity_from_lobby(infos);
    ensure_session_advertisement_ready(infos);
    reconcile_session_state_for_network(infos);
    reset_remote_game_session_join_state(infos);
}

void EOSSDK_Sessions::fix_session_host_from_peer(Session_Infos_pb& infos, std::string const& peer_id)
{
    apply_session_host_network_fix(infos, peer_id);
}

void EOSSDK_Sessions::register_discovered_session(Session_Infos_pb infos)
{
    if (infos.session_id().empty())
        return;

    normalize_session_id_for_response(infos, infos.session_id());
    prepare_session_infos_for_network(infos);

    if (session_state_t* active = find_primary_session_for_id(_sessions, infos.session_id()))
    {
        if (active->state == session_state_t::state_e::joining ||
            active->state == session_state_t::state_e::joined)
        {
            merge_session_attributes(active->infos, infos, false);
            APP_LOG(Log::LogLevel::DEBUG,
                "Discovered session %s merged into active copy (state=%d)",
                infos.session_id().c_str(),
                static_cast<int>(active->state));
            return;
        }
    }

    auto& by_id = _sessions[infos.session_id()];
    by_id.state = session_state_t::state_e::created;
    by_id.infos = infos;

    if (should_alias_discovered_session(infos))
        sync_game_session_alias(_sessions, infos.session_id(), by_id);

    if (session_state_t* game_session = get_session_by_name("GameSession"))
    {
        if (!session_owned_locally(game_session->infos))
        {
            reset_remote_game_session_join_state(game_session->infos);
            game_session->state = session_state_t::state_e::created;
        }
    }
}

void EOSSDK_Sessions::notify_join_session_accepted(Session_Infos_pb const& infos)
{
    Session_Infos_pb prepared = infos;
    normalize_session_id_for_response(prepared, prepared.session_id());
    upgrade_session_infos_for_join(prepared);
    prepare_session_infos_for_network(prepared);
    apply_session_host_network_fix(prepared);

    if (!session_has_version(prepared))
    {
        APP_LOG(Log::LogLevel::WARN,
            "JoinSessionAccepted skipped: session=%s has no Version attribute",
            prepared.session_id().c_str());
        return;
    }

    EOS_UI_EventId const ui_event_id = GetEOS_UI().register_session_join_event(prepared);
    if (ui_event_id == EOS_UI_EVENTID_INVALID)
        return;

    std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_Sessions_JoinSessionAcceptedCallbackInfo::k_iCallback));
    for (auto& notif : notifs)
    {
        EOS_Sessions_JoinSessionAcceptedCallbackInfo const& src = notif->GetCallback<EOS_Sessions_JoinSessionAcceptedCallbackInfo>();
        pFrameResult_t queued(new FrameResult);
        EOS_Sessions_JoinSessionAcceptedCallbackInfo& jsaci = queued->CreateCallback<EOS_Sessions_JoinSessionAcceptedCallbackInfo>(notif->GetFunc());
        jsaci.ClientData = src.ClientData;
        jsaci.LocalUserId = GetEOS_Connect().get_myself()->first;
        jsaci.UiEventId = ui_event_id;
        queued->done = true;
        GetCB_Manager().add_callback(this, queued);
    }

    APP_LOG(Log::LogLevel::INFO, "JoinSessionAccepted notification: session=%s ui_event=%llu",
        prepared.session_id().c_str(), static_cast<unsigned long long>(ui_event_id));
}

void EOSSDK_Sessions::attempt_steam_bridge_auto_session_join(Session_Infos_pb session)
{
    if (local_player_in_game_session(session.session_id()))
    {
        APP_LOG(Log::LogLevel::INFO,
            "Steam bridge auto join skipped: already in session %s",
            session.session_id().c_str());
        notify_join_session_accepted(session);
        return;
    }

    if (!session_has_version(session))
    {
        APP_LOG(Log::LogLevel::WARN,
            "Steam bridge auto join skipped: session=%s has no Version attribute",
            session.session_id().c_str());
        return;
    }

    normalize_session_id_for_response(session, session.session_id());
    upgrade_session_infos_for_join(session);
    reset_remote_game_session_join_state(session);
    prepare_session_infos_for_network(session);
    apply_session_host_network_fix(session);

    if (session_state_t* existing = get_session_by_name("GameSession"))
    {
        if (session_owned_locally(existing->infos) &&
            local_player_in_game_session(existing->infos.session_id()))
        {
            APP_LOG(Log::LogLevel::INFO,
                "Steam bridge auto join blocked: already in local host GameSession %s",
                existing->infos.session_id().c_str());
            return;
        }

        if (existing->state == session_state_t::state_e::created &&
            !local_player_in_game_session(existing->infos.session_id()))
        {
            APP_LOG(Log::LogLevel::INFO, "Steam bridge auto join: clearing discovered GameSession mirror");
            _sessions.erase("GameSession");
        }
    }

    if (auto it = _sessions.find(session.session_id()); it != _sessions.end())
    {
        if (it->second.state == session_state_t::state_e::created &&
            !local_player_in_game_session(session.session_id()))
            _sessions.erase(it);
    }

    session_state_t& named = _sessions["GameSession"];
    named.state = session_state_t::state_e::joining;
    named.infos = session;

    _steam_bridge_auto_join_session_id = session.session_id();

    pFrameResult_t res(new FrameResult);
    EOS_Sessions_JoinSessionCallbackInfo& jsci = res->CreateCallback<EOS_Sessions_JoinSessionCallbackInfo>((CallbackFunc)noop_join_session_cb);
    jsci.ResultCode = EOS_EResult::EOS_UnexpectedError;
    _sessions_join[session.session_id()] = res;

    APP_LOG(Log::LogLevel::INFO,
        "Steam bridge auto JoinSession: session_id=%s host=%s",
        session.session_id().c_str(),
        session.host_address().c_str());

    send_session_join_request(&named);
}

void EOSSDK_Sessions::on_steam_bridge_party_joined()
{
    if (!_steam_bridge_pending_auto_join.has_value())
        return;

    Session_Infos_pb session = std::move(*_steam_bridge_pending_auto_join);
    _steam_bridge_pending_auto_join.reset();
    attempt_steam_bridge_auto_session_join(std::move(session));
}

std::string EOSSDK_Sessions::resolve_session_host_address_for_copy(Session_Infos_pb const& infos, std::string const& peer_hint)
{
    Session_Infos_pb copy = infos;
    std::string hint = peer_hint;
    if (hint.empty())
    {
        if (copy.registered_players_size() > 0)
            hint = copy.registered_players(0);
        else if (copy.players_size() > 0)
            hint = copy.players(0);
    }
    apply_session_host_network_fix(copy, hint);
    restore_session_host_eosp2p_format(copy);
    return copy.host_address();
}

void EOSSDK_Sessions::sync_session_from_lobby(Lobby_Infos_pb const& lobby)
{
    if (lobby.lobby_id().empty())
        return;

    bool const is_game = lobby_is_game_namespace(lobby);
    bool const is_presence = lobby_is_presence_namespace(lobby);
    std::string const linked_game_session_id = resolve_advertised_game_session_id(lobby);

    Session_Infos_pb lobby_session = make_session_infos_from_lobby(lobby, lobby.lobby_id());
    session_state_t* rich = find_richest_session_for_search(_sessions, lobby.lobby_id(), lobby.owner_id());

    session_state_t& session = _sessions[lobby.lobby_id()];
    session.state = session_state_t::state_e::created;

    if (is_presence)
    {
        session.infos = std::move(lobby_session);
        if (!linked_game_session_id.empty())
            session.infos.set_session_id(linked_game_session_id);
    }
    else if (rich != nullptr && session_has_version(rich->infos))
    {
        session.infos = rich->infos;
        std::string const canonical_id = !linked_game_session_id.empty()
            ? linked_game_session_id
            : rich->infos.session_id();
        if (!canonical_id.empty())
            session.infos.set_session_id(canonical_id);
        merge_session_attributes(session.infos, lobby_session, true);
    }
    else
    {
        session.infos = std::move(lobby_session);
        if (!linked_game_session_id.empty())
            session.infos.set_session_id(linked_game_session_id);
        if (rich != nullptr)
            merge_session_attributes(session.infos, rich->infos, false);
    }

    if (session_state_t* game_session = get_session_by_name("GameSession"))
    {
        if (&session != game_session && is_game)
            merge_session_attributes(game_session->infos, lobby_session, false);
    }

    prune_stale_sessions(lobby.lobby_id());
    sync_lobby_members_to_sessions(lobby);

    if (session_state_t* game_session = get_session_by_name("GameSession"))
        patch_session_public_member_list(game_session->infos);

    APP_LOG(Log::LogLevel::DEBUG, "Synced session from lobby: lobby_id=%s session_id=%s owner_id=%s players=%d attrs=%d version=%s namespace=%s",
        lobby.lobby_id().c_str(),
        session.infos.session_id().c_str(),
        lobby.owner_id().c_str(),
        session.infos.players_size(),
        session.infos.attributes_size(),
        session_has_version(session.infos) ? "yes" : "no",
        is_presence ? "presence" : (is_game ? "game" : "other"));
}

void EOSSDK_Sessions::sync_lobby_members_to_sessions(Lobby_Infos_pb const& lobby)
{
    if (lobby.owner_id().empty() && lobby.members_size() == 0)
        return;

    auto apply_members = [&](session_state_t* session)
    {
        if (session == nullptr)
            return;

        session->infos.mutable_players()->Clear();
        auto add_player = [&](std::string const& player)
        {
            if (player.empty() || !sdk::looks_like_hex_product_user_id(player))
                return;

            GetProductUserId(player);
            for (auto const& existing : session->infos.players())
            {
                if (existing == player)
                    return;
            }

            *session->infos.add_players() = player;
        };

        add_player(lobby.owner_id());
        for (auto const& member : lobby.members())
            add_player(member.first);

        if (session != get_session_by_name("GameSession"))
        {
            register_player_to_session(lobby.owner_id(), session);
            for (auto const& member : lobby.members())
                register_player_to_session(member.first, session);
        }

        patch_session_public_member_list(session->infos);
    };

    apply_members(get_session_by_id(lobby.lobby_id()));
    if (lobby_is_game_namespace(lobby))
        apply_members(get_session_by_name("GameSession"));
}

void EOSSDK_Sessions::prune_stale_sessions(std::string const& active_lobby_id)
{
    if (active_lobby_id.empty())
        return;

    for (auto it = _sessions.begin(); it != _sessions.end();)
    {
        if (it->first == active_lobby_id || it->second.infos.session_id() == active_lobby_id)
        {
            ++it;
            continue;
        }

        if (session_state_t* game_session = get_session_by_name("GameSession"))
        {
            if (it->second.infos.session_id() == game_session->infos.session_id())
            {
                ++it;
                continue;
            }
        }

        // Named sessions (GameSession, etc.) must survive lobby sync; only prune orphan id-keyed entries.
        if (!looks_like_hex_product_user_id(it->first))
        {
            ++it;
            continue;
        }

        lobby_state_t* pLobby = GetEOS_Lobby().get_lobby_by_id(it->second.infos.session_id());
        if (pLobby == nullptr && GetEOS_Lobby().get_lobby_by_id(it->first) == nullptr)
        {
            APP_LOG(Log::LogLevel::INFO, "Pruning stale session: key=%s session_id=%s (active=%s)",
                it->first.c_str(), it->second.infos.session_id().c_str(), active_lobby_id.c_str());
            it = _sessions.erase(it);
            continue;
        }

        ++it;
    }
}

bool EOSSDK_Sessions::try_get_session_infos_for_search(std::string const& session_id, Session_Infos_pb& out, bool allow_stale_redirect)
{
    if (session_id.empty())
        return false;

    if (session_state_t* direct = get_session_by_id(session_id))
    {
        out = direct->infos;
        if (!session_has_version(out))
        {
            std::string const owner_id = session_owner_product_id(out);
            if (session_state_t* rich = find_richest_session_for_search(_sessions, session_id, owner_id))
            {
                out = rich->infos;
                APP_LOG(Log::LogLevel::INFO, "Session search direct id %s upgraded using richer session attrs=%d",
                    session_id.c_str(), out.attributes_size());
            }
            else if (session_state_t* game_session = get_session_by_name("GameSession"))
            {
                merge_session_attributes(out, game_session->infos, false);
                if (out.host_address().empty() && !game_session->infos.host_address().empty())
                    out.set_host_address(game_session->infos.host_address());
                if (out.bucket_id().empty() && !game_session->infos.bucket_id().empty())
                    out.set_bucket_id(game_session->infos.bucket_id());
            }
        }

        normalize_session_id_for_response(out, session_id);
        prepare_session_infos_for_network(out);
        return true;
    }

    std::string resolved_id = session_id;

    if (lobby_state_t* presence_lobby = GetEOS_Lobby().get_lobby_by_id(resolved_id))
    {
        auto xp = presence_lobby->infos.attributes().find("IsCrossPlatformPresence");
        bool const is_presence = xp != presence_lobby->infos.attributes().end() &&
            xp->second.value().value_case() == Lobby_Attr_Value::ValueCase::kB &&
            xp->second.value().b();

        if (is_presence && GetEOS_Lobby().i_am_owner(presence_lobby))
        {
            std::string const active_join = GetEOS_Lobby().get_active_join_lobby_id();
            if (!active_join.empty() && active_join != resolved_id)
            {
                lobby_state_t* game_lobby = GetEOS_Lobby().get_lobby_by_id(active_join);
                if (game_lobby != nullptr && lobby_is_game_namespace(game_lobby->infos))
                {
                    APP_LOG(Log::LogLevel::INFO,
                        "Session search redirect presence lobby %s -> active game lobby %s",
                        resolved_id.c_str(), active_join.c_str());
                    return try_get_session_infos_for_search(active_join, out, false);
                }
            }
        }
    }

    if (allow_stale_redirect)
    {
        std::string const active_lobby = GetEOS_Lobby().get_active_join_lobby_id();
        if (!active_lobby.empty() && resolved_id != active_lobby &&
            GetEOS_Lobby().get_lobby_by_id(resolved_id) == nullptr &&
            GetEOS_Lobby().get_lobby_by_id(active_lobby) != nullptr &&
            looks_like_hex_product_user_id(resolved_id))
        {
            bool redirect = get_session_by_id(resolved_id) != nullptr;
            if (!redirect)
            {
                if (session_state_t* game_session = get_session_by_name("GameSession"))
                    redirect = game_session->infos.session_id() == active_lobby;
            }

            if (redirect)
            {
                APP_LOG(Log::LogLevel::INFO, "Session search redirect stale id %s -> active join lobby %s",
                    resolved_id.c_str(), active_lobby.c_str());
                resolved_id = active_lobby;
            }
        }
    }

    if (lobby_state_t* join_lobby = GetEOS_Lobby().get_lobby_by_id(resolved_id))
    {
        if (lobby_is_game_namespace(join_lobby->infos))
        {
            out = build_clean_session_infos_for_join_search(resolved_id);
            if (!out.session_id().empty())
            {
                prepare_session_infos_for_network(out);
                return true;
            }
        }
    }

    if (session_state_t* pSession = get_session_by_id(resolved_id))
    {
        if (!session_has_version(pSession->infos))
        {
            std::string owner_id;
            if (pSession->infos.registered_players_size() > 0)
                owner_id = pSession->infos.registered_players(0);

            if (session_state_t* rich = find_richest_session_for_search(_sessions, resolved_id, owner_id))
            {
                out = rich->infos;
                normalize_session_id_for_response(out, resolved_id);
                APP_LOG(Log::LogLevel::INFO, "Session search upgraded %s using richer session attrs=%d",
                    resolved_id.c_str(), out.attributes_size());
                prepare_session_infos_for_network(out);
                return true;
            }
        }

        out = pSession->infos;
        normalize_session_id_for_response(out, resolved_id);
        prepare_session_infos_for_network(out);
        return true;
    }

    lobby_state_t* pLobby = GetEOS_Lobby().find_lobby_for_session_id(resolved_id);
    if (pLobby == nullptr && resolved_id != session_id)
        pLobby = GetEOS_Lobby().get_lobby_by_id(resolved_id);
    if (pLobby == nullptr)
        pLobby = GetEOS_Lobby().get_lobby_by_id(session_id);
    if (pLobby == nullptr)
    {
        if (allow_stale_redirect)
        {
            std::string const active_join = GetEOS_Lobby().get_active_join_lobby_id();
            if (!active_join.empty() && active_join != session_id)
            {
                if (session_state_t* direct = get_session_by_id(session_id))
                {
                    out = direct->infos;
                    if (out.session_id() != session_id)
                        out.set_session_id(session_id);
                    prepare_session_infos_for_network(out);
                    return true;
                }

                if (session_state_t* game_session = get_session_by_name("GameSession"))
                {
                    if (game_session->infos.session_id() == session_id || session_has_version(game_session->infos))
                    {
                        out = game_session->infos;
                        if (out.session_id() != session_id)
                            out.set_session_id(session_id);
                        APP_LOG(Log::LogLevel::INFO,
                            "Session search resolved Redpoint id %s -> GameSession %s attrs=%d",
                            session_id.c_str(), game_session->infos.session_id().c_str(), out.attributes_size());
                        prepare_session_infos_for_network(out);
                        return true;
                    }
                }

                if (get_session_by_id(session_id) == nullptr &&
                    (GetEOS_Lobby().get_lobby_by_id(session_id) != nullptr ||
                     looks_like_hex_product_user_id(session_id)))
                {
                    Session_Infos_pb merged = build_clean_session_infos_for_join_search(active_join);
                    if (!merged.session_id().empty() && session_has_version(merged))
                    {
                        out = std::move(merged);
                        prepare_session_infos_for_network(out);
                        APP_LOG(Log::LogLevel::INFO,
                            "Session search redirect unknown id %s -> GameSession via lobby %s attrs=%d",
                            session_id.c_str(), active_join.c_str(), out.attributes_size());
                        return true;
                    }

                    APP_LOG(Log::LogLevel::INFO, "Session search redirect unknown id %s -> active join lobby %s",
                        session_id.c_str(), active_join.c_str());
                    return try_get_session_infos_for_search(active_join, out, false);
                }
            }

            std::string const presence = GetEOS_Lobby().get_active_crossplatform_lobby_id();
            if (!presence.empty() && !active_join.empty() && active_join != presence && active_join != session_id)
            {
                APP_LOG(Log::LogLevel::INFO, "Session search redirect unknown id %s -> active game lobby %s",
                    session_id.c_str(), active_join.c_str());
                return try_get_session_infos_for_search(active_join, out, false);
            }
        }
        return false;
    }

    sync_session_from_lobby(pLobby->infos);

    if (session_state_t* pSession = get_session_by_id(resolved_id))
    {
        out = pSession->infos;
        if (out.session_id() != resolved_id)
            out.set_session_id(resolved_id);
        APP_LOG(Log::LogLevel::INFO, "Session search resolved lobby %s for session_id=%s attrs=%d",
            pLobby->infos.lobby_id().c_str(), resolved_id.c_str(), out.attributes_size());
        prepare_session_infos_for_network(out);
        return true;
    }

    out = make_session_infos_from_lobby(pLobby->infos, resolved_id);
    APP_LOG(Log::LogLevel::INFO, "Session search resolved lobby %s for session_id=%s",
        pLobby->infos.lobby_id().c_str(), resolved_id.c_str());
    prepare_session_infos_for_network(out);
    return true;
}

void EOSSDK_Sessions::add_player_to_session(std::string const& player, session_state_t* session)
{
    if (session != nullptr)
    {
        if(!is_player_in_session(player, session))
            *session->infos.add_players() = player;
    }
}

void EOSSDK_Sessions::remove_player_from_session(std::string const& player, session_state_t* session)
{
    if (session != nullptr)
    {
        auto it = std::find(session->infos.players().begin(), session->infos.players().end(), player);
        if (it != session->infos.players().end())
            session->infos.mutable_players()->erase(it);

        it = std::find(session->infos.registered_players().begin(), session->infos.registered_players().end(), player);
        if (it != session->infos.registered_players().end())
            session->infos.mutable_registered_players()->erase(it);
    }
}

bool EOSSDK_Sessions::register_player_to_session(std::string const& player, session_state_t* session)
{
    if (session != nullptr && !is_player_registered(player, session))
    {
        *session->infos.add_registered_players() = player;
        return true;
    }

    return false;
}

bool EOSSDK_Sessions::unregister_player_from_session(std::string const& player, session_state_t* session)
{
    if (session != nullptr)
    {
        auto it = std::find(session->infos.registered_players().begin(), session->infos.registered_players().end(), player);
        if (it != session->infos.registered_players().end())
        {
            session->infos.mutable_registered_players()->erase(it);
            return true;
        }
    }

    return false;
}

bool EOSSDK_Sessions::is_player_in_session(std::string const& player, session_state_t* session)
{
    if (session != nullptr)
    {
        auto it = std::find(session->infos.players().begin(), session->infos.players().end(), player);
        return it != session->infos.players().end();
    }
    return false;
}

bool EOSSDK_Sessions::is_player_registered(std::string const& player, session_state_t* session)
{
    if (session != nullptr)
    {
        auto it = std::find(session->infos.registered_players().begin(), session->infos.registered_players().end(), player);
        return it != session->infos.registered_players().end();
    }
    return false;
}

bool EOSSDK_Sessions::local_user_hosts_session_for_peer(std::string const& peer_product_id) const
{
    if (peer_product_id.empty())
        return false;

    std::string const local_id = Settings::Inst().productuserid->to_string();
    Session_Infos_pb const* best = nullptr;
    int best_score = -1;

    for (auto const& entry : _sessions)
    {
        Session_Infos_pb const& infos = entry.second.infos;

        auto const state = static_cast<EOS_EOnlineSessionState>(infos.state());
        bool const listen_ready =
            state == EOS_EOnlineSessionState::EOS_OSS_InProgress ||
            state == EOS_EOnlineSessionState::EOS_OSS_Starting ||
            session_attr_bool(infos, "__EOS_bListening");
        if (!listen_ready)
            continue;

        if (session_owner_product_id(infos) != local_id)
            continue;

        // ch255->ch172 redirect applies only to the Redpoint game session.
        if (!session_is_game_namespace(infos))
            continue;

        int score = 0;
        if (session_is_game_namespace(infos))
            score += 100;
        if (session_infos_has_version(infos))
            score += 50;
        if (session_infos_attr_bool(infos, "Redpoint:EOS:Ready"))
            score += 25;

        for (auto const& player : infos.registered_players())
        {
            if (player == peer_product_id)
            {
                score += 200;
                break;
            }
        }
        if (score < 200)
        {
            for (auto const& player : infos.players())
            {
                if (player == peer_product_id)
                {
                    score += 200;
                    break;
                }
            }
        }

        if (score > best_score)
        {
            best_score = score;
            best = &infos;
        }
    }

    if (best == nullptr)
        return false;

    for (auto const& player : best->registered_players())
    {
        if (player == peer_product_id)
            return true;
    }
    for (auto const& player : best->players())
    {
        if (player == peer_product_id)
            return true;
    }

    APP_LOG(Log::LogLevel::INFO,
        "Local listen-session %s owns host for peer %s even though membership is not synced yet (state=%d players=%d registered=%d)",
        best->session_id().c_str(),
        peer_product_id.c_str(),
        best->state(),
        best->players_size(),
        best->registered_players_size());
    return true;
}

/**
 * The Session Interface is used to manage sessions that can be advertised with the backend service
 * All Session Interface calls take a handle of type EOS_HSessions as the first parameter.
 * This handle can be retrieved from a EOS_HPlatform handle by using the EOS_Platform_GetSessionsInterface function.
 *
 * NOTE: At this time, this feature is only available for products that are part of the Epic Games store.
 *
 * @see EOS_Platform_GetSessionsInterface
 */

/**
  * Creates a session modification handle (EOS_HSessionModification).  The session modification handle is used to build a new session and can be applied with EOS_Sessions_UpdateSession
  * The EOS_HSessionModification must be released by calling EOS_SessionModification_Release once it no longer needed.
  *
  * @param Options Required fields for the creation of a session such as a name, bucket_id, and max players
  * @param OutSessionModificationHandle Pointer to a Session Modification Handle only set if successful
  * @return EOS_Success if we successfully created the Session Modification Handle pointed at in OutSessionModificationHandle, or an error result if the input data was invalid
  *
  * @see EOS_SessionModification_Release
  * @see EOS_Sessions_UpdateSession
  * @see EOS_SessionModification_*
  */
EOS_EResult EOSSDK_Sessions::CreateSessionModification(const EOS_Sessions_CreateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    APP_LOG(Log::LogLevel::DEBUG, "CreateSessionModification: name=%s api=%d",
        Options != nullptr && Options->SessionName != nullptr ? Options->SessionName : "(null)",
        Options != nullptr ? Options->ApiVersion : -1);

    if (Options == nullptr || Options->SessionName == nullptr || Options->BucketId == nullptr || OutSessionModificationHandle == nullptr)
    {
        set_nullptr(OutSessionModificationHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOSSDK_SessionModification* modif = new EOSSDK_SessionModification;
    modif->_api_version = Options->ApiVersion;
    modif->_type = EOSSDK_SessionModification::modif_type::creation;
    modif->_infos.set_host_address(get_preferred_lan_ipv4());

    switch (Options->ApiVersion)
    {
        sessions_create_mod_latest:
        case EOS_SESSIONS_CREATESESSIONMODIFICATION_API_005:
        case EOS_SESSIONS_CREATESESSIONMODIFICATION_API_004:
        {
            const EOS_Sessions_CreateSessionModificationOptions004* opts = reinterpret_cast<const EOS_Sessions_CreateSessionModificationOptions004*>(Options);
            (void)opts;
        }

        case EOS_SESSIONS_CREATESESSIONMODIFICATION_API_003:
        {
            const EOS_Sessions_CreateSessionModificationOptions003* opts = reinterpret_cast<const EOS_Sessions_CreateSessionModificationOptions003*>(Options);
            if (opts->SessionId != nullptr)
            {
                APP_LOG(Log::LogLevel::DEBUG, "Overriding session id: %s", modif->_infos.session_id().c_str());

                modif->_infos.set_session_id(opts->SessionId);
                if (modif->_infos.session_id().length() < EOS_SESSIONMODIFICATION_MIN_SESSIONIDOVERRIDE_LENGTH ||
                    modif->_infos.session_id().length() > EOS_SESSIONMODIFICATION_MAX_SESSIONIDOVERRIDE_LENGTH)
                {
                    delete modif;
                    set_nullptr(OutSessionModificationHandle);
                    return EOS_EResult::EOS_InvalidParameters;
                }
            }
        }

        case EOS_SESSIONS_CREATESESSIONMODIFICATION_API_002:
        {
            const EOS_Sessions_CreateSessionModificationOptions002* opts = reinterpret_cast<const EOS_Sessions_CreateSessionModificationOptions002*>(Options);
            modif->_infos.set_presence_allowed(opts->bPresenceEnabled);
        }

        case EOS_SESSIONS_CREATESESSIONMODIFICATION_API_001:
        {
            const EOS_Sessions_CreateSessionModificationOptions001* opts = reinterpret_cast<const EOS_Sessions_CreateSessionModificationOptions001*>(Options);
            modif->_infos.set_bucket_id(opts->BucketId);
            modif->_infos.set_max_players(opts->MaxPlayers);
            modif->_session_name = opts->SessionName;

            APP_LOG(Log::LogLevel::DEBUG, "Starting session creation: session_name = %s, bucket_id = %s, presence_enabled: %d", modif->_session_name.c_str(), modif->_infos.bucket_id().c_str(), (int)modif->_infos.presence_allowed());
        }
        break;

        default:
            if (Options->ApiVersion > EOS_SESSIONS_CREATESESSIONMODIFICATION_API_005)
            {
                APP_LOG(Log::LogLevel::WARN, "Unknown EOS_Sessions_CreateSessionModification API version %d, treating as %d", Options->ApiVersion, EOS_SESSIONS_CREATESESSIONMODIFICATION_API_005);
                goto sessions_create_mod_latest;
            }

            APP_LOG(Log::LogLevel::FATAL, "Unmanaged API version %d", Options->ApiVersion);
            abort();
    }
    

    *OutSessionModificationHandle = reinterpret_cast<EOS_HSessionModification>(modif);
    return EOS_EResult::EOS_Success;
}

/**
 * Creates a session modification handle (EOS_HSessionModification). The session modification handle is used to modify an existing session and can be applied with EOS_Sessions_UpdateSession.
 * The EOS_HSessionModification must be released by calling EOS_SessionModification_Release once it is no longer needed.
 *
 * @param Options Required fields such as session name
 * @param OutSessionModificationHandle Pointer to a Session Modification Handle only set if successful
 * @return EOS_Success if we successfully created the Session Modification Handle pointed at in OutSessionModificationHandle, or an error result if the input data was invalid
 *
 * @see EOS_SessionModification_Release
 * @see EOS_Sessions_UpdateSession
 * @see EOS_SessionModification_*
 */
EOS_EResult EOSSDK_Sessions::UpdateSessionModification(const EOS_Sessions_UpdateSessionModificationOptions* Options, EOS_HSessionModification* OutSessionModificationHandle)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->SessionName == nullptr || OutSessionModificationHandle == nullptr)
    {
        set_nullptr(OutSessionModificationHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOSSDK_SessionModification* modif = new EOSSDK_SessionModification;
    modif->_api_version = Options->ApiVersion;
    modif->_type = EOSSDK_SessionModification::modif_type::update;

    session_state_t* session = get_session_by_name(Options->SessionName);
    modif->_session_name = Options->SessionName;

    if (session != nullptr)
    {
        modif->_infos = session->infos;
    }
    else
    {
        switch (Options->ApiVersion)
        {
            case EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_001:
            {
                const EOS_Sessions_UpdateSessionModificationOptions001* opts = reinterpret_cast<const EOS_Sessions_UpdateSessionModificationOptions001*>(Options);
                APP_LOG(Log::LogLevel::DEBUG, "Starting session modification: session_name = %s", modif->_session_name.c_str());
            }
            break;

            default:
                if (Options->ApiVersion > EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_001)
                {
                    APP_LOG(Log::LogLevel::WARN, "Unknown EOS_Sessions_UpdateSessionModification API version %d, treating as %d", Options->ApiVersion, EOS_SESSIONS_UPDATESESSIONMODIFICATION_API_001);
                    break;
                }

                APP_LOG(Log::LogLevel::FATAL, "Unmanaged API version %d", Options->ApiVersion);
                abort();
        }
    }
    
    *OutSessionModificationHandle = reinterpret_cast<EOS_HSessionModification>(modif);
    return EOS_EResult::EOS_Success;
}

/**
 * Update a session given a session modification handle created via EOS_Sessions_CreateSessionModification or EOS_Sessions_UpdateSessionModification
 *
 * @param Options Structure containing information about the session to be updated
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the update operation completes, either successfully or in error
 *
 * @return EOS_Success if the update completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Sessions_OutOfSync if the session is out of sync and will be updated on the next connection with the backend
 *         EOS_NotFound if a session to be updated does not exist
 */
void EOSSDK_Sessions::UpdateSession(const EOS_Sessions_UpdateSessionOptions* Options, void* ClientData, const EOS_Sessions_OnUpdateSessionCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();
    APP_LOG(Log::LogLevel::DEBUG, "UpdateSession called");

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_UpdateSessionCallbackInfo& usci = res->CreateCallback<EOS_Sessions_UpdateSessionCallbackInfo>((CallbackFunc)CompletionDelegate);
    usci.ClientData = ClientData;

    if (Options == nullptr || Options->SessionModificationHandle == nullptr)
    {
        usci.ResultCode = EOS_EResult::EOS_InvalidParameters;
        {
            std::string const& sess_id = GetInvalidProductUserId()->to_string();
            char* session_id = new char[sess_id.length() + 1];
            strncpy(session_id, sess_id.c_str(), sess_id.length() + 1);
            usci.SessionId = session_id;
        }
        {
            char* str = new char[1];
            *str = '\0';
            usci.SessionName = str;
        }
    }
    else
    {
        EOSSDK_SessionModification* modif = reinterpret_cast<EOSSDK_SessionModification*>(Options->SessionModificationHandle);
        usci.SessionId = nullptr;
        {
            std::string const& sess_name = modif->_session_name;
            char* name = new char[sess_name.length() + 1];
            strncpy(name, sess_name.c_str(), sess_name.length() + 1);
            usci.SessionName = name;
        }
        session_state_t* session = get_session_by_id(modif->_infos.session_id());

        switch (modif->_type)
        {
            case EOSSDK_SessionModification::modif_type::creation:
            {
                if (session != nullptr)
                {
                    usci.ResultCode = EOS_EResult::EOS_Sessions_SessionAlreadyExists;
                }
                else
                {
                    auto& session = _sessions[modif->_session_name];

                    if (modif->_infos.session_id().empty())
                    {
                        modif->_infos.set_session_id(generate_account_id());
                    }

                    {
                        std::string const& sess_id = modif->_infos.session_id();
                        char* session_id = new char[sess_id.length() + 1];
                        strncpy(session_id, sess_id.c_str(), sess_id.length() + 1);
                        usci.SessionId = session_id;
                    }

                    // Register local host before network prep so restore_session_host_eosp2p_format
                    // and PublicMemberList patching see the owner id.
                    std::string const local_player_id = Settings::Inst().productuserid->to_string();
                    *modif->_infos.add_players() = local_player_id;
                    *modif->_infos.add_registered_players() = local_player_id;

                    session.state = session_state_t::state_e::created;
                    prepare_session_infos_for_network(modif->_infos);
                    session.infos = modif->_infos;
                    session.infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Pending));

                    APP_LOG(Log::LogLevel::DEBUG, "Session created: \n"
                        "  session_name: %s\n"
                        "  session_id: %s\n"
                        "  bucket_id: %s\n"
                        "  host_address: %s\n",
                        modif->_session_name.c_str(),
                        session.infos.session_id().c_str(),
                        session.infos.bucket_id().c_str(),
                        session.infos.host_address().c_str()
                    );

                    apply_session_host_network_fix(session.infos);
                    promote_session_if_listen_server_ready(&session);
                    sync_game_session_alias(_sessions, modif->_session_name, session);
                    send_session_info(&session);
                    GetEOS_Lobby().refresh_owned_presence_for_active_game();

                    usci.ResultCode = EOS_EResult::EOS_Success;
                    flush_pending_session_searches();
                }
            }
            break;

            case EOSSDK_SessionModification::modif_type::update  :
            {
                if (session == nullptr)
                {
                    usci.ResultCode = EOS_EResult::EOS_NotFound;
                }
                else
                {
                    modif->_infos.set_session_id(session->infos.session_id());
                    modif->_infos.set_state(session->infos.state());

                    Session_Infos_pb const preserved = session->infos;
                    int const preserved_players = preserved.players_size();
                    int const preserved_registered = preserved.registered_players_size();

                    prepare_session_infos_for_network(modif->_infos);
                    session->infos = modif->_infos;
                    merge_session_player_lists(session->infos, preserved);

                    if (session->infos.players_size() > preserved_players ||
                        session->infos.registered_players_size() > preserved_registered)
                    {
                        APP_LOG(Log::LogLevel::INFO,
                            "UpdateSession preserved joined member(s) for %s during host refresh (players %d->%d registered %d->%d)",
                            session->infos.session_id().c_str(),
                            preserved_players,
                            session->infos.players_size(),
                            preserved_registered,
                            session->infos.registered_players_size());
                    }

                    prepare_session_infos_for_network(session->infos);
                    apply_session_host_network_fix(session->infos);
                    promote_session_if_listen_server_ready(session);
                    sync_game_session_alias(_sessions, modif->_session_name, *session);
                    {
                        std::string const& sess_id = session->infos.session_id();
                        char* session_id = new char[sess_id.length() + 1];
                        strncpy(session_id, sess_id.c_str(), sess_id.length() + 1);
                        usci.SessionId = session_id;
                    }

                    APP_LOG(Log::LogLevel::DEBUG, "Session modified: \n"
                        "  session_name: %s\n"
                        "  session_id: %s\n"
                        "  bucket_id: %s\n"
                        "  host_address: %s\n",
                        modif->_session_name.c_str(),
                        modif->_infos.session_id().c_str(),
                        modif->_infos.bucket_id().c_str(),
                        session->infos.host_address().c_str()
                    );

                    usci.ResultCode = EOS_EResult::EOS_Success;

                    send_session_info(session);
                    GetEOS_Lobby().refresh_owned_presence_for_active_game();
                    flush_pending_session_searches();
                }
            }
            break;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Destroy a session given a session name
 *
 * @param Options Structure containing information about the session to be destroyed
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the destroy operation completes, either successfully or in error
 *
 * @return EOS_Success if the destroy completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_AlreadyPending if the session is already marked for destroy
 *         EOS_NotFound if a session to be destroyed does not exist
 */
void EOSSDK_Sessions::DestroySession(const EOS_Sessions_DestroySessionOptions* Options, void* ClientData, const EOS_Sessions_OnDestroySessionCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_DestroySessionCallbackInfo& dsci = res->CreateCallback<EOS_Sessions_DestroySessionCallbackInfo>((CallbackFunc)CompletionDelegate);

    dsci.ClientData = ClientData;

    if (Options == nullptr || Options->SessionName == nullptr)
    {
        dsci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        auto it = _sessions.find(Options->SessionName);
        if (it != _sessions.end())
        {
            APP_LOG(Log::LogLevel::DEBUG, "Destroying session: name %s", Options->SessionName);

            dsci.ResultCode = EOS_EResult::EOS_Success;

            auto join_it = _sessions_join.find(Options->SessionName);
            if (join_it != _sessions_join.end())
            {
                EOS_Sessions_JoinSessionCallbackInfo& jsci = join_it->second->GetCallback<EOS_Sessions_JoinSessionCallbackInfo>();
                jsci.ResultCode = EOS_EResult::EOS_UnexpectedError;
                res->done = true;
                _sessions_join.erase(join_it);
            }

            it->second.infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Destroying));

            send_session_destroy(&it->second);
            std::string const destroyed_session_id = it->second.infos.session_id();
            std::string const destroyed_name = it->first;
            //GetEOS_Connect().remove_session(GetProductUserId(it->second.infos.session_id()), it->second.infos.session_name());
            _sessions.erase(it);

            if (destroyed_name != "GameSession")
            {
                auto gs = _sessions.find("GameSession");
                if (gs != _sessions.end() && gs->second.infos.session_id() == destroyed_session_id)
                    _sessions.erase(gs);
            }
        }
        else
        {
            APP_LOG(Log::LogLevel::DEBUG, "Destroying session: name %s Not Found", Options->SessionName);

            dsci.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Join a session, creating a local session under a given session name.  Backend will validate various conditions to make sure it is possible to join the session.
 *
 * @param Options Structure containing information about the session to be joined
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the join operation completes, either successfully or in error
 *
 * @return EOS_Success if the join completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Sessions_SessionAlreadyExists if the session is already exists or is in the process of being joined
 */
void EOSSDK_Sessions::JoinSession(const EOS_Sessions_JoinSessionOptions* Options, void* ClientData, const EOS_Sessions_OnJoinSessionCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Sessions_JoinSessionCallbackInfo& jsci = res->CreateCallback<EOS_Sessions_JoinSessionCallbackInfo>((CallbackFunc)CompletionDelegate);
    jsci.ClientData = ClientData;

    if (Options == nullptr || Options->SessionHandle == nullptr || Options->SessionName == nullptr)
    {
        jsci.ResultCode = EOS_EResult::EOS_InvalidParameters;
        res->done = true;
    }
    else if (Settings::Inst().steam_passthrough)
    {
        APP_LOG(Log::LogLevel::INFO, "JoinSession ignored in steam_passthrough");
        jsci.ResultCode = EOS_EResult::EOS_NotFound;
        res->done = true;
    }
    else
    {
        EOSSDK_SessionDetails* details = reinterpret_cast<EOSSDK_SessionDetails*>(Options->SessionHandle);
        auto existing_it = _sessions.find(Options->SessionName);
        if (existing_it != _sessions.end())
        {
            bool const stale_join = existing_it->second.state == session_state_t::state_e::joining &&
                _sessions_join.find(details->_infos.session_id()) == _sessions_join.end();
            bool const discovered_mirror = existing_it->second.state == session_state_t::state_e::created &&
                !local_player_in_game_session(details->_infos.session_id());

            if (stale_join || discovered_mirror)
            {
                APP_LOG(Log::LogLevel::INFO,
                    "JoinSession replacing %s entry for name=%s session_id=%s",
                    discovered_mirror ? "discovered mirror" : "stale join",
                    Options->SessionName,
                    details->_infos.session_id().c_str());
                _sessions.erase(existing_it);
            }
        }

        if (_sessions.count(Options->SessionName) == 0)
        {
            APP_LOG(Log::LogLevel::INFO, "JoinSession: name=%s session_id=%s state=%d",
                Options->SessionName,
                details->_infos.session_id().c_str(),
                static_cast<int>(details->_infos.state()));

            switch ((EOS_EOnlineSessionState)details->_infos.state())
            {
                case EOS_EOnlineSessionState::EOS_OSS_InProgress:
                    if (!details->_infos.join_in_progress_allowed())
                    {
                        jsci.ResultCode = EOS_EResult::EOS_Sessions_NotAllowed;
                        res->done = true;
                        break;
                    }
                    // fall through: join in progress allowed

                case EOS_EOnlineSessionState::EOS_OSS_Pending:
                {
                    session_state_t& session = _sessions[Options->SessionName];
                    session.state = session_state_t::state_e::joining;
                    session.infos = details->_infos;
                    _sessions_join[details->_infos.session_id()] = res;

                    jsci.ResultCode = EOS_EResult::EOS_UnexpectedError;
                    send_session_join_request(&session);
                }
                break;

                default:
                {
                    APP_LOG(Log::LogLevel::INFO, "JoinSession rejected: state=%d not joinable",
                        static_cast<int>(details->_infos.state()));
                    jsci.ResultCode = EOS_EResult::EOS_Sessions_NotAllowed;
                    res->done = true;
                }
            }
        }
        else if (local_player_in_game_session(details->_infos.session_id()))
        {
            APP_LOG(Log::LogLevel::INFO, "JoinSession: name=%s already joined session_id=%s",
                Options->SessionName, details->_infos.session_id().c_str());
            jsci.ResultCode = EOS_EResult::EOS_Success;
            res->done = true;
        }
        else
        {
            APP_LOG(Log::LogLevel::INFO, "JoinSession: name=%s Already Exists session_id=%s",
                Options->SessionName, details->_infos.session_id().c_str());
            jsci.ResultCode = EOS_EResult::EOS_Sessions_SessionAlreadyExists;
            res->done = true;
        }
    }

    GetCB_Manager().add_callback(this, res);
}

/**
 * Mark a session as started, making it unable to find if session properties indicate "join in progress" is not available
 *
 * @param Options Structure containing information about the session to be started
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the start operation completes, either successfully or in error
 *
 * @return EOS_Success if the start completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Sessions_OutOfSync if the session is out of sync and will be updated on the next connection with the backend
 *         EOS_NotFound if a session to be started does not exist
 */
void EOSSDK_Sessions::StartSession(const EOS_Sessions_StartSessionOptions* Options, void* ClientData, const EOS_Sessions_OnStartSessionCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Sessions_StartSessionCallbackInfo& ssci = res->CreateCallback<EOS_Sessions_StartSessionCallbackInfo>((CallbackFunc)CompletionDelegate);

    ssci.ClientData = ClientData;

    if (Options == nullptr || Options->SessionName == nullptr)
    {
        ssci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        session_state_t* session = get_session_by_name(Options->SessionName);
        if (session != nullptr)
        {
            auto const state = static_cast<EOS_EOnlineSessionState>(session->infos.state());
            switch (state)
            {
                case EOS_EOnlineSessionState::EOS_OSS_Destroying:
                case EOS_EOnlineSessionState::EOS_OSS_NoSession:
                case EOS_EOnlineSessionState::EOS_OSS_Ending:
                case EOS_EOnlineSessionState::EOS_OSS_Creating:
                case EOS_EOnlineSessionState::EOS_OSS_Starting:
                    APP_LOG(Log::LogLevel::INFO, "StartSession rejected: name=%s state=%d",
                        Options->SessionName, static_cast<int>(state));
                    ssci.ResultCode = EOS_EResult::EOS_InvalidParameters;
                    break;

                case EOS_EOnlineSessionState::EOS_OSS_InProgress:
                    APP_LOG(Log::LogLevel::INFO, "StartSession: name=%s already InProgress",
                        Options->SessionName);
                    ssci.ResultCode = EOS_EResult::EOS_Success;
                    break;

                case EOS_EOnlineSessionState::EOS_OSS_Ended:
                case EOS_EOnlineSessionState::EOS_OSS_Pending:
                    APP_LOG(Log::LogLevel::INFO, "StartSession: name=%s Pending -> InProgress",
                        Options->SessionName);
                    session->infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress));
                    if (session->state == session_state_t::state_e::joining)
                        session->state = session_state_t::state_e::joined;
                    replicate_session_to_all_copies(_sessions, *session);
                    send_session_info(session);
                    ssci.ResultCode = EOS_EResult::EOS_Success;
                    break;

                default:
                    APP_LOG(Log::LogLevel::INFO, "StartSession rejected: name=%s unknown state=%d",
                        Options->SessionName, static_cast<int>(state));
                    ssci.ResultCode = EOS_EResult::EOS_InvalidParameters;
                    break;
            }
        }
        else
        {
            APP_LOG(Log::LogLevel::DEBUG, "Starting session: name %s Not Found", Options->SessionName);
            ssci.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Mark a session as ended, making it available to find if "join in progress" was disabled.  The session may be started again if desired
 *
 * @param Options Structure containing information about the session to be ended
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the end operation completes, either successfully or in error
 *
 * @return EOS_Success if the end completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Sessions_OutOfSync if the session is out of sync and will be updated on the next connection with the backend
 *         EOS_NotFound if a session to be ended does not exist
 */
void EOSSDK_Sessions::EndSession(const EOS_Sessions_EndSessionOptions* Options, void* ClientData, const EOS_Sessions_OnEndSessionCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Sessions_EndSessionCallbackInfo& esci = res->CreateCallback<EOS_Sessions_EndSessionCallbackInfo>((CallbackFunc)CompletionDelegate);

    esci.ClientData = ClientData;

    if (Options == nullptr || Options->SessionName == nullptr)
    {
        esci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        session_state_t* session = get_session_by_name(Options->SessionName);
        if (session != nullptr)
        {
            esci.ResultCode = EOS_EResult::EOS_Success;
            if (session_infos_attr_bool(session->infos, "__EOS_bListening"))
            {
                APP_LOG(Log::LogLevel::INFO, "EndSession ignored while listen server active: %s",
                    Options->SessionName);
            }
            else
            {
                session->infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_Ended));
            }
        }
        else
        {
            esci.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Register a group of players with the session, allowing them to invite others or otherwise indicate they are part of the session for determining a full session
 *
 * @param Options Structure containing information about the session and players to be registered
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the registration operation completes, either successfully or in error
 *
 * @return EOS_Success if the register completes successfully
 *         EOS_NoChange if the players to register registered previously
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Sessions_OutOfSync if the session is out of sync and will be updated on the next connection with the backend
 *         EOS_NotFound if a session to register players does not exist
 */
void EOSSDK_Sessions::RegisterPlayers(const EOS_Sessions_RegisterPlayersOptions* Options, void* ClientData, const EOS_Sessions_OnRegisterPlayersCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_RegisterPlayersCallbackInfo& rpci = res->CreateCallback<EOS_Sessions_RegisterPlayersCallbackInfo>((CallbackFunc)CompletionDelegate);
    rpci.ClientData = ClientData;

    if (Options->SessionName == nullptr || Options->PlayersToRegister == nullptr || Options->PlayersToRegisterCount == 0)
    {
        rpci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        session_state_t* session = get_session_by_name(Options->SessionName);
        if (session == nullptr)
        {
            rpci.ResultCode = EOS_EResult::EOS_NotFound;
        }
        else
        {
            std::string const local_id = Settings::Inst().productuserid->to_string();
            google::protobuf::RepeatedPtrField<std::string> registered;
            for (uint32_t i = 0; i < Options->PlayersToRegisterCount; ++i)
            {
                std::string const player_id = Options->PlayersToRegister[i]->to_string();
                if (player_id == local_id && !is_player_registered(local_id, session))
                    register_player_to_session(local_id, session);

                if (register_player_to_session(player_id, session))
                    *registered.Add() = player_id;
            }

            if (registered.empty())
            {
                rpci.ResultCode = is_player_registered(local_id, session)
                    ? EOS_EResult::EOS_NoChange
                    : EOS_EResult::EOS_Sessions_NotAllowed;
            }
            else
            {
                rpci.ResultCode = EOS_EResult::EOS_Success;
                replicate_session_to_all_copies(_sessions, *session);

                patch_session_public_member_list(session->infos);
                send_session_info(session);

                Session_Register_pb* register_ = new Session_Register_pb;

                register_->set_session_id(session->infos.session_id());
                *register_->mutable_member_ids() = std::move(registered);

                send_session_register(register_, session);
            }
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Unregister a group of players with the session, freeing up space for others to join
 *
 * @param Options Structure containing information about the session and players to be unregistered
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the unregistration operation completes, either successfully or in error
 *
 * @return EOS_Success if the unregister completes successfully
 *         EOS_NoChange if the players to unregister were not found
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Sessions_OutOfSync if the session is out of sync and will be updated on the next connection with the backend
 *         EOS_NotFound if a session to be unregister players does not exist
 */
void EOSSDK_Sessions::UnregisterPlayers(const EOS_Sessions_UnregisterPlayersOptions* Options, void* ClientData, const EOS_Sessions_OnUnregisterPlayersCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_UnregisterPlayersCallbackInfo& upci = res->CreateCallback<EOS_Sessions_UnregisterPlayersCallbackInfo>((CallbackFunc)CompletionDelegate);
    upci.ClientData = ClientData;

    if (Options->SessionName == nullptr || Options->PlayersToUnregister == nullptr || Options->PlayersToUnregisterCount == 0)
    {
        upci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        session_state_t* session = get_session_by_name(Options->SessionName);
        if (session == nullptr)
        {
            upci.ResultCode = EOS_EResult::EOS_NotFound;
        }
        else
        {
            if (is_player_registered(Settings::Inst().productuserid->to_string(), session))
            {
                google::protobuf::RepeatedPtrField<std::string> unregistered;
                for (uint32_t i = 0; i < Options->PlayersToUnregisterCount; ++i)
                {
                    if (unregister_player_from_session(Options->PlayersToUnregister[i]->to_string(), session))
                    {
                        *unregistered.Add() = Options->PlayersToUnregister[i]->to_string();
                    }
                }
                if (unregistered.empty())
                {
                    upci.ResultCode = EOS_EResult::EOS_NoChange;
                }
                else
                {
                    upci.ResultCode = EOS_EResult::EOS_Success;

                    std::string const& user_id = Settings::Inst().productuserid->to_string();

                    Session_Unregister_pb* unregister = new Session_Unregister_pb;

                    unregister->set_session_id(session->infos.session_id());
                    *unregister->mutable_member_ids() = std::move(unregistered);

                    send_session_unregister(unregister, session);
                }
            }
            else
            {
                upci.ResultCode = EOS_EResult::EOS_Sessions_NotAllowed;
            }
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Send an invite to another player.  User must have created the session or be registered in the session or else the call will fail
 *
 * @param Options Structure containing information about the session and player to invite
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the send invite operation completes, either successfully or in error
 *
 * @return EOS_Success if the send invite completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_NotFound if the session to send the invite from does not exist
 */
void EOSSDK_Sessions::SendInvite(const EOS_Sessions_SendInviteOptions* Options, void* ClientData, const EOS_Sessions_OnSendInviteCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_SendInviteCallbackInfo& sici = res->CreateCallback<EOS_Sessions_SendInviteCallbackInfo>((CallbackFunc)CompletionDelegate);

    sici.ClientData = ClientData;

    if (Options == nullptr || Options->SessionName == nullptr || Options->TargetUserId == nullptr)
    {
        sici.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        session_state_t* session = get_session_by_name(Options->SessionName);
        if (session == nullptr || GetEOS_Connect().get_user_by_productid(Options->TargetUserId) == GetEOS_Connect().get_end_users())
        {
            sici.ResultCode = EOS_EResult::EOS_NotFound;
        }
        else
        {
            Session_Invite_pb* invite = new Session_Invite_pb;
            *invite->mutable_infos() = session->infos;
            send_session_invite(Options->TargetUserId->to_string(), invite);
            sici.ResultCode = EOS_EResult::EOS_Success;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Reject an invite from another player.
 *
 * @param Options Structure containing information about the invite to reject
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the reject invite operation completes, either successfully or in error
 *
 * @return EOS_Success if the invite rejection completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_NotFound if the invite does not exist
 */
void EOSSDK_Sessions::RejectInvite(const EOS_Sessions_RejectInviteOptions* Options, void* ClientData, const EOS_Sessions_OnRejectInviteCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_RejectInviteCallbackInfo& rici = res->CreateCallback<EOS_Sessions_RejectInviteCallbackInfo>((CallbackFunc)CompletionDelegate);

    rici.ClientData = ClientData;

    auto it = std::find_if(_session_invites.begin(), _session_invites.end(), [Options]( session_invite_t& invite)
    {
        return invite.invite_id == Options->InviteId;
    });

    if (it == _session_invites.end())
    {
        rici.ResultCode = EOS_EResult::EOS_NotFound;
    }
    else
    {
        rici.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Retrieve all existing invites for a single user
 *
 * @param Options Structure containing information about the invites to query
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the query invites operation completes, either successfully or in error
 *
 */
void EOSSDK_Sessions::QueryInvites(const EOS_Sessions_QueryInvitesOptions* Options, void* ClientData, const EOS_Sessions_OnQueryInvitesCallback CompletionDelegate)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_QueryInvitesCallbackInfo& qici = res->CreateCallback<EOS_Sessions_QueryInvitesCallbackInfo>((CallbackFunc)CompletionDelegate);
    qici.LocalUserId = GetEOS_Connect().get_myself()->first;
    qici.ClientData = ClientData;

    if (Options == nullptr || Options->LocalUserId == nullptr)
    {
        qici.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        qici.ResultCode = EOS_EResult::EOS_Success;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Get the number of known invites for a given user
 *
 * @param Options the Options associated with retrieving the current invite count
 *
 * @return number of known invites for a given user or 0 if there is an error
 */
uint32_t EOSSDK_Sessions::GetInviteCount(const EOS_Sessions_GetInviteCountOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->LocalUserId != GetEOS_Connect().get_myself()->first)
        return 0;

    return static_cast<uint32_t>(_session_invites.size());
}

/**
 * Retrieve an invite id from a list of active invites for a given user
 *
 * @param Options Structure containing the input parameters
 *
 * @return EOS_Success if the input is valid and an invite id was returned
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_NotFound if the invite doesn't exist
 *
 * @see EOS_Sessions_GetInviteCount
 * @see EOS_Sessions_CopySessionHandleByInviteId
 */
EOS_EResult EOSSDK_Sessions::GetInviteIdByIndex(const EOS_Sessions_GetInviteIdByIndexOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->LocalUserId != GetEOS_Connect().get_myself()->first ||
        Options->Index >= _session_invites.size() ||
        OutBuffer == nullptr || InOutBufferLength == nullptr)
    {
        return EOS_EResult::EOS_InvalidParameters;
    }
    
    auto it = _session_invites.begin();
    std::advance(it, Options->Index);

    strncpy(OutBuffer, it->invite_id.c_str(), *InOutBufferLength);
    *InOutBufferLength = std::min<int32_t>(static_cast<int32_t>(it->invite_id.length() + 1), *InOutBufferLength);

    return EOS_EResult::EOS_Success;
}

/**
 * Create a session search handle.  This handle may be modified to include various search parameters.
 * Searching is possible in three methods, all mutually exclusive
 * - set the session id to find a specific session
 * - set the target user id to find a specific user
 * - set session parameters to find an array of sessions that match the search criteria
 *
 * @param Options Structure containing required parameters such as the maximum number of search results
 * @param OutSessionSearchHandle The new search handle or null if there was an error creating the search handle
 *
 * @return EOS_Success if the search creation completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 */
EOS_EResult EOSSDK_Sessions::CreateSessionSearch(const EOS_Sessions_CreateSessionSearchOptions* Options, EOS_HSessionSearch* OutSessionSearchHandle)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->MaxSearchResults == 0 || OutSessionSearchHandle == nullptr)
    {
        set_nullptr(OutSessionSearchHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }
    
    _session_searchs.emplace_back();
    EOSSDK_SessionSearch*& session_search = _session_searchs.back();
    session_search = new EOSSDK_SessionSearch;

    *OutSessionSearchHandle = reinterpret_cast<EOS_HSessionSearch>(session_search);

    return EOS_EResult::EOS_Success;
}

/**
 * Create a handle to an existing active session.
 *
 * @param Options Structure containing information about the active session to retrieve
 * @param OutSessionHandle The new active session handle or null if there was an error
 *
 * @return EOS_Success if the session handle was created successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound if the active session doesn't exist
 */
EOS_EResult EOSSDK_Sessions::CopyActiveSessionHandle(const EOS_Sessions_CopyActiveSessionHandleOptions* Options, EOS_HActiveSession* OutSessionHandle)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->SessionName == nullptr || OutSessionHandle == nullptr)
    {
        set_nullptr(OutSessionHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    session_state_t* session = get_session_by_name(Options->SessionName);
    if (session == nullptr)
    {
        APP_LOG(Log::LogLevel::DEBUG, "Didn't find Active Session %s", Options->SessionName);
        return EOS_EResult::EOS_NotFound;
    }

    APP_LOG(Log::LogLevel::DEBUG, "Found Active Session %s", Options->SessionName);
    EOSSDK_ActiveSession* active_session = new EOSSDK_ActiveSession;
    
    active_session->_session_name = Options->SessionName;
    active_session->_infos = session->infos;

    *OutSessionHandle = reinterpret_cast<EOS_HActiveSession>(active_session);

    return EOS_EResult::EOS_Success;
}

/**
 * Register to receive session invites.
 * @note must call RemoveNotifySessionInviteReceived to remove the notification
 *
 * @param Options Structure containing information about the session invite notification
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param Notification A callback that is fired when a session invite for a user has been received
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Sessions::AddNotifySessionInviteReceived(const EOS_Sessions_AddNotifySessionInviteReceivedOptions* Options, void* ClientData, const EOS_Sessions_OnSessionInviteReceivedCallback NotificationFn)
{
    TRACE_FUNC();
    
    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);
    
    EOS_Sessions_SessionInviteReceivedCallbackInfo& sirci = res->CreateCallback<EOS_Sessions_SessionInviteReceivedCallbackInfo>((CallbackFunc)NotificationFn);

    sirci.ClientData = ClientData;
    sirci.LocalUserId = GetEOS_Connect().get_myself()->first;
    sirci.InviteId = new char[max_accountid_length];

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving session invites.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Sessions::RemoveNotifySessionInviteReceived(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive notifications when a user accepts a session invite via the social overlay.
 * @note must call RemoveNotifySessionInviteAccepted to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Sessions::AddNotifySessionInviteAccepted(const EOS_Sessions_AddNotifySessionInviteAcceptedOptions* Options, void* ClientData, const EOS_Sessions_OnSessionInviteAcceptedCallback NotificationFn)
{
    TRACE_FUNC();

    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_SessionInviteAcceptedCallbackInfo& siaci = res->CreateCallback<EOS_Sessions_SessionInviteAcceptedCallbackInfo>((CallbackFunc)NotificationFn);

    siaci.ClientData = ClientData;
    siaci.LocalUserId = GetEOS_Connect().get_myself()->first;
    siaci.SessionId = new char[max_accountid_length];

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when a user accepts a session invite via the social overlay.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Sessions::RemoveNotifySessionInviteAccepted(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive notifications when a user accepts a session join game via the social overlay.
 * @note must call RemoveNotifyJoinSessionAccepted to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Sessions::AddNotifyJoinSessionAccepted(const EOS_Sessions_AddNotifyJoinSessionAcceptedOptions* Options, void* ClientData, const EOS_Sessions_OnJoinSessionAcceptedCallback NotificationFn)
{
    TRACE_FUNC();

    if (NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Sessions_JoinSessionAcceptedCallbackInfo& jsaci = res->CreateCallback<EOS_Sessions_JoinSessionAcceptedCallbackInfo>((CallbackFunc)NotificationFn);

    jsaci.ClientData = ClientData;
    jsaci.LocalUserId = GetEOS_Connect().get_myself()->first;
    jsaci.UiEventId = EOS_UI_EVENTID_INVALID;

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when a user accepts a session join game via the social overlay.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Sessions::RemoveNotifyJoinSessionAccepted(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * EOS_Sessions_CopySessionHandleByInviteId is used to immediately retrieve a handle to the session information from after notification of an invite
 * If the call returns an EOS_Success result, the out parameter, OutSessionHandle, must be passed to EOS_SessionDetails_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutSessionHandle out parameter used to receive the session handle
 *
 * @return EOS_Success if the information is available and passed out in OutSessionHandle
 *         EOS_InvalidParameters if you pass an invalid invite id or a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound if the invite id cannot be found
 *
 * @see EOS_Sessions_CopySessionHandleByInviteIdOptions
 * @see EOS_SessionDetails_Release
 */
EOS_EResult EOSSDK_Sessions::CopySessionHandleByInviteId(const EOS_Sessions_CopySessionHandleByInviteIdOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
    TRACE_FUNC();
    GLOBAL_LOCK();
    
    if (Options == nullptr || Options->InviteId == nullptr || OutSessionHandle == nullptr)
    {
        set_nullptr(OutSessionHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto it = std::find_if(_session_invites.begin(), _session_invites.end(), [Options](session_invite_t const& invite)
    {
        return invite.invite_id == Options->InviteId;
    });

    if (it == _session_invites.end())
    {
        return EOS_EResult::EOS_NotFound;
    }
    
    EOSSDK_SessionDetails* details = new EOSSDK_SessionDetails;

    details->_infos = it->infos;

    *OutSessionHandle = reinterpret_cast<EOS_HSessionDetails>(details);

    return EOS_EResult::EOS_Success;
}

/**
 * EOS_Sessions_CopySessionHandleByUiEventId is used to immediately retrieve a handle to the session information from after notification of a join game event.
 * If the call returns an EOS_Success result, the out parameter, OutSessionHandle, must be passed to EOS_SessionDetails_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutSessionHandle out parameter used to receive the session handle
 *
 * @return EOS_Success if the information is available and passed out in OutSessionHandle
 *         EOS_InvalidParameters if you pass an invalid invite id or a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound if the invite id cannot be found
 *
 * @see EOS_Sessions_CopySessionHandleByUiEventIdOptions
 * @see EOS_SessionDetails_Release
 */
EOS_EResult EOSSDK_Sessions::CopySessionHandleByUiEventId(const EOS_Sessions_CopySessionHandleByUiEventIdOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
    TRACE_FUNC();
    GLOBAL_LOCK();
    
    if (Options == nullptr || Options->UiEventId == EOS_UI_EVENTID_INVALID || OutSessionHandle == nullptr)
    {
        set_nullptr(OutSessionHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Session_Infos_pb infos;
    if (!GetEOS_UI().copy_session_join_event(Options->UiEventId, infos))
    {
        set_nullptr(OutSessionHandle);
        return EOS_EResult::EOS_NotFound;
    }

    prepare_session_infos_for_network(infos);
    fix_session_host_from_peer(infos, session_owner_product_id(infos));

    EOSSDK_SessionDetails* details = new EOSSDK_SessionDetails;
    details->_infos = std::move(infos);
    *OutSessionHandle = reinterpret_cast<EOS_HSessionDetails>(details);
    return EOS_EResult::EOS_Success;
}

/**
 * EOS_Sessions_CopySessionHandleForPresence is used to immediately retrieve a handle to the session information which was marked with bPresenceEnabled on create or join.
 * If the call returns an EOS_Success result, the out parameter, OutSessionHandle, must be passed to EOS_SessionDetails_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutSessionHandle out parameter used to receive the session handle
 *
 * @return EOS_Success if the information is available and passed out in OutSessionHandle
 *         EOS_InvalidParameters if you pass an invalid invite id or a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound if there is no session with bPresenceEnabled
 *
 * @see EOS_Sessions_CopySessionHandleForPresenceOptions
 * @see EOS_SessionDetails_Release
 */
EOS_EResult EOSSDK_Sessions::CopySessionHandleForPresence(const EOS_Sessions_CopySessionHandleForPresenceOptions* Options, EOS_HSessionDetails* OutSessionHandle)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || OutSessionHandle == nullptr)
    {
        set_nullptr(OutSessionHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    for (auto const& session : _sessions)
    {
        if (session.second.infos.presence_allowed())
        {
            APP_LOG(Log::LogLevel::DEBUG, "Found Session for presence");

            EOSSDK_SessionDetails *details = new EOSSDK_SessionDetails;
            details->_infos = session.second.infos;
            *OutSessionHandle = reinterpret_cast<EOS_HSessionDetails>(details);
            return EOS_EResult::EOS_Success;
        }
    }

    APP_LOG(Log::LogLevel::DEBUG, "Did not find Session for presence");
    *OutSessionHandle = nullptr;
    return EOS_EResult::EOS_NotFound;
}

/**
 * EOS_Sessions_IsUserInSession returns whether or not a given user can be found in a specified session
 *
 * @param Options Structure containing the input parameters
 *
 * @return EOS_Success if the user is found in the specified session
 *		   EOS_NotFound if the user is not found in the specified session
 *		   EOS_InvalidParameters if you pass an invalid invite id or a null pointer for the out parameter
 *		   EOS_IncompatibleVersion if the API version passed in is incorrect
 *		   EOS_Invalid_ProductUserID if an invalid target user is specified
 *		   EOS_Sessions_InvalidSession if the session specified is invalid
 */
EOS_EResult EOSSDK_Sessions::IsUserInSession(const EOS_Sessions_IsUserInSessionOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->TargetUserId == nullptr || Options->SessionName == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Options->TargetUserId == Settings::Inst().productuserid)
    {
        session_state_t* session = get_session_by_name(Options->SessionName);
        if (session != nullptr)
        {
            for (auto const& player : session->infos.players())
            {
                if (GetProductUserId(player) == Options->TargetUserId)
                {
                    return EOS_EResult::EOS_Success;
                }
            }
        }
        else
        {
            return EOS_EResult::EOS_Sessions_InvalidSession;
        }
    }
    else
    {
        auto user_infos = GetEOS_Connect().get_user_by_productid(Options->TargetUserId);
        if (user_infos != GetEOS_Connect().get_end_users())
        {
            for (auto const& session : user_infos->second.infos.sessions())
            {
                if (session.first == Options->SessionName)
                {
                    return EOS_EResult::EOS_Success;
                }
            }
        }
    }

    return EOS_EResult::EOS_NotFound;
}

/**
 * Dump the contents of active sessions that exist locally to the log output, purely for debug purposes
 *
 * @param Options Options related to dumping session state such as the session name
 *
 * @return EOS_Success if the output operation completes successfully
 *         EOS_NotFound if the session specified does not exist
 *         EOS_InvalidParameters if any of the options are incorrect
 */

EOS_EResult EOSSDK_Sessions::DumpSessionState(const EOS_Sessions_DumpSessionStateOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    return EOS_EResult::EOS_Success;
}

///////////////////////////////////////////////////////////////////////////////
//                           Network Send messages                           //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Sessions::send_to_all_members(Network_Message_pb & msg, session_state_t* session)
{
    TRACE_FUNC();
    assert(session != nullptr);

    std::vector<std::string> const peers = session_member_peers(session->infos);
    for (auto const& player : peers)
    {
        if (player != msg.source_id())
        {
            msg.set_dest_id(player);
            GetNetwork().TCPSendTo(msg);
        }
    }
    return !peers.empty();
}

bool EOSSDK_Sessions::send_session_info_request(Network::peer_t const& peerid, Session_Infos_Request_pb* req)
{
    TRACE_FUNC();
    // TODO: Make it P2P, send it to all, will have to filter results
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session = new Session_Message_pb;

    session->set_allocated_sessions_request(req);

    msg.set_allocated_session(session);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_Sessions::send_session_info(session_state_t* session)
{
    TRACE_FUNC();
    assert(session != nullptr);
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Session_Infos_pb infos_copy = session->infos;
    prepare_session_infos_for_network(infos_copy);
    apply_session_host_network_fix(infos_copy);

    Network_Message_pb msg;
    Session_Message_pb* session_pb = new Session_Message_pb;
    Session_Infos_pb* infos = new Session_Infos_pb(std::move(infos_copy));

    session_pb->set_allocated_session_infos(infos);
    msg.set_allocated_session(session_pb);
    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    bool res = send_to_all_members(msg, session);

    return res;
}

bool EOSSDK_Sessions::send_session_destroy(session_state_t *session)
{
    TRACE_FUNC();
    assert(session != nullptr);
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session_pb = new Session_Message_pb;
    Session_Destroy_pb* destr = new Session_Destroy_pb;

    destr->set_session_id(session->infos.session_id());

    session_pb->set_allocated_session_destroy(destr);
    msg.set_allocated_session(session_pb);
    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    return send_to_all_members(msg, session);
}

bool EOSSDK_Sessions::send_sessions_search_response(Network::peer_t const& peerid, Sessions_Search_response_pb* resp)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Sessions_Search_Message_pb* search = new Sessions_Search_Message_pb;

    search->set_allocated_search_response(resp);
    msg.set_allocated_sessions_search(search);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    bool const sent = GetNetwork().SendToPeer(msg);
    APP_LOG(Log::LogLevel::INFO,
        "Session search response sent peer=%s search_id=%llu sessions=%u sent=%d",
        peerid.c_str(),
        static_cast<unsigned long long>(resp->search_id()),
        static_cast<unsigned>(resp->sessions_size()),
        sent ? 1 : 0);
    return sent;
}

bool EOSSDK_Sessions::send_session_join_request(session_state_t *session)
{
    TRACE_FUNC();
    assert(session != nullptr);
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session_pb = new Session_Message_pb;
    Session_Join_Request_pb* req = new Session_Join_Request_pb;

    session_pb->set_allocated_session_join_request(req);
    msg.set_allocated_session(session_pb);
    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    req->set_session_id(session->infos.session_id());

    bool sent = send_to_all_members(msg, session);
    if (!sent)
    {
        std::string const host_id = session_owner_product_id(session->infos);
        if (!host_id.empty() && host_id != user_id)
        {
            msg.set_dest_id(host_id);
            sent = GetNetwork().TCPSendTo(msg);
            APP_LOG(Log::LogLevel::INFO, "Session join request sent directly to host %s for session %s",
                host_id.c_str(), session->infos.session_id().c_str());
        }
        else
        {
            APP_LOG(Log::LogLevel::WARN, "Session join request has no recipients for session %s (players=%d registered=%d host=%s)",
                session->infos.session_id().c_str(),
                session->infos.players_size(),
                session->infos.registered_players_size(),
                host_id.c_str());
        }
    }
    else
    {
        APP_LOG(Log::LogLevel::DEBUG, "Session join request sent for session %s to %u peer(s)",
            session->infos.session_id().c_str(),
            static_cast<unsigned>(session_member_peers(session->infos).size()));
    }

    return sent;
}

bool EOSSDK_Sessions::send_session_join_response(Network::peer_t const& peerid, Session_Join_Response_pb* resp)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session = new Session_Message_pb;

    session->set_allocated_session_join_response(resp);
    msg.set_allocated_session(session);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    session_state_t* pSession = get_session_by_id(resp->session_id());

    if (pSession != nullptr)
    {// Notify all session members of a join status
        send_to_all_members(msg, pSession);
    }
    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_Sessions::send_session_invite(Network::peer_t const& peerid, Session_Invite_pb* invite)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session = new Session_Message_pb;

    session->set_allocated_session_invite(invite);
    msg.set_allocated_session(session);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_Sessions::send_session_invite_response(Network::peer_t const& peerid, Session_Invite_Response_pb* resp)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session = new Session_Message_pb;

    session->set_allocated_session_invite_response(resp);
    msg.set_allocated_session(session);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_Sessions::send_session_register(Session_Register_pb* register_, session_state_t* session)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session_pb = new Session_Message_pb;

    session_pb->set_allocated_session_register(register_);
    msg.set_allocated_session(session_pb);

    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    return send_to_all_members(msg, session);
}

bool EOSSDK_Sessions::send_session_unregister(Session_Unregister_pb* unregister, session_state_t* session)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Session_Message_pb* session_pb = new Session_Message_pb;

    session_pb->set_allocated_session_unregister(unregister);
    msg.set_allocated_session(session_pb);

    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    return send_to_all_members(msg, session);
}

///////////////////////////////////////////////////////////////////////////////
//                          Network Receive messages                         //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Sessions::on_peer_disconnect(Network_Message_pb const& msg, Network_Peer_Disconnect_pb const& peer)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    for (auto& session : _sessions)
    {
        remove_player_from_session(msg.source_id(), &session.second);
    }

    return true;
}

bool EOSSDK_Sessions::on_session_info_request(Network_Message_pb const& msg, Session_Infos_Request_pb const& req)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    session_state_t* session = get_session_by_id(req.session_id());
    Session_Infos_pb* infos;

    if (session == nullptr)
    {
        infos = new Session_Infos_pb();
    }
    else
    {
        infos = new Session_Infos_pb(session->infos);
        prepare_session_infos_for_network(*infos);
        apply_session_host_network_fix(*infos, msg.source_id());
    }

    Network_Message_pb resp;
    Session_Message_pb* session_pb = new Session_Message_pb;

    session_pb->set_allocated_session_infos(infos);
    resp.set_allocated_session(session_pb);

    resp.set_source_id(Settings::Inst().productuserid->to_string());
    resp.set_dest_id(msg.source_id());

    return GetNetwork().TCPSendTo(resp);
}

bool EOSSDK_Sessions::on_session_info(Network_Message_pb const& msg, Session_Infos_pb const& infos)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    session_state_t* primary = find_primary_session_for_id(_sessions, infos.session_id());
    if (primary == nullptr)
        return true;

    std::string const local_id = Settings::Inst().productuserid->to_string();

    Session_Infos_pb infos_copy = infos;
    fix_session_host_from_peer(infos_copy, msg.source_id());
    prepare_session_infos_for_network(infos_copy);
    fix_session_host_from_peer(infos_copy, msg.source_id());
    patch_session_public_member_list(infos_copy);

    primary->infos = std::move(infos_copy);
    if (primary->state == session_state_t::state_e::joined ||
        primary->state == session_state_t::state_e::joining)
    {
        add_player_to_session(local_id, primary);
        register_player_to_session(local_id, primary);
    }
    if (primary->state == session_state_t::state_e::joined)
    {
        primary->state = session_state_t::state_e::joined;
        primary->infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress));
    }
    else if (infos.state() == utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress) &&
        primary->state != session_state_t::state_e::joining)
    {
        primary->state = session_state_t::state_e::joined;
    }

    replicate_session_to_all_copies(_sessions, *primary);

    APP_LOG(Log::LogLevel::INFO, "Session info synced from %s: session=%s state=%d players=%d registered=%d host=%s",
        msg.source_id().c_str(),
        primary->infos.session_id().c_str(),
        primary->infos.state(),
        primary->infos.players_size(),
        primary->infos.registered_players_size(),
        primary->infos.host_address().c_str());

    return true;
}

bool EOSSDK_Sessions::on_session_destroy(Network_Message_pb const& msg, Session_Destroy_pb const& destr)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    session_state_t* session = get_session_by_id(destr.session_id());
    if (session != nullptr)
        remove_player_from_session(msg.source_id(), session);

    return true;
}

void EOSSDK_Sessions::fill_sessions_search_response(Sessions_Search_response_pb* resp, Sessions_Search_pb const& search, bool allow_stale_redirect)
{
    resp->set_search_id(search.search_id());

    if (!search.session_id().empty())
    {
        Session_Infos_pb session_infos;
        if (try_get_session_infos_for_search(search.session_id(), session_infos, allow_stale_redirect))
        {
            prepare_session_infos_for_network(session_infos);
            if (session_infos.session_id() != search.session_id())
                session_infos.set_session_id(search.session_id());
            session_state_t temp;
            temp.infos = session_infos;
            if (session_match_from_attributes(&temp, search.parameters()))
            {
                APP_LOG(Log::LogLevel::INFO, "Session search match requested=%s session=%s host=%s state=%d players=%d version=%s",
                    search.session_id().c_str(),
                    session_infos.session_id().c_str(),
                    session_infos.host_address().c_str(),
                    session_infos.state(),
                    session_infos.players_size(),
                    session_has_version(session_infos) ? "yes" : "no");
                *resp->mutable_sessions()->Add() = std::move(session_infos);
            }
            else
            {
                APP_LOG(Log::LogLevel::DEBUG, "Session id %s found but attributes did not match",
                    search.session_id().c_str());
            }
        }
        else
        {
            APP_LOG(Log::LogLevel::INFO, "Session search by id: no session or lobby for id=%s",
                search.session_id().c_str());
        }
    }
    else if (search.parameters_size() > 0)
    {
        std::vector<session_state_t*> sessions = std::move(get_sessions_from_attributes(search.parameters()));
        APP_LOG(Log::LogLevel::DEBUG, "sessions found: %d", sessions.size());
        for (auto& session : sessions)
        {
            Session_Infos_pb session_infos = session->infos;
            prepare_session_infos_for_network(session_infos);
            *resp->mutable_sessions()->Add() = std::move(session_infos);
        }

        if (resp->sessions_size() < static_cast<int>(search.max_results()))
        {
            for (auto* pLobby : GetEOS_Lobby().get_owned_lobbies())
            {
                Session_Infos_pb session_infos = make_session_infos_from_lobby(
                    pLobby->infos,
                    !pLobby->infos.lobby_id().empty() ? pLobby->infos.lobby_id() : pLobby->infos.owner_id());
                session_state_t temp;
                temp.infos = session_infos;
                if (!session_match_from_attributes(&temp, search.parameters()))
                    continue;

                bool already_added = false;
                for (auto const& existing : resp->sessions())
                {
                    if (existing.session_id() == session_infos.session_id())
                    {
                        already_added = true;
                        break;
                    }
                }
                if (!already_added)
                {
                    sync_session_from_lobby(pLobby->infos);
                    *resp->mutable_sessions()->Add() = std::move(session_infos);
                }

                if (resp->sessions_size() >= static_cast<int>(search.max_results()))
                    break;
            }
        }
    }
    else if (GetProductUserId(search.target_id()) == GetEOS_Connect().get_myself()->first)
    {
        auto append_if_joinable = [&](Session_Infos_pb session_infos)
        {
            if (session_infos.session_id().empty())
                return;

            if (!session_is_game_namespace(session_infos) && !session_has_version(session_infos))
            {
                APP_LOG(Log::LogLevel::DEBUG,
                    "Session search by target id skipped non-game session %s",
                    session_infos.session_id().c_str());
                return;
            }

            for (auto const& existing : resp->sessions())
            {
                if (existing.session_id() == session_infos.session_id())
                    return;
            }

            prepare_session_infos_for_network(session_infos);
            *resp->mutable_sessions()->Add() = std::move(session_infos);
        };

        if (session_state_t* game_session = get_session_by_name("GameSession"))
            append_if_joinable(game_session->infos);

        std::string const active_lobby = GetEOS_Lobby().get_active_join_lobby_id();
        if (!active_lobby.empty())
        {
            lobby_state_t* pLobby = GetEOS_Lobby().get_lobby_by_id(active_lobby);
            if (pLobby != nullptr && lobby_is_game_namespace(pLobby->infos))
            {
                Session_Infos_pb session_infos = build_clean_session_infos_for_join_search(active_lobby);
                if (session_infos.session_id().empty())
                {
                    if (try_get_session_infos_for_search(active_lobby, session_infos, false))
                        append_if_joinable(std::move(session_infos));
                }
                else
                {
                    append_if_joinable(std::move(session_infos));
                }
            }
        }

        if (resp->sessions_size() == 0)
        {
            for (auto* pLobby : GetEOS_Lobby().get_owned_lobbies())
            {
                if (!lobby_is_game_namespace(pLobby->infos))
                    continue;

                Session_Infos_pb session_infos;
                if (try_get_session_infos_for_search(pLobby->infos.lobby_id(), session_infos))
                    append_if_joinable(std::move(session_infos));

                if (resp->sessions_size() >= static_cast<int>(search.max_results()))
                    break;
            }
        }

        if (resp->sessions_size() == 0)
        {
            APP_LOG(Log::LogLevel::INFO,
                "Session search by target id: no joinable game session (presence-only or GameSession missing)");
        }
    }
}

void EOSSDK_Sessions::queue_pending_session_search(Network::peer_t const& peer_id, Sessions_Search_pb const& search)
{
    pending_session_search_t pending;
    pending.peer_id = peer_id;
    pending.search = search;
    pending.created = std::chrono::steady_clock::now();
    _pending_session_searches.emplace_back(std::move(pending));

    APP_LOG(Log::LogLevel::INFO, "Session search deferred (waiting for session) for peer=%s session_id=%s search_id=%llu",
        peer_id.c_str(),
        search.session_id().c_str(),
        static_cast<unsigned long long>(search.search_id()));
}

void EOSSDK_Sessions::flush_pending_session_searches()
{
    for (auto it = _pending_session_searches.begin(); it != _pending_session_searches.end();)
    {
        Sessions_Search_response_pb* resp = new Sessions_Search_response_pb;
        fill_sessions_search_response(resp, it->search);

        if (resp->sessions_size() > 0)
        {
            APP_LOG(Log::LogLevel::INFO, "Session search deferred reply: matched %u sessions for peer=%s session_id=%s",
                static_cast<unsigned>(resp->sessions_size()),
                it->peer_id.c_str(),
                it->search.session_id().c_str());
            send_sessions_search_response(it->peer_id, resp);
            it = _pending_session_searches.erase(it);
        }
        else
        {
            delete resp;
            ++it;
        }
    }
}

void EOSSDK_Sessions::expire_pending_session_searches()
{
    auto const now = std::chrono::steady_clock::now();
    for (auto it = _pending_session_searches.begin(); it != _pending_session_searches.end();)
    {
        if ((now - it->created) > pending_session_search_timeout)
        {
            Sessions_Search_response_pb* resp = new Sessions_Search_response_pb;
            fill_sessions_search_response(resp, it->search);
            APP_LOG(Log::LogLevel::INFO, "Session search deferred timeout: matched %u sessions for peer=%s session_id=%s",
                static_cast<unsigned>(resp->sessions_size()),
                it->peer_id.c_str(),
                it->search.session_id().c_str());
            send_sessions_search_response(it->peer_id, resp);
            it = _pending_session_searches.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

static void populate_steam_bridge_session_search(Sessions_Search_pb* req, std::string const& lobby_id, Lobby_Infos_pb const& lobby_infos)
{
    std::string const advertised = resolve_advertised_game_session_id(lobby_infos);
    if (!advertised.empty())
    {
        req->set_session_id(advertised);
        return;
    }

    std::string const canonical = resolve_canonical_session_id_for_lobby(lobby_id);
    if (!canonical.empty())
    {
        req->set_session_id(canonical);
        return;
    }

    if (!lobby_infos.owner_id().empty())
        req->set_target_id(lobby_infos.owner_id());
}

bool EOSSDK_Sessions::send_steam_bridge_session_search(Network::peer_t const& host_peer, std::string const& lobby_id, Lobby_Infos_pb const& lobby_infos, int32_t bridge_search_id)
{
    Network::peer_t const resolved_host = GetEOS_Lobby().resolve_steam_bridge_session_host_peer(lobby_infos, host_peer);
    if (resolved_host.empty() || lobby_id.empty())
        return false;

    if (bridge_search_id == 0)
        bridge_search_id = _next_steam_bridge_session_search_id++;

    steam_bridge_session_pending_t pending;
    pending.lobby_id = lobby_id;
    pending.lobby_infos = lobby_infos;
    pending.host_peer = resolved_host;
    pending.expected_session_host = resolved_host;
    pending.created = std::chrono::steady_clock::now();
    _steam_bridge_session_pending[bridge_search_id] = std::move(pending);

    Sessions_Search_pb* req = new Sessions_Search_pb;
    req->set_search_id(static_cast<uint64_t>(bridge_search_id));
    populate_steam_bridge_session_search(req, lobby_id, lobby_infos);
    std::string const log_session_id = req->session_id();
    std::string const log_target_id = req->target_id();

    Network_Message_pb msg;
    Sessions_Search_Message_pb* search_msg = new Sessions_Search_Message_pb;
    search_msg->set_allocated_search(req);
    msg.set_allocated_sessions_search(search_msg);
    msg.set_source_id(Settings::Inst().productuserid->to_string());
    msg.set_dest_id(resolved_host);
    msg.set_game_id(Settings::Inst().network_game_id());

    bool sent = GetNetwork().TCPSendTo(msg);
    if (!sent)
    {
        msg.clear_dest_id();
        sent = GetNetwork().SendBroadcast(msg);
    }
    (void)search_msg->release_search();
    delete req;

    APP_LOG(Log::LogLevel::INFO,
        "Steam invite bridge: session search_id=%d host=%s hint=%s lobby=%s session_id=%s target_id=%s sent=%d",
        bridge_search_id,
        resolved_host.c_str(),
        host_peer.c_str(),
        lobby_id.c_str(),
        log_session_id.c_str(),
        log_target_id.c_str(),
        sent ? 1 : 0);

    if (!sent)
        _steam_bridge_session_pending.erase(bridge_search_id);

    return sent;
}

bool EOSSDK_Sessions::on_steam_bridge_session_search_response(Network_Message_pb const& msg, Sessions_Search_response_pb const& resp)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    int32_t const bridge_id = static_cast<int32_t>(resp.search_id());
    auto pending_it = _steam_bridge_session_pending.find(bridge_id);
    if (pending_it == _steam_bridge_session_pending.end())
        return true;

    steam_bridge_session_pending_t pending = std::move(pending_it->second);
    _steam_bridge_session_pending.erase(pending_it);

    std::string const expected_host = !pending.expected_session_host.empty()
        ? pending.expected_session_host
        : pending.host_peer;

    auto pick_best_session = [&]() -> Session_Infos_pb
    {
        Session_Infos_pb best;
        for (auto const& candidate : resp.sessions())
        {
            Session_Infos_pb copy = candidate;
            if (!session_matches_expected_host(copy, expected_host))
            {
                APP_LOG(Log::LogLevel::WARN,
                    "Steam invite bridge: session search_id=%d rejected session=%s owner=%s expected_host=%s responder=%s",
                    bridge_id,
                    copy.session_id().c_str(),
                    session_owner_product_id(copy).c_str(),
                    expected_host.c_str(),
                    msg.source_id().c_str());
                continue;
            }

            fix_session_host_from_peer(copy, expected_host.empty() ? msg.source_id() : expected_host);
            if (best.session_id().empty() || (session_has_version(copy) && !session_has_version(best)))
                best = std::move(copy);
        }
        return best;
    };

    auto try_upgrade_session = [&](Session_Infos_pb& session) -> bool
    {
        if (session_has_version(session))
            return true;

        auto try_id = [&](std::string const& session_id) -> bool
        {
            if (session_id.empty())
                return false;

            Session_Infos_pb upgraded;
            if (try_get_session_infos_for_search(session_id, upgraded, false) && session_has_version(upgraded))
            {
                if (!session_matches_expected_host(upgraded, expected_host))
                    return false;

                session = std::move(upgraded);
                fix_session_host_from_peer(session, expected_host.empty() ? msg.source_id() : expected_host);
                return true;
            }
            return false;
        };

        auto gs_it = pending.lobby_infos.attributes().find("Redpoint:EOS:GameSessionId");
        if (gs_it != pending.lobby_infos.attributes().end() &&
            gs_it->second.value().value_case() == Lobby_Attr_Value::ValueCase::kS &&
            try_id(gs_it->second.value().s()))
            return true;

        if (session_state_t* game_session = get_session_by_name("GameSession"))
        {
            if (session_has_version(game_session->infos) &&
                session_matches_expected_host(game_session->infos, expected_host))
            {
                session = game_session->infos;
                fix_session_host_from_peer(session, expected_host.empty() ? msg.source_id() : expected_host);
                return true;
            }
        }

        for (auto const& entry : _sessions)
        {
            if (!session_has_version(entry.second.infos))
                continue;
            if (!pending.lobby_id.empty() && entry.second.infos.session_id() == pending.lobby_id)
                continue;
            if (!session_matches_expected_host(entry.second.infos, expected_host))
                continue;

            session = entry.second.infos;
            fix_session_host_from_peer(session, expected_host.empty() ? msg.source_id() : expected_host);
            APP_LOG(Log::LogLevel::INFO,
                "Steam invite bridge: session search_id=%d upgraded from cached session=%s",
                bridge_id,
                session.session_id().c_str());
            return true;
        }

        return try_id(session.session_id());
    };

    auto deliver_session_and_lobby = [&](Session_Infos_pb const& session)
    {
        auto version_it = session.attributes().find("Version");
        std::string version;
        if (version_it != session.attributes().end() &&
            version_it->second.value().value_case() == Session_Attr_Value::ValueCase::kS)
        {
            version = version_it->second.value().s();
        }

        APP_LOG(Log::LogLevel::INFO,
            "Steam invite bridge: session search_id=%d resolved session=%s version=%s host=%s from=%s",
            bridge_id,
            session.session_id().c_str(),
            version.c_str(),
            session.host_address().c_str(),
            msg.source_id().c_str());

        bool const local_game_session_ready = local_player_in_game_session(session.session_id());

        Lobby_Infos_pb const join_lobby = GetEOS_Lobby().resolve_join_party_lobby(pending.lobby_infos);

        if (!local_game_session_ready)
        {
            _steam_bridge_pending_auto_join = session;
            if (!join_lobby.lobby_id().empty())
                GetEOS_Lobby().notify_join_lobby_accepted(join_lobby);
        }
        else
        {
            APP_LOG(Log::LogLevel::INFO,
                "Steam invite bridge: session search_id=%d skipping duplicate join callbacks (local player already in GameSession)",
                bridge_id);
        }

        if (!join_lobby.lobby_id().empty())
            GetEOS_Lobby().initiate_steam_bridge_network_join(join_lobby);
    };

    if (resp.sessions_size() > 0)
    {
        Session_Infos_pb session = pick_best_session();
        if (session.session_id().empty())
        {
            APP_LOG(Log::LogLevel::WARN,
                "Steam invite bridge: session search_id=%d all %u response session(s) rejected for expected_host=%s responder=%s",
                bridge_id,
                static_cast<unsigned>(resp.sessions_size()),
                expected_host.c_str(),
                msg.source_id().c_str());
        }
        else if (try_upgrade_session(session))
        {
            register_discovered_session(session);
            deliver_session_and_lobby(session);
            return true;
        }
        else
        {
            APP_LOG(Log::LogLevel::WARN,
                "Steam invite bridge: session search_id=%d host returned %u session(s) without Version; deferring JoinLobbyAccepted lobby=%s",
                bridge_id,
                static_cast<unsigned>(resp.sessions_size()),
                pending.lobby_id.c_str());
        }

        if (!pending.lobby_infos.lobby_id().empty())
            GetEOS_Lobby().initiate_steam_bridge_network_join(GetEOS_Lobby().resolve_join_party_lobby(pending.lobby_infos));
        return true;
    }

    APP_LOG(Log::LogLevel::WARN,
        "Steam invite bridge: session search_id=%d no sessions from host=%s lobby=%s",
        bridge_id,
        msg.source_id().c_str(),
        pending.lobby_id.c_str());

    Session_Infos_pb fallback;
    if (try_upgrade_session(fallback))
    {
        register_discovered_session(fallback);
        deliver_session_and_lobby(fallback);
    }
    else if (!pending.lobby_infos.lobby_id().empty())
        GetEOS_Lobby().initiate_steam_bridge_network_join(GetEOS_Lobby().resolve_join_party_lobby(pending.lobby_infos));

    return true;
}

void EOSSDK_Sessions::expire_steam_bridge_session_searches()
{
    auto const now = std::chrono::steady_clock::now();
    for (auto it = _steam_bridge_session_pending.begin(); it != _steam_bridge_session_pending.end();)
    {
        if ((now - it->second.created) > steam_bridge_session_search_timeout)
        {
            APP_LOG(Log::LogLevel::WARN,
                "Steam invite bridge: session search_id=%d timeout host=%s lobby=%s",
                it->first,
                it->second.host_peer.c_str(),
                it->second.lobby_id.c_str());

            Session_Infos_pb fallback;
            if (try_get_session_infos_for_search(it->second.lobby_id, fallback, false) && session_has_version(fallback))
            {
                _steam_bridge_pending_auto_join = fallback;
                if (!it->second.lobby_infos.lobby_id().empty())
                {
                    Lobby_Infos_pb const join_lobby = GetEOS_Lobby().resolve_join_party_lobby(it->second.lobby_infos);
                    if (!local_player_in_game_session())
                        GetEOS_Lobby().notify_join_lobby_accepted(join_lobby);
                    GetEOS_Lobby().initiate_steam_bridge_network_join(join_lobby);
                }
            }
            else if (!it->second.lobby_infos.lobby_id().empty())
            {
                GetEOS_Lobby().initiate_steam_bridge_network_join(
                    GetEOS_Lobby().resolve_join_party_lobby(it->second.lobby_infos));
            }

            it = _steam_bridge_session_pending.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool EOSSDK_Sessions::on_sessions_search(Network_Message_pb const& msg, Sessions_Search_pb const& search)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (msg.source_id() == Settings::Inst().productuserid->to_string())
        return true;

    Sessions_Search_response_pb* resp = new Sessions_Search_response_pb;
    resp->set_search_id(search.search_id());

    if (Settings::Inst().matches_network_game_id(msg.game_id()))
    {
        fill_sessions_search_response(resp, search);

        if (!search.session_id().empty())
        {
            APP_LOG(Log::LogLevel::INFO, "Session search by id: id=%s matched=%u for peer=%s",
                search.session_id().c_str(),
                static_cast<unsigned>(resp->sessions_size()),
                msg.source_id().c_str());

            if (resp->sessions_size() == 0 &&
                msg.source_id() != Settings::Inst().productuserid->to_string())
            {
                delete resp;
                queue_pending_session_search(msg.source_id(), search);
                return true;
            }
        }
        else if (!search.target_id().empty())
        {
            APP_LOG(Log::LogLevel::INFO, "Session search by target: id=%s matched=%u for peer=%s",
                search.target_id().c_str(),
                static_cast<unsigned>(resp->sessions_size()),
                msg.source_id().c_str());

            if (resp->sessions_size() == 0 &&
                msg.source_id() != Settings::Inst().productuserid->to_string())
            {
                delete resp;
                queue_pending_session_search(msg.source_id(), search);
                return true;
            }
        }
    }

    return send_sessions_search_response(msg.source_id(), resp);
}

bool EOSSDK_Sessions::on_session_join_request(Network_Message_pb const& msg, Session_Join_Request_pb const& req)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    session_state_t* pSession = get_session_by_id(req.session_id());
    if (pSession == nullptr)
    {
        lobby_state_t* pLobby = GetEOS_Lobby().find_lobby_for_session_id(req.session_id());
        if (pLobby != nullptr)
        {
            sync_session_from_lobby(pLobby->infos);
            pSession = get_session_by_id(req.session_id());
            if (pSession == nullptr)
                pSession = get_session_by_name(pLobby->infos.lobby_id());
        }
    }

    if (pSession == nullptr)
    {
        APP_LOG(Log::LogLevel::DEBUG, "Join request ignored for session %s: session not found", req.session_id().c_str());
        return true;
    }

    std::string const& local_id = Settings::Inst().productuserid->to_string();
    if (!is_player_registered(local_id, pSession) &&
        !is_player_in_session(local_id, pSession) &&
        session_owner_product_id(pSession->infos) != local_id)
    {
        APP_LOG(Log::LogLevel::DEBUG, "Join request ignored for session %s: local user cannot host (registered=%d in_players=%d)",
            req.session_id().c_str(),
            is_player_registered(local_id, pSession),
            is_player_in_session(local_id, pSession));
        return true;
    }

    if (!is_player_registered(local_id, pSession))
        register_player_to_session(local_id, pSession);

    APP_LOG(Log::LogLevel::INFO, "Join request received for session %s from %s",
        req.session_id().c_str(), msg.source_id().c_str());

    Session_Join_Response_pb* resp = new Session_Join_Response_pb;

    resp->set_session_id(req.session_id());
    resp->set_user_id(msg.source_id());

    uint32_t const join_capacity = session_join_capacity(pSession->infos, req.session_id());
    if (join_capacity > static_cast<uint32_t>(pSession->infos.players_size()))
    {
        APP_LOG(Log::LogLevel::DEBUG, "Join request accepted from %s (capacity=%u players=%d).",
            msg.source_id().c_str(), join_capacity, pSession->infos.players_size());
        resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_Success));
        add_player_to_session(msg.source_id(), pSession);
        register_player_to_session(msg.source_id(), pSession);

        patch_session_public_member_list(pSession->infos);
        apply_session_host_network_fix(pSession->infos);
        promote_session_if_listen_server_ready(pSession);

        Session_Register_pb* reg = new Session_Register_pb;
        reg->set_session_id(pSession->infos.session_id());
        *reg->add_member_ids() = msg.source_id();
        send_session_register(reg, pSession);

        GetEOS_P2P().ensure_peer_connection(msg.source_id());
        GetEOS_Lobby().admit_session_player_to_game_lobby(req.session_id(), msg.source_id());

        session_state_t* notify_session = get_session_by_name("GameSession");
        if (notify_session == nullptr)
            notify_session = pSession;

        patch_session_public_member_list(notify_session->infos);
        apply_session_host_network_fix(notify_session->infos, msg.source_id());
        send_session_info(notify_session);

        {
            Session_Infos_pb infos_copy = notify_session->infos;
            prepare_session_infos_for_network(infos_copy);
            apply_session_host_network_fix(infos_copy, msg.source_id());
            Network_Message_pb direct;
            Session_Message_pb* session_pb = new Session_Message_pb;
            session_pb->set_allocated_session_infos(new Session_Infos_pb(std::move(infos_copy)));
            direct.set_allocated_session(session_pb);
            direct.set_source_id(Settings::Inst().productuserid->to_string());
            direct.set_dest_id(msg.source_id());
            direct.set_game_id(Settings::Inst().network_game_id());
            GetNetwork().TCPSendTo(direct);
        }

        {
            EOS_ProductUserId joiner_id = GetProductUserId(msg.source_id());
            EOS_Sessions_RegisterPlayersOptions opts = {};
            opts.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
            opts.SessionName = "GameSession";
            opts.PlayersToRegister = &joiner_id;
            opts.PlayersToRegisterCount = 1;
            RegisterPlayers(&opts, nullptr, noop_register_players_cb);
        }

        Session_Infos_pb notify_infos = notify_session->infos;
        notify_infos.set_session_id(req.session_id());
        notify_join_session_accepted(notify_infos);
    }
    else
    {
        APP_LOG(Log::LogLevel::DEBUG, "Join request rejected: This session is full.");
        resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_Sessions_TooManyPlayers));
    }

    return send_session_join_response(msg.source_id(), resp);
}

bool EOSSDK_Sessions::on_session_join_response(Network_Message_pb const& msg, Session_Join_Response_pb const& resp)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    std::string const& user_id = Settings::Inst().productuserid->to_string();
    session_state_t* session = find_primary_session_for_id(_sessions, resp.session_id());

    auto reason = static_cast<EOS_EResult>(resp.reason());
    if (resp.user_id() == Settings::Inst().productuserid->to_string())
    {
        auto it = _sessions_join.find(resp.session_id());
        if (it != _sessions_join.end())
        {
            EOS_Sessions_JoinSessionCallbackInfo& jsci = it->second->GetCallback<EOS_Sessions_JoinSessionCallbackInfo>();
            jsci.ResultCode = static_cast<EOS_EResult>(resp.reason());

            switch (jsci.ResultCode)
            {// Don't wait for a consensus, sessions are P2P, the first valid response is the right one
                case EOS_EResult::EOS_Sessions_NotAllowed:
                {// If this peer doesn't know us yet, set the error code but do not stop the join request, someone might accept us
                    APP_LOG(Log::LogLevel::DEBUG, "(%s) Join request rejected: We don't know (yet?) the user.", msg.source_id().c_str());
                }
                break;

                case EOS_EResult::EOS_Sessions_TooManyPlayers:
                {
                    APP_LOG(Log::LogLevel::DEBUG, "(%s) Join rejected: This session is full.", msg.source_id().c_str());
                    it->second->done = true;
                    _sessions_join.erase(it);
                    if (session != nullptr)
                    {
                        for (auto erase_it = _sessions.begin(); erase_it != _sessions.end(); )
                        {
                            if (erase_it->second.infos.session_id() == resp.session_id())
                                erase_it = _sessions.erase(erase_it);
                            else
                                ++erase_it;
                        }
                    }
                }
                break;
                
                case EOS_EResult::EOS_Success:
                {
                    if (session == nullptr)
                    {
                        APP_LOG(Log::LogLevel::WARN, "(%s) Join accepted but session %s not found locally",
                            msg.source_id().c_str(), resp.session_id().c_str());
                        break;
                    }

                    APP_LOG(Log::LogLevel::INFO, "(%s) Join accepted, ResultCode=Success session=%s",
                        msg.source_id().c_str(), resp.session_id().c_str());
                    it->second->done = true;
                    _sessions_join.erase(it);

                    session->state = session_state_t::state_e::joined;
                    session->infos.set_state(utils::GetEnumValue(EOS_EOnlineSessionState::EOS_OSS_InProgress));
                    add_player_to_session(user_id, session);
                    register_player_to_session(user_id, session);
                    apply_session_host_network_fix(session->infos, msg.source_id());
                    prepare_session_infos_for_network(session->infos);
                    apply_session_host_network_fix(session->infos, msg.source_id());
                    replicate_session_to_all_copies(_sessions, *session);

                    {
                        EOS_ProductUserId local_player = GetProductUserId(user_id);
                        EOS_Sessions_RegisterPlayersOptions reg_opts = {};
                        reg_opts.ApiVersion = EOS_SESSIONS_REGISTERPLAYERS_API_LATEST;
                        reg_opts.SessionName = "GameSession";
                        reg_opts.PlayersToRegister = &local_player;
                        reg_opts.PlayersToRegisterCount = 1;
                        RegisterPlayers(&reg_opts, nullptr, noop_register_players_cb);
                    }

                    if (session_state_t* refreshed = find_primary_session_for_id(_sessions, resp.session_id()))
                        replicate_session_to_all_copies(_sessions, *refreshed);

                    if (session_state_t* notify_session = find_primary_session_for_id(_sessions, resp.session_id()))
                        notify_join_session_accepted(notify_session->infos);

                    if (_steam_bridge_auto_join_session_id == resp.session_id())
                        _steam_bridge_auto_join_session_id.clear();

                    std::string const host_id = session_owner_product_id(session->infos);
                    if (!host_id.empty() && host_id != user_id)
                    {
                        Session_Infos_Request_pb* req = new Session_Infos_Request_pb;
                        req->set_session_id(resp.session_id());
                        send_session_info_request(host_id, req);
                        APP_LOG(Log::LogLevel::INFO, "Requested session info from host %s for session %s",
                            host_id.c_str(), resp.session_id().c_str());
                        GetEOS_P2P().ensure_peer_connection(host_id);
                    }
                }
                break;
            }
        }
        else if (session != nullptr && session->state == session_state_t::state_e::joined)
        {
            APP_LOG(Log::LogLevel::DEBUG, "Join response ignored: session %s already joined.", resp.session_id().c_str());
        }
        else
        {
            APP_LOG(Log::LogLevel::DEBUG, "Join request not found.");
        }
    }
    else if(session != nullptr && is_player_in_session(user_id, session))
    {// We are not joining, so someone else is joining
        if (reason == EOS_EResult::EOS_Success)
        {// If the user has been accepted in the session
            APP_LOG(Log::LogLevel::DEBUG, "Add new player (%s) to session.", resp.user_id().c_str());
            add_player_to_session(resp.user_id(), session);
            replicate_session_to_all_copies(_sessions, *session);
        }
    }

    return true;
}

bool EOSSDK_Sessions::on_session_invite(Network_Message_pb const& msg, Session_Invite_pb const& invite)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    EOS_ProductUserId target_id = GetProductUserId(msg.source_id());
    
    session_invite_t invite_infos;
    invite_infos.infos = invite.infos();
    invite_infos.invite_id = std::move(generate_account_id());
    invite_infos.peer_id = target_id;

    _session_invites.emplace_back(std::move(invite_infos));
    std::string const& invite_id = _session_invites.back().invite_id;

    std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_Sessions_SessionInviteReceivedCallbackInfo::k_iCallback));
    for (auto& notif : notifs)
    {
        EOS_Sessions_SessionInviteReceivedCallbackInfo& sirci = notif->GetCallback<EOS_Sessions_SessionInviteReceivedCallbackInfo>();
        strncpy(const_cast<char*>(sirci.InviteId), invite_id.c_str(), max_accountid_length);
        sirci.TargetUserId = target_id;

        notif->GetFunc()(notif->GetFuncParam());
    }

    return true;
}

bool EOSSDK_Sessions::on_session_invite_response(Network_Message_pb const& msg, Session_Invite_Response_pb const& resp)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_Sessions_SessionInviteAcceptedCallbackInfo::k_iCallback));
    for (auto& notif : notifs)
    {
        EOS_Sessions_SessionInviteAcceptedCallbackInfo& siacbi = notif->GetCallback<EOS_Sessions_SessionInviteAcceptedCallbackInfo>();

        siacbi.TargetUserId = GetProductUserId(msg.source_id());
        strncpy(const_cast<char*>(siacbi.SessionId), resp.session_id().c_str(), max_accountid_length);

        notif->GetFunc()(notif->GetFuncParam());
    }

    return true;
}

bool EOSSDK_Sessions::on_session_register(Network_Message_pb const& msg, Session_Register_pb const& register_)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    session_state_t* pSession = find_primary_session_for_id(_sessions, register_.session_id());

    if (pSession != nullptr && is_player_registered(msg.source_id(), pSession))
    {
        for (auto const& member_id : register_.member_ids())
        {
            register_player_to_session(member_id, pSession);
            add_player_to_session(member_id, pSession);
        }
        patch_session_public_member_list(pSession->infos);
        replicate_session_to_all_copies(_sessions, *pSession);
        APP_LOG(Log::LogLevel::INFO, "Session register synced from %s for session %s (%d member(s))",
            msg.source_id().c_str(), register_.session_id().c_str(), register_.member_ids_size());
    }

    return true;
}

bool EOSSDK_Sessions::on_session_unregister(Network_Message_pb const& msg, Session_Unregister_pb const& unregister)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    session_state_t* pSession = get_session_by_id(unregister.session_id());

    if (is_player_registered(msg.source_id(), pSession))
    {
        for (auto const& member_id : unregister.member_ids())
        {
            unregister_player_from_session(member_id, pSession);
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//                                 IRunFrame                                 //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Sessions::CBRunFrame()
{
    GLOBAL_LOCK();

    expire_pending_session_searches();
    expire_steam_bridge_session_searches();
    flush_pending_session_searches();

    for (auto it = _session_searchs.begin(); it != _session_searchs.end();)
    {
        if ((*it)->released())
        {
            delete* it;
            it = _session_searchs.erase(it);
        }
        else
        {
            ++it;
        }
    }

    return true;
}

bool EOSSDK_Sessions::RunNetwork(Network_Message_pb const& msg)
{
    switch (msg.messages_case())
    {
        case Network_Message_pb::MessagesCase::kSession:
        {
            if (GetProductUserId(msg.source_id()) == GetEOS_Connect().get_myself()->first)
                return true;

            Session_Message_pb const& session = msg.session();

            switch (session.message_case())
            {
                case Session_Message_pb::MessageCase::kSessionsRequest      : return on_session_info_request(msg, session.sessions_request());
                case Session_Message_pb::MessageCase::kSessionInfos         : return on_session_info(msg, session.session_infos());
                case Session_Message_pb::MessageCase::kSessionDestroy       : return on_session_destroy(msg, session.session_destroy());
                case Session_Message_pb::MessageCase::kSessionJoinRequest   : return on_session_join_request(msg, session.session_join_request());
                case Session_Message_pb::MessageCase::kSessionJoinResponse  : return on_session_join_response(msg, session.session_join_response());
                case Session_Message_pb::MessageCase::kSessionInvite        : return on_session_invite(msg, session.session_invite());
                case Session_Message_pb::MessageCase::kSessionInviteResponse: return on_session_invite_response(msg, session.session_invite_response());
                case Session_Message_pb::MessageCase::kSessionRegister      : return on_session_register(msg, session.session_register());
                case Session_Message_pb::MessageCase::kSessionUnregister    : return on_session_unregister(msg, session.session_unregister());
                default: APP_LOG(Log::LogLevel::WARN, "Unhandled network message %d", session.message_case());
            }
        }
        break;

        case Network_Message_pb::MessagesCase::kSessionsSearch:
        {
            Sessions_Search_Message_pb const& search = msg.sessions_search();

            switch (search.message_case())
            {
                case Sessions_Search_Message_pb::MessageCase::kSearch: return on_sessions_search(msg, search.search());
                case Sessions_Search_Message_pb::MessageCase::kSearchResponse: return on_steam_bridge_session_search_response(msg, search.search_response());
            }
        }
    }
    

    return true;
}

bool EOSSDK_Sessions::RunCallbacks(pFrameResult_t res)
{
    GLOBAL_LOCK();

    switch (res->ICallback())
    {
        case EOS_Sessions_JoinSessionCallbackInfo::k_iCallback:
        {
            auto now = std::chrono::steady_clock::now();
            if ((now - res->created_time) > join_timeout)
            {
                EOS_Sessions_JoinSessionCallbackInfo& jsci = res->GetCallback<EOS_Sessions_JoinSessionCallbackInfo>();
                if (jsci.ResultCode == EOS_EResult::EOS_UnexpectedError)
                {// If its the default error code, set the result code to TimedOut
                    jsci.ResultCode = EOS_EResult::EOS_TimedOut;
                }

                APP_LOG(Log::LogLevel::WARN, "JoinSession timed out: session ResultCode=%d",
                    static_cast<int>(jsci.ResultCode));

                auto join_it = std::find_if(_sessions_join.begin(), _sessions_join.end(), [&res]( std::pair<std::string const, pFrameResult_t> &join )
                {
                    return res == join.second;
                });
                if (join_it != _sessions_join.end())
                {// We found the join callback
                    auto session_it = std::find_if(_sessions.begin(), _sessions.end(), [join_it]( std::pair<const std::string, session_state_t>& item)
                    {// Look if we can find the session
                        return item.second.infos.session_id() == join_it->first;
                    });
                    if (session_it != _sessions.end())
                    {// Session found, we got a timeout so remove it
                        _sessions.erase(session_it);
                    }

                    _sessions_join.erase(join_it);
                }

                res->done = true;
            }
        }
        break;
    }

    return res->done;
}

void EOSSDK_Sessions::FreeCallback(pFrameResult_t res)
{
    GLOBAL_LOCK();

    switch (res->ICallback())
    {
        /////////////////////////////
        //        Callbacks        //
        /////////////////////////////
        case EOS_Sessions_UpdateSessionCallbackInfo::k_iCallback:
        {
            EOS_Sessions_UpdateSessionCallbackInfo& usci = res->GetCallback<EOS_Sessions_UpdateSessionCallbackInfo>();
            delete[]usci.SessionId;
            delete[]usci.SessionName;
        }
        break;
        /////////////////////////////
        //      Notifications      //
        /////////////////////////////
        case EOS_Sessions_SessionInviteReceivedCallbackInfo::k_iCallback:
        {
            EOS_Sessions_SessionInviteReceivedCallbackInfo& callback = res->GetCallback<EOS_Sessions_SessionInviteReceivedCallbackInfo>();
            // Free resources
            delete[]callback.InviteId;
        }
        break;
        case EOS_Sessions_SessionInviteAcceptedCallbackInfo::k_iCallback:
        {
            EOS_Sessions_SessionInviteAcceptedCallbackInfo& callback = res->GetCallback<EOS_Sessions_SessionInviteAcceptedCallbackInfo>();
            // Free resources
            delete[]callback.SessionId;
        }
        break;
        
    }
}

}