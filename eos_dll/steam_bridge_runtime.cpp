/*
 * Copyright (C) 2020 Nemirtingas
 * Runtime bridge to gbe_fork / steam_api in the same process (GetProcAddress only).
 */

#include "steam_bridge_runtime.h"

#include "log.h"
#include "os_funcs.h"

#include <chrono>
#include <cstring>
#include <vector>

#if defined(__WINDOWS__)
#include <windows.h>
#include <tlhelp32.h>
#endif

namespace steam_bridge
{
namespace
{

#if defined(__WINDOWS__)
static std::string g_last_rich_presence_connect;
static HMODULE g_steam_module = nullptr;

using steam_api_init_t = bool(__cdecl*)();
using steam_user_v_t = void*(__cdecl*)();
using get_steam_id_t = uint64_t(__cdecl*)(void*);
using steam_friends_v_t = void*(__cdecl*)();
using set_rich_presence_t = bool(__cdecl*)(void*, char const*, char const*);
using clear_rich_presence_t = void(__cdecl*)(void*);
using get_friend_rich_presence_t = char const*(__cdecl*)(void*, uint64_t, char const*);
using get_friend_game_played_t = bool(__cdecl*)(void*, uint64_t, void*);
using run_callbacks_t = void(__cdecl*)();

constexpr char const* kSteamUserExports[] = {
    "SteamAPI_SteamUser_v023",
    "SteamAPI_SteamUser_v022",
    "SteamAPI_SteamUser_v021",
    "SteamAPI_SteamUser_v020",
};

constexpr char const* kSteamFriendsExports[] = {
    "SteamAPI_SteamFriends_v017",
    "SteamAPI_SteamFriends_v016",
    "SteamAPI_SteamFriends_v015",
};

#pragma pack(push, 1)
struct FriendGameInfoFlat
{
    uint64_t game_id;
    uint32_t game_ip;
    uint16_t game_port;
    uint16_t query_port;
    uint64_t steam_id_lobby;
};
#pragma pack(pop)

static std::string join_path(std::string const& dir, char const* file)
{
    if (dir.empty())
        return file;

    char const last = dir.back();
    if (last == '\\' || last == '/')
        return dir + file;

    return dir + "\\" + file;
}

static bool module_exports_steam_api(HMODULE mod)
{
    if (mod == nullptr)
        return false;

    return GetProcAddress(mod, "SteamAPI_RegisterCallback") != nullptr ||
        GetProcAddress(mod, "SteamAPI_Init") != nullptr;
}

static HMODULE find_mapped_steam_module()
{
    for (char const* name : { "steam_api64.dll", "steam_api.dll" })
    {
        HMODULE const mod = GetModuleHandleA(name);
        if (module_exports_steam_api(mod))
            return mod;
    }

    HANDLE const snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return nullptr;

    HMODULE found = nullptr;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snap, &entry))
    {
        do
        {
            HMODULE const mod = entry.hModule;
            if (module_exports_steam_api(mod))
            {
                found = mod;
                break;
            }
        } while (Module32NextW(snap, &entry));
    }

    CloseHandle(snap);
    return found;
}

static std::vector<std::string> steam_dll_load_candidates()
{
    std::vector<std::string> paths;
    std::string const exe_path = get_executable_path();
    std::string exe_dir;
    if (!exe_path.empty())
    {
        size_t const slash = exe_path.find_last_of("\\/");
        exe_dir = slash == std::string::npos ? std::string{} : exe_path.substr(0, slash);
    }

    if (!exe_dir.empty())
    {
        for (char const* name : { "steam_api64.dll", "steam_api.dll" })
            paths.push_back(join_path(exe_dir, name));
    }

    return paths;
}

static bool try_load_steam_module_from_disk()
{
    for (std::string const& path : steam_dll_load_candidates())
    {
        if (path.empty())
            continue;

        HMODULE const loaded = LoadLibraryA(path.c_str());
        if (!module_exports_steam_api(loaded))
        {
            if (loaded != nullptr)
                FreeLibrary(loaded);
            continue;
        }

        APP_LOG(Log::LogLevel::INFO, "Steam bridge: loaded steam_api from %s", path.c_str());
        return true;
    }

    return false;
}

static HMODULE steam_module_handle()
{
    if (g_steam_module != nullptr)
        return g_steam_module;

    g_steam_module = find_mapped_steam_module();
    return g_steam_module;
}

static bool ensure_steam_module_loaded()
{
    if (steam_module_handle() != nullptr)
        return true;

    static std::chrono::steady_clock::time_point last_load_attempt{};
    auto const now = std::chrono::steady_clock::now();
    if (last_load_attempt.time_since_epoch().count() != 0 &&
        (now - last_load_attempt) < std::chrono::seconds(2))
    {
        return false;
    }

    last_load_attempt = now;
    if (!try_load_steam_module_from_disk())
        return false;

    g_steam_module = find_mapped_steam_module();
    return g_steam_module != nullptr;
}

static void* resolve_export(char const* name)
{
    HMODULE const steam = steam_module_handle();
    if (steam == nullptr || name == nullptr)
        return nullptr;
    return reinterpret_cast<void*>(GetProcAddress(steam, name));
}

void* steam_user_interface()
{
    for (char const* export_name : kSteamUserExports)
    {
        auto steam_user = reinterpret_cast<steam_user_v_t>(resolve_export(export_name));
        if (steam_user == nullptr)
            continue;

        void* user = steam_user();
        if (user != nullptr)
            return user;
    }
    return nullptr;
}

