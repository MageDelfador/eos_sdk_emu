// Private Epic SDK exports shipped with KLETKA / RedpointEOS (not in public SDK headers).
#include "common_includes.h"
#include "eos_api_trace.h"

#ifdef Options
#undef Options
#endif

#define EOS_PRIVATE_STUB_RESULT(name) \
    EOS_DECLARE_FUNC(EOS_EResult) name(void) { EOS_API_TRACE(); return EOS_EResult::EOS_Success; }

#define EOS_PRIVATE_STUB_VOID(name) \
    EOS_DECLARE_FUNC(void) name(void) { EOS_API_TRACE(); }

#define EOS_PRIVATE_STUB_NOTIF(name) \
    EOS_DECLARE_FUNC(EOS_NotificationId) name(void) { EOS_API_TRACE(); return EOS_INVALID_NOTIFICATIONID; }

#define EOS_PRIVATE_STUB_PTR(name) \
    EOS_DECLARE_FUNC(void*) name(void) { EOS_API_TRACE(); return nullptr; }

#define EOS_PRIVATE_STUB_STR(name) \
    EOS_DECLARE_FUNC(const char*) name(void) { EOS_API_TRACE(); return "Unknown"; }

#define EOS_PRIVATE_STUB_BOOL(name) \
    EOS_DECLARE_FUNC(EOS_Bool) name(void) { EOS_API_TRACE(); return EOS_FALSE; }

#define EOS_PRIVATE_STUB_INT(name) \
    EOS_DECLARE_FUNC(int32_t) name(void) { EOS_API_TRACE(); return 0; }

EOS_PRIVATE_STUB_RESULT(EOS_Mercury_Initialize)
EOS_PRIVATE_STUB_VOID(EOS_Mercury_Shutdown)

EOS_DECLARE_FUNC(void) EOS_Mercury_Tick(void)
{
    static uint32_t tick_count = 0;
    EOS_API_TRACE_THROTTLED(tick_count, 300);
}

EOS_PRIVATE_STUB_VOID(EOS_BeginScopeEvent)
EOS_PRIVATE_STUB_VOID(EOS_EndScopeEvent)

EOS_DECLARE_FUNC(const char*) EOS_EApplicationStatus_ToString(EOS_EApplicationStatus ApplicationStatus)
{
    (void)ApplicationStatus;
    return "EOS_AS_Foreground";
}

EOS_PRIVATE_STUB_RESULT(EOS_AntiCheatClient_GetModuleBuildId)
EOS_PRIVATE_STUB_RESULT(EOS_AntiCheatClient_Reserved02)

EOS_PRIVATE_STUB_RESULT(EOS_Audio_CreateNewInputStream)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_CreateNewOutputStream)
EOS_PRIVATE_STUB_VOID(EOS_Audio_DestroyInputStream)
EOS_PRIVATE_STUB_VOID(EOS_Audio_DestroyOutputStream)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_EnableCommunicationsModeOutputDevices)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_GetInputDeviceInfo)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_GetInputStreamInfo)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_GetOutputDeviceInfo)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_GetOutputStreamInfo)
EOS_PRIVATE_STUB_BOOL(EOS_Audio_IsInputStreamDeviceDisconnected)
EOS_PRIVATE_STUB_BOOL(EOS_Audio_IsInputStreamSilent)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_QueryInputDevices)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_QueryOutputDevices)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_RegisterUser)
EOS_PRIVATE_STUB_VOID(EOS_Audio_RemoveNotifyDevicesChanged)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_SetFeatureEnabledForInputStream)
EOS_PRIVATE_STUB_NOTIF(EOS_Audio_SetNotifyDevicesChanged)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_StartInputStream)
EOS_PRIVATE_STUB_RESULT(EOS_Audio_StartOutputStream)
EOS_PRIVATE_STUB_VOID(EOS_Audio_StopInputStream)
EOS_PRIVATE_STUB_VOID(EOS_Audio_StopOutputStream)
EOS_PRIVATE_STUB_VOID(EOS_Audio_UnregisterUser)

EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_CreateNewInputStream)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_CreateNewOutputStream)
EOS_PRIVATE_STUB_VOID(EOS_BroadcastAudio_DestroyInputStream)
EOS_PRIVATE_STUB_VOID(EOS_BroadcastAudio_DestroyOutputStream)
EOS_PRIVATE_STUB_INT(EOS_BroadcastAudio_GetCurrentGainLevel)
EOS_PRIVATE_STUB_INT(EOS_BroadcastAudio_GetCurrentMicAmplitude)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_GetInputStreamInfo)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_GetOutputStreamInfo)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_PushPacketToOutputStream)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_SetEncoderSettings)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_SetMicProcessingSettings)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_StartInputStream)
EOS_PRIVATE_STUB_RESULT(EOS_BroadcastAudio_StartOutputStream)
EOS_PRIVATE_STUB_VOID(EOS_BroadcastAudio_StopInputStream)
EOS_PRIVATE_STUB_VOID(EOS_BroadcastAudio_StopOutputStream)

EOS_PRIVATE_STUB_RESULT(EOS_Ecom_CopyLastRedeemEntitlementsResultByIndex)
EOS_PRIVATE_STUB_INT(EOS_Ecom_GetLastRedeemEntitlementsResultCount)

EOS_PRIVATE_STUB_RESULT(EOS_PresenceModification_SetTemplateData)
EOS_PRIVATE_STUB_RESULT(EOS_PresenceModification_SetTemplateId)

EOS_PRIVATE_STUB_NOTIF(EOS_RTC_AddNotifyRoomBeforeJoin)
EOS_PRIVATE_STUB_VOID(EOS_RTC_RemoveNotifyRoomBeforeJoin)

EOS_PRIVATE_STUB_NOTIF(EOS_UI_AddNotifyOnScreenKeyboardRequested)
EOS_PRIVATE_STUB_RESULT(EOS_UI_ConfigureOnScreenKeyboard)
EOS_PRIVATE_STUB_VOID(EOS_UI_RemoveNotifyOnScreenKeyboardRequested)
