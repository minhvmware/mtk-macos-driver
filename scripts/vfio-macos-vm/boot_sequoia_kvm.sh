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
    -device qemu-xhci,id=xhci,p2=8,p3=8
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
    -qmp "unix:${VM_DIR}/qmp.sock,server,nowait"
    # Expose dist/ as a read-only USB FAT drive inside macOS Finder for easy kext access
    -drive id=KextShare,if=none,format=raw,read-only=on,file="fat:ro:${REPO_ROOT}/dist"
    -device usb-storage,bus=xhci.0,drive=KextShare,removable=on
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
    else
        # Ensure PCI power state is active (D0) even if already bound earlier
        [ -f "/sys/bus/pci/devices/${PCI_BDF}/power/control" ] && echo "on" > "/sys/bus/pci/devices/${PCI_BDF}/power/control" || true
        RESTORE_WIFI_ON_EXIT=1
    fi

    REAL_USER="${SUDO_USER:-quangminh}"
    REAL_UID=$(id -u "${REAL_USER}")
    IOMMU_GRP=$(basename "$(readlink -f "/sys/bus/pci/devices/${PCI_BDF}/iommu_group")")
    chown "${REAL_USER}:${REAL_USER}" "/dev/vfio/${IOMMU_GRP}"
    chmod 0666 /dev/vfio/vfio 2>/dev/null || true

    echo "[*] Attaching MT7902 PCIe Wi-Fi (${PCI_SHORT}) directly on pcie.0 (Root Complex Endpoint)..."
    QEMU_ARGS+=(
        -device "vfio-pci,host=${PCI_SHORT},bus=pcie.0,addr=0x08"
    )

    BT_USB_LINE=$(lsusb -d "${BT_VID#0x}:${BT_PID#0x}" 2>/dev/null | head -n 1 || true)
    if [ -n "${BT_USB_LINE}" ]; then
        BT_BUS=$(echo "${BT_USB_LINE}" | awk '{print $2}')
        BT_DEV=$(echo "${BT_USB_LINE}" | awk '{gsub(":", "", $4); print $4}')
        if [ -e "/dev/bus/usb/${BT_BUS}/${BT_DEV}" ]; then
            chown "${REAL_USER}:${REAL_USER}" "/dev/bus/usb/${BT_BUS}/${BT_DEV}"
        fi
        echo "[*] Attaching MT7902 USB Bluetooth (${BT_VID}:${BT_PID} at /dev/bus/usb/${BT_BUS}/${BT_DEV})..."
        QEMU_ARGS+=(-device "usb-host,bus=xhci.0,vendorid=${BT_VID},productid=${BT_PID}")
    fi

    # Secondary NAT NIC (uses iPhone tethering on host) for SSH & downloading macOS installer
    QEMU_ARGS+=(
        -netdev user,id=net0,hostfwd=tcp::50922-:22
        -device virtio-net-pci,netdev=net0,id=vnet0,mac=52:54:00:c9:18:27
    )

    touch "${VM_DIR}/serial.log"
    chown "${REAL_USER}:${REAL_USER}" "${VM_DIR}/serial.log" "${OVMF_VARS}" "${HDD_IMG}" "${OC_IMG}" 2>/dev/null || true
    rm -f "${VM_DIR}/qmp.sock"

    # Raise locked-memory limit for 8 GiB VFIO DMA pinning, then drop to desktop user without PAM resetting ulimit!
    ulimit -l unlimited
    REAL_GID=$(id -g "${REAL_USER}")
    WAYLAND_SOCK=$(ls "/run/user/${REAL_UID}/"wayland-* 2>/dev/null | grep -v '\.lock$' | head -n 1 || true)
    WAYLAND_NAME="${WAYLAND_SOCK##*/}"
    [ -n "${WAYLAND_NAME}" ] || WAYLAND_NAME="wayland-1"

    echo "[*] Kernel serial log will be written to: ${VM_DIR}/serial.log"
    setpriv --reuid="${REAL_UID}" --regid="${REAL_GID}" --init-groups env \
        HOME="/home/${REAL_USER}" \
        USER="${REAL_USER}" \
        LOGNAME="${REAL_USER}" \
        XDG_RUNTIME_DIR="/run/user/${REAL_UID}" \
        WAYLAND_DISPLAY="${WAYLAND_NAME}" \
        DISPLAY="${DISPLAY:-:0}" \
        GDK_BACKEND=wayland,x11 \
        qemu-system-x86_64 "${QEMU_ARGS[@]}"
    exit $?
else
    echo "[*] Booting in --install mode (NAT networking via host; MT7902 stays on Linux)..."
    rm -f "${VM_DIR}/qmp.sock"
    QEMU_ARGS+=(
        -netdev user,id=net0,hostfwd=tcp::50922-:22
        -device virtio-net-pci,netdev=net0,id=net0,mac=52:54:00:c9:18:27
    )
fi

echo "[*] Kernel serial log will be written to: ${VM_DIR}/serial.log"
exec env GDK_BACKEND=wayland,x11 qemu-system-x86_64 "${QEMU_ARGS[@]}"
