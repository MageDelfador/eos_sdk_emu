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

#include "settings.h"
#include "eos_client_api.h"
#include "eos_sdk_version.h"
#include "gse_steam_config.h"

namespace
{
    constexpr char default_custom_broadcast[] = "";
    constexpr char default_language[] = "zh";

    bool is_epic_account_id(std::string const& id)
    {
        if (id.size() != 32)
            return false;

        return std::all_of(id.begin(), id.end(), [](char c)
        {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        });
    }

}

template<typename T>
T get_setting(nlohmann::json& settings, std::string const& key, T default_val)
{
    T val;
    try
    {
        val = settings[key].get<T>();
    }
    catch (...)
    {
        val = default_val;
        settings[key] = default_val;
    }
    return val;
}

Settings::Settings()
{
    try
    {
        load_settings();
    }
    catch (...)
    {
#ifndef DISABLE_LOG
        OutputDebugStringA("NemirtingasEpicEmu: settings init failed, using defaults\n");
#endif
        try
        {
            load_settings_defaults();
        }
        catch (...)
        {
            std::string const appdata_dir = FileManager::join(get_userdata_path(), emu_savepath);
            config_path = !appdata_dir.empty()
                ? FileManager::join(appdata_dir, settings_file_name)
                : std::string();
#ifndef DISABLE_LOG
            Log::set_loglevel(Log::LogLevel::OFF);
#endif
            username = u8"DefaultName";
            userid = GetInvalidEpicUserId();
            productuserid = GetInvalidProductUserId();
            language = default_language;
            gamename = "DefaultGameName";
            appid = "InvalidAppid";
            unlock_dlcs = true;
            disable_online_networking = false;
            custom_broadcast = default_custom_broadcast;
            savepath = "appdata";
        }
    }
}

Settings::~Settings()
{

}

static bool load_json(std::string const& file_path, nlohmann::json& json)
{
    try
    {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            APP_LOG(Log::LogLevel::WARN, "File not found: %s", file_path.c_str());
            return false;
        }

        std::streamoff const size = file.tellg();
        if (size <= 0 || size > 1024 * 1024)
        {
            APP_LOG(Log::LogLevel::ERR, "Invalid settings file size %lld: %s", static_cast<long long>(size), file_path.c_str());
            return false;
        }

        file.seekg(0, std::ios::beg);
        std::string buffer(static_cast<size_t>(size), '\0');
        if (!file.read(buffer.data(), size))
        {
            APP_LOG(Log::LogLevel::ERR, "Failed to read: %s", file_path.c_str());
            return false;
        }

        json = nlohmann::json::parse(buffer, nullptr, false);
        if (json.is_discarded())
        {
            APP_LOG(Log::LogLevel::ERR, "Error while parsing JSON %s", file_path.c_str());
            return false;
        }

        return true;
    }
    catch (...)
    {
        APP_LOG(Log::LogLevel::ERR, "Error while loading JSON %s", file_path.c_str());
        return false;
    }
}

static bool save_json(std::string const& file_path, nlohmann::json const& json)
{
    try
    {
        std::ofstream file(file_path, std::ios::trunc | std::ios::out);
        if (!file)
        {
            APP_LOG(Log::LogLevel::ERR, "Failed to save: %s", file_path.c_str());
            return false;
        }
        file << std::setw(2) << json;
        return static_cast<bool>(file);
    }
    catch (...)
    {
        APP_LOG(Log::LogLevel::ERR, "Failed to save: %s", file_path.c_str());
        return false;
    }
}

Settings& Settings::Inst()
{
    static Settings inst;
    return inst;
}

void Settings::apply_save_directories()
{
    std::string settings_dir;
    if (savepath == "appdata")
    {
        settings_dir = get_userdata_path();
    }
    else if (FileManager::is_absolute(savepath))
    {
        settings_dir = savepath;
    }
    else
    {
        settings_dir = FileManager::join(FileManager::dirname(get_executable_path()), savepath);
    }

    FileManager::set_root_dir(settings_dir);
    settings_dir = FileManager::root_dir();
    settings_dir = FileManager::join(settings_dir, emu_savepath, userid->to_string());

    FileManager::set_root_dir(FileManager::join(settings_dir, appid));
}

void Settings::load_settings_defaults()
{
    GLOBAL_LOCK();

    std::string const appdata_dir = FileManager::join(get_userdata_path(), emu_savepath);
    std::string const exe_dir = FileManager::dirname(get_executable_path());
    config_path = !appdata_dir.empty()
        ? FileManager::join(appdata_dir, settings_file_name)
        : FileManager::join(exe_dir, settings_file_name);

#ifndef DISABLE_LOG
    Log::set_loglevel(Log::LogLevel::OFF);
#endif

    username = u8"DefaultName";
    userid = GetEpicUserId(generate_epic_id_user());
    language = default_language;
    gamename = "DefaultGameName";
    appid = "InvalidAppid";
    unlock_dlcs = true;
    disable_online_networking = false;
    custom_broadcast = default_custom_broadcast;
    savepath = "appdata";
    productuserid = GetProductUserId(generate_productuserid_from_epicid(userid->to_string()));

    if (try_resolve_steam_app_id(steam_appid))
    {
        APP_LOG(Log::LogLevel::INFO, "Resolved Steam AppId %s (config appid stays %s until EOS ProductId is known)",
            steam_appid.c_str(), appid.c_str());
    }

    apply_save_directories();
    save_settings();
}