void* steam_friends_interface()
{
    for (char const* export_name : kSteamFriendsExports)
    {
        auto steam_friends = reinterpret_cast<steam_friends_v_t>(resolve_export(export_name));
        if (steam_friends == nullptr)
            continue;

        void* friends = steam_friends();
        if (friends != nullptr)
            return friends;
    }
    return nullptr;
}

#endif // __WINDOWS__

} // namespace

bool module_loaded()
{
#if defined(__WINDOWS__)
    return steam_module_handle() != nullptr;
#else
    return false;
#endif
}

bool ensure_module()
{
#if defined(__WINDOWS__)
    return ensure_steam_module_loaded();
#else
    return false;
#endif
}

void* module_handle()
{
#if defined(__WINDOWS__)
    return steam_module_handle();
#else
    return nullptr;
#endif
}

void* get_export(char const* name)
{
#if defined(__WINDOWS__)
    if (steam_module_handle() == nullptr)
        return nullptr;

    if (name == nullptr)
        return nullptr;

    return reinterpret_cast<void*>(GetProcAddress(steam_module_handle(), name));
#else
    (void)name;
    return nullptr;
#endif
}

bool try_init()
{
#if defined(__WINDOWS__)
    if (!ensure_module())
        return false;

    auto init = reinterpret_cast<steam_api_init_t>(get_export("SteamAPI_Init"));
    if (init == nullptr)
        return false;
    return init();
#else
    return false;
#endif
}

std::string local_steam_id(bool refresh)
{
#if defined(__WINDOWS__)
    static std::string cached;
    static bool have_cache = false;

    if (!refresh && have_cache)
        return cached;

    if (!ensure_module())
    {
        cached.clear();
        have_cache = false;
        return {};
    }

    try_init();

    auto get_steam_id = reinterpret_cast<get_steam_id_t>(get_export("SteamAPI_ISteamUser_GetSteamID"));
    if (get_steam_id == nullptr)
        return have_cache ? cached : std::string{};

    void* const user = steam_user_interface();
    if (user == nullptr)
        return have_cache ? cached : std::string{};

    uint64_t const id = get_steam_id(user);
    if (id == 0)
        return have_cache ? cached : std::string{};

    cached = std::to_string(id);
    have_cache = true;
    return cached;
#else
    (void)refresh;
    return {};
#endif
}

FriendJoinHints friend_join_hints(uint64_t steam_friend_id)
{
    FriendJoinHints hints;
    if (steam_friend_id == 0)
        return hints;

#if defined(__WINDOWS__)
    if (!ensure_module())
        return hints;

    try_init();

    void* const friends = steam_friends_interface();
    if (friends == nullptr)
        return hints;

    auto get_rich = reinterpret_cast<get_friend_rich_presence_t>(
        get_export("SteamAPI_ISteamFriends_GetFriendRichPresence"));
    if (get_rich != nullptr)
    {
        char const* value = get_rich(friends, steam_friend_id, kRichPresenceConnectKey);
        if (value != nullptr && value[0] != '\0')
            hints.connect_string = value;
    }

    auto get_played = reinterpret_cast<get_friend_game_played_t>(
        get_export("SteamAPI_ISteamFriends_GetFriendGamePlayed"));
    if (get_played != nullptr)
    {
        FriendGameInfoFlat info{};
        if (get_played(friends, steam_friend_id, &info) && info.steam_id_lobby != 0)
            hints.steam_lobby_id = info.steam_id_lobby;
    }
#endif

    return hints;
}

bool set_rich_presence_connect(char const* connect_value)
{
    if (connect_value == nullptr || connect_value[0] == '\0')
        return false;

#if defined(__WINDOWS__)
    if (!ensure_module())
        return false;

    try_init();

    void* const friends = steam_friends_interface();
    if (friends == nullptr)
        return false;

    auto set_rich = reinterpret_cast<set_rich_presence_t>(
        get_export("SteamAPI_ISteamFriends_SetRichPresence"));
    if (set_rich == nullptr)
        return false;

    if (g_last_rich_presence_connect == connect_value)
        return true;

    bool const ok = set_rich(friends, kRichPresenceConnectKey, connect_value);
    if (ok)
        g_last_rich_presence_connect = connect_value;
    APP_LOG(Log::LogLevel::DEBUG, "Steam bridge: SetRichPresence connect='%s' ok=%d", connect_value, ok ? 1 : 0);
    return ok;
#else
    (void)connect_value;
    return false;
#endif
}

void clear_rich_presence_connect()
{
#if defined(__WINDOWS__)
    g_last_rich_presence_connect.clear();

    if (!module_loaded())
        return;

    try_init();

    void* const friends = steam_friends_interface();
    if (friends == nullptr)
        return;

    auto clear_rich = reinterpret_cast<clear_rich_presence_t>(
        get_export("SteamAPI_ISteamFriends_ClearRichPresence"));
    if (clear_rich != nullptr)
        clear_rich(friends);
    else
    {
        auto set_rich = reinterpret_cast<set_rich_presence_t>(
            get_export("SteamAPI_ISteamFriends_SetRichPresence"));
        if (set_rich != nullptr)
            set_rich(friends, kRichPresenceConnectKey, nullptr);
    }

    APP_LOG(Log::LogLevel::INFO, "Steam bridge: cleared rich presence connect");
#endif
}

void run_callbacks()
{
#if defined(__WINDOWS__)
    if (!module_loaded())
        return;

    auto run = reinterpret_cast<run_callbacks_t>(get_export("SteamAPI_RunCallbacks"));
    if (run == nullptr)
        return;

    run();
#endif
}

} // namespace steam_bridge
