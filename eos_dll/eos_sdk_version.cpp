#include "eos_sdk_version.h"

#include "settings.h"
#include "eos_types.h"

#include <mutex>
#include <string>

namespace sdk
{
namespace
{
    std::mutex g_version_mutex;
    std::string g_config_override;
    int32_t g_initialize_api = 0;
    int32_t g_platform_options_api = 0;
    std::string g_reported_version;

    constexpr char const* VERSION_LATEST = EOS_VERSION_STRING "-guard40";
    constexpr char const* VERSION_1164 = "1.16.4-36651368";

    bool looks_like_legacy_sdk(int32_t platform_api, int32_t initialize_api)
    {
        if (platform_api > 0 && platform_api <= EOS_PLATFORM_OPTIONS_API_014)
            return true;

        if (platform_api <= 0 && initialize_api > 0 && initialize_api <= EOS_INITIALIZE_API_004)
            return true;

        return false;
    }

    void recompute_reported_version()
    {
        if (!g_config_override.empty())
        {
            g_reported_version = g_config_override;
            return;
        }

        if (looks_like_legacy_sdk(g_platform_options_api, g_initialize_api))
            g_reported_version = VERSION_1164;
        else
            g_reported_version = VERSION_LATEST;
    }
}

void EosSdkVersion::set_config_override(std::string const& version)
{
    std::lock_guard<std::mutex> lock(g_version_mutex);
    g_config_override = version;
    recompute_reported_version();
}

void EosSdkVersion::on_initialize_api(int32_t api_version)
{
    if (api_version <= 0)
        return;

    std::lock_guard<std::mutex> lock(g_version_mutex);
    if (g_initialize_api == 0 || api_version < g_initialize_api)
        g_initialize_api = api_version;
    recompute_reported_version();

    APP_LOG(Log::LogLevel::INFO,
        "EOS SDK compat: Initialize ApiVersion=%d, reported version=%s",
        api_version,
        g_reported_version.c_str());
}

void EosSdkVersion::on_platform_options_api(int32_t api_version)
{
    if (api_version <= 0)
        return;

    std::lock_guard<std::mutex> lock(g_version_mutex);
    g_platform_options_api = api_version;
    recompute_reported_version();

    APP_LOG(Log::LogLevel::INFO,
        "EOS SDK compat: Platform Options ApiVersion=%d, reported version=%s",
        api_version,
        g_reported_version.c_str());
}

const char* EosSdkVersion::reported_string()
{
    std::lock_guard<std::mutex> lock(g_version_mutex);
    if (g_reported_version.empty())
        recompute_reported_version();
    return g_reported_version.c_str();
}

}
