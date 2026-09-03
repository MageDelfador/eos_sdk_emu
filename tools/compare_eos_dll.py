#!/usr/bin/env python3
import re
import sys
from collections import Counter
from pathlib import Path

import pefile

ROOT = Path(__file__).resolve().parents[1]
ORIG = Path(
    r"f:\SteamLibrary\steamapps\common\The Spell Brigade"
    r"\TheSpellBrigade_Data\Plugins\x86_64\EOSSDK-Win64-Shipping.dll"
)
EMU = ROOT / "build" / "bin" / "Release" / "EOSSDK-Win64-Shipping.dll"
GEN = ROOT / "tools" / "generate_eos_stubs.py"
STUB_CPP = ROOT / "eos_dll" / "eos_missing_stubs.generated.cpp"
GFX = Path(
    r"f:\SteamLibrary\steamapps\common\The Spell Brigade"
    r"\TheSpellBrigade_Data\Plugins\x86_64\GfxPluginNativeRender-x64.dll"
)


def exports(path: Path) -> list[str]:
    pe = pefile.PE(str(path), fast_load=True)
    pe.parse_data_directories(
        directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"]]
    )
    out = []
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
        for sym in pe.DIRECTORY_ENTRY_EXPORT.symbols:
            if sym.name:
                out.append(sym.name.decode())
    return sorted(set(out))


def main() -> int:
    orig_ex = exports(ORIG)
    emu_ex = exports(EMU)
    stub_names = re.findall(
        r"EOS_DECLARE_FUNC\([^)]+\)\s+(EOS_\w+)\(",
        STUB_CPP.read_text(encoding="utf-8", errors="ignore"),
    )
    manual = set(re.findall(r'"(EOS_[^"]+)"', GEN.read_text(encoding="utf-8")))
    stubs = set(stub_names)

    print(f"ORIG exports: {len(orig_ex)}  size: {ORIG.stat().st_size // 1024} KB")
    print(f"EMU  exports: {len(emu_ex)}  size: {EMU.stat().st_size // 1024} KB")
    print(f"Generated stubs: {len(stubs)}")
    print(f"Manual overrides: {len(manual)}")
    print(f"Missing from EMU: {len(set(orig_ex) - set(emu_ex))}")
    print(f"Extra in EMU: {len(set(emu_ex) - set(orig_ex))}")

    key_prefixes = [
        "Lobby",
        "P2P",
        "Presence",
        "Connect",
        "Auth",
        "Friends",
        "Sessions",
        "RTC",
        "UserInfo",
        "IntegratedPlatform",
        "CustomInvites",
        "Achievements",
        "AntiCheatClient",
    ]
    print("\n=== Subsystem coverage (ORIG exports) ===")
    for prefix in key_prefixes:
        funcs = [e for e in orig_ex if e.startswith("EOS_" + prefix)]
        stubbed = [f for f in funcs if f in stubs]
        manual_f = [f for f in funcs if f in manual]
        impl = [f for f in funcs if f not in stubs and f not in manual]
        print(
            f"{prefix:20} total={len(funcs):3}  real~={len(impl):3}  "
            f"stub={len(stubbed):3}  manual={len(manual_f):3}"
        )
        if stubbed:
            print("  stub:", ", ".join(stubbed[:6]), ("..." if len(stubbed) > 6 else ""))

    if GFX.exists():
        data = GFX.read_bytes()
        plug_apis = sorted(
            set(m.group(0).decode() for m in re.finditer(rb"EOS_[A-Za-z0-9_]+", data))
        )
        print(f"\n=== Unity GfxPluginNativeRender ({len(plug_apis)} refs) ===")
        for a in plug_apis:
            if a in stubs:
                tag = "STUB"
            elif a in manual:
                tag = "MANUAL"
            elif a in orig_ex:
                tag = "IMPL"
            else:
                tag = "?"
            print(f"  {a:55} [{tag}]")

    # stub return patterns
    stub_text = STUB_CPP.read_text(encoding="utf-8", errors="ignore")
    success_stubs = len(re.findall(r"return EOS_EResult::EOS_Success", stub_text))
    invalid_notif = len(re.findall(r"return EOS_INVALID_NOTIFICATIONID", stub_text))
    print(f"\nStub patterns: Success={success_stubs} InvalidNotification={invalid_notif}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
