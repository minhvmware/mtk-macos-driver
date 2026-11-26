#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Inspect host KVM, IOMMU group isolation, PCIe MT7902 (14c3:7902), and USB BT (13d3:3579)
# before launching a macOS QEMU/KVM (or docker-osx) VM with hardware passthrough.

set -euo pipefail

PCI_BDF="${MT7902_PCI_BDF:-0000:02:00.0}"
USB_VID_PID="${MT7902_BT_VIDPID:-13d3:3579}"

echo "=================================================================="
echo " MT7902 macOS KVM / VFIO Hardware Passthrough Readiness Check"
echo "=================================================================="

# 1. KVM check
if [ -e /dev/kvm ]; then
    echo "[OK] /dev/kvm is present."
else
    echo "[ERR] /dev/kvm not found. Enable Intel VT-x / AMD-V in BIOS."
fi

# 2. PCIe Wi-Fi card check
if [ -d "/sys/bus/pci/devices/${PCI_BDF}" ]; then
    VENDOR=$(cat "/sys/bus/pci/devices/${PCI_BDF}/vendor")
    DEVICE=$(cat "/sys/bus/pci/devices/${PCI_BDF}/device")
    DRIVER=$(basename "$(readlink -f "/sys/bus/pci/devices/${PCI_BDF}/driver" 2>/dev/null || echo none)")
    echo "[OK] Found PCIe device ${PCI_BDF} (${VENDOR}:${DEVICE}), active driver: ${DRIVER}"
else
    echo "[WARN] PCIe device ${PCI_BDF} not found. Override with MT7902_PCI_BDF=0000:xx:00.0"
fi

# 3. IOMMU group check
if [ -L "/sys/bus/pci/devices/${PCI_BDF}/iommu_group" ]; then
    GROUP_PATH=$(readlink -f "/sys/bus/pci/devices/${PCI_BDF}/iommu_group")
    GROUP_NUM=$(basename "${GROUP_PATH}")
    PEER_COUNT=$(ls -1 "${GROUP_PATH}/devices" | wc -l)
    echo "[OK] IOMMU is ENABLED — ${PCI_BDF} is in IOMMU group ${GROUP_NUM} (${PEER_COUNT} device(s)):"
    for dev in "${GROUP_PATH}/devices/"*; do
        echo "     - $(basename "${dev}")"
    done
    if [ "${PEER_COUNT}" -gt 1 ]; then
        echo "[WARN] Multiple devices share IOMMU group ${GROUP_NUM}. Ensure root port ACS or bind all non-bridge peers."
    fi
else
    echo "[ACTION REQUIRED] IOMMU is NOT active in your current Linux kernel boot parameters!"
    CPU_VENDOR=$(grep -m1 'vendor_id' /proc/cpuinfo | awk '{print $3}' || true)
    if [ "${CPU_VENDOR}" = "GenuineIntel" ]; then
        KARG="intel_iommu=on iommu=pt"
    else
        KARG="amd_iommu=on iommu=pt"
    fi
    echo ""
    echo "  On Fedora, enable IOMMU and reboot with:"
    echo "    sudo grubby --update-kernel=ALL --args=\"${KARG}\""
    echo "    sudo reboot"
    echo ""
fi

# 4. USB Bluetooth check
if command -v lsusb >/dev/null 2>&1; then
    if lsusb -d "${USB_VID_PID}" >/dev/null 2>&1; then
        echo "[OK] Found MT7902 USB Bluetooth (${USB_VID_PID}): $(lsusb -d "${USB_VID_PID}")"
        echo "     (Note: USB passthrough works immediately in QEMU/docker-osx even without IOMMU!)"
    else
        echo "[WARN] USB device ${USB_VID_PID} not detected by lsusb."
    fi
fi

echo "=================================================================="
