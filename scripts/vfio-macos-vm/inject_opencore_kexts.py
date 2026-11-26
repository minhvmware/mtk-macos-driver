#!/usr/bin/env python3
"""
Inject AirportMT7902.kext, MT7902BluetoothFirmware.kext, and BlueToolFixup.kext
into an OpenCore FAT32 image (using unprivileged mtools) and update EFI/OC/config.plist.
"""

import argparse
import plistlib
import subprocess
import tempfile
from pathlib import Path


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def make_kext_entry(bundle_path: str, exec_path: str, comment: str) -> dict:
    return {
        "Arch": "x86_64",
        "BundlePath": bundle_path,
        "Comment": comment,
        "Enabled": True,
        "ExecutablePath": exec_path,
        "MaxKernel": "",
        "MinKernel": "",
        "PlistPath": "Contents/Info.plist",
    }


def patch_config_plist(plist_path: Path) -> None:
    with plist_path.open("rb") as f:
        cfg = plistlib.load(f)

    # 1. Inject kexts in Kernel -> Add (after Lilu/VirtualSMC)
    kernel_add = cfg.setdefault("Kernel", {}).setdefault("Add", [])
    wanted = [
        make_kext_entry(
            "BlueToolFixup.kext",
            "Contents/MacOS/BlueToolFixup",
            "Acidanthera BlueToolFixup for macOS 12+ Bluetooth",
        ),
        make_kext_entry(
            "MT7902BluetoothFirmware.kext",
            "Contents/MacOS/MT7902BluetoothFirmware",
            "MediaTek MT7902 USB Bluetooth 79xx WMT Firmware Loader",
        ),
        make_kext_entry(
            "AirportMT7902.kext",
            "Contents/MacOS/AirportMT7902",
            "MediaTek MT7902 Native macOS Wi-Fi 6E Driver",
        ),
    ]
    wanted_names = {w["BundlePath"] for w in wanted}
    kernel_add[:] = [k for k in kernel_add if k.get("BundlePath") not in wanted_names]
    kernel_add.extend(wanted)

    # 2. Ensure permissive security & verbose serial/screen kext debug flags for testing
    misc_sec = cfg.setdefault("Misc", {}).setdefault("Security", {})
    misc_sec["DmgLoading"] = "Any"
    misc_sec["SecureBootModel"] = "Disabled"
    misc_sec["Vault"] = "Optional"

    nvram_add = cfg.setdefault("NVRAM", {}).setdefault("Add", {})
    apple_boot = nvram_add.setdefault("7C436110-AB2A-4BBB-A880-FE41995C9F82", {})
    cur_args = apple_boot.get("boot-args", "")
    for flag in [
        "-v",
        "keepsyms=1",
        "debug=0x100",
        "serial=1",
        "amfi=0x80",
        "-lilubetaall",
        "-btlfxboardid",
        "-btlfxallowanyaddr",
    ]:
        if flag not in cur_args.split():
            cur_args = (cur_args + " " + flag).strip()
    apple_boot["boot-args"] = cur_args
    # csr-active-config = 0x00000803 (LE: 03 08 00 00) allows unsigned third-party kexts
    apple_boot["csr-active-config"] = bytes([0x03, 0x08, 0x00, 0x00])

    nvram_del = cfg.setdefault("NVRAM", {}).setdefault("Delete", {})
    del_boot = nvram_del.setdefault("7C436110-AB2A-4BBB-A880-FE41995C9F82", [])
    for key in ["boot-args", "csr-active-config"]:
        if key not in del_boot:
            del_boot.append(key)

    with plist_path.open("wb") as f:
        plistlib.dump(cfg, f, sort_keys=False)


def main() -> None:
    parser = argparse.ArgumentParser(description="Inject MT7902 kexts into OpenCore FAT32 image")
    parser.add_argument("--oc-img", type=Path, required=True, help="Raw FAT32 OpenCore image path")
    parser.add_argument("--wifi-kext", type=Path, required=True, help="Path to AirportMT7902.kext")
    parser.add_argument("--bt-kext", type=Path, required=True, help="Path to MT7902BluetoothFirmware.kext")
    parser.add_argument("--bluetool-kext", type=Path, required=True, help="Path to BlueToolFixup.kext")
    args = parser.parse_args()

    img_spec = str(args.oc_img)

    for kext in [args.bluetool_kext, args.bt_kext, args.wifi_kext]:
        if not kext.is_dir():
            raise FileNotFoundError(f"Missing kext bundle: {kext}")
        # Remove existing copy in image if present, then copy recursively
        subprocess.run(["mdeltree", "-i", img_spec, f"::/EFI/OC/Kexts/{kext.name}"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        run(["mcopy", "-s", "-o", "-i", img_spec, str(kext), "::/EFI/OC/Kexts/"])

    with tempfile.TemporaryDirectory() as tmp:
        cfg_tmp = Path(tmp) / "config.plist"
        run(["mcopy", "-o", "-i", img_spec, "::/EFI/OC/config.plist", str(cfg_tmp)])
        patch_config_plist(cfg_tmp)
        run(["mcopy", "-o", "-i", img_spec, str(cfg_tmp), "::/EFI/OC/config.plist"])

    print(f"[OK] Injected AirportMT7902.kext, MT7902BluetoothFirmware.kext & BlueToolFixup.kext into {args.oc_img}")


if __name__ == "__main__":
    main()
