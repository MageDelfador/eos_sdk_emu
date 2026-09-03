#pragma once

#include <cstdint>
#include <string>

namespace sdk
{
    /** Tracks the SDK version string reported to the game via EOS_GetVersion(). */
    class EosSdkVersion
    {
    public:
        static void set_config_override(std::string const& version);
        static void on_initialize_api(int32_t api_version);
        static void on_platform_options_api(int32_t api_version);
        static const char* reported_string();
    };
}
