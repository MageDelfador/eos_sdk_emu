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
#include "settings.h"
#include "steam_bridge_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace sdk
{

namespace
{
bool lobby_is_game_namespace(Lobby_Infos_pb const& infos);

static std::string lobby_attr_key_lower(std::string const& key)
{
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

static void deduplicate_lobby_attributes(Lobby_Infos_pb& infos)
{
    std::unordered_map<std::string, std::string> canonical_by_lower;
    std::vector<std::string> keys_to_erase;

    for (auto const& attr : infos.attributes())
    {
        std::string const lower = lobby_attr_key_lower(attr.first);
        auto it = canonical_by_lower.find(lower);
        if (it == canonical_by_lower.end())
            canonical_by_lower.emplace(lower, attr.first);
        else
            keys_to_erase.push_back(attr.first);
    }

    for (std::string const& key : keys_to_erase)
        infos.mutable_attributes()->erase(key);
}

static void strip_member_attrs_overlapping_lobby(Lobby_Infos_pb& infos)
{
    std::unordered_set<std::string> lobby_keys_lower;
    for (auto const& attr : infos.attributes())
        lobby_keys_lower.insert(lobby_attr_key_lower(attr.first));

    if (lobby_keys_lower.empty())
        return;

    for (auto& member : *infos.mutable_members())
    {
        for (auto it = member.second.mutable_attributes()->begin();
             it != member.second.mutable_attributes()->end();)
        {
            if (lobby_keys_lower.count(lobby_attr_key_lower(it->first)))
                it = member.second.mutable_attributes()->erase(it);
            else
                ++it;
        }
    }
}

static bool is_lobby_level_only_member_attr_key(std::string const& key)
{
    std::string const lower = lobby_attr_key_lower(key);
    if (lower == "creation_date" || lower == "join_code")
        return true;

    return lower.rfind("redpoint:eos:publicmemberlist:", 0) == 0;
}

static void strip_lobby_level_keys_from_member_attrs(Lobby_Infos_pb& infos)
{
    for (auto& member : *infos.mutable_members())
    {
        for (auto it = member.second.mutable_attributes()->begin();
             it != member.second.mutable_attributes()->end();)
        {
            if (is_lobby_level_only_member_attr_key(it->first))
                it = member.second.mutable_attributes()->erase(it);
            else
                ++it;
        }
    }
}

static void deduplicate_member_attributes(Lobby_Infos_pb& infos)
{
    for (auto& member : *infos.mutable_members())
    {
        std::unordered_map<std::string, std::string> canonical_by_lower;
        std::vector<std::string> keys_to_erase;

        for (auto const& attr : member.second.attributes())
        {
            std::string const lower = lobby_attr_key_lower(attr.first);
            auto it = canonical_by_lower.find(lower);
            if (it == canonical_by_lower.end())
                canonical_by_lower.emplace(lower, attr.first);
            else
                keys_to_erase.push_back(attr.first);
        }

        for (std::string const& key : keys_to_erase)
            member.second.mutable_attributes()->erase(key);
    }
}

static bool lobby_attributes_equal(Lobby_Attribute const& a, Lobby_Attribute const& b)
{
    if (a.visibility_type() != b.visibility_type())
        return false;
    if (a.value().value_case() != b.value().value_case())
        return false;

    switch (a.value().value_case())
    {
        case Lobby_Attr_Value::ValueCase::kB: return a.value().b() == b.value().b();
        case Lobby_Attr_Value::ValueCase::kI: return a.value().i() == b.value().i();
        case Lobby_Attr_Value::ValueCase::kD: return a.value().d() == b.value().d();
        case Lobby_Attr_Value::ValueCase::kS: return a.value().s() == b.value().s();
        default: return true;
    }
}

static bool is_member_writable_lobby_attr_key(std::string const& key)
{
    std::string const lower = lobby_attr_key_lower(key);
    return lower == "is_matchmaking" || lower == "is_game_ongoing";
}

static bool apply_non_owner_lobby_modification(lobby_state_t* pLobby, Lobby_Infos_pb const& modif, std::string const& local_id)
{
    if (pLobby == nullptr || local_id.empty())
        return false;

    if (modif.max_lobby_member() != pLobby->infos.max_lobby_member())
        return false;
    if (modif.bucket_id() != pLobby->infos.bucket_id())
        return false;
    if (modif.permission_level() != pLobby->infos.permission_level())
        return false;

    bool applied = false;
    bool any_lobby_attr_change = false;
    for (auto const& attr : modif.attributes())
    {
        auto existing = pLobby->infos.attributes().find(attr.first);
        if (existing != pLobby->infos.attributes().end() && lobby_attributes_equal(existing->second, attr.second))
            continue;

        any_lobby_attr_change = true;
        if (!is_member_writable_lobby_attr_key(attr.first))
            return false;

        Lobby_Member_Infos_pb& member = (*pLobby->infos.mutable_members())[local_id];
        (*member.mutable_attributes())[attr.first] = attr.second;
        applied = true;
    }

    if (!any_lobby_attr_change)
        return true;

    return applied;
}

void publish_local_join_info(std::string const& lobby_id)
{
    std::string const active_join = GetEOS_Lobby().get_active_join_lobby_id();
    if (!active_join.empty() && !lobby_id.empty() && active_join != lobby_id)
    {
        lobby_state_t* requested = GetEOS_Lobby().get_lobby_by_id(lobby_id);
        if (requested != nullptr && !lobby_is_game_namespace(requested->infos))
            return;
    }

    std::string join_id = active_join;

    if (join_id.empty() && !lobby_id.empty())
    {
        lobby_state_t* lobby = GetEOS_Lobby().get_lobby_by_id(lobby_id);
        if (lobby != nullptr && lobby_is_game_namespace(lobby->infos))
            join_id = lobby_id;
    }

    if (join_id.empty())
        join_id = lobby_id;

    if (join_id.empty())
        return;

    GetEOS_Presence().set_local_join_info(join_id);
    GetEOS_Presence().send_my_presence_info_to_all_peers();
    APP_LOG(Log::LogLevel::INFO, "Published lobby join info in presence (EOS_JoinInfo): %s (requested=%s active_join=%s)",
        join_id.c_str(),
        lobby_id.c_str(),
        GetEOS_Lobby().get_active_join_lobby_id().c_str());
}

bool lobby_attr_bool(Lobby_Infos_pb const& infos, char const* key)
{
    auto it = infos.attributes().find(key);
    return it != infos.attributes().end() && it->second.value().value_case() == Session_Attr_Value::ValueCase::kB && it->second.value().b();
}

void set_lobby_attr_bool(Lobby_Infos_pb& infos, char const* key, bool value)
{
    auto& attr = (*infos.mutable_attributes())[key];
    attr.set_visibility_type(utils::GetEnumValue(EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC));
    attr.mutable_value()->set_b(value);
}

void set_lobby_attr_int(Lobby_Infos_pb& infos, char const* key, int64_t value)
{
    auto& attr = (*infos.mutable_attributes())[key];
    attr.set_visibility_type(utils::GetEnumValue(EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC));
    attr.mutable_value()->set_i(value);
}

void set_lobby_attr_string(Lobby_Infos_pb& infos, char const* key, std::string const& value)
{
    auto& attr = (*infos.mutable_attributes())[key];
    attr.set_visibility_type(utils::GetEnumValue(EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC));
    attr.mutable_value()->set_s(value);
}

void update_lobby_public_member_list(lobby_state_t* lobby)
{
    if (lobby == nullptr)
        return;

    static char const kCurrentMembersKey[] = "Redpoint:EOS:PublicMemberList:CurrentMembers";

    std::string members;
    auto append_member = [&](std::string const& id)
    {
        if (!sdk::looks_like_hex_product_user_id(id))
            return;

        GetProductUserId(id);
        if (members.find(id) != std::string::npos)
            return;

        if (!members.empty())
            members += ',';
        members += id;
    };

    append_member(lobby->infos.owner_id());
    for (auto const& member : lobby->infos.members())
        append_member(member.first);

    if (members.empty())
        return;

    for (auto it = lobby->infos.mutable_attributes()->begin(); it != lobby->infos.mutable_attributes()->end();)
    {
        if (it->first.find("PublicMemberList:CurrentMembers:") != std::string::npos)
            it = lobby->infos.mutable_attributes()->erase(it);
        else
            ++it;
    }

    int index = 0;
    auto add_indexed = [&](std::string const& id)
    {
        if (!sdk::looks_like_hex_product_user_id(id))
            return;
        std::string key = std::string("Redpoint:EOS:PublicMemberList:CurrentMembers:") + std::to_string(index++);
        set_lobby_attr_string(lobby->infos, key.c_str(), id);
    };

    add_indexed(lobby->infos.owner_id());
    for (auto const& member : lobby->infos.members())
        add_indexed(member.first);

    set_lobby_attr_string(lobby->infos, kCurrentMembersKey, members);
    set_lobby_attr_int(lobby->infos, "Redpoint:EOS:PublicMemberList:MaxMembers", index);
    set_lobby_attr_int(lobby->infos, "PlayerCount", index);
}

static void split_csv_product_ids(std::string const& value, std::vector<std::string>& out)
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

static void hydrate_lobby_members_from_public_list(Lobby_Infos_pb& infos)
{
    if (infos.members_size() > 0)
        return;

    static char const kCurrentMembersKey[] = "Redpoint:EOS:PublicMemberList:CurrentMembers";
    static char const kIndexedPrefix[] = "Redpoint:EOS:PublicMemberList:CurrentMembers:";

    std::vector<std::string> ids;
    auto add_id = [&](std::string const& id)
    {
        if (!sdk::looks_like_hex_product_user_id(id))
            return;

        if (std::find(ids.begin(), ids.end(), id) != ids.end())
            return;

        ids.push_back(id);
    };

    auto existing = infos.attributes().find(kCurrentMembersKey);
    if (existing != infos.attributes().end() &&
        existing->second.value().value_case() == Lobby_Attr_Value::ValueCase::kS)
    {
        split_csv_product_ids(existing->second.value().s(), ids);
    }

    for (auto const& attr : infos.attributes())
    {
        if (attr.first.rfind(kIndexedPrefix, 0) != 0)
            continue;
        if (attr.second.value().value_case() != Lobby_Attr_Value::ValueCase::kS)
            continue;

        add_id(attr.second.value().s());
    }

    for (auto const& id : ids)
        (*infos.mutable_members())[id];
}

static void ensure_owner_in_lobby_members(Lobby_Infos_pb& infos)
{
    if (!infos.owner_id().empty())
        (*infos.mutable_members())[infos.owner_id()];
}

static std::string derive_join_code_from_lobby_id(std::string const& lobby_id)
{
    if (lobby_id.size() >= 6)
        return lobby_id.substr(lobby_id.size() - 6);
    return lobby_id;
}

std::string lobby_attr_string(Lobby_Infos_pb const& infos, char const* key)
{
    auto it = infos.attributes().find(key);
    if (it == infos.attributes().end() || it->second.value().value_case() != Lobby_Attr_Value::ValueCase::kS)
        return {};

    return it->second.value().s();
}

bool lobby_is_game_namespace(Lobby_Infos_pb const& infos)
{
    return lobby_attr_string(infos, "Redpoint:EOS:NamespaceFilter") == "GAME";
}

static void ensure_redpoint_unity_lobby_attrs(Lobby_Infos_pb& infos)
{
    bool const own_lobby = !infos.owner_id().empty() &&
        infos.owner_id() == Settings::Inst().productuserid->to_string();

    if (own_lobby && lobby_attr_string(infos, "join_code").empty() && !infos.lobby_id().empty())
    {
        set_lobby_attr_string(infos, "join_code", derive_join_code_from_lobby_id(infos.lobby_id()));
    }

    if (lobby_is_game_namespace(infos) && !lobby_attr_bool(infos, "Redpoint:EOS:Ready"))
        set_lobby_attr_bool(infos, "Redpoint:EOS:Ready", true);
}

static std::vector<std::string> lobby_member_ids_ordered(Lobby_Infos_pb const& infos)
{
    std::vector<std::string> ids;
    auto add_id = [&](std::string const& id)
    {
        if (!sdk::looks_like_hex_product_user_id(id))
            return;

        if (std::find(ids.begin(), ids.end(), id) != ids.end())
            return;

        ids.push_back(id);
    };

    add_id(infos.owner_id());
    for (auto const& member : infos.members())
        add_id(member.first);

    return ids;
}

bool presence_is_actively_hosting(Lobby_Infos_pb const& infos)
{
    if (lobby_attr_string(infos, "StatusTextFormatTextId_Key") == "Lobby")
        return true;

    return lobby_attr_bool(infos, "Custom_joinable");
}
}

void EOSSDK_Lobby::prepare_lobby_infos_for_unity(Lobby_Infos_pb& infos)
{
    hydrate_lobby_members_from_public_list(infos);
    ensure_owner_in_lobby_members(infos);
    ensure_redpoint_unity_lobby_attrs(infos);
    deduplicate_lobby_attributes(infos);
    strip_member_attrs_overlapping_lobby(infos);
    strip_lobby_level_keys_from_member_attrs(infos);
    deduplicate_member_attributes(infos);

    if (!infos.owner_id().empty())
        GetProductUserId(infos.owner_id());
    for (auto const& member : infos.members())
        GetProductUserId(member.first);
}

uint32_t EOSSDK_Lobby::lobby_member_count_for_read(Lobby_Infos_pb const& infos) const
{
    Lobby_Infos_pb copy = infos;
    const_cast<EOSSDK_Lobby*>(this)->prepare_lobby_infos_for_unity(copy);
    return static_cast<uint32_t>(lobby_member_ids_ordered(copy).size());
}

EOS_ProductUserId EOSSDK_Lobby::lobby_member_by_index(Lobby_Infos_pb const& infos, uint32_t member_index) const
{
    Lobby_Infos_pb copy = infos;
    const_cast<EOSSDK_Lobby*>(this)->prepare_lobby_infos_for_unity(copy);
    auto const ids = lobby_member_ids_ordered(copy);
    if (member_index >= ids.size())
        return GetInvalidProductUserId();

    return GetProductUserId(ids[member_index]);
}

decltype(EOSSDK_Lobby::join_timeout) EOSSDK_Lobby::join_timeout;

decltype(EOSSDK_Lobby::join_id) EOSSDK_Lobby::join_id(0);

EOSSDK_Lobby::EOSSDK_Lobby()
{
    GetCB_Manager().register_frame(this);
    GetCB_Manager().register_callbacks(this);
    GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kLobby);
    GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kLobbiesSearch);
}

EOSSDK_Lobby::~EOSSDK_Lobby()
{
    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kLobbiesSearch);
    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kLobby);
    GetCB_Manager().unregister_callbacks(this);
    GetCB_Manager().unregister_frame(this);

    GetCB_Manager().remove_all_notifications(this);
}

lobby_state_t* EOSSDK_Lobby::get_lobby_by_id(std::string const& lobby_id)
{
    auto it = _lobbies.find(lobby_id);
    if (it == _lobbies.end())
        return nullptr;

    return &(it->second._state);
}

static bool lobby_infos_match_session_id(Lobby_Infos_pb const& infos, std::string const& session_id)
{
    if (session_id.empty())
        return false;

    if (infos.lobby_id() == session_id || infos.owner_id() == session_id)
        return true;

    if (Settings::Inst().userid != nullptr && Settings::Inst().userid->IsValid() &&
        session_id == Settings::Inst().userid->to_string())
    {
        return true;
    }

    auto myself = GetEOS_Connect().get_myself();
    if (myself != GetEOS_Connect().get_end_users() && myself->second.infos.userid() == session_id)
        return true;

    for (auto const& attr : infos.attributes())
    {
        if (attr.second.value().value_case() == Lobby_Attr_Value::ValueCase::kS &&
            attr.second.value().s() == session_id)
        {
            return true;
        }
    }

    return false;
}

lobby_state_t* EOSSDK_Lobby::find_lobby_for_session_id(std::string const& session_id)
{
    if (session_id.empty())
        return nullptr;

    if (lobby_state_t* pLobby = get_lobby_by_id(session_id))
    {
        if (i_am_owner(pLobby))
            return pLobby;
    }

    for (auto& lobby : _lobbies)
    {
        lobby_state_t* pLobby = &lobby.second._state;
        if (!i_am_owner(pLobby))
            continue;

        if (lobby_infos_match_session_id(pLobby->infos, session_id))
            return pLobby;
    }

    if (GetEOS_Sessions().get_session_by_id(session_id) != nullptr)
    {
        for (auto& lobby : _lobbies)
        {
            lobby_state_t* pLobby = &lobby.second._state;
            if (i_am_owner(pLobby))
                return pLobby;
        }
    }

    return nullptr;
}

std::vector<lobby_state_t*> EOSSDK_Lobby::get_owned_lobbies()
{
    std::vector<lobby_state_t*> res;
    for (auto& lobby : _lobbies)
    {
        lobby_state_t* pLobby = &lobby.second._state;
        if (i_am_owner(pLobby))
            res.emplace_back(pLobby);
    }
    return res;
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

    //APP_LOG(Log::LogLevel::DEBUG, "Testing Lobby Attr: %s: (lobby)%s %s (search)%s, result: %s", attr_name.c_str(), v1.c_str(), search_attr_to_string(op), std::to_string(v2).c_str(), res ? "true" : "false");
    return res;
}

