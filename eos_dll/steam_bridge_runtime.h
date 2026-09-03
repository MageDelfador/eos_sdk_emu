/*
 * Copyright (C) 2020 Nemirtingas
 * Runtime bridge to gbe_fork / steam_api in the same process (GetProcAddress only).
 */

#ifndef __INCLUDED_STEAM_BRIDGE_RUNTIME_H__
#define __INCLUDED_STEAM_BRIDGE_RUNTIME_H__

#include <cstdint>
#include <string>

namespace steam_bridge
{

constexpr char kRichPresenceConnectKey[] = "connect";

/** True when steam_api64.dll or steam_api.dll is mapped in this process. */
bool module_loaded();

/** Resolve steam_api from mapped modules or load from the game directory if needed. */
bool ensure_module();

/** Module handle for steam_api in this process, or nullptr. */
void* module_handle();

/** GetProcAddress against the resolved steam_api module. */
void* get_export(char const* name);

/** Idempotent SteamAPI_Init when export is present. */
bool try_init();

/** Local SteamID64 string from ISteamUser::GetSteamID, or empty if unavailable. */
std::string local_steam_id(bool refresh = false);

struct FriendJoinHints
{
    std::string connect_string;
    uint64_t steam_lobby_id = 0;
};

/** Read friend's rich presence "connect" and/or lobby id from ISteamFriends. */
FriendJoinHints friend_join_hints(uint64_t steam_friend_id);

/** Set/clear rich presence key "connect" (gbe_fork LAN friend sync). */
bool set_rich_presence_connect(char const* connect_value);
void clear_rich_presence_connect();

/** Pump SteamAPI callbacks (GameRichPresenceJoinRequested, etc.). No-op if steam_api is absent. */
void run_callbacks();

} // namespace steam_bridge

#endif
