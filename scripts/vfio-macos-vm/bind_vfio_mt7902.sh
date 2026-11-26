#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Safely detach MediaTek MT7902 PCIe Wi-Fi (14c3:7902) from Linux mt7921e
# and bind it to vfio-pci so QEMU / docker-osx macOS VM can claim raw BAR0 + MSI + DMA.

set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    echo "Run as root: sudo $0"
    exit 1
fi

PCI_BDF="${MT7902_PCI_BDF:-0000:02:00.0}"
SYS_DEV="/sys/bus/pci/devices/${PCI_BDF}"

if [ ! -d "${SYS_DEV}" ]; then
    echo "[ERR] PCI device ${PCI_BDF} does not exist."
    exit 1
fi

if [ ! -L "${SYS_DEV}/iommu_group" ]; then
    echo "[ERR] IOMMU group missing for ${PCI_BDF}. Run scripts/vfio-macos-vm/check_iommu.sh first."
    exit 1
fi

modprobe vfio-pci

CUR_DRV=$(basename "$(readlink -f "${SYS_DEV}/driver" 2>/dev/null || echo none)")
if [ "${CUR_DRV}" = "vfio-pci" ]; then
    echo "[OK] ${PCI_BDF} is already bound to vfio-pci."
    exit 0
fi

if [ "${CUR_DRV}" != "none" ]; then
    echo "[*] Unbinding ${PCI_BDF} from ${CUR_DRV}..."
    echo "${PCI_BDF}" > "${SYS_DEV}/driver/unbind"
fi

echo "vfio-pci" > "${SYS_DEV}/driver_override"
echo "${PCI_BDF}" > /sys/bus/pci/drivers/vfio-pci/bind

echo "[OK] ${PCI_BDF} (14c3:7902) is now bound to vfio-pci (/dev/vfio/$(basename "$(readlink -f "${SYS_DEV}/iommu_group")"))."
