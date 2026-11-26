#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Automated macOS 15 Sequoia QEMU/KVM + OpenCore setup with MT7902 kexts pre-injected.
#
# Run WITHOUT sudo while host Wi-Fi (wlo1) is online:
#   ./scripts/vfio-macos-vm/setup_sequoia_kvm.sh
#
# Optional flags:
#   SKIP_RECOVERY_DOWNLOAD=1 ./scripts/vfio-macos-vm/setup_sequoia_kvm.sh  (only build OpenCore-MT7902.img)

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VM_DIR="${REPO_ROOT}/vm"
MACOS_TARGET="${MACOS_TARGET:-Sequoia}" # Sequoia | Sonoma | Ventura | Tahoe
DISK_SIZE="${DISK_SIZE:-80G}"

WIFI_KEXT="${REPO_ROOT}/dist/AirportMT7902-${MACOS_TARGET}/AirportMT7902.kext"
BT_KEXT="${REPO_ROOT}/dist/MT7902BluetoothFirmware/MT7902BluetoothFirmware.kext"

mkdir -p "${VM_DIR}"

if [ ! -d "${WIFI_KEXT}" ] || [ ! -d "${BT_KEXT}" ]; then
    echo "[ERR] Built kexts not found in ${REPO_ROOT}/dist/."
    echo "      Download from GitHub Actions first: gh run download -n MT7902-macOS-Kexts-Release -D dist"
    exit 1
fi

# 1. Clone kholia/OSX-KVM template if not present
if [ ! -d "${VM_DIR}/OSX-KVM" ]; then
    echo "[*] Cloning kholia/OSX-KVM into ${VM_DIR}/OSX-KVM..."
    git clone --depth 1 https://github.com/kholia/OSX-KVM.git "${VM_DIR}/OSX-KVM"
fi

# 2. Download Acidanthera BlueToolFixup.kext (from BrcmPatchRAM release)
BLUETOOL_KEXT="${VM_DIR}/BlueToolFixup.kext"
if [ ! -d "${BLUETOOL_KEXT}" ]; then
    echo "[*] Downloading Acidanthera BlueToolFixup.kext..."
    BRCM_URL=$(curl -fsSL https://api.github.com/repos/acidanthera/BrcmPatchRAM/releases/latest \
        | python3 -c 'import sys, json; rel=json.load(sys.stdin); print(next(a["browser_download_url"] for a in rel["assets"] if "RELEASE" in a["name"] and a["name"].endswith(".zip")))')
    curl -fL "${BRCM_URL}" -o "${VM_DIR}/BrcmPatchRAM.zip"
    python3 -m zipfile -e "${VM_DIR}/BrcmPatchRAM.zip" "${VM_DIR}/brcm_extracted"
    FOUND_BT=$(find "${VM_DIR}/brcm_extracted" -type d -name "BlueToolFixup.kext" | head -n 1)
    cp -R "${FOUND_BT}" "${BLUETOOL_KEXT}"
    rm -rf "${VM_DIR}/BrcmPatchRAM.zip" "${VM_DIR}/brcm_extracted"
fi

# 3. Build a spacious 256 MiB FAT32 OpenCore image with MT7902 kexts pre-linked
OC_RAW_SRC="${VM_DIR}/oc_orig.raw"
OC_MT7902_IMG="${VM_DIR}/OpenCore-MT7902.img"
echo "[*] Extracting base OpenCore EFI from OSX-KVM..."
qemu-img convert -f qcow2 -O raw "${VM_DIR}/OSX-KVM/OpenCore/OpenCore.qcow2" "${OC_RAW_SRC}"

# Find ESP byte offset inside oc_orig.raw using sfdisk JSON
ESP_OFFSET=$(python3 - "${OC_RAW_SRC}" <<'EOF'
import json, subprocess, sys
out = subprocess.check_output(["/usr/sbin/sfdisk", "-J", sys.argv[1]], text=True)
tbl = json.loads(out)["partitiontable"]
sector = tbl.get("sectorsize", 512)
for p in tbl["partitions"]:
    if "EFI" in p.get("name", "") or p.get("type", "").upper().startswith("C12A7328"):
        print(p["start"] * sector)
        sys.exit(0)
print(tbl["partitions"][0]["start"] * sector)
EOF
)

TMP_EFI=$(mktemp -d)
mcopy -s -i "${OC_RAW_SRC}@@${ESP_OFFSET}" "::/EFI" "${TMP_EFI}/"
rm -f "${OC_RAW_SRC}"