void Settings::load_settings()
{
    bool default_config = false;

    GLOBAL_LOCK();

    nlohmann::json settings;
    std::string const appdata_dir = FileManager::join(get_userdata_path(), emu_savepath);
    std::string const module_dir = FileManager::dirname(get_module_path());
    std::string const exe_dir = FileManager::dirname(get_executable_path());
    std::string const appdata_config = FileManager::join(appdata_dir, settings_file_name);

    auto try_load = [&](std::string const& dir) -> bool
    {
        if (dir.empty())
            return false;

        std::string const candidate = FileManager::join(dir, settings_file_name);
        if (!load_json(candidate, settings))
            return false;

        default_config = false;
        return true;
    };

	try_load(appdata_dir);
    if (!try_load(module_dir) && !try_load(exe_dir))
        default_config = true;

    if (!appdata_dir.empty())
        config_path = appdata_config;
    else if (!exe_dir.empty())
        config_path = FileManager::join(exe_dir, settings_file_name);
    else
        config_path = FileManager::join(module_dir, settings_file_name);

#ifndef DISABLE_LOG
    Log::LogLevel llvl;
    switchstr(get_setting(settings, "log_level", std::string(default_config ? "OFF" : "OFF")))
    {
        casestr("TRACE"): llvl = Log::LogLevel::TRACE; break;
        casestr("DEBUG"): llvl = Log::LogLevel::DEBUG; break;
        casestr("INFO") : llvl = Log::LogLevel::INFO ; break;
        casestr("WARN") : llvl = Log::LogLevel::WARN ; break;
        casestr("ERR")  : llvl = Log::LogLevel::ERR  ; break;
        casestr("FATAL"): llvl = Log::LogLevel::FATAL; break;
        casestr("OFF")  :
        default         : llvl = Log::LogLevel::OFF;
    }
    APP_LOG(Log::LogLevel::INFO, "Setting log level to: %s", Log::loglevel_to_str(llvl));
    Log::set_loglevel(llvl);
#endif

    APP_LOG(Log::LogLevel::INFO, "Configuration Path: %s", config_path.c_str());
    if (default_config)
    {
        APP_LOG(Log::LogLevel::WARN, "Error while loading settings, building a default one");
    }

    APP_LOG(Log::LogLevel::INFO, "Emulator version %s", _EMU_VERSION_);

    gse_steam_user_config_t gse_config;
    bool const has_gse_config = try_load_gse_steam_user_config(gse_config);

    if (has_gse_config && gse_config.has_account_name())
    {
        username = gse_config.account_name;
        APP_LOG(Log::LogLevel::INFO, "username from gse steam config: %s", username.c_str());
    }
    else
    {
        username = get_setting(settings, "username", std::string(u8"DefaultName"));
    }

    if (username.empty() || !utf8::is_valid(username.begin(), username.end()))
    {
        APP_LOG(Log::LogLevel::WARN, "Invalid username, resetting to default name.");
        username = u8"DefaultName";
    }

    settings["username"] = username;

    if (has_gse_config && gse_config.has_account_steamid() && is_valid_steam64(gse_config.account_steamid))
    {
        steam64 = gse_config.account_steamid;
        std::string const epic_id = generate_epic_id_from_steam64(gse_config.account_steamid);
        userid = GetEpicUserId(epic_id);
        APP_LOG(Log::LogLevel::INFO,
            "epicid from GSE steam64 %s: %s",
            gse_config.account_steamid.c_str(),
            epic_id.c_str());
    }
    else
    {
        userid = GetEpicUserId(get_setting(settings, "epicid", std::string("")));
        if (!userid->IsValid())
        {
            if (username == "DefaultName")
            {
                APP_LOG(Log::LogLevel::INFO, "Username == DefaultName, generating random epic id");
                userid = GetEpicUserId(generate_epic_id_user());
            }
            else
            {
                APP_LOG(Log::LogLevel::INFO, "Username != DefaultName, generating random epic id based on your username");
                userid = GetEpicUserId(generate_epic_id_user_from_name(username));
            }
        }
    }

    language                  = get_setting(settings, "language", std::string(default_language));
    gamename                  = get_setting(settings, "gamename", std::string("DefaultGameName"));
    appid                     = get_setting(settings, "appid", std::string("InvalidAppid"));
    unlock_dlcs               = get_setting(settings, "unlock_dlcs", bool(true));
    disable_online_networking = get_setting(settings, "disable_online_networking", bool(false));
    steam_passthrough         = get_setting(settings, "steam_passthrough", bool(false));
    APP_LOG(Log::LogLevel::INFO, "steam_passthrough: %s", steam_passthrough ? "enabled" : "disabled");
    custom_broadcast          = get_setting(settings, "custom_broadcast", std::string(default_custom_broadcast));
    if (custom_broadcast.empty())
        custom_broadcast = default_custom_broadcast;
    settings["custom_broadcast"] = custom_broadcast;
    eos_sdk_version           = get_setting(settings, "eos_sdk_version", std::string(""));
    if (!eos_sdk_version.empty())
    {
        sdk::EosSdkVersion::set_config_override(eos_sdk_version);
        APP_LOG(Log::LogLevel::INFO, "eos_sdk_version override from config: %s", eos_sdk_version.c_str());
    }
    savepath                  = get_setting(settings, "savepath", std::string("appdata"));

    if (try_resolve_steam_app_id(steam_appid))
    {
        APP_LOG(Log::LogLevel::INFO, "Resolved Steam AppId %s (config appid: %s)",
            steam_appid.c_str(), appid.c_str());
    }

    std::string const saved_productuserid = get_setting(settings, "productuserid", std::string(""));
    if (!saved_productuserid.empty() && saved_productuserid.size() == 32)
    {
        this->productuserid = GetProductUserId(saved_productuserid);
        APP_LOG(Log::LogLevel::INFO, "Loaded productuserid from config: %s", this->productuserid->to_string().c_str());
    }
    else
    {
        this->productuserid = GetProductUserId(generate_productuserid_from_epicid(userid->to_string()));
        APP_LOG(Log::LogLevel::INFO, "Generated productuserid from epicid: %s", this->productuserid->to_string().c_str());
        settings["productuserid"] = this->productuserid->to_string();
    }

    apply_save_directories();
    
    save_settings();
}

