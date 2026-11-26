#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Release MediaTek MT7902 (14c3:7902) from vfio-pci and re-attach to Linux mt7921e.

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

CUR_DRV=$(basename "$(readlink -f "${SYS_DEV}/driver" 2>/dev/null || echo none)")
if [ "${CUR_DRV}" = "vfio-pci" ]; then
    echo "[*] Unbinding ${PCI_BDF} from vfio-pci..."
    echo "${PCI_BDF}" > /sys/bus/pci/drivers/vfio-pci/unbind
fi

echo "" > "${SYS_DEV}/driver_override"
modprobe mt7921e || true
echo "${PCI_BDF}" > /sys/bus/pci/drivers_probe

NEW_DRV=$(basename "$(readlink -f "${SYS_DEV}/driver" 2>/dev/null || echo none)")
echo "[OK] ${PCI_BDF} restored to host driver: ${NEW_DRV}"
