// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#pragma pack(push, 8)

/** The most recent version of the EOS_Sessions_CreateSessionModification API (SDK 1.19). */
#define EOS_SESSIONS_CREATESESSIONMODIFICATION_API_005 5

EOS_STRUCT(EOS_Sessions_CreateSessionModificationOptions005, (
	int32_t ApiVersion;
	const char* SessionName;
	const char* BucketId;
	uint32_t MaxPlayers;
	EOS_ProductUserId LocalUserId;
	EOS_Bool bPresenceEnabled;
	const char* SessionId;
	EOS_Bool bSanctionsEnabled;
	const uint32_t* AllowedPlatformIds;
	uint32_t AllowedPlatformIdsCount;
));

#pragma pack(pop)
