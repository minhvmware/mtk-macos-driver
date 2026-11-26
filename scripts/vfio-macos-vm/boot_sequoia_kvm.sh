#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Native QEMU/KVM launcher for macOS 15 Sequoia with MT7902 PCIe VFIO + USB BT passthrough.
#
# Modes:
#   ./scripts/vfio-macos-vm/boot_sequoia_kvm.sh --install
#       Uses virtio/vmxnet3 NAT networking (keeps host Wi-Fi wlo1 active so macOS Recovery
#       can download & install macOS 15 Sequoia onto vm/mac_hdd_ng.qcow2).
#
#   sudo ./scripts/vfio-macos-vm/boot_sequoia_kvm.sh --passthrough
#       Automatically binds 0000:02:00.0 (14c3:7902) to vfio-pci, passes through BOTH
#       MT7902 PCIe Wi-Fi + USB Bluetooth (13d3:3579) into macOS 15 Sequoia, logs kernel
#       serial output to vm/serial.log, and restores mt7921e to Fedora on VM exit!

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VM_DIR="${REPO_ROOT}/vm"
MODE="${1:---install}"

PCI_SHORT="${MT7902_PCI_SHORT:-02:00.0}"
PCI_BDF="0000:${PCI_SHORT}"
BT_VID="${MT7902_BT_VID:-0x13d3}"
BT_PID="${MT7902_BT_PID:-0x3579}"
RAM_MB="${RAM_MB:-8192}"
SMP_CORES="${SMP_CORES:-4}"

OC_IMG="${VM_DIR}/OpenCore-MT7902.img"
HDD_IMG="${VM_DIR}/mac_hdd_ng.qcow2"
OVMF_CODE="${VM_DIR}/OSX-KVM/OVMF_CODE_4M.fd"
[ -f "${OVMF_CODE}" ] || OVMF_CODE="${VM_DIR}/OSX-KVM/OVMF_CODE.fd"
[ -f "${OVMF_CODE}" ] || OVMF_CODE="/usr/share/edk2/ovmf/OVMF_CODE.fd"
OVMF_VARS="${VM_DIR}/OVMF_VARS.fd"

if [ ! -f "${OC_IMG}" ] || [ ! -f "${HDD_IMG}" ]; then
    echo "[ERR] VM images missing in ${VM_DIR}. Run ./scripts/vfio-macos-vm/setup_sequoia_kvm.sh first."
    exit 1
fi

# Ensure KVM ignores unhandled Apple MSRs (required by XNU kernel on QEMU/KVM)
if [ -f /sys/module/kvm/parameters/ignore_msrs ]; then
    MSR_CUR=$(cat /sys/module/kvm/parameters/ignore_msrs)
    if [ "${MSR_CUR}" != "Y" ] && [ "${MSR_CUR}" != "1" ]; then
        echo "[*] Enabling kvm.ignore_msrs=1 (required by macOS XNU kernel)..."
        if [ "$(id -u)" -eq 0 ]; then
            echo 1 > /sys/module/kvm/parameters/ignore_msrs
            echo 0 > /sys/module/kvm/parameters/report_ignored_msrs 2>/dev/null || true
        else
            sudo sh -c 'echo 1 > /sys/module/kvm/parameters/ignore_msrs; echo 0 > /sys/module/kvm/parameters/report_ignored_msrs 2>/dev/null || true'
        fi
    fi
fi

