#!/usr/bin/env python3
import struct
import sys

def get_exports(path):
    with open(path, "rb") as f:
        data = f.read()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_hdr_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    export_rva = struct.unpack_from("<I", data, opt + (112 if magic == 0x20B else 96))[0]
    sec_off = pe_off + 24 + opt_hdr_size

    def rva_to_off(rva):
        for i in range(num_sections):
            s = sec_off + i * 40
            va = struct.unpack_from("<I", data, s + 12)[0]
            raw = struct.unpack_from("<I", data, s + 20)[0]
            vsize = struct.unpack_from("<I", data, s + 8)[0]
            rawsize = struct.unpack_from("<I", data, s + 16)[0]
            if va <= rva < va + max(vsize, rawsize):
                return rva - va + raw
        return None

    off = rva_to_off(export_rva)
    num_names = struct.unpack_from("<I", data, off + 24)[0]
    names_off = rva_to_off(struct.unpack_from("<I", data, off + 32)[0])
    out = set()
    for i in range(num_names):
        name_rva = struct.unpack_from("<I", data, names_off + i * 4)[0]
        noff = rva_to_off(name_rva)
        end = data.find(b"\x00", noff)
        out.add(data[noff:end].decode("ascii", "ignore"))
    return out


def get_imports(path, dll="EOSSDK-Win64-Shipping.dll"):
    with open(path, "rb") as f:
        data = f.read()
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    num_sections = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_hdr_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    opt = pe_off + 24
    magic = struct.unpack_from("<H", data, opt)[0]
    import_rva = struct.unpack_from("<I", data, opt + (120 if magic == 0x20B else 104))[0]
    sec_off = pe_off + 24 + opt_hdr_size

    def rva_to_off(rva):
        for i in range(num_sections):
            s = sec_off + i * 40
            va = struct.unpack_from("<I", data, s + 12)[0]
            raw = struct.unpack_from("<I", data, s + 20)[0]
            vsize = struct.unpack_from("<I", data, s + 8)[0]
            rawsize = struct.unpack_from("<I", data, s + 16)[0]
            if va <= rva < va + max(vsize, rawsize):
                return rva - va + raw
        return None

    off = rva_to_off(import_rva)
    while True:
        orig_first_thunk = struct.unpack_from("<I", data, off)[0]
        name_rva = struct.unpack_from("<I", data, off + 12)[0]
        first_thunk = struct.unpack_from("<I", data, off + 16)[0]
        if orig_first_thunk == 0 and name_rva == 0 and first_thunk == 0:
            break
        noff = rva_to_off(name_rva)
        name = data[noff : data.find(b"\x00", noff)].decode("ascii", "ignore")
        if name == dll:
            funcs = []
            fo = rva_to_off(orig_first_thunk or first_thunk)
            idx = 0
            ptr_size = 8 if magic == 0x20B else 4
            while True:
                entry = struct.unpack_from("<Q" if magic == 0x20B else "<I", data, fo + idx * ptr_size)[0]
                if entry == 0:
                    break
                if not (entry & (1 << (63 if magic == 0x20B else 31))):
                    ho = rva_to_off(entry & 0x7FFFFFFF)
                    funcs.append(data[ho + 2 : data.find(b"\x00", ho + 2)].decode("ascii", "ignore"))
                idx += 1
            return funcs
        off += 20
    return []


def main():
    exe = r"f:\SteamLibrary\steamapps\common\Space Marine 2\client_pc\root\bin\pc\Warhammer 40000 Space Marine 2 - Retail.exe"
    orig = r"f:\SteamLibrary\steamapps\common\Space Marine 2\client_pc\root\bin\pc\EOSSDK-Win64-Shipping.dll"
    emu = r"c:\git\eos_sdk_emu\build\bin\Release\EOSSDK-Win64-Shipping.dll"

    orig_exports = get_exports(orig)
    emu_exports = get_exports(emu)
    imports = get_imports(exe)

    missing_from_emu = sorted(orig_exports - emu_exports)
    missing_from_imports = sorted(set(imports) - emu_exports)
    missing_imports_in_orig = sorted(set(imports) - orig_exports)

    print(f"Original exports: {len(orig_exports)}")
    print(f"Emu exports: {len(emu_exports)}")
    print(f"EXE direct imports: {len(imports)}")
    print(f"Missing from emu vs original ({len(missing_from_emu)}):")
    for x in missing_from_emu:
        print(f"  {x}")
    print(f"EXE imports missing in emu ({len(missing_from_imports)}):")
    for x in missing_from_imports:
        print(f"  {x}")
    print(f"EXE imports missing even in original ({len(missing_imports_in_orig)}):")
    for x in missing_imports_in_orig:
        print(f"  {x}")

    ac = sorted(x for x in imports if "AntiCheat" in x)
    print(f"\nAntiCheat imports ({len(ac)}):")
    for x in ac:
        ok = "OK" if x in emu_exports else "MISSING"
        print(f"  [{ok}] {x}")


if __name__ == "__main__":
    main()