std::string Settings::network_game_id() const
{
    if (!eos_product_id.empty())
        return eos_product_id;

    if (!steam_appid.empty())
        return steam_appid;

    return appid;
}

bool Settings::matches_network_game_id(std::string const& remote_game_id) const
{
    if (remote_game_id.empty())
        return false;

    if (!eos_product_id.empty() && remote_game_id == eos_product_id)
        return true;

    if (!steam_appid.empty() && remote_game_id == steam_appid)
        return true;

    if (remote_game_id == appid)
        return true;

    // Older builds tagged network packets with EpicAccountId instead of app/product id.
    if (is_epic_account_id(remote_game_id) && userid != nullptr && userid->IsValid())
        return true;

    return false;
}

void Settings::apply_runtime_product_id(std::string const& product_id)
{
    if (product_id.empty())
        return;

    GLOBAL_LOCK();

    eos_product_id = product_id;
    APP_LOG(Log::LogLevel::INFO, "EOS ProductId from platform: %s (Steam AppId: %s, config appid: %s)",
        eos_product_id.c_str(),
        steam_appid.empty() ? "-" : steam_appid.c_str(),
        appid.c_str());

    static std::string const invalid_appid = "InvalidAppid";
    if (appid == invalid_appid || !is_epic_account_id(appid))
    {
        APP_LOG(Log::LogLevel::INFO, "appid inferred from EOS_Platform_Options.ProductId: %s (was '%s')",
            product_id.c_str(), appid.c_str());
        appid = product_id;

        apply_save_directories();
        try
        {
            save_settings();
        }
        catch (...)
        {
            APP_LOG(Log::LogLevel::WARN, "save_settings failed in apply_runtime_product_id");
        }
    }
}

void Settings::save_settings()
{
	std::string const last_settings_config = FileManager::join(appdata_dir, "last_config.json");
    nlohmann::json last_settings;
    APP_LOG(Log::LogLevel::INFO, "Saving lastest emu settings: %s", last_settings_config.c_str());

    last_settings["appid"]                     = appid;
    last_settings["username"]                  = username;
    last_settings["epicid"]                    = userid->to_string();
    last_settings["productuserid"]             = productuserid->to_string();
    last_settings["language"]                  = language;
    last_settings["gamename"]                  = gamename;
    last_settings["unlock_dlcs"]               = unlock_dlcs;
    last_settings["disable_online_networking"] = disable_online_networking;
    last_settings["steam_passthrough"]         = steam_passthrough;
#ifndef DISABLE_LOG
    last_settings["log_level"]                 = Log::loglevel_to_str();
#endif
    last_settings["custom_broadcast"]          = custom_broadcast;
    last_settings["savepath"]                  = savepath;
	
    FileManager::create_directory(FileManager::dirname(last_settings_config));
    save_json(last_settings_config, last_settings);
	
    if (!appdata_dir.empty())
        return;

    nlohmann::json settings;
    APP_LOG(Log::LogLevel::INFO, "Saving base settings: %s", appdata_config.c_str());

    settings["username"]                  = username;
    settings["epicid"]                    = userid->to_string();
    settings["productuserid"]             = productuserid->to_string();
    settings["language"]                  = language;

    FileManager::create_directory(FileManager::dirname(appdata_config));
    save_json(appdata_config, settings);
	
}
