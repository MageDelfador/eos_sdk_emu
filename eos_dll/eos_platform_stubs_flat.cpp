#include "eossdk_platform.h"
#include "eos_stub_handles.h"
#include "eos_api_trace.h"

#ifdef Options
#undef Options
#endif

using namespace sdk;

EOS_DECLARE_FUNC(EOS_HAntiCheatClient) EOS_Platform_GetAntiCheatClientInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HAntiCheatClient>(&StubAntiCheatClient());
}

EOS_DECLARE_FUNC(EOS_HAntiCheatServer) EOS_Platform_GetAntiCheatServerInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HAntiCheatServer>(&StubAntiCheatServer());
}

EOS_DECLARE_FUNC(EOS_HCustomInvites) EOS_Platform_GetCustomInvitesInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HCustomInvites>(&StubCustomInvites());
}

EOS_DECLARE_FUNC(EOS_HIntegratedPlatform) EOS_Platform_GetIntegratedPlatformInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HIntegratedPlatform>(&StubIntegratedPlatform());
}

EOS_DECLARE_FUNC(EOS_HKWS) EOS_Platform_GetKWSInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HKWS>(&StubKWS());
}

EOS_DECLARE_FUNC(EOS_HMods) EOS_Platform_GetModsInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HMods>(&StubMods());
}

EOS_DECLARE_FUNC(EOS_HProgressionSnapshot) EOS_Platform_GetProgressionSnapshotInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HProgressionSnapshot>(&StubProgressionSnapshot());
}

EOS_DECLARE_FUNC(EOS_HRTCAdmin) EOS_Platform_GetRTCAdminInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HRTCAdmin>(&StubRTCAdmin());
}

EOS_DECLARE_FUNC(EOS_HRTC) EOS_Platform_GetRTCInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HRTC>(&StubRTC());
}

EOS_DECLARE_FUNC(EOS_HReports) EOS_Platform_GetReportsInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HReports>(&StubReports());
}

EOS_DECLARE_FUNC(EOS_HSanctions) EOS_Platform_GetSanctionsInterface(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return reinterpret_cast<EOS_HSanctions>(&StubSanctions());
}

EOS_DECLARE_FUNC(EOS_EApplicationStatus) EOS_Platform_GetApplicationStatus(EOS_HPlatform Handle)
{
    EOS_API_TRACE_POLL(500);
    (void)Handle;
    return EOS_EApplicationStatus::EOS_AS_Foreground;
}

EOS_DECLARE_FUNC(void) EOS_Platform_SetApplicationStatus(EOS_HPlatform Handle, EOS_EApplicationStatus NewStatus)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)NewStatus;
}

EOS_DECLARE_FUNC(EOS_ENetworkStatus) EOS_Platform_GetNetworkStatus(EOS_HPlatform Handle)
{
    EOS_API_TRACE();
    (void)Handle;
    return EOS_ENetworkStatus::EOS_NS_Online;
}

EOS_DECLARE_FUNC(void) EOS_Platform_SetNetworkStatus(EOS_HPlatform Handle, EOS_ENetworkStatus NewStatus)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)NewStatus;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_Platform_GetDesktopCrossplayStatus(EOS_HPlatform Handle, const EOS_Platform_GetDesktopCrossplayStatusOptions* Options, EOS_Platform_DesktopCrossplayStatusInfo* OutDesktopCrossplayStatusInfo)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    if (OutDesktopCrossplayStatusInfo != nullptr)
    {
        OutDesktopCrossplayStatusInfo->Status = EOS_EDesktopCrossplayStatus::EOS_DCS_OK;
        OutDesktopCrossplayStatusInfo->ServiceInitResult = 0;
    }
    return EOS_EResult::EOS_Success;
}