echo "[*] Creating 256 MiB OpenCore-MT7902.img and injecting ${MACOS_TARGET} kexts..."
rm -f "${OC_MT7902_IMG}"
truncate -s 256M "${OC_MT7902_IMG}"
/usr/sbin/mkfs.vfat -F 32 -n "OPENCORE" "${OC_MT7902_IMG}" >/dev/null
mcopy -s -i "${OC_MT7902_IMG}" "${TMP_EFI}/EFI" "::/"
rm -rf "${TMP_EFI}"

python3 "${REPO_ROOT}/scripts/vfio-macos-vm/inject_opencore_kexts.py" \
    --oc-img "${OC_MT7902_IMG}" \
    --wifi-kext "${WIFI_KEXT}" \
    --bt-kext "${BT_KEXT}" \
    --bluetool-kext "${BLUETOOL_KEXT}"

# Copy 1080p OVMF_VARS locally so UEFI NVRAM is writable per-VM
if [ ! -f "${VM_DIR}/OVMF_VARS.fd" ]; then
    if [ -f "${VM_DIR}/OSX-KVM/OVMF_VARS-1920x1080.fd" ]; then
        cp "${VM_DIR}/OSX-KVM/OVMF_VARS-1920x1080.fd" "${VM_DIR}/OVMF_VARS.fd"
    elif [ -f "${VM_DIR}/OSX-KVM/OVMF_VARS.fd" ]; then
        cp "${VM_DIR}/OSX-KVM/OVMF_VARS.fd" "${VM_DIR}/OVMF_VARS.fd"
    else
        cp /usr/share/edk2/ovmf/OVMF_VARS.fd "${VM_DIR}/OVMF_VARS.fd"
    fi
fi

# 4. Create 80 GiB QCOW2 macOS system disk if not present
if [ ! -f "${VM_DIR}/mac_hdd_ng.qcow2" ]; then
    echo "[*] Creating ${DISK_SIZE} thin-provisioned macOS disk at ${VM_DIR}/mac_hdd_ng.qcow2..."
    qemu-img create -f qcow2 "${VM_DIR}/mac_hdd_ng.qcow2" "${DISK_SIZE}"
fi

# 5. Download official Apple macOS Recovery BaseSystem unless skipped
SHORTNAME=$(echo "${MACOS_TARGET}" | tr '[:upper:]' '[:lower:]')
if [ "${SKIP_RECOVERY_DOWNLOAD:-0}" != "1" ] && [ ! -f "${VM_DIR}/BaseSystem.img" ]; then
    if [ ! -f "${VM_DIR}/BaseSystem.dmg" ]; then
        echo "[*] Downloading official Apple macOS ${MACOS_TARGET} Recovery image..."
        python3 - "${VM_DIR}" "${SHORTNAME}" <<'PY'
import os, sys, runpy
vm_dir, short = sys.argv[1], sys.argv[2]
os.chdir(vm_dir)
os.get_terminal_size = lambda *a, **kw: os.terminal_size((80, 24))
sys.argv = ["OSX-KVM/fetch-macOS-v2.py", "-s", short]
try:
    runpy.run_path("OSX-KVM/fetch-macOS-v2.py", run_name="__main__")
except SystemExit as e:
    if e.code not in (None, 0):
        raise
PY
    fi
    if [ -f "${VM_DIR}/BaseSystem.dmg" ]; then
        echo "[*] Converting BaseSystem.dmg -> BaseSystem.img..."
        if command -v dmg2img >/dev/null 2>&1; then
            dmg2img -i "${VM_DIR}/BaseSystem.dmg" "${VM_DIR}/BaseSystem.img"
        elif python3 "${REPO_ROOT}/scripts/vfio-macos-vm/dmg2raw.py" "${VM_DIR}/BaseSystem.dmg" "${VM_DIR}/BaseSystem.img"; then
            true
        else
            qemu-img convert -f dmg -O raw "${VM_DIR}/BaseSystem.dmg" "${VM_DIR}/BaseSystem.img"
        fi
    fi
fi

echo "=================================================================="
echo "[OK] macOS ${MACOS_TARGET} KVM + MT7902 OpenCore image ready at:"
echo "     - OpenCore image : ${OC_MT7902_IMG}"
echo "     - System disk    : ${VM_DIR}/mac_hdd_ng.qcow2"
echo ""
echo "Next steps:"
echo "  1) Install macOS 15 Sequoia (using host Wi-Fi NAT):"
echo "       ./scripts/vfio-macos-vm/boot_sequoia_kvm.sh --install"
echo "  2) Once installed, boot with REAL MT7902 PCIe Wi-Fi + USB BT passthrough:"
echo "       sudo ./scripts/vfio-macos-vm/boot_sequoia_kvm.sh --passthrough"
echo "=================================================================="