MY_CPU_OPTS="+ssse3,+sse4.2,+popcnt,+avx,+aes,+xsave,+xsaveopt,check"
QEMU_ARGS=(
    -enable-kvm
    -m "${RAM_MB}"
    -cpu "Skylake-Client,-hle,-rtm,kvm=on,vendor=GenuineIntel,+invtsc,vmware-cpuid-freq=on,${MY_CPU_OPTS}"
    -machine q35
    -smp "${SMP_CORES},cores=${SMP_CORES},sockets=1"
    -device qemu-xhci,id=xhci
    -device usb-kbd,bus=xhci.0
    -device usb-tablet,bus=xhci.0
    -device usb-ehci,id=ehci
    -device isa-applesmc,osk="ourhardworkbythesewordsguardedpleasedontsteal(c)AppleComputerInc"
    -drive if=pflash,format=raw,readonly=on,file="${OVMF_CODE}"
    -drive if=pflash,format=raw,file="${OVMF_VARS}"
    -smbios type=2
    -device ich9-intel-hda -device hda-duplex
    -device ich9-ahci,id=sata
    -drive id=OpenCoreBoot,if=none,format=raw,file="${OC_IMG}"
    -device ide-hd,bus=sata.2,drive=OpenCoreBoot,bootindex=1
    -drive id=MacHDD,if=none,format=qcow2,file="${HDD_IMG}"
    -device ide-hd,bus=sata.4,drive=MacHDD
    -device vmware-svga
    -serial "file:${VM_DIR}/serial.log"
    # Expose dist/ as a read-only FAT drive inside macOS Finder for easy kext updates
    -drive id=KextShare,if=none,format=raw,read-only=on,file="fat:ro:${REPO_ROOT}/dist"
    -device ide-hd,bus=sata.5,drive=KextShare
)

if [ -f "${VM_DIR}/BaseSystem.img" ]; then
    QEMU_ARGS+=(
        -drive id=InstallMedia,if=none,format=raw,file="${VM_DIR}/BaseSystem.img"
        -device ide-hd,bus=sata.3,drive=InstallMedia
    )
fi

RESTORE_WIFI_ON_EXIT=0
cleanup() {
    if [ "${RESTORE_WIFI_ON_EXIT}" = "1" ]; then
        echo ""
        echo "[*] VM exited — restoring MT7902 Wi-Fi (${PCI_BDF}) back to Fedora mt7921e..."
        "${REPO_ROOT}/scripts/vfio-macos-vm/unbind_vfio_mt7902.sh" || true
    fi
}
trap cleanup EXIT

if [ "${MODE}" = "--passthrough" ]; then
    if [ "$(id -u)" -ne 0 ]; then
        echo "[ERR] --passthrough requires root for VFIO/USB access: sudo $0 --passthrough"
        exit 1
    fi

    CUR_DRV=$(basename "$(readlink -f "/sys/bus/pci/devices/${PCI_BDF}/driver" 2>/dev/null || echo none)")
    if [ "${CUR_DRV}" != "vfio-pci" ]; then
        "${REPO_ROOT}/scripts/vfio-macos-vm/bind_vfio_mt7902.sh"
        RESTORE_WIFI_ON_EXIT=1
    fi

    echo "[*] Attaching MT7902 PCIe Wi-Fi (${PCI_SHORT}) via VFIO-PCI..."
    QEMU_ARGS+=(
        -device pcie-root-port,id=pcie_mt7902,slot=4,chassis=4
        -device "vfio-pci,host=${PCI_SHORT},bus=pcie_mt7902"
    )

    if lsusb -d "${BT_VID#0x}:${BT_PID#0x}" >/dev/null 2>&1; then
        echo "[*] Attaching MT7902 USB Bluetooth (${BT_VID}:${BT_PID})..."
        QEMU_ARGS+=(-device "usb-host,bus=xhci.0,vendorid=${BT_VID},productid=${BT_PID}")
    fi

    # Keep user-mode secondary NIC on port 50922 for SSH access into macOS guest
    QEMU_ARGS+=(
        -netdev user,id=net0,hostfwd=tcp::50922-:22
        -device virtio-net-pci,netdev=net0,id=vnet0,mac=52:54:00:c9:18:27
    )
else
    echo "[*] Booting in --install mode (NAT networking via host wlo1; MT7902 stays on Linux)..."
    QEMU_ARGS+=(
        -netdev user,id=net0,hostfwd=tcp::50922-:22
        -device virtio-net-pci,netdev=net0,id=net0,mac=52:54:00:c9:18:27
    )
fi

echo "[*] Kernel serial log will be written to: ${VM_DIR}/serial.log"
exec qemu-system-x86_64 "${QEMU_ARGS[@]}"
