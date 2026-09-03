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

#include "gse_steam_config.h"

namespace
{
    static std::string trim(std::string value)
    {
        auto not_space = [](unsigned char c) { return !std::isspace(c); };
        value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
        value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
        return value;
    }

    static bool read_text_file(std::string const& path_utf8, std::string& out)
    {
        out.clear();
        if (path_utf8.empty())
            return false;

#if defined(__WINDOWS__)
        std::wstring wpath;
        utf8::utf8to16(path_utf8.begin(), path_utf8.end(), std::back_inserter(wpath));
        FILE* file = _wfopen(wpath.c_str(), L"rb");
#else
        FILE* file = fopen(path_utf8.c_str(), "rb");
#endif
        if (file == nullptr)
            return false;

        if (fseek(file, 0, SEEK_END) != 0)
        {
            fclose(file);
            return false;
        }

        long const size = ftell(file);
        if (size <= 0 || size > 1024 * 1024)
        {
            fclose(file);
            return false;
        }

        if (fseek(file, 0, SEEK_SET) != 0)
        {
            fclose(file);
            return false;
        }

        out.resize(static_cast<size_t>(size));
        size_t const read = fread(out.data(), 1, out.size(), file);
        fclose(file);
        if (read != out.size())
        {
            out.clear();
            return false;
        }

        return true;
    }

    static bool parse_gse_user_ini(std::string const& content, gse_steam_user_config_t& config)
    {
        std::string current_section;
        std::istringstream stream(content);
        std::string line;

        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            line = trim(line);
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                current_section = line.substr(1, line.size() - 2);
                continue;
            }

            if (current_section != "user::general")
                continue;

            size_t const eq = line.find('=');
            if (eq == std::string::npos)
                continue;

