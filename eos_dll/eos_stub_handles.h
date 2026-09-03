#pragma once

#include "common_includes.h"

namespace sdk
{
    struct StubHandle final
    {
        uint32_t tag = 0;
    };

    inline StubHandle& StubAntiCheatClient() { static StubHandle h{1}; return h; }
    inline StubHandle& StubAntiCheatServer() { static StubHandle h{2}; return h; }
    inline StubHandle& StubCustomInvites() { static StubHandle h{3}; return h; }
    inline StubHandle& StubIntegratedPlatform() { static StubHandle h{4}; return h; }
    inline StubHandle& StubKWS() { static StubHandle h{5}; return h; }
    inline StubHandle& StubMods() { static StubHandle h{6}; return h; }
    inline StubHandle& StubProgressionSnapshot() { static StubHandle h{7}; return h; }
    inline StubHandle& StubRTCAdmin() { static StubHandle h{8}; return h; }
    inline StubHandle& StubRTC() { static StubHandle h{9}; return h; }
    inline StubHandle& StubRTCAudio() { static StubHandle h{12}; return h; }
    inline StubHandle& StubRTCData() { static StubHandle h{13}; return h; }
    inline StubHandle& StubReports() { static StubHandle h{10}; return h; }
    inline StubHandle& StubSanctions() { static StubHandle h{11}; return h; }
}