static bool lobby_search_missing_attribute_matches(Lobby_Search_Parameter const& param)
{
    if (param.param().empty())
        return false;

    for (auto& comparisons : param.param())
    {
        EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
        switch (comparisons.second.value_case())
        {
            case Lobby_Attr_Value::ValueCase::kB:
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_EQUAL && !comparisons.second.b())
                    continue;
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_NOTEQUAL && comparisons.second.b())
                    continue;
                return false;
            case Lobby_Attr_Value::ValueCase::kI:
                if (comp == EOS_EOnlineComparisonOp::EOS_CO_EQUAL && comparisons.second.i() == 0)
                    continue;
                return false;
            case Lobby_Attr_Value::ValueCase::kS:
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

static bool lobby_attr_is_public(Lobby_Attribute const& attr)
{
    return attr.visibility_type() == static_cast<int>(EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC);
}

static std::string lobby_attr_value_debug(Lobby_Attr_Value const& v)
{
    switch (v.value_case())
    {
        case Lobby_Attr_Value::ValueCase::kB:
            return v.b() ? "true" : "false";
        case Lobby_Attr_Value::ValueCase::kI:
            return std::to_string(v.i());
        case Lobby_Attr_Value::ValueCase::kD:
            return std::to_string(v.d());
        case Lobby_Attr_Value::ValueCase::kS:
            return v.s();
        default:
            return "?";
    }
}

static bool parse_attr_bool_string(std::string const& s)
{
    return s == "true" || s == "True" || s == "1";
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

static bool lobby_value_in_search_tokens(std::string const& lobby_value, std::string const& search_value)
{
    if (search_value.find(';') == std::string::npos && search_value.find(',') == std::string::npos)
        return lobby_value == search_value;

    std::vector<std::string> tokens;
    split_search_delimited_values(search_value, tokens);
    for (auto const& token : tokens)
    {
        if (lobby_value == token)
            return true;
    }
    return false;
}

static int count_lobby_value_in_search_tokens(std::string const& lobby_value, std::string const& search_value)
{
    if (search_value.find(';') == std::string::npos && search_value.find(',') == std::string::npos)
        return lobby_value == search_value ? 1 : 0;

    int matches = 0;
    std::vector<std::string> tokens;
    split_search_delimited_values(search_value, tokens);
    for (auto const& token : tokens)
    {
        if (lobby_value == token)
            ++matches;
    }
    return matches;
}

static bool compare_string_search_op(std::string const& lobby_value, EOS_EOnlineComparisonOp op, std::string const& search_value, std::string const& attr_name)
{
    switch (op)
    {
        case EOS_EOnlineComparisonOp::EOS_CO_EQUAL:
            return lobby_value == search_value;
        case EOS_EOnlineComparisonOp::EOS_CO_NOTEQUAL:
            return lobby_value != search_value;
        case EOS_EOnlineComparisonOp::EOS_CO_CONTAINS:
            return lobby_value.find(search_value) != std::string::npos;
        case EOS_EOnlineComparisonOp::EOS_CO_ANYOF:
            return lobby_value_in_search_tokens(lobby_value, search_value);
        case EOS_EOnlineComparisonOp::EOS_CO_NOTANYOF:
            return !lobby_value_in_search_tokens(lobby_value, search_value);
        case EOS_EOnlineComparisonOp::EOS_CO_ONEOF:
            return count_lobby_value_in_search_tokens(lobby_value, search_value) == 1;
        case EOS_EOnlineComparisonOp::EOS_CO_NOTONEOF:
            return count_lobby_value_in_search_tokens(lobby_value, search_value) != 1;
        default:
            return compare_attribute_values(lobby_value, op, search_value, attr_name);
    }
}

static bool compare_lobby_attr_to_search(Lobby_Attribute const& lobby_attr, EOS_EOnlineComparisonOp comp, Lobby_Attr_Value const& search_value, std::string const& attr_name)
{
    auto const& lobby_value = lobby_attr.value();
    if (search_value.value_case() == lobby_value.value_case())
    {
        switch (search_value.value_case())
        {
            case Lobby_Attr_Value::ValueCase::kB:
                return compare_attribute_values(lobby_value.b(), comp, search_value.b(), attr_name);
            case Lobby_Attr_Value::ValueCase::kI:
                return compare_attribute_values(lobby_value.i(), comp, search_value.i(), attr_name);
            case Lobby_Attr_Value::ValueCase::kD:
                return compare_attribute_values(lobby_value.d(), comp, search_value.d(), attr_name);
            case Lobby_Attr_Value::ValueCase::kS:
                return compare_string_search_op(lobby_value.s(), comp, search_value.s(), attr_name);
            default:
                return false;
        }
    }

    // bool <-> string ("true"/"false") mismatch seen in some Unity EOS integrations
    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kB && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kS)
    {
        bool const lobby_bool = parse_attr_bool_string(lobby_value.s());
        bool const search_bool = search_value.b();
        return compare_attribute_values(lobby_bool, comp, search_bool, attr_name);
    }
    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kS && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kB)
    {
        bool const search_bool = parse_attr_bool_string(search_value.s());
        bool const lobby_bool = lobby_value.b();
        return compare_attribute_values(lobby_bool, comp, search_bool, attr_name);
    }
    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kI && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kB)
        return compare_attribute_values(static_cast<int64_t>(lobby_value.b() ? 1 : 0), comp, search_value.i(), attr_name);
    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kB && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kI)
        return compare_attribute_values(lobby_value.i(), comp, static_cast<int64_t>(search_value.b() ? 1 : 0), attr_name);

    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kI && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kS)
    {
        int64_t lobby_i = 0;
        if (try_parse_attr_int64(lobby_value.s(), lobby_i))
        {
            int64_t const search_i = search_value.i();
            return compare_attribute_values(lobby_i, comp, search_i, attr_name);
        }
    }
    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kS && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kI)
    {
        int64_t search_i = 0;
        if (try_parse_attr_int64(search_value.s(), search_i))
        {
            int64_t const lobby_i = lobby_value.i();
            return compare_attribute_values(lobby_i, comp, search_i, attr_name);
        }
    }

    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kS && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kD)
        return compare_string_search_op(std::to_string(lobby_value.d()), comp, search_value.s(), attr_name);
    if (search_value.value_case() == Lobby_Attr_Value::ValueCase::kD && lobby_value.value_case() == Lobby_Attr_Value::ValueCase::kS)
        return compare_string_search_op(lobby_value.s(), comp, std::to_string(search_value.d()), attr_name);

    return false;
}

static bool is_custom_lobby_search_param(std::string const& key)
{
    if (key == "is_matchmaking")
        return false;

    switchstr(key)
    {
        casestr(EOS_LOBBY_SEARCH_MINCURRENTMEMBERS):
        casestr(EOS_LOBBY_SEARCH_MINSLOTSAVAILABLE):
        casestr(EOS_LOBBY_SEARCH_BUCKET_ID):
            return false;
    }
    return true;
}

static bool lobby_search_targets_crossplatform_presence(google::protobuf::Map<std::string, Lobby_Search_Parameter> const& parameters)
{
    auto ns = parameters.find("Redpoint:EOS:NamespaceFilter");
    if (ns != parameters.end())
    {
        for (auto const& comp : ns->second.param())
        {
            if (comp.second.value_case() == Lobby_Attr_Value::ValueCase::kS &&
                comp.second.s() == "CROSSPLATFORMPRESENCE")
                return true;
        }
    }

    auto xp = parameters.find("IsCrossPlatformPresence");
    if (xp != parameters.end())
    {
        for (auto const& comp : xp->second.param())
        {
            if (comp.second.value_case() == Lobby_Attr_Value::ValueCase::kB && comp.second.b())
                return true;
            if (comp.second.value_case() == Lobby_Attr_Value::ValueCase::kS &&
                parse_attr_bool_string(comp.second.s()))
                return true;
        }
    }
    return false;
}

static bool lobby_search_user_id_matches_value(std::string const& user_id, Lobby_Search_Parameter const& param)
{
    if (user_id.empty() || param.param().empty())
        return false;

    for (auto const& comparisons : param.param())
    {
        if (comparisons.second.value_case() != Lobby_Attr_Value::ValueCase::kS)
            continue;

        EOS_EOnlineComparisonOp const comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
        if (compare_string_search_op(user_id, comp, comparisons.second.s(), "UserId"))
            return true;
    }
    return false;
}

static bool lobby_search_user_id_matches_owner(Lobby_Infos_pb const& infos, Lobby_Search_Parameter const& param)
{
    return lobby_search_user_id_matches_value(infos.owner_id(), param);
}

static int lobby_member_count(Lobby_Infos_pb const& infos)
{
    std::vector<std::string> ids;
    auto add_id = [&](std::string const& id)
    {
        if (!sdk::looks_like_hex_product_user_id(id))
            return;
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    };

    add_id(infos.owner_id());
    for (auto const& member : infos.members())
        add_id(member.first);

    return static_cast<int>(ids.size());
}

static void patch_presence_lobby_for_active_game(Lobby_Infos_pb& presence, Lobby_Infos_pb const& game)
{
    set_lobby_attr_string(presence, "ActivityState", "Online");
    set_lobby_attr_string(presence, "StatusTextFormatTextId_Key", "Lobby");
    set_lobby_attr_string(presence, "StatusTextFormatTextId_Namespace", "PresenceStatus");
    set_lobby_attr_string(presence, "Custom_steam_display", "#Status_InLobby");
    set_lobby_attr_bool(presence, "Custom_joinable", true);
    set_lobby_attr_int(presence, "StatusTextFormatArgument_Value_joinable", 1);

    int const party_members = lobby_member_count(game);
    if (party_members > 0)
        set_lobby_attr_int(presence, "AdvertisedPartyCurrentMemberCount", party_members);

    session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession");
    if (game_session == nullptr || game_session->infos.session_id().empty())
        game_session = GetEOS_Sessions().get_session_by_name("Default");

    if (game_session == nullptr || game_session->infos.session_id().empty())
    {
        std::string const from_game = lobby_attr_string(game, "Redpoint:EOS:GameSessionId");
        if (!from_game.empty())
        {
            set_lobby_attr_string(presence, "Redpoint:EOS:GameSessionId", from_game);
            set_lobby_attr_string(presence, "AdvertisedSessionId", std::string("Session:") + from_game);
            set_lobby_attr_string(presence, "Redpoint:EOS:PartyLobbyId", game.lobby_id());
        }
        return;
    }

    std::string const& session_id = game_session->infos.session_id();
    set_lobby_attr_string(presence, "Redpoint:EOS:GameSessionId", session_id);
    set_lobby_attr_string(presence, "AdvertisedSessionId", std::string("Session:") + session_id);
    set_lobby_attr_string(presence, "Redpoint:EOS:PartyLobbyId", game.lobby_id());
}

static void patch_remote_presence_join_attrs(Lobby_Infos_pb& presence)
{
    if (!lobby_attr_bool(presence, "IsCrossPlatformPresence"))
        return;

    std::string advertised = lobby_attr_string(presence, "AdvertisedSessionId");
    if (advertised.empty())
    {
        std::string const session_id = lobby_attr_string(presence, "Redpoint:EOS:GameSessionId");
        if (!session_id.empty())
            set_lobby_attr_string(presence, "AdvertisedSessionId", std::string("Session:") + session_id);
    }

    if (!lobby_attr_string(presence, "AdvertisedSessionId").empty() ||
        lobby_attr_string(presence, "ActivityState") == "Online")
    {
        if (lobby_attr_string(presence, "StatusTextFormatTextId_Key") == "Lobby" ||
            lobby_attr_bool(presence, "Custom_joinable"))
        {
            set_lobby_attr_bool(presence, "Custom_joinable", true);
            set_lobby_attr_int(presence, "StatusTextFormatArgument_Value_joinable", 1);
        }
    }
}

static bool response_has_owner_lobby(Lobbies_Search_response_pb const& resp, std::string const& owner_id)
{
    for (auto const& lobby : resp.lobbies())
    {
        if (!owner_id.empty() && lobby.owner_id() == owner_id)
            return true;
    }
    return false;
}

std::vector<lobby_state_t*> EOSSDK_Lobby::get_lobbies_from_attributes(google::protobuf::Map<std::string, Lobby_Search_Parameter> const& parameters)
{
    std::vector<lobby_state_t*> res;
    for (auto& lobby : _lobbies)
    {
        bool found = true;
        for (auto& param : parameters)
        {
            // Well known parameters
            switchstr(param.first)
            {
                casestr(EOS_LOBBY_SEARCH_MINCURRENTMEMBERS) :
                {
                    auto it = param.second.param().find(utils::GetEnumValue(EOS_EOnlineComparisonOp::EOS_CO_GREATERTHANOREQUAL));
                    if (it != param.second.param().end())
                    {// Wrong comparison type should never happen, it's already tested in the search.
                        switch(it->second.value_case())
                        {// Wrong parameter type should never happen, it's already tested in the search.
                            case Lobby_Attr_Value::ValueCase::kI:
                            {
                                int64_t lobby_current_members = lobby.second._state.infos.members_size();
                                int64_t min_current_members = it->second.i();
                                found = compare_attribute_values(lobby_current_members, EOS_EOnlineComparisonOp::EOS_CO_GREATERTHANOREQUAL, min_current_members, param.first);
                            }
                            break;

                            default:
                            {
                                APP_LOG(Log::LogLevel::INFO, "Triied " EOS_LOBBY_SEARCH_MINCURRENTMEMBERS " with a comparator different than EOS_CO_GREATERTHANOREQUAL: FIX ME!");
                                found = false;
                            }
                        }
                    }
                }
                break;

                casestr(EOS_LOBBY_SEARCH_MINSLOTSAVAILABLE) :
                {
                    auto it = param.second.param().find(utils::GetEnumValue(EOS_EOnlineComparisonOp::EOS_CO_GREATERTHANOREQUAL));
                    if (it != param.second.param().end())
                    {// Wrong comparison type should never happen, it's already tested in the search.
                        switch(it->second.value_case())
                        {// Wrong parameter type should never happen, it's already tested in the search.
                            case Lobby_Attr_Value::ValueCase::kI:
                            {
                                int64_t lobby_slots_available = static_cast<int64_t>(lobby.second._state.infos.max_lobby_member()) - lobby.second._state.infos.members_size();
                                int64_t min_slots_available = it->second.i();
                                found = compare_attribute_values(lobby_slots_available, EOS_EOnlineComparisonOp::EOS_CO_GREATERTHANOREQUAL, min_slots_available, param.first);
                            }
                            break;

                            default:
                            {
                                APP_LOG(Log::LogLevel::INFO, "Triied " EOS_LOBBY_SEARCH_MINSLOTSAVAILABLE " with a comparator different than EOS_CO_GREATERTHANOREQUAL: FIX ME!");
                                found = false;
                            }
                        }
                    }
                }
                break;

                casestr(EOS_LOBBY_SEARCH_BUCKET_ID) :
                {
                    for (auto& comparisons : param.second.param())
                    {
                        EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
                        switch (comparisons.second.value_case())
                        {
                            case Lobby_Attr_Value::ValueCase::kS:
                            {
                                std::string const& s_lobby = lobby.second._state.infos.bucket_id();
                                std::string const& s_search = comparisons.second.s();
                                found = compare_attribute_values(s_lobby, comp, s_search, param.first);
                            }
                            break;
                            default:
                                found = false;
                        }
                        if (!found)
                            break;
                    }
                }
                break;

                default:
                    // Spell Brigade and similar titles: host invite lobby vs client matchmaking search disagree on this flag.
                    if (param.first == "is_matchmaking")
                        continue;

                    if (param.first == "UserId")
                    {
                        if (lobby_search_user_id_matches_owner(lobby.second._state.infos, param.second))
                            continue;
                    }

                    auto it = lobby.second._state.infos.attributes().find(param.first);
                    if (it == lobby.second._state.infos.attributes().end() || !lobby_attr_is_public(it->second))
                    {
                        found = lobby_search_missing_attribute_matches(param.second);
                    }
                    else
                    {
                        for (auto& comparisons : param.second.param())
                        {
                            EOS_EOnlineComparisonOp comp = static_cast<EOS_EOnlineComparisonOp>(comparisons.first);
                            found = compare_lobby_attr_to_search(it->second, comp, comparisons.second, param.first);
                            if (!found)
                            {
                                APP_LOG(Log::LogLevel::DEBUG,
                                    "Lobby search attr mismatch key='%s' lobby=%s search=%s visibility=%d",
                                    param.first.c_str(),
                                    lobby_attr_value_debug(it->second.value()).c_str(),
                                    lobby_attr_value_debug(comparisons.second).c_str(),
                                    it->second.visibility_type());
                                break;
                            }
                        }
                    }
            }
            if (found == false)
            {
                APP_LOG(Log::LogLevel::DEBUG, "Lobby search param '%s' didn't match lobby %s",
                    param.first.c_str(), lobby.second._state.infos.lobby_id().c_str());
                break;
            }
        }

        if (found)
        {
            res.emplace_back(&lobby.second._state);
        }
    }

    return res;
}

