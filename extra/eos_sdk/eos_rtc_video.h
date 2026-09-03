// Minimal RTC Video declarations for legacy SDK export parity (e.g. SDK 1.16.x).
#pragma once

#include "eos_common.h"
#include "eos_rtc_audio_types.h"

EXTERN_C typedef struct EOS_RTCVideoHandle* EOS_HRTCVideo;
EXTERN_C typedef struct EOS_RTCVideoFrameFormatHandle* EOS_HRTCVideoFrameFormat;

enum { k_iRTCVideoCallbackBase = 26000 };

#pragma pack(push, 8)

EOS_STRUCT(EOS_RTCVideo_AddNotifyParticipantUpdatedOptions001, (
	int32_t ApiVersion;
));

EOS_STRUCT(EOS_RTCVideo_ParticipantUpdatedCallbackInfo, (
	enum { k_iCallback = k_iRTCVideoCallbackBase + 1 };
	EOS_EResult ResultCode;
	void* ClientData;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	EOS_ProductUserId ParticipantId;
	EOS_Bool bVideoEnabled;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnParticipantUpdatedCallback, const EOS_RTCVideo_ParticipantUpdatedCallbackInfo* Data);

EOS_STRUCT(EOS_RTCVideo_AddNotifyVideoReceivedOptions001, (
	int32_t ApiVersion;
));

EOS_STRUCT(EOS_RTCVideo_VideoReceivedCallbackInfo, (
	enum { k_iCallback = k_iRTCVideoCallbackBase + 2 };
	void* ClientData;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	EOS_ProductUserId ParticipantId;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnVideoReceivedCallback, const EOS_RTCVideo_VideoReceivedCallbackInfo* Data);

EOS_STRUCT(EOS_RTCVideo_CreateOutgoingVideoFrameFormatOptions001, (
	int32_t ApiVersion;
));

EOS_STRUCT(EOS_RTCVideo_SendVideoOptions001, (
	int32_t ApiVersion;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	void* Buffer;
));

EOS_STRUCT(EOS_RTCVideo_UpdateSendingOptions001, (
	int32_t ApiVersion;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	EOS_Bool bVideoEnabled;
));

EOS_STRUCT(EOS_RTCVideo_UpdateSendingCallbackInfo, (
	enum { k_iCallback = k_iRTCVideoCallbackBase + 3 };
	EOS_EResult ResultCode;
	void* ClientData;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	EOS_Bool bVideoEnabled;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnUpdateSendingCallback, const EOS_RTCVideo_UpdateSendingCallbackInfo* Data);

EOS_STRUCT(EOS_RTCVideo_UpdateReceivingOptions001, (
	int32_t ApiVersion;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	EOS_ProductUserId ParticipantId;
	EOS_Bool bVideoEnabled;
));

EOS_STRUCT(EOS_RTCVideo_UpdateReceivingCallbackInfo, (
	enum { k_iCallback = k_iRTCVideoCallbackBase + 4 };
	EOS_EResult ResultCode;
	void* ClientData;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	EOS_ProductUserId ParticipantId;
	EOS_Bool bVideoEnabled;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnUpdateReceivingCallback, const EOS_RTCVideo_UpdateReceivingCallbackInfo* Data);

EOS_STRUCT(EOS_RTCVideo_SetVideoAllocationCallbackOptions001, (
	int32_t ApiVersion;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnVideoAllocationCallback, void* ClientData, void* OutBuffer, uint32_t* OutBufferSize);

EOS_STRUCT(EOS_RTCVideo_SetVideoReleaseCallbackOptions001, (
	int32_t ApiVersion;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnVideoReleaseCallback, void* ClientData, void* Buffer);

EOS_STRUCT(EOS_RTCVideo_SetAdaptVideoFrameCallbackOptions001, (
	int32_t ApiVersion;
));

EOS_DECLARE_CALLBACK(EOS_RTCVideo_OnAdaptVideoFrameCallback, void* ClientData, void* InBuffer, void* OutBuffer);

EOS_STRUCT(EOS_RTCAudio_SetPositionOptions001, (
	int32_t ApiVersion;
	EOS_ProductUserId LocalUserId;
	const char* RoomName;
	float PositionX;
	float PositionY;
	float PositionZ;
));

#pragma pack(pop)

#define EOS_RTCVideo_AddNotifyParticipantUpdatedOptions EOS_RTCVideo_AddNotifyParticipantUpdatedOptions001
#define EOS_RTCVideo_AddNotifyVideoReceivedOptions      EOS_RTCVideo_AddNotifyVideoReceivedOptions001
#define EOS_RTCVideo_CreateOutgoingVideoFrameFormatOptions EOS_RTCVideo_CreateOutgoingVideoFrameFormatOptions001
#define EOS_RTCVideo_SendVideoOptions                   EOS_RTCVideo_SendVideoOptions001
#define EOS_RTCVideo_UpdateSendingOptions               EOS_RTCVideo_UpdateSendingOptions001
#define EOS_RTCVideo_UpdateReceivingOptions             EOS_RTCVideo_UpdateReceivingOptions001
#define EOS_RTCVideo_SetVideoAllocationCallbackOptions  EOS_RTCVideo_SetVideoAllocationCallbackOptions001
#define EOS_RTCVideo_SetVideoReleaseCallbackOptions     EOS_RTCVideo_SetVideoReleaseCallbackOptions001
#define EOS_RTCVideo_SetAdaptVideoFrameCallbackOptions  EOS_RTCVideo_SetAdaptVideoFrameCallbackOptions001
#define EOS_RTCAudio_SetPositionOptions                 EOS_RTCAudio_SetPositionOptions001

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCVideo_AddNotifyParticipantUpdated(
	EOS_HRTCVideo Handle,
	EOS_RTCVideo_AddNotifyParticipantUpdatedOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnParticipantUpdatedCallback CompletionDelegate);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_RemoveNotifyParticipantUpdated(EOS_HRTCVideo Handle, EOS_NotificationId NotificationId);

EOS_DECLARE_FUNC(EOS_NotificationId) EOS_RTCVideo_AddNotifyVideoReceived(
	EOS_HRTCVideo Handle,
	EOS_RTCVideo_AddNotifyVideoReceivedOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnVideoReceivedCallback CompletionDelegate);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_RemoveNotifyVideoReceived(EOS_HRTCVideo Handle, EOS_NotificationId NotificationId);

EOS_DECLARE_FUNC(EOS_HRTCVideoFrameFormat) EOS_RTCVideo_CreateOutgoingVideoFrameFormat(
	EOS_HRTCVideo Handle,
	const EOS_RTCVideo_CreateOutgoingVideoFrameFormatOptions* Options);

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCVideo_SendVideo(EOS_HRTCVideo Handle, const EOS_RTCVideo_SendVideoOptions* Options);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_UpdateSending(
	EOS_HRTCVideo Handle,
	const EOS_RTCVideo_UpdateSendingOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnUpdateSendingCallback CompletionDelegate);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_UpdateReceiving(
	EOS_HRTCVideo Handle,
	const EOS_RTCVideo_UpdateReceivingOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnUpdateReceivingCallback CompletionDelegate);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_SetVideoAllocationCallback(
	EOS_HRTCVideo Handle,
	const EOS_RTCVideo_SetVideoAllocationCallbackOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnVideoAllocationCallback Callback);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_SetVideoReleaseCallback(
	EOS_HRTCVideo Handle,
	const EOS_RTCVideo_SetVideoReleaseCallbackOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnVideoReleaseCallback Callback);

EOS_DECLARE_FUNC(void) EOS_RTCVideo_SetAdaptVideoFrameCallback(
	EOS_HRTCVideo Handle,
	const EOS_RTCVideo_SetAdaptVideoFrameCallbackOptions* Options,
	void* ClientData,
	const EOS_RTCVideo_OnAdaptVideoFrameCallback Callback);

EOS_DECLARE_FUNC(EOS_EResult) EOS_RTCAudio_SetPosition(EOS_HRTCAudio Handle, const EOS_RTCAudio_SetPositionOptions* Options);
