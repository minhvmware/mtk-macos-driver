// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Lilu plugin for MediaTek MT7902 Bluetooth on macOS 12–26:
//   1. Injects HCI_LE_Set_Random_Address (0x2005) before BLE Scan Parameters (0x200B/0x2041)
//      so MediaTek MT7902 firmware enables BLE scanning (Nearby Devices).
//   2. Returns kern.hv_vmm_present=0 exclusively for bluetoothd/BlueTool so macOS 14+/15+
//      does not skip USB Bluetooth chip initialization inside VMs or VMM-spoofed Hackintoshes.

#ifndef MT7902_BLUETOOTH_PATCHER_HPP
#define MT7902_BLUETOOTH_PATCHER_HPP

#include <Headers/kern_patcher.hpp>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <sys/queue.h>
#include "MT7902BtPatcherLogic.hpp"

#define MTKBTP_LOG_TAG "mtkbtp"

// XNU sysctl_oid / sysctl_req definitions (matching Acidanthera RestrictEvents SoftwareUpdate.hpp)
#define MTK_CTL_MAXNAME        12
#define MTK_CTLTYPE            0xF
#define MTK_CTLTYPE_NODE       1
#define MTK_OID_MUTABLE_ANCHOR (-2147483647 - 1)
#define MTK_SYSCTL_OUT(r, p, l) ((r)->oldfunc)((r), (p), (l))

struct mtk_sysctl_oid_list;

struct mtk_sysctl_oid {
    struct mtk_sysctl_oid_list *oid_parent;
    SLIST_ENTRY(mtk_sysctl_oid) oid_link;
    int oid_number;
    int oid_kind;
    void *oid_arg1;
    int oid_arg2;
    const char *oid_name;
    int (*oid_handler)(struct mtk_sysctl_oid *oidp, void *arg1, int arg2, struct mtk_sysctl_req *req);
    const char *oid_fmt;
    const char *oid_descr;
    int oid_version;
    int oid_refcnt;
};

SLIST_HEAD(mtk_sysctl_oid_list, mtk_sysctl_oid);

struct mtk_sysctl_oid_iterator {
    struct mtk_sysctl_oid *a;
    struct mtk_sysctl_oid *b;
};

struct mtk_sysctl_req {
    struct proc *p;
    int lock;
    user_addr_t oldptr;
    size_t oldlen;
    size_t oldidx;
    int (*oldfunc)(struct mtk_sysctl_req *, const void *, size_t);
    user_addr_t newptr;
    size_t newlen;
    size_t newidx;
    int (*newfunc)(struct mtk_sysctl_req *, void *, size_t);
};

class MT7902BtPatcherCore {
public:
    bool init();
    void processKernel(KernelPatcher &patcher);
    void processKext(KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size);

    static int mySysctlHvVmmPresent(struct mtk_sysctl_oid *oidp, void *arg1, int arg2, struct mtk_sysctl_req *req);
    static IOReturn myHostDeviceRequest(void *that,
                                        IOService *provider,
                                        StandardUSB::DeviceRequest &request,
                                        void *data,
                                        IOMemoryDescriptor *descriptor,
                                        unsigned int &length,
                                        IOUSBHostCompletion *completion,
                                        unsigned int timeout);

    mach_vm_address_t orgSysctlHvVmmPresent {};
    mach_vm_address_t orgHostDeviceRequest {};

private:
    static mt7902::bt::HciRandomAddrPatcherState sHciState;
    static uint32_t sHciCmdLogCount;
    static uint32_t sVmmBypassLogCount;
};

#endif // MT7902_BLUETOOTH_PATCHER_HPP
