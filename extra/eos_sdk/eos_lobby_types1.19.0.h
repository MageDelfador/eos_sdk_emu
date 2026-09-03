// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#pragma pack(push, 8)

EOS_ENUM(EOS_ELobbyRTCRoomJoinActionType,
	EOS_LRRJAT_AutomaticJoin = 0,
	EOS_LRRJAT_ManualJoin = 1
);

/** The most recent version of the EOS_Lobby_CreateLobby API (SDK 1.19). */
#define EOS_LOBBY_CREATELOBBY_API_010 10

EOS_STRUCT(EOS_Lobby_CreateLobbyOptions010, (
	int32_t ApiVersion;
	EOS_ProductUserId LocalUserId;
	uint32_t MaxLobbyMembers;
	EOS_ELobbyPermissionLevel PermissionLevel;
	EOS_Bool bPresenceEnabled;
	EOS_Bool bAllowInvites;
	const char* BucketId;
	EOS_Bool bDisableHostMigration;
	EOS_Bool bEnableRTCRoom;
	const EOS_Lobby_LocalRTCOptions* LocalRTCOptions;
	EOS_LobbyId LobbyId;
	EOS_Bool bEnableJoinById;
	EOS_Bool bRejoinAfterKickRequiresInvite;
	const uint32_t* AllowedPlatformIds;
	uint32_t AllowedPlatformIdsCount;
	EOS_Bool bCrossplayOptOut;
	EOS_ELobbyRTCRoomJoinActionType RTCRoomJoinActionType;
));

/** The most recent version of the EOS_Lobby_JoinRTCRoom API. */
#define EOS_LOBBY_JOINRTCROOM_API_LATEST 1

EOS_STRUCT(EOS_Lobby_JoinRTCRoomOptions, (
	int32_t ApiVersion;
	EOS_LobbyId LobbyId;
	EOS_ProductUserId LocalUserId;
	const EOS_Lobby_LocalRTCOptions* LocalRTCOptions;
));

EOS_STRUCT(EOS_Lobby_JoinRTCRoomCallbackInfo, (
	EOS_EResult ResultCode;
	void* ClientData;
	EOS_LobbyId LobbyId;
));

EOS_DECLARE_CALLBACK(EOS_Lobby_OnJoinRTCRoomCallback, const EOS_Lobby_JoinRTCRoomCallbackInfo* Data);

/** The most recent version of the EOS_Lobby_LeaveRTCRoom API. */
#define EOS_LOBBY_LEAVERTCROOM_API_LATEST 1

EOS_STRUCT(EOS_Lobby_LeaveRTCRoomOptions, (
	int32_t ApiVersion;
	EOS_LobbyId LobbyId;
	EOS_ProductUserId LocalUserId;
));

EOS_STRUCT(EOS_Lobby_LeaveRTCRoomCallbackInfo, (
	EOS_EResult ResultCode;
	void* ClientData;
	EOS_LobbyId LobbyId;
));

EOS_DECLARE_CALLBACK(EOS_Lobby_OnLeaveRTCRoomCallback, const EOS_Lobby_LeaveRTCRoomCallbackInfo* Data);

#pragma pack(pop)
