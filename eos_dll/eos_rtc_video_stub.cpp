#include "common_includes.h"
#include "eos_rtc_video.h"
#include "eos_rtc_audio.h"
#include "eos_stub_handles.h"
#include "settings.h"
#include "eos_api_trace.h"

namespace
{
    struct StubRTCVideoFrameFormat final
    {
        uint32_t tag = 14;
    };

    StubRTCVideoFrameFormat& StubVideoFrameFormat()
    {
        static StubRTCVideoFrameFormat format;
        return format;
    }
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCVideo_AddNotifyParticipantUpdated(
    EOS_HRTCVideo Handle,
    EOS_RTCVideo_AddNotifyParticipantUpdatedOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnParticipantUpdatedCallback CompletionDelegate)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)ClientData;
    (void)CompletionDelegate;
    return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_RemoveNotifyParticipantUpdated(EOS_HRTCVideo Handle, EOS_NotificationId NotificationId)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)NotificationId;
}

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCVideo_AddNotifyVideoReceived(
    EOS_HRTCVideo Handle,
    EOS_RTCVideo_AddNotifyVideoReceivedOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnVideoReceivedCallback CompletionDelegate)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)ClientData;
    (void)CompletionDelegate;
    return EOS_INVALID_NOTIFICATIONID;
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_RemoveNotifyVideoReceived(EOS_HRTCVideo Handle, EOS_NotificationId NotificationId)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)NotificationId;
}

EOS_DECLARE_FUNC(EOS_HRTCVideoFrameFormat) EOS_RTCVideo_CreateOutgoingVideoFrameFormat(
    EOS_HRTCVideo Handle,
    const EOS_RTCVideo_CreateOutgoingVideoFrameFormatOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    return reinterpret_cast<EOS_HRTCVideoFrameFormat>(&StubVideoFrameFormat());
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCVideo_SendVideo(EOS_HRTCVideo Handle, const EOS_RTCVideo_SendVideoOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    return EOS_EResult::EOS_Success;
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_UpdateSending(
    EOS_HRTCVideo Handle,
    const EOS_RTCVideo_UpdateSendingOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnUpdateSendingCallback CompletionDelegate)
{
    EOS_API_TRACE();
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCVideo_UpdateSendingCallbackInfo info = {};
    info.ClientData = ClientData;
    info.ResultCode = EOS_EResult::EOS_Success;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.bVideoEnabled = Options != nullptr ? Options->bVideoEnabled : EOS_TRUE;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_UpdateReceiving(
    EOS_HRTCVideo Handle,
    const EOS_RTCVideo_UpdateReceivingOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnUpdateReceivingCallback CompletionDelegate)
{
    EOS_API_TRACE();
    (void)Handle;
    if (CompletionDelegate == nullptr)
        return;

    EOS_RTCVideo_UpdateReceivingCallbackInfo info = {};
    info.ClientData = ClientData;
    info.ResultCode = EOS_EResult::EOS_Success;
    info.LocalUserId = Options != nullptr ? Options->LocalUserId : Settings::Inst().productuserid;
    info.RoomName = Options != nullptr ? Options->RoomName : nullptr;
    info.ParticipantId = Options != nullptr ? Options->ParticipantId : nullptr;
    info.bVideoEnabled = Options != nullptr ? Options->bVideoEnabled : EOS_TRUE;
    CompletionDelegate(&info);
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_SetVideoAllocationCallback(
    EOS_HRTCVideo Handle,
    const EOS_RTCVideo_SetVideoAllocationCallbackOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnVideoAllocationCallback Callback)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)ClientData;
    (void)Callback;
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_SetVideoReleaseCallback(
    EOS_HRTCVideo Handle,
    const EOS_RTCVideo_SetVideoReleaseCallbackOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnVideoReleaseCallback Callback)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)ClientData;
    (void)Callback;
}

EOS_DECLARE_FUNC(void) EOS_RTCVideo_SetAdaptVideoFrameCallback(
    EOS_HRTCVideo Handle,
    const EOS_RTCVideo_SetAdaptVideoFrameCallbackOptions* Options,
    void* ClientData,
    const EOS_RTCVideo_OnAdaptVideoFrameCallback Callback)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    (void)ClientData;
    (void)Callback;
}

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SetPosition(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetPositionOptions* Options)
{
    EOS_API_TRACE();
    (void)Handle;
    (void)Options;
    return EOS_EResult::EOS_Success;
}
