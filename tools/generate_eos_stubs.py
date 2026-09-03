#!/usr/bin/env python3
"""Generate stub EOS API exports missing from the Redux build."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK_DIRS = [
    ROOT / "extra" / "eos_sdk",
    ROOT.parent / "EOS-SDK-49960398-Release-v1.19.0.3" / "SDK" / "Include",
]
TARGET_EXPORTS = Path(__file__).with_name("missing_exports.txt")
OUTPUT_CPP = ROOT / "eos_dll" / "eos_missing_stubs.generated.cpp"
DUMPBIN = Path(
    r"C:\Program Files\Microsoft Visual Studio\2022\Preview\VC\Tools\MSVC\14.42.34433\bin\Hostx64\x64\dumpbin.exe"
)
BUILT_DLL = ROOT / "build" / "bin" / "Release" / "EOSSDK-Win64-Shipping.dll"
REFERENCE_DLL = Path(
    r"c:\Program Files (x86)\Steam\steamapps\common\KLETKA\KLETKA\Binaries\Win64\RedpointEOS\EOSSDK-Win64-Shipping.dll"
)
PUBLIC_DLL = REFERENCE_DLL if REFERENCE_DLL.exists() else Path(
    r"c:\Users\Vladimir\Desktop\emu\EOSSDK-Win64-Shipping.dll"
)

MANUAL_STUBS = {
    "EOS_AntiCheatClient_AddNotifyMessageToPeer",
    "EOS_AntiCheatClient_AddNotifyPeerActionRequired",
    "EOS_AntiCheatClient_AddNotifyPeerAuthStatusChanged",
    "EOS_AntiCheatClient_BeginSession",
    "EOS_AntiCheatClient_EndSession",
    "EOS_AntiCheatClient_PollStatus",
    "EOS_AntiCheatClient_ReceiveMessageFromPeer",
    "EOS_AntiCheatClient_RegisterPeer",
    "EOS_AntiCheatClient_RemoveNotifyMessageToPeer",
    "EOS_AntiCheatClient_RemoveNotifyPeerActionRequired",
    "EOS_AntiCheatClient_RemoveNotifyPeerAuthStatusChanged",
    "EOS_AntiCheatClient_UnregisterPeer",
    "EOS_IntegratedPlatform_CreateIntegratedPlatformOptionsContainer",
    "EOS_IntegratedPlatformOptionsContainer_Add",
    "EOS_IntegratedPlatformOptionsContainer_Release",
    "EOS_IntegratedPlatform_AddNotifyUserLoginStatusChanged",
    "EOS_IntegratedPlatform_RemoveNotifyUserLoginStatusChanged",
    "EOS_IntegratedPlatform_SetUserLoginStatus",
    "EOS_IntegratedPlatform_SetUserPreLogoutCallback",
    "EOS_IntegratedPlatform_ClearUserPreLogoutCallback",
    "EOS_IntegratedPlatform_FinalizeDeferredUserLogout",
    "EOS_Auth_CopyIdToken",
    "EOS_Auth_QueryIdToken",
    "EOS_Connect_CopyIdToken",
    "EOS_Lobby_AddNotifyRTCRoomConnectionChanged",
    "EOS_Lobby_GetRTCRoomName",
    "EOS_Lobby_IsRTCRoomConnected",
    "EOS_Lobby_JoinRTCRoom",
    "EOS_Lobby_LeaveRTCRoom",
    "EOS_Lobby_RemoveNotifyRTCRoomConnectionChanged",
    "EOS_Lobby_JoinLobbyById",
    "EOS_RTCAdmin_CopyUserTokenByIndex",
    "EOS_RTCAdmin_CopyUserTokenByUserId",
    "EOS_RTCAdmin_QueryJoinRoomToken",
    "EOS_RTCAdmin_UserToken_Release",
    "EOS_RTC_JoinRoom",
    "EOS_RTC_LeaveRoom",
    "EOS_RTC_GetAudioInterface",
    "EOS_RTCAudio_RegisterPlatformUser",
    "EOS_RTCAudio_QueryInputDevicesInformation",
    "EOS_RTCAudio_QueryOutputDevicesInformation",
    "EOS_RTCAudio_UpdateReceiving",
    "EOS_RTCAudio_UpdateSending",
    "EOS_RTCAudio_UpdateReceivingVolume",
    "EOS_RTCAudio_UpdateSendingVolume",
    "EOS_RTCAudio_SetInputDeviceSettings",
    "EOS_RTCAudio_SetOutputDeviceSettings",
    "EOS_RTCAudio_SetPosition",
    "EOS_RTCVideo_AddNotifyParticipantUpdated",
    "EOS_RTCVideo_AddNotifyVideoReceived",
    "EOS_RTCVideo_CreateOutgoingVideoFrameFormat",
    "EOS_RTCVideo_RemoveNotifyParticipantUpdated",
    "EOS_RTCVideo_RemoveNotifyVideoReceived",
    "EOS_RTCVideo_SendVideo",
    "EOS_RTCVideo_SetAdaptVideoFrameCallback",
    "EOS_RTCVideo_SetVideoAllocationCallback",
    "EOS_RTCVideo_SetVideoReleaseCallback",
    "EOS_RTCVideo_UpdateReceiving",
    "EOS_RTCVideo_UpdateSending",
}

PRIVATE_EXPORTS = {
    "EOS_Mercury_Initialize",
    "EOS_Mercury_Shutdown",
    "EOS_Mercury_Tick",
    "EOS_BeginScopeEvent",
    "EOS_EndScopeEvent",
    "EOS_EApplicationStatus_ToString",
    "EOS_AntiCheatClient_GetModuleBuildId",
    "EOS_AntiCheatClient_Reserved02",
    "EOS_Audio_CreateNewInputStream",
    "EOS_Audio_CreateNewOutputStream",
    "EOS_Audio_DestroyInputStream",
    "EOS_Audio_DestroyOutputStream",
    "EOS_Audio_EnableCommunicationsModeOutputDevices",
    "EOS_Audio_GetInputDeviceInfo",
    "EOS_Audio_GetInputStreamInfo",
    "EOS_Audio_GetOutputDeviceInfo",
    "EOS_Audio_GetOutputStreamInfo",
    "EOS_Audio_IsInputStreamDeviceDisconnected",
    "EOS_Audio_IsInputStreamSilent",
    "EOS_Audio_QueryInputDevices",
    "EOS_Audio_QueryOutputDevices",
    "EOS_Audio_RegisterUser",
    "EOS_Audio_RemoveNotifyDevicesChanged",
    "EOS_Audio_SetFeatureEnabledForInputStream",
    "EOS_Audio_SetNotifyDevicesChanged",
    "EOS_Audio_StartInputStream",
    "EOS_Audio_StartOutputStream",
    "EOS_Audio_StopInputStream",
    "EOS_Audio_StopOutputStream",
    "EOS_Audio_UnregisterUser",
    "EOS_BroadcastAudio_CreateNewInputStream",
    "EOS_BroadcastAudio_CreateNewOutputStream",
    "EOS_BroadcastAudio_DestroyInputStream",
    "EOS_BroadcastAudio_DestroyOutputStream",
    "EOS_BroadcastAudio_GetCurrentGainLevel",
    "EOS_BroadcastAudio_GetCurrentMicAmplitude",
    "EOS_BroadcastAudio_GetInputStreamInfo",
    "EOS_BroadcastAudio_GetOutputStreamInfo",
    "EOS_BroadcastAudio_PushPacketToOutputStream",
    "EOS_BroadcastAudio_SetEncoderSettings",
    "EOS_BroadcastAudio_SetMicProcessingSettings",
    "EOS_BroadcastAudio_StartInputStream",
    "EOS_BroadcastAudio_StartOutputStream",
    "EOS_BroadcastAudio_StopInputStream",
    "EOS_BroadcastAudio_StopOutputStream",
    "EOS_Ecom_CopyLastRedeemEntitlementsResultByIndex",
    "EOS_Ecom_GetLastRedeemEntitlementsResultCount",
    "EOS_PresenceModification_SetTemplateData",
    "EOS_PresenceModification_SetTemplateId",
    "EOS_RTC_AddNotifyRoomBeforeJoin",
    "EOS_RTC_RemoveNotifyRoomBeforeJoin",
    "EOS_UI_AddNotifyOnScreenKeyboardRequested",
    "EOS_UI_ConfigureOnScreenKeyboard",
    "EOS_UI_RemoveNotifyOnScreenKeyboardRequested",
    "EOS_UserInfo_BestDisplayName_Release",
    "EOS_UserInfo_CopyBestDisplayName",
    "EOS_UserInfo_CopyBestDisplayNameWithPlatform",
    "EOS_UserInfo_GetLocalPlatformType",
}

DECL_RE = re.compile(
    r"EOS_DECLARE_FUNC\(([^)]*)\)\s+(EOS_[A-Za-z0-9_]+)\s*\(([^;]*)\)\s*;",
    re.MULTILINE,
)
EXPORT_RE = re.compile(r"^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)", re.MULTILINE)

PLATFORM_GETTERS = {
    "EOS_Platform_GetAntiCheatClientInterface": "EOS_HAntiCheatClient",
    "EOS_Platform_GetAntiCheatServerInterface": "EOS_HAntiCheatServer",
    "EOS_Platform_GetCustomInvitesInterface": "EOS_HCustomInvites",
    "EOS_Platform_GetIntegratedPlatformInterface": "EOS_HIntegratedPlatform",
    "EOS_Platform_GetKWSInterface": "EOS_HKWS",
    "EOS_Platform_GetModsInterface": "EOS_HMods",
    "EOS_Platform_GetProgressionSnapshotInterface": "EOS_HProgressionSnapshot",
    "EOS_Platform_GetRTCAdminInterface": "EOS_HRTCAdmin",
    "EOS_Platform_GetRTCInterface": "EOS_HRTC",
    "EOS_Platform_GetReportsInterface": "EOS_HReports",
    "EOS_Platform_GetSanctionsInterface": "EOS_HSanctions",
    "EOS_Platform_GetApplicationStatus": "EOS_EApplicationStatus",
    "EOS_Platform_SetApplicationStatus": "void",
    "EOS_Platform_GetNetworkStatus": "EOS_ENetworkStatus",
    "EOS_Platform_SetNetworkStatus": "void",
    "EOS_Platform_GetDesktopCrossplayStatus": "EOS_EResult",
}


def get_exports(path: Path) -> set[str]:
    out = subprocess.check_output(
        [str(DUMPBIN), "/exports", str(path)],
        stderr=subprocess.STDOUT,
        text=True,
        errors="ignore",
    )
    return set(EXPORT_RE.findall(out))


def collect_declarations() -> dict[str, tuple[str, str]]:
    decls: dict[str, tuple[str, str]] = {}
    for sdk_dir in SDK_DIRS:
        if not sdk_dir.exists():
            continue
        for header in sdk_dir.rglob("*"):
            if header.suffix not in {".h", ".inl"}:
                continue
            text = header.read_text(encoding="utf-8", errors="ignore")
            for match in DECL_RE.finditer(text):
                decls[match.group(2)] = (match.group(1).strip(), match.group(3).strip())
    return decls


def missing_names() -> list[str]:
    if TARGET_EXPORTS.exists():
        names = [
            line.strip()
            for line in TARGET_EXPORTS.read_text(encoding="utf-8").splitlines()
            if line.strip()
        ]
        return names

    if not PUBLIC_DLL.exists() or not BUILT_DLL.exists():
        raise SystemExit("missing_exports.txt not found and DLL pair unavailable")

    return sorted(get_exports(PUBLIC_DLL) - get_exports(BUILT_DLL))


def param_names(params: str) -> list[str]:
    names: list[str] = []
    for part in params.split(","):
        part = part.strip()
        if not part or part == "void":
            continue
        token = part.split()[-1]
        names.append(token.strip("*&"))
    return names


def unused_params(params: str) -> str:
    lines = [f"    (void){name};" for name in param_names(params)]
    return "\n".join(lines) + ("\n" if lines else "")


def stub_body(name: str, ret_type: str, params: str) -> str:
    prefix = "    EOS_API_TRACE();\n" + unused_params(params)
    if ret_type == "void":
        return prefix.rstrip("\n")

    if ret_type == "EOS_NotificationId":
        return prefix + "    return EOS_INVALID_NOTIFICATIONID;\n"

    if ret_type == "EOS_EResult":
        if "Copy" in name:
            return prefix + "    return EOS_EResult::EOS_NotImplemented;\n"
        return prefix + "    return EOS_EResult::EOS_Success;\n"

    if ret_type == "EOS_Bool":
        return prefix + "    return EOS_FALSE;\n"

    if ret_type == "EOS_ENetworkStatus":
        return prefix + "    return EOS_ENetworkStatus::EOS_NS_Online;\n"

    if ret_type == "EOS_EApplicationStatus":
        return prefix + "    return EOS_EApplicationStatus::EOS_AS_Foreground;\n"

    if ret_type in {"int32_t", "uint32_t", "uint64_t", "int64_t"}:
        return prefix + "    return 0;\n"

    if ret_type.startswith("EOS_"):
        return prefix + f"    return static_cast<{ret_type}>(0);\n"

    return prefix + "    return nullptr;\n"


def render_function(name: str, ret_type: str, params: str) -> str:
    body = stub_body(name, ret_type, params)
    return f"EOS_DECLARE_FUNC({ret_type}) {name}({params})\n{{\n{body}}}\n"


STUB_INCLUDES = [
    "eos_anticheatclient.h",
    "eos_anticheatserver.h",
    "eos_auth.h",
    "eos_connect.h",
    "eos_custominvites.h",
    "eos_ecom.h",
    "eos_friends.h",
    "eos_integratedplatform.h",
    "eos_kws.h",
    "eos_lobby.h",
    "eos_mods.h",
    "eos_p2p.h",
    "eos_playerdatastorage.h",
    "eos_progressionsnapshot.h",
    "eos_reports.h",
    "eos_rtc.h",
    "eos_rtc_admin.h",
    "eos_rtc_audio.h",
    "eos_rtc_data.h",
    "eos_sanctions.h",
    "eos_sessions.h",
    "eos_ui.h",
    "eos_userinfo.h",
]


def render_preamble() -> list[str]:
    lines = [
        "// Auto-generated by tools/generate_eos_stubs.py. Do not edit manually.",
        "",
        "#define WIN32_LEAN_AND_MEAN",
        "#define NOMINMAX",
        "#include <Windows.h>",
        "#ifdef Options",
        "#undef Options",
        "#endif",
        "",
        "#define EOS_BUILD_DLL 1",
        "",
        '#include "eos_sdk.h"',
        "",
        '#include "eos_api_trace.h"',
        "",
    ]
    for header in STUB_INCLUDES:
        lines.append(f'#include "{header}"')
    lines.append("")
    return lines


def main() -> int:
    decls = collect_declarations()
    missing = missing_names()
    skip = set(PLATFORM_GETTERS.keys()) | MANUAL_STUBS | PRIVATE_EXPORTS
    to_generate = [name for name in missing if name not in skip]

    missing_without_sig = [name for name in to_generate if name not in decls]
    if missing_without_sig:
        print("Missing SDK signatures for:", ", ".join(missing_without_sig[:20]), file=sys.stderr)
        return 1

    lines = render_preamble()
    for name in to_generate:
        ret_type, params = decls[name]
        lines.append(render_function(name, ret_type, params))

    OUTPUT_CPP.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {len(to_generate)} stubs -> {OUTPUT_CPP}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
