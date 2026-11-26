#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Launch a macOS KVM test VM (via Podman/Docker `sickcodes/docker-osx` or direct QEMU)
# passing through:
#   1. MediaTek MT7902 PCIe Wi-Fi 6E (0000:02:00.0, 14c3:7902) via VFIO-PCI (when bound)
#   2. MediaTek MT7902 USB Bluetooth (13d3:3579) via QEMU usb-host
#
# Usage:
#   # Mode A: USB BT passthrough + PCIe Wi-Fi VFIO passthrough (after bind_vfio_mt7902.sh)
#   sudo ./scripts/vfio-macos-vm/run_macos_kvm_passthrough.sh
#
#   # Mode B: USB BT passthrough only (works even before enabling IOMMU!)
#   MT7902_SKIP_VFIO=1 sudo ./scripts/vfio-macos-vm/run_macos_kvm_passthrough.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
ENGINE="${CONTAINER_ENGINE:-$(command -v podman 2>/dev/null || command -v docker 2>/dev/null || true)}"

PCI_SHORT="${MT7902_PCI_SHORT:-02:00.0}"
PCI_BDF="0000:${PCI_SHORT}"
BT_VID="${MT7902_BT_VID:-0x13d3}"
BT_PID="${MT7902_BT_PID:-0x3579}"
MACOS_VERSION="${MACOS_VERSION:-sequoia}" # ventura | sonoma | sequoia
RAM_GB="${RAM_GB:-8}"
CORES="${CORES:-4}"

if [ -z "${ENGINE}" ]; then
    echo "[ERR] Neither podman nor docker found in PATH."
    exit 1
fi

EXTRA_QEMU_ARGS="-usb -device qemu-xhci,id=xhci0"
CONTAINER_DEV_ARGS=(--device /dev/kvm --device /dev/bus/usb)

# Attach USB Bluetooth if present on host
if lsusb -d "${BT_VID#0x}:${BT_PID#0x}" >/dev/null 2>&1; then
    echo "[*] Passing through MT7902 USB Bluetooth (${BT_VID}:${BT_PID})..."
    EXTRA_QEMU_ARGS="${EXTRA_QEMU_ARGS} -device usb-host,bus=xhci0.0,vendorid=${BT_VID},productid=${BT_PID}"
else
    echo "[WARN] USB Bluetooth ${BT_VID}:${BT_PID} not found on host; skipping usb-host arg."
fi

# Attach VFIO PCIe Wi-Fi if bound to vfio-pci
if [ "${MT7902_SKIP_VFIO:-0}" != "1" ]; then
    CUR_DRV=$(basename "$(readlink -f "/sys/bus/pci/devices/${PCI_BDF}/driver" 2>/dev/null || echo none)")
    if [ "${CUR_DRV}" = "vfio-pci" ]; then
        IOMMU_GRP=$(basename "$(readlink -f "/sys/bus/pci/devices/${PCI_BDF}/iommu_group")")
        echo "[*] Passing through MT7902 PCIe Wi-Fi (${PCI_BDF}, IOMMU group ${IOMMU_GRP})..."
        CONTAINER_DEV_ARGS+=(--device /dev/vfio/vfio --device "/dev/vfio/${IOMMU_GRP}")
        EXTRA_QEMU_ARGS="${EXTRA_QEMU_ARGS} -device pcie-root-port,id=pcie_mtk0,slot=4,chassis=4 -device vfio-pci,host=${PCI_SHORT},bus=pcie_mtk0"
    else
        echo "[WARN] ${PCI_BDF} is currently bound to '${CUR_DRV}' (not vfio-pci)."
        echo "       Run 'sudo ./scripts/vfio-macos-vm/bind_vfio_mt7902.sh' first for PCIe Wi-Fi passthrough,"
        echo "       or continue with USB Bluetooth passthrough only."
    fi
fi

echo "[*] Starting docker-osx (${MACOS_VERSION}) via ${ENGINE}..."
echo "    Workspace mounted inside container at /mnt/mtk-macos-driver"

exec "${ENGINE}" run -it \
    --privileged \
    "${CONTAINER_DEV_ARGS[@]}" \
    --ulimit memlock=-1:-1 \
    -p 50922:10022 \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    -v "${REPO_ROOT}:/mnt/mtk-macos-driver:Z" \
    -e "DISPLAY=${DISPLAY:-:0.0}" \
    -e "RAM=${RAM_GB}" \
    -e "SMP=${CORES}" \
    -e "CORES=${CORES}" \
    -e "SHORTNAME=${MACOS_VERSION}" \
    -e "EXTRA=${EXTRA_QEMU_ARGS}" \
    docker.io/sickcodes/docker-osx:latest
