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

#pragma once

#include "common_includes.h"

struct gse_steam_user_config_t
{
    std::string account_name;
    std::string account_steamid;

    bool has_account_name() const { return !account_name.empty(); }
    bool has_account_steamid() const { return !account_steamid.empty(); }
};

LOCAL_API std::string gse_steam_user_config_path();
LOCAL_API bool try_load_gse_steam_user_config(gse_steam_user_config_t& config);
LOCAL_API bool is_valid_steam64(std::string const& steam64);
LOCAL_API bool try_resolve_steam_app_id(std::string& out_app_id);