            std::string const key = trim(line.substr(0, eq));
            std::string const value = trim(line.substr(eq + 1));
            if (key == "account_name")
                config.account_name = value;
            else if (key == "account_steamid")
                config.account_steamid = value;
        }

        return config.has_account_name() || config.has_account_steamid();
    }

    static std::string get_current_directory()
    {
#if defined(__WINDOWS__)
        wchar_t buf[MAX_PATH];
        DWORD const len = GetCurrentDirectoryW(MAX_PATH, buf);
        if (len == 0 || len >= MAX_PATH)
            return std::string();

        std::string out;
        utf8::utf16to8(buf, buf + len, std::back_inserter(out));
        return out;
#else
        char buf[PATH_MAX];
        if (getcwd(buf, sizeof(buf)) == nullptr)
            return std::string();
        return std::string(buf);
#endif
    }

    static void append_unique_path(std::vector<std::string>& paths, std::string path)
    {
        if (path.empty())
            return;

        path = FileManager::canonical_path(path);
        for (auto const& existing : paths)
        {
            if (existing == path)
                return;
        }

        paths.emplace_back(std::move(path));
    }

    static void append_steamworks_paths_from_root(std::vector<std::string>& paths, std::string const& steamworks_root)
    {
        if (!FileManager::is_dir(steamworks_root))
            return;

        for (auto const& version_dir : FileManager::list_files(steamworks_root, false))
        {
            append_unique_path(paths,
                FileManager::join(steamworks_root, version_dir, "Win64", "steam_settings", "steam_appid.txt"));
        }
    }

    static void append_ue_steamworks_paths(std::vector<std::string>& paths, std::string const& exe_dir)
    {
        if (exe_dir.empty())
            return;

        append_steamworks_paths_from_root(paths, FileManager::canonical_path(
            FileManager::join(exe_dir, "..", "..", "Engine", "Binaries", "ThirdParty", "Steamworks")));
        append_steamworks_paths_from_root(paths, FileManager::canonical_path(
            FileManager::join(exe_dir, "..", "..", "..", "Engine", "Binaries", "ThirdParty", "Steamworks")));
    }

    static void append_steam_api_module_paths(std::vector<std::string>& paths)
    {
        for (char const* dll_name : { "steam_api64.dll", "steam_api.dll" })
        {
            void* const module = get_module_handle(dll_name);
            if (module == nullptr)
                continue;

            std::string const module_dir = FileManager::dirname(get_module_path_for_handle(module));
            append_unique_path(paths, FileManager::join(module_dir, "steam_settings", "steam_appid.txt"));
            append_unique_path(paths, FileManager::join(module_dir, "steam_appid.txt"));
        }
    }

    static std::vector<std::string> steam_appid_file_candidates()
    {
        std::vector<std::string> paths;
        std::string const exe_dir = FileManager::dirname(get_executable_path());
        std::string const module_dir = FileManager::dirname(get_module_path());
        std::string const cwd = get_current_directory();

        append_steam_api_module_paths(paths);

        for (auto const& base : { exe_dir, module_dir, cwd })
        {
            if (base.empty())
                continue;

            append_unique_path(paths, FileManager::join(base, "steam_settings", "steam_appid.txt"));
        }

        append_ue_steamworks_paths(paths, exe_dir);

        append_unique_path(paths, FileManager::join(exe_dir, "steam_appid.txt"));
        append_unique_path(paths, FileManager::join(module_dir, "steam_appid.txt"));
        append_unique_path(paths, "steam_appid.txt");

        return paths;
    }

    static bool parse_steam_app_id_text(std::string const& text, std::string& out_app_id)
    {
        std::string value = trim(text);
        if (value.empty())
            return false;

        try
        {
            unsigned long const appid = std::stoul(value);
            if (appid == 0)
                return false;

            out_app_id = std::to_string(appid);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    static bool try_read_steam_appid_file(std::string const& path, std::string& out_app_id)
    {
        std::string content;
        if (!read_text_file(path, content))
            return false;

        if (!parse_steam_app_id_text(content, out_app_id))
            return false;

        APP_LOG(Log::LogLevel::INFO, "Steam AppId from %s: %s", path.c_str(), out_app_id.c_str());
        return true;
    }

    static bool try_steam_app_id_from_env(std::string& out_app_id)
    {
        for (char const* var : { "SteamAppId", "SteamGameId", "SteamOverlayGameId" })
        {
            std::string const value = trim(get_env_var(var));
            if (parse_steam_app_id_text(value, out_app_id))
            {
                APP_LOG(Log::LogLevel::INFO, "Steam AppId from %s: %s", var, out_app_id.c_str());
                return true;
            }
        }

        return false;
    }

#if defined(__WINDOWS__)
    static bool try_steam_app_id_from_api(std::string& out_app_id)
    {
        using SteamUtilsFactoryFn = void* (*)();
        using GetAppIDFn = uint32_t (*)(void*);

        for (char const* dll_name : { "steam_api64.dll", "steam_api.dll" })
        {
            void* const module = get_module_handle(dll_name);
            if (module == nullptr)
                continue;

            auto const get_app_id = reinterpret_cast<GetAppIDFn>(
                GetProcAddress(static_cast<HMODULE>(module), "SteamAPI_ISteamUtils_GetAppID"));
            if (get_app_id == nullptr)
                continue;

            for (char const* utils_export : {
                "SteamAPI_SteamUtils_v010",
                "SteamAPI_SteamUtils_v009",
                "SteamAPI_SteamUtils_v008",
            })
            {
                auto const utils_factory = reinterpret_cast<SteamUtilsFactoryFn>(
                    GetProcAddress(static_cast<HMODULE>(module), utils_export));
                if (utils_factory == nullptr)
                    continue;

                void* const utils = utils_factory();
                if (utils == nullptr)
                    continue;

                uint32_t const appid = get_app_id(utils);
                if (appid == 0)
                    continue;

                out_app_id = std::to_string(appid);
                APP_LOG(Log::LogLevel::INFO, "Steam AppId from %s (%s): %s",
                    dll_name, utils_export, out_app_id.c_str());
                return true;
            }
        }

        return false;
    }
#else
    static bool try_steam_app_id_from_api(std::string&)
    {
        return false;
    }
#endif
}

LOCAL_API std::string gse_steam_user_config_path()
{
    std::string const appdata = get_userdata_path();
    if (appdata.empty())
        return std::string();

    return FileManager::join(appdata, "GSE Saves", "settings", "configs.user.ini");
}

LOCAL_API bool is_valid_steam64(std::string const& steam64)
{
    if (steam64.empty() || steam64.size() > 20)
        return false;

    if (!std::all_of(steam64.begin(), steam64.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return false;

    try
    {
        uint64_t const value = std::stoull(steam64);
        return value >= 76561197960265728ULL;
    }
    catch (...)
    {
        return false;
    }
}

LOCAL_API bool try_load_gse_steam_user_config(gse_steam_user_config_t& config)
{
    config = {};

    std::string const config_path = gse_steam_user_config_path();
    if (config_path.empty())
        return false;

    std::string content;
    if (!read_text_file(config_path, content))
        return false;

    if (!parse_gse_user_ini(content, config))
        return false;

    APP_LOG(Log::LogLevel::INFO, "Loaded GSE steam config: %s", config_path.c_str());
    return true;
}

LOCAL_API bool try_resolve_steam_app_id(std::string& out_app_id)
{
    out_app_id.clear();

    for (auto const& path : steam_appid_file_candidates())
    {
        if (try_read_steam_appid_file(path, out_app_id))
            return true;
    }

    if (try_steam_app_id_from_env(out_app_id))
        return true;

    return try_steam_app_id_from_api(out_app_id);
}