void EOSSDK_Lobby::refresh_owned_presence_for_active_game()
{
    std::string const active_join = get_active_join_lobby_id();
    lobby_state_t* game_lobby = active_join.empty() ? nullptr : get_lobby_by_id(active_join);
    if (game_lobby == nullptr || !lobby_is_game_namespace(game_lobby->infos))
        return;

    session_state_t* game_session = GetEOS_Sessions().get_session_by_name("GameSession");
    if (game_session == nullptr)
        game_session = GetEOS_Sessions().get_session_by_name("Default");

    std::string const local_id = Settings::Inst().productuserid->to_string();
    if (game_session != nullptr && !game_session->infos.session_id().empty())
    {
        EOS_ProductUserId const session_owner = owner_user_id_for_session_infos(game_session->infos);
        if (session_owner != nullptr && session_owner->to_string() != local_id)
        {
            session_state_t* owned = GetEOS_Sessions().get_session_by_name("Default");
            if (owned != nullptr)
            {
                EOS_ProductUserId const owned_id = owner_user_id_for_session_infos(owned->infos);
                if (owned_id != nullptr && owned_id->to_string() == local_id)
                    game_session = owned;
                else
                    game_session = nullptr;
            }
            else
            {
                game_session = nullptr;
            }
        }
    }

    if (game_session != nullptr && !game_session->infos.session_id().empty())
    {
        set_lobby_attr_string(game_lobby->infos, "Redpoint:EOS:GameSessionId", game_session->infos.session_id());
        set_lobby_attr_string(game_lobby->infos, "AdvertisedSessionId",
            std::string("Session:") + game_session->infos.session_id());
    }

    for (auto& lobby : _lobbies)
    {
        if (!i_am_owner(&lobby.second._state))
            continue;

        if (!lobby_attr_bool(lobby.second._state.infos, "IsCrossPlatformPresence"))
            continue;

        patch_presence_lobby_for_active_game(lobby.second._state.infos, game_lobby->infos);
        patch_crossplatform_joinable_lobby(lobby.second._state.infos);

        std::string const advertised = lobby_attr_string(lobby.second._state.infos, "AdvertisedSessionId");
        if (advertised.empty())
            return;

        if (advertised == _last_presence_advertised_session_id)
            return;

        _last_presence_advertised_session_id = advertised;
        send_lobby_update(&lobby.second._state);
        flush_pending_lobby_searches();

        APP_LOG(Log::LogLevel::INFO,
            "Refreshed presence lobby %s for active game %s (joinable=%d AdvertisedSessionId=%s)",
            lobby.second._state.infos.lobby_id().c_str(),
            game_lobby->infos.lobby_id().c_str(),
            lobby_attr_bool(lobby.second._state.infos, "Custom_joinable") ? 1 : 0,
            advertised.c_str());
        return;
    }
}

lobby_state_t* EOSSDK_Lobby::find_owner_game_lobby(std::string const& owner_id)
{
    if (owner_id.empty())
        return nullptr;

    std::string const active_join = get_active_join_lobby_id();
    if (!active_join.empty())
    {
        if (lobby_state_t* active = get_lobby_by_id(active_join))
        {
            if (active->infos.owner_id() == owner_id && lobby_is_game_namespace(active->infos))
                return active;
        }
    }

    lobby_state_t* fallback = nullptr;
    for (auto& lobby : _lobbies)
    {
        lobby_state_t* pLobby = &lobby.second._state;
        if (pLobby->infos.owner_id() != owner_id)
            continue;
        if (!lobby_is_game_namespace(pLobby->infos))
            continue;

        if (lobby_attr_bool(pLobby->infos, "Redpoint:EOS:Ready"))
            return pLobby;

        if (fallback == nullptr)
            fallback = pLobby;
    }

    return fallback;
}

Lobby_Infos_pb EOSSDK_Lobby::resolve_join_party_lobby(Lobby_Infos_pb const& infos)
{
    if (lobby_is_game_namespace(infos))
        return infos;

    if (lobby_state_t* game_lobby = find_owner_game_lobby(infos.owner_id()))
    {
        if (game_lobby->infos.lobby_id() != infos.lobby_id())
        {
            APP_LOG(Log::LogLevel::INFO,
                "resolve_join_party_lobby: party=%s -> game lobby=%s owner=%s",
                infos.lobby_id().c_str(),
                game_lobby->infos.lobby_id().c_str(),
                infos.owner_id().c_str());
        }
        return game_lobby->infos;
    }

    std::string const party_id = lobby_attr_string(infos, "Redpoint:EOS:PartyLobbyId");
    if (!party_id.empty())
    {
        if (lobby_state_t* game_lobby = get_lobby_by_id(party_id))
            return game_lobby->infos;
    }

    return infos;
}

std::string EOSSDK_Lobby::resolve_steam_bridge_session_host_peer(Lobby_Infos_pb const& infos, std::string const& response_peer)
{
    Lobby_Infos_pb const target = resolve_join_party_lobby(infos);

    if (lobby_is_game_namespace(target) && !target.owner_id().empty())
        return target.owner_id();

    if (!target.owner_id().empty())
        return target.owner_id();

    if (!infos.owner_id().empty())
        return infos.owner_id();

    return response_peer;
}

void EOSSDK_Lobby::patch_crossplatform_joinable_lobby(Lobby_Infos_pb& infos) const
{
    if (!lobby_attr_bool(infos, "IsCrossPlatformPresence"))
        return;

    if (lobby_attr_string(infos, "StatusTextFormatTextId_Key") == "Lobby")
    {
        set_lobby_attr_bool(infos, "Custom_joinable", true);
        set_lobby_attr_int(infos, "StatusTextFormatArgument_Value_joinable", 1);
        return;
    }

    bool const own_lobby = !infos.owner_id().empty() &&
        infos.owner_id() == Settings::Inst().productuserid->to_string();

    if (own_lobby && presence_is_actively_hosting(infos))
    {
        std::string const active_join = get_active_join_lobby_id();
        if (!active_join.empty() && active_join != infos.lobby_id())
        {
            auto game_it = _lobbies.find(active_join);
            if (game_it != _lobbies.end() && lobby_is_game_namespace(game_it->second._state.infos))
            {
                set_lobby_attr_bool(infos, "Custom_joinable", true);
                set_lobby_attr_int(infos, "StatusTextFormatArgument_Value_joinable", 1);
            }
        }
        return;
    }

    if (!own_lobby && lobby_attr_bool(infos, "Custom_joinable"))
    {
        set_lobby_attr_bool(infos, "Custom_joinable", true);
        set_lobby_attr_int(infos, "StatusTextFormatArgument_Value_joinable", 1);
    }
    else if (lobby_attr_string(infos, "ActivityState") == "Online")
    {
        // Online/MainMenu presence is displayable but not necessarily joinable.
        // Active game presence is patched to joinable by patch_presence_lobby_for_active_game().
    }
}

std::string EOSSDK_Lobby::get_active_join_lobby_id() const
{
    {
        std::string game_lobby;
        int game_score = -1;

        for (auto const& entry : _lobbies)
        {
            if (!i_am_owner(&entry.second._state))
                continue;

            Lobby_Infos_pb const& infos = entry.second._state.infos;
            if (!lobby_is_game_namespace(infos) || !lobby_attr_bool(infos, "Redpoint:EOS:Ready"))
                continue;

            int score = static_cast<int>(infos.attributes_size());
            if (infos.brtcroomenabled())
                score += 1000;

            if (score > game_score)
            {
                game_score = score;
                game_lobby = infos.lobby_id();
            }
        }

        if (!game_lobby.empty())
            return game_lobby;
    }

    std::string const presence = get_active_crossplatform_lobby_id();
    bool actively_hosting = false;

    if (!presence.empty())
    {
        auto presence_it = _lobbies.find(presence);
        if (presence_it != _lobbies.end())
            actively_hosting = presence_is_actively_hosting(presence_it->second._state.infos);
    }

    if (actively_hosting)
    {
        std::string game_lobby;
        int game_score = -1;

        for (auto const& lobby : _lobbies)
        {
            if (!i_am_owner(&lobby.second._state))
                continue;

            Lobby_Infos_pb const& infos = lobby.second._state.infos;
            if (!lobby_is_game_namespace(infos) || !lobby_attr_bool(infos, "Redpoint:EOS:Ready") || !infos.brtcroomenabled())
                continue;

            int score = infos.attributes_size();
            if (score > game_score)
            {
                game_score = score;
                game_lobby = infos.lobby_id();
            }
        }

        if (!game_lobby.empty())
            return game_lobby;
    }

    return presence;
}

std::string EOSSDK_Lobby::get_active_crossplatform_lobby_id() const
{
    auto pick_best = [this](bool require_ready) -> std::string
    {
        std::string best;
        int best_score = -1;

        for (auto const& lobby : _lobbies)
        {
            if (!i_am_owner(&lobby.second._state))
                continue;

            Lobby_Infos_pb const& infos = lobby.second._state.infos;
            if (!lobby_attr_bool(infos, "IsCrossPlatformPresence"))
                continue;

            if (require_ready && !lobby_attr_bool(infos, "Redpoint:EOS:Ready"))
                continue;

            int score = infos.attributes_size();
            if (!infos.brtcroomenabled())
                score += 100;

            if (score > best_score)
            {
                best_score = score;
                best = infos.lobby_id();
            }
        }

        return best;
    };

    std::string const ready = pick_best(true);
    if (!ready.empty())
        return ready;

    // Fallback: game may not have set Redpoint:EOS:Ready yet during lobby creation.
    return pick_best(false);
}

void EOSSDK_Lobby::add_lobby_to_search_response(Lobbies_Search_response_pb* resp, Lobby_Infos_pb const& infos)
{
    Lobby_Infos_pb copy = infos;

    std::string const active_join = get_active_join_lobby_id();
    if (!active_join.empty() && lobby_attr_bool(copy, "IsCrossPlatformPresence"))
    {
        lobby_state_t* game_lobby = get_lobby_by_id(active_join);
        if (game_lobby != nullptr && lobby_is_game_namespace(game_lobby->infos))
            patch_presence_lobby_for_active_game(copy, game_lobby->infos);
    }

    patch_crossplatform_joinable_lobby(copy);
    prepare_lobby_infos_for_unity(copy);
    *resp->mutable_lobbies()->Add() = std::move(copy);
}

void EOSSDK_Lobby::fill_lobbies_search_response(Lobbies_Search_response_pb* resp, Lobbies_Search_pb const& search)
{
    resp->set_search_id(search.search_id());

    if (search.parameters_size() > 0)
    {
        std::vector<lobby_state_t*> lobbies = std::move(get_lobbies_from_attributes(search.parameters()));
        std::string const active_id = get_active_crossplatform_lobby_id();
        auto user_id_param = search.parameters().find("UserId");
        for (auto& lobby : lobbies)
        {
            // Allow non-owned presence lobbies when searching by target UserId (reversed join support)
            if (!i_am_owner(lobby))
            {
                if (!lobby_attr_bool(lobby->infos, "IsCrossPlatformPresence"))
                    continue;
                if (user_id_param == search.parameters().end() ||
                    !lobby_search_user_id_matches_owner(lobby->infos, user_id_param->second))
                    continue;
            }

            // When searching by explicit UserId (presence search for a friend), do not exclude other presence lobbies
            // even if we have our own active crossplatform lobby. This is required for crossplay-enabled games
            // and reversed join scenarios (the other player created the presence lobby).
            if (user_id_param == search.parameters().end() &&
                !active_id.empty() && lobby->infos.lobby_id() != active_id &&
                lobby_attr_bool(lobby->infos, "IsCrossPlatformPresence"))
                continue;

            add_lobby_to_search_response(resp, lobby->infos);
        }

        if (lobby_search_targets_crossplatform_presence(search.parameters()) &&
            !response_has_owner_lobby(*resp, Settings::Inst().productuserid->to_string()))
        {
            std::string const active_join = get_active_join_lobby_id();
            lobby_state_t* game_lobby = active_join.empty() ? nullptr : get_lobby_by_id(active_join);
            if (game_lobby != nullptr && lobby_is_game_namespace(game_lobby->infos))
            {
                for (auto& lobby : _lobbies)
                {
                    if (!i_am_owner(&lobby.second._state))
                        continue;

                    Lobby_Infos_pb const& infos = lobby.second._state.infos;
                    if (!lobby_attr_bool(infos, "IsCrossPlatformPresence"))
                        continue;

                    auto user_id_param = search.parameters().find("UserId");
                    if (user_id_param != search.parameters().end() &&
                        !lobby_search_user_id_matches_owner(infos, user_id_param->second))
                        continue;

                    add_lobby_to_search_response(resp, infos);
                    break;
                }
            }
        }
    }
    else if (!search.lobby_id().empty())
    {
        lobby_state_t* pLobby = get_lobby_by_id(search.lobby_id());
        if (pLobby != nullptr && i_am_owner(pLobby))
        {
            add_lobby_to_search_response(resp, pLobby->infos);
        }
        else
        {
            for (auto& lobby : _lobbies)
            {
                if (i_am_owner(&lobby.second._state))
                {
                    add_lobby_to_search_response(resp, lobby.second._state.infos);
                    APP_LOG(Log::LogLevel::INFO,
                        "Lobbies_Search: stale lobby_id='%s' -> returning owned lobby_id='%s'",
                        search.lobby_id().c_str(),
                        lobby.second._state.infos.lobby_id().c_str());
                    break;
                }
            }
        }
    }
    else if (GetProductUserId(search.target_id()) == GetEOS_Connect().get_myself()->first)
    {
        if (lobby_state_t* game_lobby = find_owner_game_lobby(search.target_id()))
        {
            add_lobby_to_search_response(resp, game_lobby->infos);
        }
        else
        {
            std::string const active_id = get_active_crossplatform_lobby_id();
            if (!active_id.empty())
            {
                lobby_state_t* pLobby = get_lobby_by_id(active_id);
                if (pLobby != nullptr)
                    add_lobby_to_search_response(resp, pLobby->infos);
            }
            else
            {
                for (auto& lobby : _lobbies)
                {
                    if (i_am_owner(&lobby.second._state))
                        add_lobby_to_search_response(resp, lobby.second._state.infos);
                }
            }
        }
    }
    else if (!search.target_id().empty())
    {
        if (lobby_state_t* game_lobby = find_owner_game_lobby(search.target_id()))
        {
            add_lobby_to_search_response(resp, game_lobby->infos);
        }
        else
        {
            for (auto& lobby : _lobbies)
            {
                Lobby_Infos_pb const& infos = lobby.second._state.infos;
                if (!lobby_attr_bool(infos, "IsCrossPlatformPresence"))
                    continue;
                if (infos.owner_id() != search.target_id())
                    continue;

                add_lobby_to_search_response(resp, infos);
                break;
            }
        }
    }
}

bool EOSSDK_Lobby::should_defer_attribute_lobby_search(Lobbies_Search_pb const& search)
{
    if (search.parameters().empty())
        return false;

    if (lobby_search_targets_crossplatform_presence(search.parameters()))
    {
        auto user_id_param = search.parameters().find("UserId");
        std::string const local_id = Settings::Inst().productuserid->to_string();
        bool const targets_local_user = user_id_param == search.parameters().end() ||
            lobby_search_user_id_matches_value(local_id, user_id_param->second);

        if (targets_local_user)
        {
            bool has_owned_presence = false;
            for (auto const& lobby : _lobbies)
            {
                if (!i_am_owner(&lobby.second._state))
                    continue;

                Lobby_Infos_pb const& infos = lobby.second._state.infos;
                if (lobby_is_game_namespace(infos) || !lobby_attr_bool(infos, "IsCrossPlatformPresence"))
                    continue;

                has_owned_presence = true;
                break;
            }

            if (!has_owned_presence)
            {
                APP_LOG(Log::LogLevel::INFO, "Lobby search deferred: local cross-platform presence lobby not created yet");
                return true;
            }
        }
    }

    for (auto& lobby : _lobbies)
    {
        if (!i_am_owner(&lobby.second._state))
            continue;

        Lobby_Infos_pb const& infos = lobby.second._state.infos;
        if (lobby_is_game_namespace(infos) || !lobby_attr_bool(infos, "IsCrossPlatformPresence"))
            continue;

        for (auto& param : search.parameters())
        {
            if (!is_custom_lobby_search_param(param.first))
                continue;

            auto it = lobby.second._state.infos.attributes().find(param.first);
            if (it != lobby.second._state.infos.attributes().end() && lobby_attr_is_public(it->second))
                continue;

            if (!lobby_search_missing_attribute_matches(param.second))
                return true;
        }
    }

    return false;
}

void EOSSDK_Lobby::queue_pending_lobby_search(Network::peer_t const& peer_id, Lobbies_Search_pb const& search)
{
    pending_lobby_search_t pending;
    pending.peer_id = peer_id;
    pending.search = search;
    pending.created = std::chrono::steady_clock::now();
    _pending_lobby_searches.emplace_back(std::move(pending));

    APP_LOG(Log::LogLevel::INFO, "Lobby search deferred (waiting for lobby attributes) for peer=%s search_id=%llu",
        peer_id.c_str(), static_cast<unsigned long long>(search.search_id()));
}

void EOSSDK_Lobby::flush_pending_lobby_searches()
{
    for (auto it = _pending_lobby_searches.begin(); it != _pending_lobby_searches.end();)
    {
        Lobbies_Search_response_pb* resp = new Lobbies_Search_response_pb;
        fill_lobbies_search_response(resp, it->search);

        if (resp->lobbies_size() > 0)
        {
            APP_LOG(Log::LogLevel::INFO, "Lobby search deferred reply: matched %u lobbies for peer=%s",
                static_cast<unsigned>(resp->lobbies_size()), it->peer_id.c_str());
            send_lobbies_search_response(it->peer_id, resp);
            it = _pending_lobby_searches.erase(it);
        }
        else
        {
            delete resp;
            ++it;
        }
    }
}

