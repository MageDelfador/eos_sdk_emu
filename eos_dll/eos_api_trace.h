#pragma once

#include "Log.h"

namespace sdk
{
    inline thread_local int g_eos_in_game_callback = 0;
    inline thread_local int g_eos_api_depth = 0;

    class EosGameCallbackScope
    {
    public:
        EosGameCallbackScope() { ++g_eos_in_game_callback; }
        ~EosGameCallbackScope() { --g_eos_in_game_callback; }
    };

    class EosApiTraceScope
    {
        const char* _name;

    public:
        explicit EosApiTraceScope(const char* name) : _name(name)
        {
            APP_LOG(Log::LogLevel::DEBUG, "[EOS_API depth=%d in_cb=%d] >> %s",
                g_eos_api_depth, g_eos_in_game_callback, _name);
            ++g_eos_api_depth;
        }

        ~EosApiTraceScope()
        {
            --g_eos_api_depth;
            APP_LOG(Log::LogLevel::DEBUG, "[EOS_API depth=%d in_cb=%d] << %s",
                g_eos_api_depth, g_eos_in_game_callback, _name);
        }
    };

    inline void eos_log_api_throttled(const char* name, uint32_t& counter, uint32_t period = 300)
    {
        if ((counter++ % period) == 0)
        {
            APP_LOG(Log::LogLevel::DEBUG, "[EOS_API throttled in_cb=%d] %s (#%u)",
                g_eos_in_game_callback, name, counter);
        }
    }
}

#ifndef DISABLE_LOG
#define EOS_API_TRACE() sdk::EosApiTraceScope __eos_api_trace__(__FUNCTION__)
#define EOS_API_TRACE_POLL(period) \
    do { \
        static uint32_t __eos_api_poll_counter__ = 0; \
        sdk::eos_log_api_throttled(__FUNCTION__, __eos_api_poll_counter__, period); \
    } while (0)
#define EOS_GAME_CALLBACK_SCOPE() sdk::EosGameCallbackScope __eos_game_cb__
#define EOS_API_TRACE_THROTTLED(counter, period) sdk::eos_log_api_throttled(__FUNCTION__, counter, period)
#else
#define EOS_API_TRACE()
#define EOS_API_TRACE_POLL(period)
#define EOS_GAME_CALLBACK_SCOPE()
#define EOS_API_TRACE_THROTTLED(counter, period)
#endif