void EOSSDK_Lobby::expire_pending_lobby_searches()
{
    auto const now = std::chrono::steady_clock::now();
    for (auto it = _pending_lobby_searches.begin(); it != _pending_lobby_searches.end();)
    {
        if ((now - it->created) > pending_search_timeout)
        {
            Lobbies_Search_response_pb* resp = new Lobbies_Search_response_pb;
            fill_lobbies_search_response(resp, it->search);
            APP_LOG(Log::LogLevel::INFO, "Lobby search deferred timeout: matched %u lobbies for peer=%s",
                static_cast<unsigned>(resp->lobbies_size()), it->peer_id.c_str());
            send_lobbies_search_response(it->peer_id, resp);
            it = _pending_lobby_searches.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool EOSSDK_Lobby::add_member_to_lobby(std::string const& member, lobby_state_t* lobby)
{
    APP_LOG(Log::LogLevel::TRACE, "");
    assert(lobby != nullptr);

    auto& members = *lobby->infos.mutable_members();
    auto it = members.find(member);
    if (it != members.end())
    {
        return false;
    }

    (*lobby->infos.mutable_members())[member];
    return true;
}

bool EOSSDK_Lobby::remove_member_from_lobby(std::string const& member, lobby_state_t* lobby)
{
    APP_LOG(Log::LogLevel::TRACE, "");
    assert(lobby != nullptr);

    auto& members = *lobby->infos.mutable_members();
    auto it = members.find(member);
    if (it != members.end())
    {
        members.erase(it);
        return true;
    }

    return false;
}

bool EOSSDK_Lobby::is_member_in_lobby(std::string const& member, lobby_state_t* lobby)
{
    assert(lobby != nullptr);

    auto& members = lobby->infos.members();
    return (members.find(member) != members.end());
}

bool EOSSDK_Lobby::is_peer_member_of_my_owned_lobby(std::string const& peer_id) const
{
    if (peer_id.empty())
        return false;

    for (auto const& entry : _lobbies)
    {
        lobby_state_t const& lobby = entry.second._state;
        if (!i_am_owner(&lobby))
            continue;

        if (lobby.infos.owner_id() == peer_id)
            return true;

        if (lobby.infos.members().find(peer_id) != lobby.infos.members().end())
            return true;
    }

    return false;
}

bool EOSSDK_Lobby::i_am_owner(lobby_state_t const* lobby) const
{
    assert(lobby != nullptr);
    
    return (GetProductUserId(lobby->infos.owner_id()) == GetEOS_Connect().get_myself()->first);    
}

void EOSSDK_Lobby::notify_lobby_update(lobby_state_t* lobby)
{
    assert(lobby != nullptr);

    std::vector<pFrameResult_t> notifs = GetCB_Manager().get_notifications(this, EOS_Lobby_LobbyUpdateReceivedCallbackInfo::k_iCallback);
    for (auto& notif : notifs)
    {
        EOS_Lobby_LobbyUpdateReceivedCallbackInfo const& src = notif->GetCallback<EOS_Lobby_LobbyUpdateReceivedCallbackInfo>();
        pFrameResult_t queued(new FrameResult);
        EOS_Lobby_LobbyUpdateReceivedCallbackInfo& lurci = queued->CreateCallback<EOS_Lobby_LobbyUpdateReceivedCallbackInfo>(notif->GetFunc());
        lurci.ClientData = src.ClientData;
        {
            size_t const len = lobby->infos.lobby_id().length() + 1;
            char* str = new char[len];
            strncpy(str, lobby->infos.lobby_id().c_str(), len);
            lurci.LobbyId = str;
        }
        queued->done = true;
        GetCB_Manager().add_callback(this, queued);
    }
}

void EOSSDK_Lobby::notify_lobby_member_status_update(std::string const& member, EOS_ELobbyMemberStatus new_status, lobby_state_t* lobby)
{
    assert(lobby != nullptr);

    EOS_ProductUserId member_id = GetProductUserId(member);
    std::vector<pFrameResult_t> notifs = GetCB_Manager().get_notifications(this, EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo::k_iCallback);
    for (auto& notif : notifs)
    {
        EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo const& src = notif->GetCallback<EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo>();
        pFrameResult_t queued(new FrameResult);
        EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo& lmsrci = queued->CreateCallback<EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo>(notif->GetFunc());
        lmsrci.ClientData = src.ClientData;
        {
            size_t const len = lobby->infos.lobby_id().length() + 1;
            char* str = new char[len];
            strncpy(str, lobby->infos.lobby_id().c_str(), len);
            lmsrci.LobbyId = str;
        }
        lmsrci.TargetUserId = member_id;
        lmsrci.CurrentStatus = new_status;
        queued->done = true;
        GetCB_Manager().add_callback(this, queued);
    }
}

void EOSSDK_Lobby::notify_lobby_member_update(std::string const& member, lobby_state_t* lobby)
{
    assert(lobby != nullptr);

    EOS_ProductUserId member_id = GetProductUserId(member);
    std::vector<pFrameResult_t> notifs(std::move(GetCB_Manager().get_notifications(this, EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo::k_iCallback)));
    for (auto& notif : notifs)
    {
        EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo& lmurci = notif->GetCallback<EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo>();
        strncpy(const_cast<char*>(lmurci.LobbyId), lobby->infos.lobby_id().c_str(), max_accountid_length);
        lmurci.TargetUserId = member_id;
        notif->GetFunc()(notif->GetFuncParam());
    }
}

void EOSSDK_Lobby::notify_lobby_invite_received(std::string const& invite_id, EOS_ProductUserId from_id)
{
    std::vector<pFrameResult_t> notifs(std::move(GetCB_Manager().get_notifications(this, EOS_Lobby_LobbyInviteReceivedCallbackInfo::k_iCallback)));
    for (auto& notif : notifs)
    {
        EOS_Lobby_LobbyInviteReceivedCallbackInfo& lirci = notif->GetCallback<EOS_Lobby_LobbyInviteReceivedCallbackInfo>();
        strncpy(const_cast<char*>(lirci.InviteId), invite_id.c_str(), max_accountid_length);
        lirci.TargetUserId = from_id;
        notif->GetFunc()(notif->GetFuncParam());
    }
}

void EOSSDK_Lobby::complete_pending_join_success(Lobby_Join_Response_pb const& resp, Network_Message_pb const& msg, lobby_join_t& join)
{
    if (join.cb != nullptr)
        join.cb->done = true;

    auto& lobby = _lobbies[resp.infos().lobby_id()];
    lobby._state.state = lobby_state_t::joined;
    lobby._state.infos = resp.infos();
    std::string const local_id = Settings::Inst().productuserid->to_string();
    add_member_to_lobby(local_id, &lobby._state);
    prepare_lobby_infos_for_unity(lobby._state.infos);
    update_lobby_public_member_list(&lobby._state);
    GetEOS_Sessions().sync_session_from_lobby(lobby._state.infos);
    notify_lobby_member_status_update(local_id, EOS_ELobbyMemberStatus::EOS_LMS_JOINED, &lobby._state);
    notify_lobby_update(&lobby._state);

    if (join.cb == nullptr)
    {
        APP_LOG(Log::LogLevel::INFO, "Steam bridge network join success: lobby=%s from=%s members=%d",
            resp.infos().lobby_id().c_str(),
            msg.source_id().c_str(),
            lobby._state.infos.members_size());
        GetEOS_Sessions().on_steam_bridge_party_joined();
        return;
    }

    switch (join.kind)
    {
        case lobby_join_kind_t::join_lobby:
        {
            EOS_Lobby_JoinLobbyCallbackInfo& jlci = join.cb->GetCallback<EOS_Lobby_JoinLobbyCallbackInfo>();
            jlci.ResultCode = EOS_EResult::EOS_Success;
            APP_LOG(Log::LogLevel::INFO, "JoinLobby success: lobby=%s from=%s",
                resp.infos().lobby_id().c_str(), msg.source_id().c_str());
        }
        break;

        case lobby_join_kind_t::join_lobby_by_id:
        {
            auto& jlbci = *reinterpret_cast<EOS_Lobby_JoinLobbyByIdCallbackInfo*>(join.cb->GetFuncParam());
            jlbci.ResultCode = EOS_EResult::EOS_Success;
            APP_LOG(Log::LogLevel::INFO, "JoinLobbyById success: lobby=%s from=%s",
                resp.infos().lobby_id().c_str(), msg.source_id().c_str());
        }
        break;
    }
}

bool EOSSDK_Lobby::admit_session_player_to_game_lobby(std::string const& session_id, std::string const& player_id)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (session_id.empty() || player_id.empty())
        return false;

    lobby_state_t* pLobby = get_lobby_by_id(session_id);
    if (pLobby == nullptr)
        pLobby = find_lobby_for_session_id(session_id);
    if (pLobby == nullptr || !i_am_owner(pLobby))
        return false;

    GetProductUserId(player_id);

    if (is_member_in_lobby(player_id, pLobby))
        return false;

    if (pLobby->infos.max_lobby_member() <= static_cast<uint32_t>(pLobby->infos.members_size()))
    {
        APP_LOG(Log::LogLevel::WARN, "Game lobby full, cannot admit session player %s to lobby %s",
            player_id.c_str(), pLobby->infos.lobby_id().c_str());
        return false;
    }

    add_member_to_lobby(player_id, pLobby);
    update_lobby_public_member_list(pLobby);

    send_lobby_member_join(player_id, pLobby);
    notify_lobby_member_status_update(player_id, EOS_ELobbyMemberStatus::EOS_LMS_JOINED, pLobby);
    notify_lobby_update(pLobby);
    send_lobby_update(pLobby);
    GetEOS_Sessions().sync_session_from_lobby(pLobby->infos);

    APP_LOG(Log::LogLevel::INFO, "Admitted session player %s to game lobby %s (members=%d)",
        player_id.c_str(), pLobby->infos.lobby_id().c_str(), pLobby->infos.members_size());
    return true;
}

void EOSSDK_Lobby::deliver_join_lobby_accepted_callbacks(Lobby_Infos_pb const& infos, bool force_redeliver)
{
    if (infos.lobby_id().empty())
        return;

    Lobby_Infos_pb prepared = infos;
    if (lobby_state_t* cached = get_lobby_by_id(infos.lobby_id()))
    {
        if (cached->infos.members_size() >= prepared.members_size())
            prepared = cached->infos;
    }
    prepare_lobby_infos_for_unity(prepared);

    auto const now = std::chrono::steady_clock::now();
    int const member_count = static_cast<int>(lobby_member_ids_ordered(prepared).size());
    if (!force_redeliver
        && prepared.lobby_id() == _last_join_lobby_accepted_id
        && (now - _last_join_lobby_accepted_time) < std::chrono::seconds(5)
        && member_count <= _last_join_lobby_accepted_members)
    {
        APP_LOG(Log::LogLevel::DEBUG,
            "JoinLobbyAccepted suppressed duplicate for lobby=%s members=%d",
            prepared.lobby_id().c_str(),
            member_count);
        return;
    }

    GetEOS_Sessions().sync_session_from_lobby(prepared);

    EOS_UI_EventId const ui_event_id = GetEOS_UI().register_lobby_join_event(prepared);
    if (ui_event_id == EOS_UI_EVENTID_INVALID)
        return;

    _last_join_lobby_accepted_id = prepared.lobby_id();
    _last_join_lobby_accepted_time = now;
    _last_join_lobby_accepted_members = member_count;

    std::vector<pFrameResult_t> notifs = GetCB_Manager().get_notifications(this, EOS_Lobby_JoinLobbyAcceptedCallbackInfo::k_iCallback);
    if (notifs.empty())
    {
        _pending_join_lobby_accepted = prepared;
        _pending_join_lobby_accepted_ui_event = ui_event_id;
        _pending_join_lobby_accepted_valid = true;
        APP_LOG(Log::LogLevel::DEBUG,
            "JoinLobbyAccepted: deferred until handler registers lobby=%s ui_event=%llu",
            prepared.lobby_id().c_str(),
            static_cast<unsigned long long>(ui_event_id));
        APP_LOG(Log::LogLevel::INFO, "JoinLobbyAccepted notification: lobby=%s ui_event=%llu members=%d handlers=0 (pending)",
            prepared.lobby_id().c_str(),
            static_cast<unsigned long long>(ui_event_id),
            member_count);
        return;
    }

    _pending_join_lobby_accepted_valid = false;

    for (auto& notif : notifs)
    {
        EOS_Lobby_JoinLobbyAcceptedCallbackInfo const& src = notif->GetCallback<EOS_Lobby_JoinLobbyAcceptedCallbackInfo>();
        pFrameResult_t queued(new FrameResult);
        EOS_Lobby_JoinLobbyAcceptedCallbackInfo& jlaci = queued->CreateCallback<EOS_Lobby_JoinLobbyAcceptedCallbackInfo>(notif->GetFunc());
        jlaci.ClientData = src.ClientData;
        jlaci.LocalUserId = GetEOS_Connect().get_myself()->first;
        jlaci.UiEventId = ui_event_id;
        queued->done = true;
        GetCB_Manager().add_callback(this, queued);
    }

    APP_LOG(Log::LogLevel::INFO, "JoinLobbyAccepted notification: lobby=%s ui_event=%llu members=%d handlers=%zu",
        prepared.lobby_id().c_str(),
        static_cast<unsigned long long>(ui_event_id),
        member_count,
        notifs.size());
}

void EOSSDK_Lobby::notify_join_lobby_accepted(Lobby_Infos_pb const& infos)
{
    deliver_join_lobby_accepted_callbacks(infos, false);
}

/**
 * The Lobby Interface is used to manage lobbies that provide a persistent connection between users and
 * notifications of data sharing/updates.  Lobbies may also be found by advertising and searching with the backend service.
 * All Lobby Interface calls take a handle of type EOS_HLobby as the first parameter.
 * This handle can be retrieved from a EOS_HPlatform handle by using the EOS_Platform_GetLobbyInterface function.
 *
 * NOTE: At this time, this feature is only available for products that are part of the Epic Games store.
 *
 * @see EOS_Platform_GetLobbyInterface
 */

 /**
  * Creates a lobby and adds the user to the lobby membership.  There is no data associated with the lobby at the start and can be added vis EOS_Lobby_UpdateLobbyModification
  *
  * @param Options Required fields for the creation of a lobby such as a user count and its starting advertised state
  * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
  * @param CompletionDelegate A callback that is fired when the create operation completes, either successfully or in error
  *
  * @return EOS_Success if the creation completes successfully
  *         EOS_InvalidParameters if any of the options are incorrect
  *         EOS_LimitExceeded if the number of allowed lobbies is exceeded
  */
void EOSSDK_Lobby::CreateLobby(const EOS_Lobby_CreateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnCreateLobbyCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_CreateLobbyCallbackInfo& clci = res->CreateCallback<EOS_Lobby_CreateLobbyCallbackInfo>((CallbackFunc)CompletionDelegate);

    clci.ClientData = ClientData;
    
    {
        char* str = new char[sdk::max_accountid_length];
        *str = '\0';
        clci.LobbyId = str;
    }

    // Can't set a MaxLobbyMembers to less than the current member count
    if (Options == nullptr || Options->MaxLobbyMembers < 1 || Options->MaxLobbyMembers > EOS_LOBBY_MAX_LOBBY_MEMBERS)
    {
        clci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else if (_lobbies.size() >= EOS_LOBBY_MAX_LOBBIES)
    {
        clci.ResultCode = EOS_EResult::EOS_LimitExceeded;
    }
    else
    {
        std::string lobby_id(std::move(generate_account_id()));

       

        switch (Options->ApiVersion)
        {
        lobby_create_latest:
        case EOS_LOBBY_CREATELOBBY_API_010:
        case EOS_LOBBY_CREATELOBBY_API_009:
        {
            const EOS_Lobby_CreateLobbyOptions009* opts = reinterpret_cast<const EOS_Lobby_CreateLobbyOptions009*>(Options);
            if (opts->LobbyId != NULL) {
                lobby_id = std::string(opts->LobbyId);
            }
            auto& infos = _lobbies[lobby_id];
            infos._state.infos.set_allowedplatformids(opts->AllowedPlatformIds, opts->AllowedPlatformIdsCount);
            infos._state.infos.set_allowedplatformidscount(opts->AllowedPlatformIdsCount);
            infos._state.infos.set_ballowjoinbyid(opts->bEnableJoinById);
            infos._state.infos.set_brejoinafterkickrequiresinvite(opts->bRejoinAfterKickRequiresInvite);
        }
        case EOS_LOBBY_CREATELOBBY_API_008:
        case EOS_LOBBY_CREATELOBBY_API_007:
        {
            const EOS_Lobby_CreateLobbyOptions007* opts = reinterpret_cast<const EOS_Lobby_CreateLobbyOptions007*>(Options);
            if (opts->LobbyId != NULL) {
                lobby_id = std::string(opts->LobbyId);
            }
            auto& infos = _lobbies[lobby_id];
            infos._state.infos.set_brtcroomenabled(opts->bEnableRTCRoom);
            infos._state.infos.set_bpresenceenabled(opts->bPresenceEnabled);
        }
        //case EOS_LOBBY_CREATELOBBY_API_006:
        case EOS_LOBBY_CREATELOBBY_API_005:
        {
            const EOS_Lobby_CreateLobbyOptions005* opts = reinterpret_cast<const EOS_Lobby_CreateLobbyOptions005*>(Options);
            auto& infos = _lobbies[lobby_id];
            infos._state.infos.set_ballowhostmigration(!opts->bDisableHostMigration);
        }
        case EOS_LOBBY_CREATELOBBY_API_004:
        {
            const EOS_Lobby_CreateLobbyOptions004* opts = reinterpret_cast<const EOS_Lobby_CreateLobbyOptions004*>(Options);
            auto& infos = _lobbies[lobby_id];
            std::string bucket_id(std::move((opts->BucketId)));
            infos._state.infos.set_bucket_id(bucket_id);
        }
        //case EOS_LOBBY_CREATELOBBY_API_003:
        case EOS_LOBBY_CREATELOBBY_API_002:
        {
            const EOS_Lobby_CreateLobbyOptions002* opts = reinterpret_cast<const EOS_Lobby_CreateLobbyOptions002*>(Options);
            // Can't set a MaxLobbyMembers to less than the current member count
            strncpy(const_cast<char*>(clci.LobbyId), lobby_id.c_str(), max_accountid_length);
            const_cast<char*>(clci.LobbyId)[64] = 0;

            auto& infos = _lobbies[lobby_id];
            infos._state.infos.set_lobby_id(lobby_id);
            infos._state.infos.set_owner_id(GetEOS_Connect().get_myself()->first->to_string());
            infos._state.infos.set_max_lobby_member(opts->MaxLobbyMembers);
            infos._state.infos.set_permission_level(utils::GetEnumValue(opts->PermissionLevel));
            (*infos._state.infos.mutable_members())[GetEOS_Connect().get_myself()->first->to_string()];
            infos._state.state = lobby_state_t::created;

            clci.ResultCode = EOS_EResult::EOS_Success;
        }
        break;
        default:
            if (Options->ApiVersion > EOS_LOBBY_CREATELOBBY_API_010)
            {
                APP_LOG(Log::LogLevel::WARN, "Unknown EOS_Lobby_CreateLobby API version %d, treating as %d", Options->ApiVersion, EOS_LOBBY_CREATELOBBY_API_010);
                goto lobby_create_latest;
            }

            APP_LOG(Log::LogLevel::FATAL, "Unmanaged API version %d", Options->ApiVersion);
            abort();
        }

        if (clci.ResultCode == EOS_EResult::EOS_Success)
        {
            auto lobby_it = _lobbies.find(std::string(clci.LobbyId));
            const bool rtc_enabled = lobby_it != _lobbies.end() && lobby_it->second._state.infos.brtcroomenabled();
            APP_LOG(Log::LogLevel::DEBUG, "CreateLobby success: id=%s rtc=%d", clci.LobbyId, rtc_enabled ? 1 : 0);
            if (lobby_it != _lobbies.end())
            {
                GetEOS_Sessions().sync_session_from_lobby(lobby_it->second._state.infos);
                publish_local_join_info(clci.LobbyId);
                sync_steam_rich_presence_for_lobby(&lobby_it->second._state);
                flush_pending_lobby_searches();

                // Bootstrap: immediately broadcast presence lobby so peers can find it via LobbySearch by target_id
                if (lobby_attr_bool(lobby_it->second._state.infos, "IsCrossPlatformPresence"))
                {
                    send_lobby_update(&lobby_it->second._state);
                }
                else if (lobby_is_game_namespace(lobby_it->second._state.infos))
                {
                    refresh_owned_presence_for_active_game();
                }
            }
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Destroy a lobby given a lobby id
 *
 * @param Options Structure containing information about the lobby to be destroyed
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the destroy operation completes, either successfully or in error
 *
 * @return EOS_Success if the destroy completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_AlreadyPending if the lobby is already marked for destroy
 *         EOS_NotFound if the lobby to be destroyed does not exist
 */
void EOSSDK_Lobby::DestroyLobby(const EOS_Lobby_DestroyLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnDestroyLobbyCallback CompletionDelegate)
{
    TRACE_FUNC();
    APP_LOG(Log::LogLevel::INFO, "TODO");

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_DestroyLobbyCallbackInfo& dlci = res->CreateCallback<EOS_Lobby_DestroyLobbyCallbackInfo>((CallbackFunc)CompletionDelegate);

    dlci.ClientData = ClientData;
    
    {
        char* str;
        if (Options->LobbyId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->LobbyId) + 1;
            str = new char[len];
            strncpy(str, Options->LobbyId, len);
        }
        dlci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyId == nullptr)
    {
        dlci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        lobby_state_t* pLobby = get_lobby_by_id(Options->LobbyId);

        //send_lobby_promote_member(remote_id, ...);
        dlci.ResultCode = EOS_EResult::EOS_NotFound;
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Join a lobby, creating a local instance under a given lobby id.  Backend will validate various conditions to make sure it is possible to join the lobby.
 *
 * @param Options Structure containing information about the lobby to be joined
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the join operation completes, either successfully or in error
 *
 * @return EOS_Success if the destroy completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 */
void EOSSDK_Lobby::JoinLobby(const EOS_Lobby_JoinLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_JoinLobbyCallbackInfo& jlci = res->CreateCallback<EOS_Lobby_JoinLobbyCallbackInfo>((CallbackFunc)CompletionDelegate);

    EOSSDK_LobbyDetails* pLobbyDetails = reinterpret_cast<EOSSDK_LobbyDetails*>(Options->LobbyDetailsHandle);

    jlci.ClientData = ClientData;
    
    {
        char* str;
        if (pLobbyDetails == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = pLobbyDetails->_state.infos.lobby_id().length() + 1;
            str = new char[len];
            strncpy(str, pLobbyDetails->_state.infos.lobby_id().c_str(), len);
        }
        jlci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyDetailsHandle == nullptr)
    {
        jlci.ResultCode = EOS_EResult::EOS_InvalidParameters;
        res->done = true;
    }
    else if (Settings::Inst().steam_passthrough)
    {
        APP_LOG(Log::LogLevel::INFO, "JoinLobby ignored in steam_passthrough");
        jlci.ResultCode = EOS_EResult::EOS_NotFound;
        res->done = true;
    }
    else if (_lobbies.size() >= EOS_LOBBY_MAX_LOBBIES)
    {
        jlci.ResultCode = EOS_EResult::EOS_LimitExceeded;
        res->done = true;
    }
    else
    {
        Lobby_Join_Request_pb* request = new Lobby_Join_Request_pb;
        request->set_lobby_id(pLobbyDetails->_state.infos.lobby_id());
        int32_t const current_join_id = join_id++;
        request->set_join_id(current_join_id);

        lobby_join_t pending;
        pending.cb = res;
        pending.ignore_non_success = false;
        pending.kind = lobby_join_kind_t::join_lobby;
        _joins_requests[current_join_id] = std::move(pending);

        if (!send_lobby_join_request(pLobbyDetails->_state.infos.owner_id(), request))
        {
            jlci.ResultCode = EOS_EResult::EOS_NotFound;
            res->done = true;
            _joins_requests.erase(current_join_id);
        }
        else
        {
            APP_LOG(Log::LogLevel::INFO, "JoinLobby request sent: lobby=%s owner=%s join_id=%d",
                pLobbyDetails->_state.infos.lobby_id().c_str(),
                pLobbyDetails->_state.infos.owner_id().c_str(),
                current_join_id);
        }
    }

    GetCB_Manager().add_callback(this, res);
}

void EOSSDK_Lobby::JoinLobbyById(const EOS_Lobby_JoinLobbyByIdOptions002* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyByIdCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    void* buff = res->AllocCallback((CallbackFunc)CompletionDelegate, sizeof(EOS_Lobby_JoinLobbyByIdCallbackInfo), k_iJoinLobbyByIdCallback);
    auto& jlbci = *new (buff) EOS_Lobby_JoinLobbyByIdCallbackInfo();

    jlbci.ClientData = ClientData;

    {
        char* str;
        if (Options == nullptr || Options->LobbyId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->LobbyId) + 1;
            str = new char[len];
            strncpy(str, Options->LobbyId, len);
        }
        jlbci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyId == nullptr || *Options->LobbyId == '\0')
    {
        jlbci.ResultCode = EOS_EResult::EOS_InvalidParameters;
        res->done = true;
    }
    else if (Settings::Inst().steam_passthrough)
    {
        APP_LOG(Log::LogLevel::INFO, "JoinLobbyById ignored in steam_passthrough");
        jlbci.ResultCode = EOS_EResult::EOS_NotFound;
        res->done = true;
    }
    else if (_lobbies.size() >= EOS_LOBBY_MAX_LOBBIES)
    {
        jlbci.ResultCode = EOS_EResult::EOS_LimitExceeded;
        res->done = true;
    }
    else
    {
        Lobby_Join_Request_pb* request = new Lobby_Join_Request_pb;
        request->set_lobby_id(Options->LobbyId);
        int32_t const current_join_id = join_id++;
        request->set_join_id(current_join_id);

        lobby_join_t pending;
        pending.cb = res;
        pending.ignore_non_success = true;
        pending.kind = lobby_join_kind_t::join_lobby_by_id;
        _joins_requests[current_join_id] = std::move(pending);

        if (!send_lobby_join_request_broadcast(request))
        {
            jlbci.ResultCode = EOS_EResult::EOS_NotFound;
            res->done = true;
            _joins_requests.erase(current_join_id);
        }
        else
        {
            APP_LOG(Log::LogLevel::INFO, "JoinLobbyById request broadcast: lobby=%s join_id=%d",
                Options->LobbyId, current_join_id);
        }
    }

    GetCB_Manager().add_callback(this, res);
}

/**
 * Leave a lobby given a lobby id
 *
 * @param Options Structure containing information about the lobby to be left
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the leave operation completes, either successfully or in error
 *
 * @return EOS_Success if the leave completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_AlreadyPending if the lobby is already marked for leave
 *         EOS_NotFound if a lobby to be left does not exist
 */
void EOSSDK_Lobby::LeaveLobby(const EOS_Lobby_LeaveLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnLeaveLobbyCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_LeaveLobbyCallbackInfo& llci = res->CreateCallback<EOS_Lobby_LeaveLobbyCallbackInfo>((CallbackFunc)CompletionDelegate);

    llci.ClientData = ClientData;
    
    {
        char* str;
        if (Options->LobbyId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->LobbyId) + 1;
            str = new char[len];
            strncpy(str, Options->LobbyId, len);
        }
        llci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyId == nullptr)
    {
        llci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        auto it = _lobbies.find(Options->LobbyId);
        if (it != _lobbies.end())
        {
            // TODO: If we're the owner, destroy the lobby ?
            send_lobby_member_leave(GetEOS_Connect().get_myself()->first->to_string(), &it->second._state, EOS_ELobbyMemberStatus::EOS_LMS_LEFT);
            llci.ResultCode = EOS_EResult::EOS_Success;
            _lobbies.erase(it);
            clear_steam_rich_presence_if_no_owned_lobby();
        }
        else
        {
            llci.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Creates a lobby modification handle (EOS_HLobbyModification). The lobby modification handle is used to modify an existing lobby and can be applied with EOS_Lobby_UpdateLobby.
 * The EOS_HLobbyModification must be released by calling EOS_LobbyModification_Release once it is no longer needed.
 *
 * @param Options Required fields such as lobby id
 * @param OutLobbyModificationHandle Pointer to a Lobby Modification Handle only set if successful
 * @return EOS_Success if we successfully created the Lobby Modification Handle pointed at in OutLobbyModificationHandle, or an error result if the input data was invalid
 *		   EOS_InvalidParameters if any of the options are incorrect
 *
 * @see EOS_LobbyModification_Release
 * @see EOS_Lobby_UpdateLobby
 * @see EOS_LobbyModification_*
 */
EOS_EResult EOSSDK_Lobby::UpdateLobbyModification(const EOS_Lobby_UpdateLobbyModificationOptions* Options, EOS_HLobbyModification* OutLobbyModificationHandle)
{
    TRACE_FUNC();

    if (Options == nullptr || OutLobbyModificationHandle == nullptr)
    {
        set_nullptr(OutLobbyModificationHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto lobby_it = _lobbies.find(Options->LobbyId);
    if (lobby_it == _lobbies.end())
    {
        *OutLobbyModificationHandle = nullptr;
        return EOS_EResult::EOS_InvalidParameters;
    }

    EOSSDK_LobbyModification* pLobbyModif = new EOSSDK_LobbyModification;
    pLobbyModif->_infos = lobby_it->second._state.infos;

    *OutLobbyModificationHandle = reinterpret_cast<EOS_HLobbyModification>(pLobbyModif);

    return EOS_EResult::EOS_Success;
}

/**
 * Update a lobby given a lobby modification handle created via EOS_Lobby_UpdateLobbyModification
 *
 * @param Options Structure containing information about the lobby to be updated
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the update operation completes, either successfully or in error
 *
 * @return EOS_Success if the update completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Lobby_NotOwner if the lobby modification contains modifications that are only allowed by the owner
 *         EOS_NotFound if the lobby to update does not exist
 */
void EOSSDK_Lobby::UpdateLobby(const EOS_Lobby_UpdateLobbyOptions* Options, void* ClientData, const EOS_Lobby_OnUpdateLobbyCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_UpdateLobbyCallbackInfo& ulci = res->CreateCallback<EOS_Lobby_UpdateLobbyCallbackInfo>((CallbackFunc)CompletionDelegate);

    ulci.ClientData = ClientData;
    ulci.ResultCode = EOS_EResult::EOS_InvalidParameters;

    {
        char* str = new char[1];
        *str = '\0';
        ulci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyModificationHandle == nullptr)
    {
        APP_LOG(Log::LogLevel::DEBUG, "UpdateLobby: invalid parameters");
    }
    else
    {
        EOSSDK_LobbyModification* pLobbyModif = reinterpret_cast<EOSSDK_LobbyModification*>(Options->LobbyModificationHandle);

        {
            delete[] ulci.LobbyId;
            size_t len = pLobbyModif->_infos.lobby_id().length() + 1;
            char* str = new char[len];
            strncpy(str, pLobbyModif->_infos.lobby_id().c_str(), len);
            ulci.LobbyId = str;
        }

        lobby_state_t* pLobby = get_lobby_by_id(pLobbyModif->_infos.lobby_id());
        if (pLobby == nullptr)
        {
            ulci.ResultCode = EOS_EResult::EOS_NotFound;
            APP_LOG(Log::LogLevel::DEBUG, "UpdateLobby: lobby not found %s", pLobbyModif->_infos.lobby_id().c_str());
        }
        else if (pLobbyModif->_lobby_modified)
        {
            if (i_am_owner(pLobby))
            {
                pLobby->infos = pLobbyModif->_infos;
                ulci.ResultCode = EOS_EResult::EOS_Success;
                send_lobby_update(pLobby);
                GetEOS_Sessions().sync_session_from_lobby(pLobby->infos);
                publish_local_join_info(pLobby->infos.lobby_id());
                sync_steam_rich_presence_for_lobby(pLobby);
                if (lobby_is_game_namespace(pLobby->infos))
                    refresh_owned_presence_for_active_game();
                {
                    GLOBAL_LOCK();
                    flush_pending_lobby_searches();
                }

                if (pLobbyModif->_member_modified)
                {
                    send_lobby_member_update(GetEOS_Connect().get_myself()->first->to_string(), pLobby);
                }
            }
            else
            {
                std::string const local_id = Settings::Inst().productuserid->to_string();
                if (apply_non_owner_lobby_modification(pLobby, pLobbyModif->_infos, local_id))
                {
                    ulci.ResultCode = EOS_EResult::EOS_Success;
                    send_lobby_member_update(local_id, pLobby);
                    GetEOS_Sessions().sync_session_from_lobby(pLobby->infos);
                    APP_LOG(Log::LogLevel::INFO,
                        "UpdateLobby: applied member-writable attrs for non-owner lobby=%s member=%s",
                        pLobby->infos.lobby_id().c_str(),
                        local_id.c_str());
                }
                else
                {
                    ulci.ResultCode = EOS_EResult::EOS_Lobby_NotOwner;
                }
            }
        }
        else if (pLobbyModif->_member_modified)
        {
            ulci.ResultCode = EOS_EResult::EOS_Success;

            *pLobby->infos.mutable_members() = pLobbyModif->_infos.members();
            send_lobby_member_update(GetEOS_Connect().get_myself()->first->to_string(), pLobby);
        }
        else
        {
            ulci.ResultCode = EOS_EResult::EOS_Success;
        }

        APP_LOG(Log::LogLevel::DEBUG, "UpdateLobby: lobby=%s result=%s rtc=%d",
            ulci.LobbyId != nullptr ? ulci.LobbyId : "",
            EOS_EResult_ToString(ulci.ResultCode),
            pLobby->infos.brtcroomenabled() ? 1 : 0);
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Promote an existing member of the lobby to owner, allowing them to make lobby data modifications
 *
 * @param Options Structure containing information about the lobby and member to be promoted
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the promotion operation completes, either successfully or in error
 *
 * @return EOS_Success if the promote completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Lobby_NotOwner if the calling user is not the owner of the lobby
 *         EOS_NotFound if the lobby of interest does not exist
 */
void EOSSDK_Lobby::PromoteMember(const EOS_Lobby_PromoteMemberOptions* Options, void* ClientData, const EOS_Lobby_OnPromoteMemberCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_PromoteMemberCallbackInfo& pmci = res->CreateCallback<EOS_Lobby_PromoteMemberCallbackInfo>((CallbackFunc)CompletionDelegate);

    pmci.ClientData = ClientData;
    {
        char* str;
        if (Options->LobbyId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->LobbyId) + 1;
            str = new char[len];
            strncpy(str, Options->LobbyId, len);
        }
        pmci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyId == nullptr || Options->TargetUserId == nullptr)
    {
        pmci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        lobby_state_t* pLobby = get_lobby_by_id(Options->LobbyId);
        if (pLobby != nullptr)
        {
            if (i_am_owner(pLobby))
            {
                if (is_member_in_lobby(Options->TargetUserId->to_string(), pLobby))
                {
                    send_lobby_member_promote(Options->TargetUserId->to_string(), pLobby);
                    pLobby->infos.set_owner_id(Options->TargetUserId->to_string());

                    // Is this notifiied ?
                    notify_lobby_member_status_update(Options->TargetUserId->to_string(), EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED, pLobby);
                }
                else
                {
                    pmci.ResultCode = EOS_EResult::EOS_NotFound;
                }
            }
            else
            {
                pmci.ResultCode = EOS_EResult::EOS_Lobby_NotOwner;
            }
        }
        else
        {
            pmci.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Kick an existing member from the lobby
 *
 * @param Options Structure containing information about the lobby and member to be kicked
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the kick operation completes, either successfully or in error
 *
 * @return EOS_Success if the kick completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_Lobby_NotOwner if the calling user is not the owner of the lobby
 *         EOS_NotFound if a lobby of interest does not exist
 */
void EOSSDK_Lobby::KickMember(const EOS_Lobby_KickMemberOptions* Options, void* ClientData, const EOS_Lobby_OnKickMemberCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_KickMemberCallbackInfo& kmci = res->CreateCallback<EOS_Lobby_KickMemberCallbackInfo>((CallbackFunc)CompletionDelegate);

    kmci.ClientData = ClientData;
    {
        char* str;
        if (Options->LobbyId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->LobbyId) + 1;
            str = new char[len];
            strncpy(str, Options->LobbyId, len);
        }
        kmci.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyId == nullptr || Options->TargetUserId == nullptr)
    {
        kmci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        lobby_state_t* pLobby = get_lobby_by_id(Options->LobbyId);
        if (pLobby != nullptr)
        {
            if (i_am_owner(pLobby))
            {
                if (is_member_in_lobby(Options->TargetUserId->to_string(), pLobby))
                {
                    send_lobby_member_leave(Options->TargetUserId->to_string(), pLobby, EOS_ELobbyMemberStatus::EOS_LMS_KICKED);
                    kmci.ResultCode = EOS_EResult::EOS_Success;
                }
                else
                {
                    kmci.ResultCode = EOS_EResult::EOS_NotFound;
                }
            }
            else
            {
                kmci.ResultCode = EOS_EResult::EOS_Lobby_NotOwner;
            }
        }
        else
        {
            kmci.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Register to receive notifications when a lobby owner updates the attributes associated with the lobby.
 * @note must call RemoveNotifyLobbyUpdateReceived to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Lobby::AddNotifyLobbyUpdateReceived(const EOS_Lobby_AddNotifyLobbyUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyUpdateReceivedCallback NotificationFn)
{
    TRACE_FUNC();

    if (Options == nullptr || NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new  FrameResult);
    EOS_Lobby_LobbyUpdateReceivedCallbackInfo& lurci = res->CreateCallback<EOS_Lobby_LobbyUpdateReceivedCallbackInfo>((CallbackFunc)NotificationFn);

    lurci.ClientData = ClientData;
    {
        char* str = new char[max_accountid_length];
        *str = '\0';
        lurci.LobbyId = str;
    }

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when a lobby changes its data.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Lobby::RemoveNotifyLobbyUpdateReceived(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive notifications when a lobby member updates the attributes associated with themselves inside the lobby.
 * @note must call RemoveNotifyLobbyMemberUpdateReceived to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Lobby::AddNotifyLobbyMemberUpdateReceived(const EOS_Lobby_AddNotifyLobbyMemberUpdateReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberUpdateReceivedCallback NotificationFn)
{
    TRACE_FUNC();

    if (Options == nullptr || NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new  FrameResult);
    EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo& lmurci = res->CreateCallback<EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo>((CallbackFunc)NotificationFn);

    lmurci.ClientData = ClientData;
    {
        char* str = new char[max_accountid_length];
        *str = '\0';
        lmurci.LobbyId = str;
    }
    lmurci.TargetUserId = GetInvalidProductUserId();

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when lobby members change their data.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Lobby::RemoveNotifyLobbyMemberUpdateReceived(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive notifications about the changing status of lobby members.
 * @note must call RemoveNotifyLobbyMemberStatusReceived to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Lobby::AddNotifyLobbyMemberStatusReceived(const EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyMemberStatusReceivedCallback NotificationFn)
{
    TRACE_FUNC();

    if (Options == nullptr || NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new  FrameResult);
    EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo& lmsrci = res->CreateCallback<EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo>((CallbackFunc)NotificationFn);

    lmsrci.ClientData = ClientData;
    {
        char* str = new char[max_accountid_length];
        *str = '\0';
        lmsrci.LobbyId = str;
    }
    lmsrci.CurrentStatus = EOS_ELobbyMemberStatus::EOS_LMS_CLOSED;
    lmsrci.TargetUserId = GetInvalidProductUserId();

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when lobby members status change.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Lobby::RemoveNotifyLobbyMemberStatusReceived(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Send an invite to another user.  User must be a member of the lobby or else the call will fail
 *
 * @param Options Structure containing information about the lobby and user to invite
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the send invite operation completes, either successfully or in error
 *
 * @return EOS_Success if the send invite completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_NotFound if the lobby to send the invite from does not exist
 */
void EOSSDK_Lobby::SendInvite(const EOS_Lobby_SendInviteOptions* Options, void* ClientData, const EOS_Lobby_OnSendInviteCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_SendInviteCallbackInfo& sici = res->CreateCallback<EOS_Lobby_SendInviteCallbackInfo>((CallbackFunc)CompletionDelegate);

    sici.ClientData = ClientData;

    {
        char* str;
        if (Options->LobbyId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->LobbyId) + 1;
            str = new char[len];
            strncpy(str, Options->LobbyId, len);
        }
        sici.LobbyId = str;
    }

    if (Options == nullptr || Options->LobbyId == nullptr || Options->TargetUserId == nullptr)
    {
        sici.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        lobby_state_t* pLobby = get_lobby_by_id(Options->LobbyId);
        std::string const& local_id = Settings::Inst().productuserid->to_string();
        if (pLobby != nullptr && (i_am_owner(pLobby) || is_member_in_lobby(local_id, pLobby)))
        {
            Lobby_Invite_pb* invite = new Lobby_Invite_pb;

            *invite->mutable_infos() = pLobby->infos;
            if (send_lobby_invite(Options->TargetUserId->to_string(), invite))
            {
                sici.ResultCode = EOS_EResult::EOS_Success;
                APP_LOG(Log::LogLevel::INFO, "Lobby invite sent: lobby=%s target=%s",
                    Options->LobbyId, Options->TargetUserId->to_string().c_str());
            }
            else
            {
                sici.ResultCode = EOS_EResult::EOS_NotFound;
                delete invite;
            }
        }
        else
        {
            sici.ResultCode = EOS_EResult::EOS_NotFound;
        }
    }

    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Reject an invite from another user.
 *
 * @param Options Structure containing information about the invite to reject
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate
 * @param CompletionDelegate A callback that is fired when the reject invite operation completes, either successfully or in error
 *
 * @return EOS_Success if the invite rejection completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_NotFound if the invite does not exist
 */
void EOSSDK_Lobby::RejectInvite(const EOS_Lobby_RejectInviteOptions* Options, void* ClientData, const EOS_Lobby_OnRejectInviteCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_RejectInviteCallbackInfo& rici = res->CreateCallback<EOS_Lobby_RejectInviteCallbackInfo>((CallbackFunc)CompletionDelegate);

    rici.ClientData = ClientData;

    {
        char* str;
        if (Options->InviteId == nullptr)
        {
            str = new char[1];
            *str = '\0';
        }
        else
        {
            size_t len = strlen(Options->InviteId) + 1;
            str = new char[len];
            strncpy(str, Options->InviteId, len);
        }
        rici.InviteId = str;
    }
    
    if (Options == nullptr || Options->InviteId == nullptr)
    {
        rici.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        auto it = _lobby_invites.find(Options->InviteId);
        if (it != _lobby_invites.end())
        {
            _lobby_invites.erase(it);
            rici.ResultCode = EOS_EResult::EOS_Success;
        }
        else
        {
            rici.ResultCode = EOS_EResult::EOS_NotFound;
        }
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
void EOSSDK_Lobby::QueryInvites(const EOS_Lobby_QueryInvitesOptions* Options, void* ClientData, const EOS_Lobby_OnQueryInvitesCallback CompletionDelegate)
{
    TRACE_FUNC();

    if (CompletionDelegate == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_Lobby_QueryInvitesCallbackInfo& qici = res->CreateCallback<EOS_Lobby_QueryInvitesCallbackInfo>((CallbackFunc)CompletionDelegate);

    qici.ClientData = ClientData;
    qici.LocalUserId = GetEOS_Connect().get_myself()->first;

    if (Options == nullptr)
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
uint32_t EOSSDK_Lobby::GetInviteCount(const EOS_Lobby_GetInviteCountOptions* Options)
{
    TRACE_FUNC();

    if (Options == nullptr)
        return 0;

    return static_cast<uint32_t>(_lobby_invites.size());
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
 * @see EOS_Lobby_GetInviteCount
 * @see EOS_Lobby_CopyLobbyDetailsHandleByInviteId
 */
EOS_EResult EOSSDK_Lobby::GetInviteIdByIndex(const EOS_Lobby_GetInviteIdByIndexOptions* Options, char* OutBuffer, int32_t* InOutBufferLength)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->Index >= _lobby_invites.size() || InOutBufferLength == nullptr )
        return EOS_EResult::EOS_InvalidParameters;

    auto it = _lobby_invites.begin();
    std::advance(it, Options->Index);

    if (OutBuffer != nullptr)
    {
        *InOutBufferLength = std::min<int32_t>(*InOutBufferLength, static_cast<int32_t>(it->first.length() + 1));
        strncpy(OutBuffer, it->first.c_str(), *InOutBufferLength);
    }
    else
    {
        *InOutBufferLength = static_cast<int32_t>(it->first.length() + 1);
    }
    
    return EOS_EResult::EOS_Success;
}

/**
 * Create a lobby search handle.  This handle may be modified to include various search parameters.
 * Searching is possible in three methods, all mutually exclusive
 * - set the lobby id to find a specific lobby
 * - set the target user id to find a specific user
 * - set lobby parameters to find an array of lobbies that match the search criteria (not available yet)
 *
 * @param Options Structure containing required parameters such as the maximum number of search results
 * @param OutLobbySearchHandle The new search handle or null if there was an error creating the search handle
 *
 * @return EOS_Success if the search creation completes successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 */
EOS_EResult EOSSDK_Lobby::CreateLobbySearch(const EOS_Lobby_CreateLobbySearchOptions* Options, EOS_HLobbySearch* OutLobbySearchHandle)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->MaxResults == 0 || OutLobbySearchHandle == nullptr)
    {
        set_nullptr(OutLobbySearchHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    _lobbies_searchs.emplace_back();
    EOSSDK_LobbySearch*& search = _lobbies_searchs.back();
    search = new EOSSDK_LobbySearch;
    search->_max_results = Options->MaxResults;

    *OutLobbySearchHandle = reinterpret_cast<EOS_HLobbySearch>(search);

    return EOS_EResult::EOS_Success;
}

/**
 * Register to receive notifications about lobby invites sent to local users.
 * @note must call RemoveNotifyLobbyInviteReceived to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Lobby::AddNotifyLobbyInviteReceived(const EOS_Lobby_AddNotifyLobbyInviteReceivedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteReceivedCallback NotificationFn)
{
    TRACE_FUNC();

    if (Options == nullptr || NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Lobby_LobbyInviteReceivedCallbackInfo& lirci = res->CreateCallback<EOS_Lobby_LobbyInviteReceivedCallbackInfo>((CallbackFunc)NotificationFn);
    lirci.ClientData = ClientData;
    lirci.LocalUserId = GetEOS_Connect().get_myself()->first;
    lirci.TargetUserId = GetInvalidProductUserId();
    {
        char *str = new char[EOS_LOBBY_INVITEID_MAX_LENGTH + 1];
        *str = '\0';
        lirci.InviteId = str;
    }

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when a user receives a lobby invitation.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Lobby::RemoveNotifyLobbyInviteReceived(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive notifications about lobby invites accepted by local user via the overlay.
 * @note must call RemoveNotifyLobbyInviteAccepted to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Lobby::AddNotifyLobbyInviteAccepted(const EOS_Lobby_AddNotifyLobbyInviteAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnLobbyInviteAcceptedCallback NotificationFn)
{
    TRACE_FUNC();

    if (Options == nullptr || NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Lobby_LobbyInviteAcceptedCallbackInfo& liaci = res->CreateCallback<EOS_Lobby_LobbyInviteAcceptedCallbackInfo>((CallbackFunc)NotificationFn);
    liaci.ClientData = ClientData;
    liaci.LocalUserId = GetEOS_Connect().get_myself()->first;
    liaci.TargetUserId = GetInvalidProductUserId();
    {
        char* str = new char[EOS_LOBBY_INVITEID_MAX_LENGTH + 1];
        *str = '\0';
        liaci.InviteId = str;
    }

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Unregister from receiving notifications when a user accepts a lobby invitation via the overlay.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Lobby::RemoveNotifyLobbyInviteAccepted(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * Register to receive notifications about lobby join game accepted by local user via the overlay.
 * @note must call RemoveNotifyJoinLobbyAccepted to remove the notification
 *
 * @param Options Structure containing information about the request.
 * @param ClientData Arbitrary data that is passed back to you in the CompletionDelegate.
 * @param Notification A callback that is fired when a a notification is received.
 *
 * @return handle representing the registered callback
 */
EOS_NotificationId EOSSDK_Lobby::AddNotifyJoinLobbyAccepted(const EOS_Lobby_AddNotifyJoinLobbyAcceptedOptions* Options, void* ClientData, const EOS_Lobby_OnJoinLobbyAcceptedCallback NotificationFn)
{
    TRACE_FUNC();

    if (Options == nullptr || NotificationFn == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_Lobby_JoinLobbyAcceptedCallbackInfo& jlaci = res->CreateCallback<EOS_Lobby_JoinLobbyAcceptedCallbackInfo>((CallbackFunc)NotificationFn);
    jlaci.ClientData = ClientData;
    jlaci.LocalUserId = GetEOS_Connect().get_myself()->first;
    jlaci.UiEventId = EOS_UI_EVENTID_INVALID;

    EOS_NotificationId const id = GetCB_Manager().add_notification(this, res);

    if (_pending_join_lobby_accepted_valid)
    {
        APP_LOG(Log::LogLevel::INFO,
            "JoinLobbyAccepted: delivering pending notification lobby=%s ui_event=%llu",
            _pending_join_lobby_accepted.lobby_id().c_str(),
            static_cast<unsigned long long>(_pending_join_lobby_accepted_ui_event));
        deliver_join_lobby_accepted_callbacks(_pending_join_lobby_accepted, true);
    }

    return id;
}

/**
 * Unregister from receiving notifications when a user accepts a lobby invitation via the overlay.
 *
 * @param InId Handle representing the registered callback
 */
void EOSSDK_Lobby::RemoveNotifyJoinLobbyAccepted(EOS_NotificationId InId)
{
    TRACE_FUNC();

    GetCB_Manager().remove_notification(this, InId);
}

/**
 * EOS_Lobby_CopyLobbyDetailsHandleByInviteId is used to immediately retrieve a handle to the lobby information from after notification of an invite
 * If the call returns an EOS_Success result, the out parameter, OutLobbyDetailsHandle, must be passed to EOS_LobbyDetails_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutLobbyDetailsHandle out parameter used to receive the lobby handle
 *
 * @return EOS_Success if the information is available and passed out in OutLobbyDetailsHandle
 *         EOS_InvalidParameters if you pass an invalid invite id or a null pointer for the out parameter
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound If the invite id cannot be found
 *
 * @see EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions
 * @see EOS_LobbyDetails_Release
 */
EOS_EResult EOSSDK_Lobby::CopyLobbyDetailsHandleByInviteId(const EOS_Lobby_CopyLobbyDetailsHandleByInviteIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->InviteId == nullptr || OutLobbyDetailsHandle == nullptr)
    {
        set_nullptr(OutLobbyDetailsHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto it = _lobby_invites.find(Options->InviteId);
    if (it == _lobby_invites.end())
    {
        *OutLobbyDetailsHandle = nullptr;
        return EOS_EResult::EOS_NotFound;
    }

    EOSSDK_LobbyDetails* pLobbyDetails = new EOSSDK_LobbyDetails;
    pLobbyDetails->_state.infos = it->second.infos;
    *OutLobbyDetailsHandle = reinterpret_cast<EOS_HLobbyDetails>(pLobbyDetails);

    return EOS_EResult::EOS_Success;
}

/**
 * EOS_Lobby_CopyLobbyDetailsHandleByUiEventId is used to immediately retrieve a handle to the lobby information from after notification of an join game
 * If the call returns an EOS_Success result, the out parameter, OutLobbyDetailsHandle, must be passed to EOS_LobbyDetails_Release to release the memory associated with it.
 *
 * @param Options Structure containing the input parameters
 * @param OutLobbyDetailsHandle out parameter used to receive the lobby handle
 *
 * @return EOS_Success if the information is available and passed out in OutLobbyDetailsHandle
 *         EOS_InvalidParameters if you pass an invalid ui event id
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound If the invite id cannot be found
 *
 * @see EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions
 * @see EOS_LobbyDetails_Release
 */
EOS_EResult EOSSDK_Lobby::CopyLobbyDetailsHandleByUiEventId(const EOS_Lobby_CopyLobbyDetailsHandleByUiEventIdOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    TRACE_FUNC();

    if (Options == nullptr || Options->UiEventId == EOS_UI_EVENTID_INVALID || OutLobbyDetailsHandle == nullptr)
    {
        set_nullptr(OutLobbyDetailsHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    Lobby_Infos_pb lobby_infos;
    if (!GetEOS_UI().copy_lobby_join_event(Options->UiEventId, lobby_infos))
    {
        *OutLobbyDetailsHandle = nullptr;
        return EOS_EResult::EOS_NotFound;
    }

    EOSSDK_LobbyDetails* pLobbyDetails = new EOSSDK_LobbyDetails;
    pLobbyDetails->_state.infos = std::move(lobby_infos);
    *OutLobbyDetailsHandle = reinterpret_cast<EOS_HLobbyDetails>(pLobbyDetails);

    return EOS_EResult::EOS_Success;
}

/**
 * Create a handle to an existing lobby.
 * If the call returns an EOS_Success result, the out parameter, OutLobbyDetailsHandle, must be passed to EOS_LobbyDetails_Release to release the memory associated with it.
 *
 * @param Options Structure containing information about the lobby to retrieve
 * @param OutLobbyDetailsHandle The new active lobby handle or null if there was an error
 *
 * @return EOS_Success if the lobby handle was created successfully
 *         EOS_InvalidParameters if any of the options are incorrect
 *         EOS_IncompatibleVersion if the API version passed in is incorrect
 *         EOS_NotFound if the lobby doesn't exist
 */
EOS_EResult EOSSDK_Lobby::CopyLobbyDetailsHandle(const EOS_Lobby_CopyLobbyDetailsHandleOptions* Options, EOS_HLobbyDetails* OutLobbyDetailsHandle)
{
    TRACE_FUNC();

    if (Options == nullptr || OutLobbyDetailsHandle == nullptr)
    {
        set_nullptr(OutLobbyDetailsHandle);
        return EOS_EResult::EOS_InvalidParameters;
    }

    auto it = _lobbies.find(Options->LobbyId);
    if (it == _lobbies.end())
    {
        *OutLobbyDetailsHandle = nullptr;
        return EOS_EResult::EOS_NotFound;
    }

    EOSSDK_LobbyDetails* pLobbyDetails = new EOSSDK_LobbyDetails;
    pLobbyDetails->_state = it->second._state;

    *OutLobbyDetailsHandle = reinterpret_cast<EOS_HLobbyDetails>(pLobbyDetails);

    return EOS_EResult::EOS_Success;
}

///////////////////////////////////////////////////////////////////////////////
//                           Network Send messages                           //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Lobby::send_to_all_members(Network_Message_pb& msg, lobby_state_t* lobby)
{
    TRACE_FUNC();
    assert(lobby != nullptr);

    for (auto const& member : lobby->infos.members())
    {
        if (member.first != msg.source_id())
        {
            msg.set_dest_id(member.first);
            GetNetwork().SendToPeer(msg);
        }
    }
    return true;
}

bool EOSSDK_Lobby::send_to_all_members_or_owner(Network_Message_pb& msg, lobby_state_t* lobby)
{
    TRACE_FUNC();
    
    assert(lobby != nullptr);

    if (i_am_owner(lobby))
    {
        return send_to_all_members(msg, lobby);
    }
    
    msg.set_dest_id(lobby->infos.owner_id());
    return GetNetwork().SendToPeer(msg);
}

bool EOSSDK_Lobby::send_lobby_update_to_peer(lobby_state_t* pLobby, Network::peer_t const& peer_id)
{
    if (pLobby == nullptr || peer_id.empty())
        return false;

    std::string const& user_id = Settings::Inst().productuserid->to_string();
    if (peer_id == user_id)
        return false;

    Lobby_Update_pb update_body;
    update_body.set_lobby_id(pLobby->infos.lobby_id());
    update_body.set_max_lobby_member(pLobby->infos.max_lobby_member());
    update_body.set_permission_level(pLobby->infos.permission_level());
    *update_body.mutable_attributes() = pLobby->infos.attributes();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby = new Lobby_Message_pb;
    Lobby_Update_pb* update = new Lobby_Update_pb;
    update->CopyFrom(update_body);
    lobby->set_allocated_lobby_update(update);
    msg.set_allocated_lobby(lobby);
    msg.set_source_id(user_id);
    msg.set_dest_id(peer_id);
    msg.set_game_id(Settings::Inst().network_game_id());
    return GetNetwork().TCPSendTo(msg);
}

void EOSSDK_Lobby::ingest_remote_lobby_from_search(Lobby_Infos_pb const& infos, Network::peer_t const& source_id)
{
    if (infos.lobby_id().empty())
        return;

    lobby_state_t* pLobby = get_lobby_by_id(infos.lobby_id());
    if (pLobby != nullptr && i_am_owner(pLobby))
        return;

    if (pLobby == nullptr)
    {
        auto& entry = _lobbies[infos.lobby_id()];
        entry._state.state = lobby_state_t::created;
        entry._state.infos = infos;
        if (entry._state.infos.owner_id().empty() && !source_id.empty())
            entry._state.infos.set_owner_id(source_id);
        pLobby = &entry._state;
    }
    else
    {
        pLobby->infos = infos;
        if (pLobby->infos.owner_id().empty() && !source_id.empty())
            pLobby->infos.set_owner_id(source_id);
    }

    if (lobby_attr_bool(pLobby->infos, "IsCrossPlatformPresence"))
    {
        if (i_am_owner(pLobby))
        {
            for (auto& lobby : _lobbies)
            {
                if (lobby.second._state.infos.owner_id() != pLobby->infos.owner_id())
                    continue;
                if (!lobby_is_game_namespace(lobby.second._state.infos))
                    continue;

                patch_presence_lobby_for_active_game(pLobby->infos, lobby.second._state.infos);
                break;
            }
        }
        else
        {
            patch_remote_presence_join_attrs(pLobby->infos);
        }
        patch_crossplatform_joinable_lobby(pLobby->infos);

        EOS_EpicAccountId owner_account = GetEpicUserId(pLobby->infos.owner_id());
        if (owner_account->IsValid())
            GetEOS_Presence().ensure_default_peer_presence(owner_account);
    }

    GetEOS_Sessions().sync_session_from_lobby(pLobby->infos);
    notify_lobby_update(pLobby);
}

void EOSSDK_Lobby::on_peer_authenticated(Network::peer_t const& peer_id)
{
    TRACE_FUNC();

    if (peer_id.empty() || peer_id == Settings::Inst().productuserid->to_string())
        return;

    unsigned pushed = 0;
    for (auto& lobby : _lobbies)
    {
        if (!i_am_owner(&lobby.second._state))
            continue;

        if (!lobby_attr_bool(lobby.second._state.infos, "IsCrossPlatformPresence"))
            continue;

        if (send_lobby_update_to_peer(&lobby.second._state, peer_id))
            ++pushed;
    }

    APP_LOG(Log::LogLevel::INFO, "Peer authenticated bootstrap: pushed %u presence lobby update(s) to %s",
        pushed, peer_id.c_str());

    refresh_owned_presence_for_active_game();
    flush_pending_lobby_searches();
}

bool EOSSDK_Lobby::send_lobby_update(lobby_state_t* pLobby)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Lobby_Update_pb update_body;
    update_body.set_lobby_id(pLobby->infos.lobby_id());
    update_body.set_max_lobby_member(pLobby->infos.max_lobby_member());
    update_body.set_permission_level(pLobby->infos.permission_level());
    *update_body.mutable_attributes() = pLobby->infos.attributes();

    bool const broadcast_presence = lobby_attr_bool(pLobby->infos, "IsCrossPlatformPresence");

    auto send_update_to = [&](Network::peer_t const& dest)
    {
        Network_Message_pb msg;
        Lobby_Message_pb* lobby = new Lobby_Message_pb;
        Lobby_Update_pb* update = new Lobby_Update_pb;
        update->CopyFrom(update_body);
        lobby->set_allocated_lobby_update(update);
        msg.set_allocated_lobby(lobby);
        msg.set_source_id(user_id);
        msg.set_dest_id(dest);
        msg.set_game_id(Settings::Inst().network_game_id());
        GetNetwork().TCPSendTo(msg);
    };

    for (auto const& member : pLobby->infos.members())
    {
        if (member.first != user_id)
            send_update_to(member.first);
    }

    if (broadcast_presence)
    {
        auto& users = GetEOS_Connect()._users;
        for (auto user_it = ++users.begin(); user_it != users.end(); ++user_it)
        {
            // Send presence lobby updates to all connected peers (not only fully authentified)
            // so that reversed join and crossplay scenarios work when the other side creates the lobby first.
            if (!user_it->second.connected)
                continue;

            std::string const& peer = user_it->first->to_string();
            if (peer == user_id || is_member_in_lobby(peer, pLobby))
                continue;

            send_update_to(peer);
        }

        GetEOS_Presence().send_my_presence_info_to_all_peers();
    }

    return true;
}

bool EOSSDK_Lobby::send_lobbies_search_response(Network::peer_t const& peerid, Lobbies_Search_response_pb* resp)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobbies_Search_Message_pb* search = new Lobbies_Search_Message_pb;

    search->set_allocated_search_response(resp);
    msg.set_allocated_lobbies_search(search);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().SendToPeer(msg);
}

bool EOSSDK_Lobby::send_lobby_join_request(Network::peer_t const& peerid, Lobby_Join_Request_pb* req)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;

    lobby_pb->set_allocated_lobby_join_request(req);
    msg.set_allocated_lobby(lobby_pb);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().SendToPeer(msg);
}

bool EOSSDK_Lobby::send_lobby_join_request_broadcast(Lobby_Join_Request_pb* req)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;

    lobby_pb->set_allocated_lobby_join_request(req);
    msg.set_allocated_lobby(lobby_pb);

    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    std::set<Network::peer_t> const tcp_peers = GetNetwork().TCPSendToAllPeers(msg);
    if (!tcp_peers.empty())
    {
        APP_LOG(Log::LogLevel::DEBUG, "Lobby join request broadcast via TCP to %u peers", static_cast<unsigned>(tcp_peers.size()));
        return true;
    }

    if (GetNetwork().SendBroadcast(msg))
    {
        APP_LOG(Log::LogLevel::DEBUG, "Lobby join request broadcast via UDP");
        return true;
    }

    return false;
}

bool EOSSDK_Lobby::send_lobby_join_response(Network::peer_t const& peerid, Lobby_Join_Response_pb* resp)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;

    lobby_pb->set_allocated_lobby_join_response(resp);
    msg.set_allocated_lobby(lobby_pb);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().SendToPeer(msg);
}

bool EOSSDK_Lobby::send_lobby_invite(Network::peer_t const& peerid, Lobby_Invite_pb* invite)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;

    lobby_pb->set_allocated_lobby_invite(invite);
    msg.set_allocated_lobby(lobby_pb);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    return GetNetwork().SendToPeer(msg);
}

bool EOSSDK_Lobby::send_lobby_member_update(Network::peer_t const& member_id, lobby_state_t* pLobby)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    auto it = pLobby->infos.members().find(member_id);
    if (it != pLobby->infos.members().end())
    {
        Network_Message_pb msg;
        Lobby_Message_pb* lobby = new Lobby_Message_pb;

        Lobby_Member_Update_pb* update = new Lobby_Member_Update_pb;
        update->set_lobby_id(pLobby->infos.lobby_id());
        (*update->mutable_member())[member_id] = it->second;

        lobby->set_allocated_member_update(update);
        msg.set_allocated_lobby(lobby);
        msg.set_source_id(user_id);
        msg.set_game_id(Settings::Inst().network_game_id());

        return send_to_all_members_or_owner(msg, pLobby);
    }
    return false;
}

bool EOSSDK_Lobby::send_lobby_member_join(Network::peer_t const& member_id, lobby_state_t* lobby)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;
    Lobby_Member_Join_pb* join = new Lobby_Member_Join_pb;

    join->set_lobby_id(lobby->infos.lobby_id());
    join->set_member_id(member_id);

    lobby_pb->set_allocated_member_join(join);
    msg.set_allocated_lobby(lobby_pb);
    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    return send_to_all_members(msg, lobby);
}

bool EOSSDK_Lobby::send_lobby_member_leave(Network::peer_t const& member_id, lobby_state_t* lobby, EOS_ELobbyMemberStatus reason)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;
    Lobby_Member_Leave_pb* leave = new Lobby_Member_Leave_pb;

    leave->set_lobby_id(lobby->infos.lobby_id());
    leave->set_member_id(member_id);
    leave->set_reason(utils::GetEnumValue(reason));

    lobby_pb->set_allocated_member_leave(leave);
    msg.set_allocated_lobby(lobby_pb);
    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    return send_to_all_members_or_owner(msg, lobby);
}

bool EOSSDK_Lobby::send_lobby_member_promote(Network::peer_t const& member_id, lobby_state_t* lobby)
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;
    Lobby_Member_Promote_pb* promote = new Lobby_Member_Promote_pb;

    promote->set_lobby_id(lobby->infos.lobby_id());
    promote->set_member_id(member_id);

    lobby_pb->set_allocated_member_promote(promote);
    msg.set_allocated_lobby(lobby_pb);

    msg.set_source_id(user_id);
    msg.set_game_id(Settings::Inst().network_game_id());

    // Only the lobby owner can promote, so send to all members
    return send_to_all_members(msg, lobby);
}

///////////////////////////////////////////////////////////////////////////////
//                          Network Receive messages                         //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Lobby::on_peer_disconnect(Network_Message_pb const& msg, Network_Peer_Disconnect_pb const& peer)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    for (auto& lobby : _lobbies)
    {
        if (i_am_owner(&lobby.second._state))
        {
            if (remove_member_from_lobby(msg.source_id(), &lobby.second._state))
            {
                std::string const& user_id = Settings::Inst().productuserid->to_string();

                Network_Message_pb msg_resp;
                Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;
                Lobby_Member_Leave_pb* member_leave = new Lobby_Member_Leave_pb;

                member_leave->set_lobby_id(lobby.second._state.infos.lobby_id());
                member_leave->set_member_id(msg.source_id());
                member_leave->set_reason(utils::GetEnumValue(EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED));

                lobby_pb->set_allocated_member_leave(member_leave);
                msg_resp.set_allocated_lobby(lobby_pb);

                msg_resp.set_source_id(user_id);

                send_to_all_members(msg_resp, &lobby.second._state);

                notify_lobby_member_status_update(msg.source_id(), EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED, &lobby.second._state);
            }
        }
    }

    return true;
}

bool EOSSDK_Lobby::on_lobby_update(Network_Message_pb const& msg, Lobby_Update_pb const& update)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_state_t* pLobby = get_lobby_by_id(update.lobby_id());

    if (pLobby == nullptr)
    {
        auto& entry = _lobbies[update.lobby_id()];
        entry._state.state = lobby_state_t::created;
        entry._state.infos.set_lobby_id(update.lobby_id());
        entry._state.infos.set_owner_id(msg.source_id());
        pLobby = &entry._state;
    }

    pLobby->infos.set_max_lobby_member(update.max_lobby_member());
    pLobby->infos.set_permission_level(update.permission_level());
    *pLobby->infos.mutable_attributes() = update.attributes();

    if (pLobby->infos.owner_id().empty())
        pLobby->infos.set_owner_id(msg.source_id());

    if (!i_am_owner(pLobby))
    {
        if (lobby_attr_bool(pLobby->infos, "IsCrossPlatformPresence"))
        {
            for (auto& lobby : _lobbies)
            {
                if (lobby.second._state.infos.owner_id() != pLobby->infos.owner_id())
                    continue;
                if (!lobby_is_game_namespace(lobby.second._state.infos))
                    continue;

                patch_presence_lobby_for_active_game(pLobby->infos, lobby.second._state.infos);
                break;
            }
            patch_crossplatform_joinable_lobby(pLobby->infos);

            EOS_EpicAccountId owner_account = GetEpicUserId(pLobby->infos.owner_id());
            if (owner_account->IsValid())
                GetEOS_Presence().ensure_default_peer_presence(owner_account);
        }

        GetEOS_Sessions().sync_session_from_lobby(pLobby->infos);
    }

    notify_lobby_update(pLobby);

    return true;
}

bool EOSSDK_Lobby::on_lobby_member_update(Network_Message_pb const& msg, Lobby_Member_Update_pb const& update)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_state_t* pLobby = get_lobby_by_id(update.lobby_id());

    if (pLobby != nullptr)
    {
        auto const& member = *update.member().begin();
        (*pLobby->infos.mutable_members())[member.first] = member.second;

        notify_lobby_member_update(member.first, pLobby);
    }

    return true;
}

bool EOSSDK_Lobby::on_lobbies_search(Network_Message_pb const& msg, Lobbies_Search_pb const& search)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (search.parameters_size() > 0)
    {
        for (auto const& param : search.parameters())
        {
            for (auto const& comp : param.second.param())
            {
                APP_LOG(Log::LogLevel::DEBUG, "Lobby search request param key='%s' op=%d value=%s from peer=%s",
                    param.first.c_str(),
                    comp.first,
                    lobby_attr_value_debug(comp.second).c_str(),
                    msg.source_id().c_str());
            }
        }
    }

    Lobbies_Search_response_pb* resp = new Lobbies_Search_response_pb;
    resp->set_search_id(search.search_id());

    if (Settings::Inst().matches_network_game_id(msg.game_id()))
    {
        fill_lobbies_search_response(resp, search);

        if (search.parameters_size() > 0)
        {
            APP_LOG(Log::LogLevel::INFO, "Lobby search by attributes: matched %u lobbies for peer=%s",
                static_cast<unsigned>(resp->lobbies_size()), msg.source_id().c_str());

            if (resp->lobbies_size() == 0 && should_defer_attribute_lobby_search(search))
            {
                APP_LOG(Log::LogLevel::INFO,
                    "Lobby search deferred: waiting for lobby attributes before replying (peer=%s)",
                    msg.source_id().c_str());
                delete resp;
                queue_pending_lobby_search(msg.source_id(), search);
                return true;
            }
        }
        else if (!search.lobby_id().empty())
        {
            if (resp->lobbies_size() > 0)
            {
                APP_LOG(Log::LogLevel::INFO, "Lobby search response: lobby_id=%s to peer=%s",
                    search.lobby_id().c_str(), msg.source_id().c_str());
            }
            else
            {
                APP_LOG(Log::LogLevel::INFO, "Lobby search by id: lobby %s not found (owner=0) for peer=%s",
                    search.lobby_id().c_str(), msg.source_id().c_str());
            }
        }
    }
    else
    {
        APP_LOG(Log::LogLevel::WARN, "Lobby search ignored: game_id mismatch remote=%s local=%s peer=%s",
            msg.game_id().c_str(),
            Settings::Inst().network_game_id().c_str(),
            msg.source_id().c_str());
    }

    return send_lobbies_search_response(msg.source_id(), resp);
}

bool EOSSDK_Lobby::on_lobby_join_request(Network_Message_pb const& msg, Lobby_Join_Request_pb const& req)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_state_t* pLobby = get_lobby_by_id(req.lobby_id());
    
    Lobby_Join_Response_pb* resp = new Lobby_Join_Response_pb;

    resp->set_join_id(req.join_id());

    if (pLobby == nullptr)
    {
        for (auto& lobby : _lobbies)
        {
            if (i_am_owner(&lobby.second._state))
            {
                pLobby = &lobby.second._state;
                APP_LOG(Log::LogLevel::INFO,
                    "Lobby join request: stale lobby_id='%s' from %s -> owned lobby_id='%s'",
                    req.lobby_id().c_str(),
                    msg.source_id().c_str(),
                    pLobby->infos.lobby_id().c_str());
                break;
            }
        }
    }

    if (pLobby == nullptr)
    {// Lobby not found
        resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_NotFound));
    }
    else
    {
        lobby_state_t* join_target = pLobby;
        if (i_am_owner(pLobby) &&
            lobby_attr_bool(pLobby->infos, "IsCrossPlatformPresence"))
        {
            if (lobby_state_t* game_lobby = find_owner_game_lobby(pLobby->infos.owner_id()))
                join_target = game_lobby;
        }

        if (i_am_owner(join_target))
        {// I am the owner, I can decide if the client can join
            bool const already_member = is_member_in_lobby(msg.source_id(), join_target);
            if (already_member || join_target->infos.max_lobby_member() > static_cast<uint32_t>(join_target->infos.members_size()))
            {// Can join (or re-sync an existing member)
                bool const newly_added = add_member_to_lobby(msg.source_id(), join_target);
                if (newly_added || is_member_in_lobby(msg.source_id(), join_target))
                {
                    prepare_lobby_infos_for_unity(join_target->infos);
                    update_lobby_public_member_list(join_target);

                    resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_Success));
                    *resp->mutable_infos() = join_target->infos;

                    if (newly_added)
                    {
                        notify_lobby_member_status_update(msg.source_id(), EOS_ELobbyMemberStatus::EOS_LMS_JOINED, join_target);
                        notify_lobby_update(join_target);
                        send_lobby_member_join(msg.source_id(), join_target);
                    }
                    send_lobby_update_to_peer(join_target, msg.source_id());
                    send_lobby_update(join_target);
                    GetEOS_Sessions().sync_session_from_lobby(join_target->infos);

                    if (join_target != pLobby && newly_added)
                    {
                        add_member_to_lobby(msg.source_id(), pLobby);
                        update_lobby_public_member_list(pLobby);
                        send_lobby_update_to_peer(pLobby, msg.source_id());
                    }

                    std::string session_id = lobby_attr_string(join_target->infos, "Redpoint:EOS:GameSessionId");
                    if (session_id.empty())
                    {
                        if (session_state_t* gs = GetEOS_Sessions().get_session_by_name("GameSession"))
                            session_id = gs->infos.session_id();
                    }
                    if (!session_id.empty())
                        admit_session_player_to_game_lobby(session_id, msg.source_id());
                }
                else
                {
                    resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_UnexpectedError));
                }
            }
            else
            {// Lobby full
                resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_Lobby_TooManyPlayers));
            }
        }
        else
        {// I'm not the owner, I don't have permissions to allow a client to join
            resp->set_reason(utils::GetEnumValue(EOS_EResult::EOS_Lobby_NoPermission));
        }
    }

    return send_lobby_join_response(msg.source_id(), resp);
}

bool EOSSDK_Lobby::on_lobby_join_response(Network_Message_pb const& msg, Lobby_Join_Response_pb const& resp)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    auto it = _joins_requests.find(resp.join_id());
    if (it == _joins_requests.end())
        return true;

    EOS_EResult const reason = static_cast<EOS_EResult>(resp.reason());
    if (reason != EOS_EResult::EOS_Success)
    {
        if (it->second.ignore_non_success &&
            (reason == EOS_EResult::EOS_NotFound || reason == EOS_EResult::EOS_Lobby_NoPermission))
        {
            APP_LOG(Log::LogLevel::DEBUG, "Lobby join response ignored (broadcast): join_id=%d reason=%d from=%s",
                resp.join_id(), static_cast<int>(reason), msg.source_id().c_str());
            _joins_requests.erase(it);
            return true;
        }

        if (it->second.cb != nullptr)
        {
            it->second.cb->done = true;
            switch (it->second.kind)
            {
                case lobby_join_kind_t::join_lobby:
                    it->second.cb->GetCallback<EOS_Lobby_JoinLobbyCallbackInfo>().ResultCode = reason;
                    break;
                case lobby_join_kind_t::join_lobby_by_id:
                    reinterpret_cast<EOS_Lobby_JoinLobbyByIdCallbackInfo*>(it->second.cb->GetFuncParam())->ResultCode = reason;
                    break;
            }
        }

        _joins_requests.erase(it);
        return true;
    }

    complete_pending_join_success(resp, msg, it->second);
    _joins_requests.erase(it);

    return true;
}

bool EOSSDK_Lobby::on_lobby_invite(Network_Message_pb const& msg, Lobby_Invite_pb const& invite)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_invite_t new_invite;
    new_invite.peer_id = GetProductUserId(msg.source_id());
    new_invite.infos = invite.infos();
    _lobby_invites.emplace(generate_account_id(), std::move(new_invite));

    auto& lobby_invite = *_lobby_invites.rbegin();
    notify_lobby_invite_received(lobby_invite.first, lobby_invite.second.peer_id);

    return true;
}

bool EOSSDK_Lobby::on_lobby_member_join(Network_Message_pb const& msg, Lobby_Member_Join_pb const& join)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_state_t* pLobby = get_lobby_by_id(join.lobby_id());
    if (pLobby != nullptr)
    {
        if (add_member_to_lobby(join.member_id(), pLobby))
            update_lobby_public_member_list(pLobby);

        notify_lobby_member_status_update(join.member_id(), EOS_ELobbyMemberStatus::EOS_LMS_JOINED, pLobby);
        notify_lobby_update(pLobby);
        GetEOS_Sessions().sync_session_from_lobby(pLobby->infos);
    }

    return true;
}

bool EOSSDK_Lobby::on_lobby_member_leave(Network_Message_pb const& msg, Lobby_Member_Leave_pb const& leave)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_state_t* pLobby = get_lobby_by_id(leave.lobby_id());
    if (pLobby != nullptr && remove_member_from_lobby(leave.member_id(), pLobby))
    {
        if (i_am_owner(pLobby))
        {// If I am the lobby owner, send the leave message to all clients
            std::string const& user_id = Settings::Inst().productuserid->to_string();

            Network_Message_pb msg_resp;
            Lobby_Message_pb* lobby_pb = new Lobby_Message_pb;
            Lobby_Member_Leave_pb* leave_pb = new Lobby_Member_Leave_pb(leave);

            lobby_pb->set_allocated_member_leave(leave_pb);
            msg_resp.set_allocated_lobby(lobby_pb);

            msg_resp.set_source_id(user_id);

            send_to_all_members(msg_resp, pLobby);
        }

        notify_lobby_member_status_update(leave.member_id(), (EOS_ELobbyMemberStatus)leave.reason(), pLobby);

        switch ((EOS_ELobbyMemberStatus)leave.reason())
        {
            case EOS_ELobbyMemberStatus::EOS_LMS_KICKED:
            {
                if (GetProductUserId(leave.member_id()) == Settings::Inst().productuserid)
                {// If I am the one behing kicked
                    _lobbies.erase(leave.lobby_id());
                }
            }
            break;
        }
    }

    return true;
}

bool EOSSDK_Lobby::on_lobby_member_promote(Network_Message_pb const& msg, Lobby_Member_Promote_pb const& promote)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    lobby_state_t* pLobby = get_lobby_by_id(promote.lobby_id());
    if (pLobby != nullptr && is_member_in_lobby(promote.member_id(), pLobby))
    {
        pLobby->infos.set_owner_id(promote.member_id());

        notify_lobby_member_status_update(promote.member_id(), EOS_ELobbyMemberStatus::EOS_LMS_PROMOTED, pLobby);
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//                                 IRunFrame                                 //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_Lobby::CBRunFrame()
{
    GLOBAL_LOCK();

    try_register_steam_bridge_callbacks();
    steam_bridge::run_callbacks();

    auto now = std::chrono::steady_clock::now();
    for (auto it = _joins_requests.begin(); it != _joins_requests.end();)
    {
        auto const created = it->second.cb != nullptr
            ? it->second.cb->created_time
            : it->second.created;

        if ((now - created) > join_timeout)
        {
            if (it->second.cb != nullptr)
            {
                it->second.cb->done = true;
                switch (it->second.kind)
                {
                    case lobby_join_kind_t::join_lobby:
                        it->second.cb->GetCallback<EOS_Lobby_JoinLobbyCallbackInfo>().ResultCode = EOS_EResult::EOS_TimedOut;
                        break;
                    case lobby_join_kind_t::join_lobby_by_id:
                        reinterpret_cast<EOS_Lobby_JoinLobbyByIdCallbackInfo*>(it->second.cb->GetFuncParam())->ResultCode = EOS_EResult::EOS_TimedOut;
                        break;
                }
            }
            APP_LOG(Log::LogLevel::WARN, "Lobby join timed out: join_id=%d", it->first);
            it = _joins_requests.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = _steam_bridge_joins.begin(); it != _steam_bridge_joins.end();)
    {
        if ((now - it->second.created) > join_timeout)
        {
            APP_LOG(Log::LogLevel::WARN,
                "Steam invite bridge: search_id=%d timed out eos_lobby='%s'",
                it->first,
                it->second.lobby_id.c_str());
            if (!it->second.lobby_id.empty())
                broadcast_steam_bridge_join_attempt(it->second.lobby_id);
            it = _steam_bridge_joins.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = _lobbies_searchs.begin(); it != _lobbies_searchs.end();)
    {
        if ((*it)->released())
        {
            delete *it;
            it = _lobbies_searchs.erase(it);
        }
        else
        {
            ++it;
        }
    }

    expire_pending_lobby_searches();

    return true;
}

bool EOSSDK_Lobby::RunNetwork(Network_Message_pb const& msg)
{
    switch (msg.messages_case())
    {
        case Network_Message_pb::MessagesCase::kLobby:
        {
            Lobby_Message_pb const& lobby = msg.lobby();
            switch (lobby.message_case())
            {
                case Lobby_Message_pb::MessageCase::kLobbyUpdate      : return on_lobby_update        (msg, lobby.lobby_update());
                case Lobby_Message_pb::MessageCase::kLobbyJoinRequest : return on_lobby_join_request  (msg, lobby.lobby_join_request());
                case Lobby_Message_pb::MessageCase::kLobbyJoinResponse: return on_lobby_join_response (msg, lobby.lobby_join_response());
                case Lobby_Message_pb::MessageCase::kLobbyInvite      : return on_lobby_invite        (msg, lobby.lobby_invite());

                case Lobby_Message_pb::MessageCase::kMemberUpdate     : return on_lobby_member_update (msg, lobby.member_update());
                case Lobby_Message_pb::MessageCase::kMemberJoin       : return on_lobby_member_join   (msg, lobby.member_join());
                case Lobby_Message_pb::MessageCase::kMemberLeave      : return on_lobby_member_leave  (msg, lobby.member_leave());
                case Lobby_Message_pb::MessageCase::kMemberPromote    : return on_lobby_member_promote(msg, lobby.member_promote());
            }
        }
        break;

        case Network_Message_pb::MessagesCase::kLobbiesSearch:
        {
            Lobbies_Search_Message_pb const& search = msg.lobbies_search();
            switch (search.message_case())
            {
                case Lobbies_Search_Message_pb::MessageCase::kSearch: return on_lobbies_search(msg, search.search());
                case Lobbies_Search_Message_pb::MessageCase::kSearchResponse: return on_steam_bridge_lobbies_search_response(msg, search.search_response());
            }
        }
        break;
    }

    return true;
}

bool EOSSDK_Lobby::RunCallbacks(pFrameResult_t res)
{
    GLOBAL_LOCK();

    return res->done;;
}

void EOSSDK_Lobby::FreeCallback(pFrameResult_t res)
{
    GLOBAL_LOCK();

    switch (res->ICallback())
    {
        /////////////////////////////
        //        Callbacks        //
        /////////////////////////////
        case EOS_Lobby_CreateLobbyCallbackInfo::k_iCallback:
        {
            EOS_Lobby_CreateLobbyCallbackInfo& callback = res->GetCallback<EOS_Lobby_CreateLobbyCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_DestroyLobbyCallbackInfo::k_iCallback:
        {
            EOS_Lobby_DestroyLobbyCallbackInfo& callback = res->GetCallback<EOS_Lobby_DestroyLobbyCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_JoinLobbyCallbackInfo::k_iCallback:
        {
            EOS_Lobby_JoinLobbyCallbackInfo& callback = res->GetCallback<EOS_Lobby_JoinLobbyCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case k_iJoinLobbyByIdCallback:
        {
            auto& callback = *reinterpret_cast<EOS_Lobby_JoinLobbyByIdCallbackInfo*>(res->GetFuncParam());
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_LeaveLobbyCallbackInfo::k_iCallback:
        {
            EOS_Lobby_LeaveLobbyCallbackInfo& callback = res->GetCallback<EOS_Lobby_LeaveLobbyCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_UpdateLobbyCallbackInfo::k_iCallback:
        {
            EOS_Lobby_UpdateLobbyCallbackInfo& callback = res->GetCallback<EOS_Lobby_UpdateLobbyCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_PromoteMemberCallbackInfo::k_iCallback:
        {
            EOS_Lobby_PromoteMemberCallbackInfo& callback = res->GetCallback<EOS_Lobby_PromoteMemberCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_KickMemberCallbackInfo::k_iCallback:
        {
            EOS_Lobby_KickMemberCallbackInfo& callback = res->GetCallback<EOS_Lobby_KickMemberCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_SendInviteCallbackInfo::k_iCallback:
        {
            EOS_Lobby_SendInviteCallbackInfo& callback = res->GetCallback<EOS_Lobby_SendInviteCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_RejectInviteCallbackInfo::k_iCallback:
        {
            EOS_Lobby_RejectInviteCallbackInfo& callback = res->GetCallback<EOS_Lobby_RejectInviteCallbackInfo>();
            // Free resources
            delete[]callback.InviteId;
        }
        break;

        /////////////////////////////
        //      Notifications      //
        /////////////////////////////
        case EOS_Lobby_LobbyUpdateReceivedCallbackInfo::k_iCallback:
        {
            EOS_Lobby_LobbyUpdateReceivedCallbackInfo& callback = res->GetCallback<EOS_Lobby_LobbyUpdateReceivedCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo::k_iCallback:
        {
            EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo& callback = res->GetCallback<EOS_Lobby_LobbyMemberUpdateReceivedCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo::k_iCallback:
        {
            EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo& callback = res->GetCallback<EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo>();
            // Free resources
            delete[]callback.LobbyId;
        }
        break;

        case EOS_Lobby_LobbyInviteReceivedCallbackInfo::k_iCallback:
        {
            EOS_Lobby_LobbyInviteReceivedCallbackInfo& callback = res->GetCallback<EOS_Lobby_LobbyInviteReceivedCallbackInfo>();
            // Free resources
            delete[]callback.InviteId;
        }
        break;

        case EOS_Lobby_LobbyInviteAcceptedCallbackInfo::k_iCallback:
        {
            EOS_Lobby_LobbyInviteAcceptedCallbackInfo& callback = res->GetCallback<EOS_Lobby_LobbyInviteAcceptedCallbackInfo>();
            // Free resources
            delete[]callback.InviteId;
        }
        break;

    }
}

}