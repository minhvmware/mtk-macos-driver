// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#include <Headers/kern_api.hpp>
#include <Headers/kern_user.hpp>
#include <Headers/kern_util.hpp>
#include <Headers/plugin_start.hpp>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <sys/proc.h>

#include "MT7902BluetoothPatcher.hpp"

static MT7902BtPatcherCore gPatcherCore;
static MT7902BtPatcherCore *gCallbackCore = nullptr;

mt7902::bt::HciRandomAddrPatcherState MT7902BtPatcherCore::sHciState;
uint32_t MT7902BtPatcherCore::sHciCmdLogCount = 0;
uint32_t MT7902BtPatcherCore::sVmmBypassLogCount = 0;

static const char *bootargOff[] {
    "-mtkbtpoff"
};

static const char *bootargDebug[] {
    "-mtkbtpdbg"
};

static const char *bootargBeta[] {
    "-mtkbtpbeta"
};

PluginConfiguration ADDPR(config) {
    xStringify(PRODUCT_NAME),
    parseModuleVersion(xStringify(MODULE_VERSION)),
    LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
    bootargOff,
    arrsize(bootargOff),
    bootargDebug,
    arrsize(bootargDebug),
    bootargBeta,
    arrsize(bootargBeta),
    KernelVersion::Monterey,
    KernelVersion::Tahoe,
    []() {
        gPatcherCore.init();
    }
};

static const char *kIOUSBHostFamilyPaths[] {
    "/System/Library/Extensions/IOUSBHostFamily.kext/Contents/MacOS/IOUSBHostFamily"
};

static KernelPatcher::KextInfo kIOUSBHostFamilyInfo {
    "com.apple.iokit.IOUSBHostFamily",
    kIOUSBHostFamilyPaths,
    1,
    {true, true},
    {},
    KernelPatcher::KextInfo::Unloaded
};

namespace {

mtk_sysctl_oid_iterator sysctlIteratorBegin(mtk_sysctl_oid_list *l) {
    mtk_sysctl_oid_iterator it {};
    mtk_sysctl_oid *a = SLIST_FIRST(l);
    if (!a)
        return it;
    if (a->oid_number == MTK_OID_MUTABLE_ANCHOR) {
        it.a = SLIST_NEXT(a, oid_link);
        it.b = SLIST_FIRST(reinterpret_cast<mtk_sysctl_oid_list *>(a->oid_arg1));
    } else {
        it.a = a;
    }
    return it;
}

mtk_sysctl_oid *sysctlIteratorNext(mtk_sysctl_oid_iterator *it) {
    mtk_sysctl_oid *a = it->a;
    mtk_sysctl_oid *b = it->b;
    if (a) {
        it->a = SLIST_NEXT(a, oid_link);
        return a;
    }
    if (b) {
        it->b = SLIST_NEXT(b, oid_link);
        return b;
    }
    return nullptr;
}

mtk_sysctl_oid *findSysctlByName(mtk_sysctl_oid_list *rootList, const char *orgName) {
    char named[64];
    strlcpy(named, orgName, sizeof(named));
    char *name = named;
    size_t len = strlen(name);
    if (len > 0 && name[len - 1] == '.')
        name[len - 1] = '\0';

    char *p = name;
    while (*p && *p != '.')
        ++p;
    char sep = *p;
    if (sep == '.')
        *p = '\0';

    mtk_sysctl_oid_iterator it = sysctlIteratorBegin(rootList);
    mtk_sysctl_oid *oidp = sysctlIteratorNext(&it);
    size_t depth = 0;

    while (oidp && depth < MTK_CTL_MAXNAME) {
        if (strcmp(name, oidp->oid_name) != 0) {
            oidp = sysctlIteratorNext(&it);
            continue;
        }
        ++depth;

        if (sep == '\0')
            return oidp;

        if ((oidp->oid_kind & MTK_CTLTYPE) != MTK_CTLTYPE_NODE)
            break;
        if (oidp->oid_handler)
            break;

        it = sysctlIteratorBegin(reinterpret_cast<mtk_sysctl_oid_list *>(oidp->oid_arg1));
        oidp = sysctlIteratorNext(&it);

        *p = sep;
        name = p + 1;
        p = name;
        while (*p && *p != '.')
            ++p;
        sep = *p;
        if (sep == '.')
            *p = '\0';
    }

    return nullptr;
}

} // namespace

bool MT7902BtPatcherCore::init() {
    gCallbackCore = this;
    sHciState.reset();
    SYSLOG(MTKBTP_LOG_TAG, "MT7902BluetoothPatcher initializing on Darwin %d", getKernelVersion());

    // 1. Hook kern.hv_vmm_present so bluetoothd/BlueTool do not skip USB BT init in VMs / VMM-spoofed systems
    lilu.onPatcherLoadForce(
        [](void *user, KernelPatcher &patcher) {
            static_cast<MT7902BtPatcherCore *>(user)->processKernel(patcher);
        },
        this);

    // 2. Hook IOUSBHostDevice::deviceRequest to inject HCI_LE_Set_Random_Address before LE scan setup
    lilu.onKextLoadForce(
        &kIOUSBHostFamilyInfo,
        1,
        [](void *user, KernelPatcher &patcher, size_t index, mach_vm_address_t address, size_t size) {
            static_cast<MT7902BtPatcherCore *>(user)->processKext(patcher, index, address, size);
        },
        this);

    return true;
}

void MT7902BtPatcherCore::processKernel(KernelPatcher &patcher) {
    auto *sysctlChildren = reinterpret_cast<mtk_sysctl_oid_list *>(
        patcher.solveSymbol(KernelPatcher::KernelID, "_sysctl__children"));
    if (!sysctlChildren) {
        SYSLOG(MTKBTP_LOG_TAG, "failed to resolve _sysctl__children");
        patcher.clearError();
        return;
    }

    mtk_sysctl_oid *vmmOid = findSysctlByName(sysctlChildren, "kern.hv_vmm_present");
    if (!vmmOid || !vmmOid->oid_handler) {
        SYSLOG(MTKBTP_LOG_TAG, "failed to resolve kern.hv_vmm_present oid");
        return;
    }

    orgSysctlHvVmmPresent = patcher.routeFunction(
        reinterpret_cast<mach_vm_address_t>(vmmOid->oid_handler),
        reinterpret_cast<mach_vm_address_t>(mySysctlHvVmmPresent),
        true);
    if (!orgSysctlHvVmmPresent) {
        SYSLOG(MTKBTP_LOG_TAG, "failed to route kern.hv_vmm_present handler");
        patcher.clearError();
        return;
    }

    SYSLOG(MTKBTP_LOG_TAG, "routed kern.hv_vmm_present for bluetoothd/BlueTool");
}

void MT7902BtPatcherCore::processKext(KernelPatcher &patcher, size_t index,
                                      mach_vm_address_t address, size_t size) {
    if (kIOUSBHostFamilyInfo.loadIndex != index)
        return;

    KernelPatcher::RouteRequest req {
        "__ZN15IOUSBHostDevice13deviceRequestEP9IOServiceRN11StandardUSB13DeviceRequestEPvP18IOMemoryDescriptorRjP19IOUSBHostCompletionj",
        myHostDeviceRequest,
        orgHostDeviceRequest
    };

    patcher.routeMultiple(index, &req, 1, address, size);
    if (patcher.getError() == KernelPatcher::Error::NoError) {
        SYSLOG(MTKBTP_LOG_TAG, "routed IOUSBHostDevice::deviceRequest for MT7902 BLE scan fix");
    } else {
        SYSLOG(MTKBTP_LOG_TAG, "failed to route IOUSBHostDevice::deviceRequest (err=%d)", patcher.getError());
        patcher.clearError();
    }
}

int MT7902BtPatcherCore::mySysctlHvVmmPresent(struct mtk_sysctl_oid *oidp, void *arg1, int arg2,
                                              struct mtk_sysctl_req *req) {
    if (req && req->p) {
        char procname[64] {};
        proc_name(proc_pid(req->p), procname, sizeof(procname));
        if (mt7902::bt::isBluetoothVmBypassProcess(procname)) {
            if (sVmmBypassLogCount < 4) {
                ++sVmmBypassLogCount;
                SYSLOG(MTKBTP_LOG_TAG, "kern.hv_vmm_present -> 0 for %s", procname);
            }
            int vmmOff = 0;
            return MTK_SYSCTL_OUT(req, &vmmOff, sizeof(vmmOff));
        }
    }

    return FunctionCast(mySysctlHvVmmPresent, gCallbackCore->orgSysctlHvVmmPresent)(oidp, arg1, arg2, req);
}

IOReturn MT7902BtPatcherCore::myHostDeviceRequest(void *that,
                                                  IOService *provider,
                                                  StandardUSB::DeviceRequest &request,
                                                  void *data,
                                                  IOMemoryDescriptor *descriptor,
                                                  unsigned int &length,
                                                  IOUSBHostCompletion *completion,
                                                  unsigned int timeout) {
    auto origFn = FunctionCast(myHostDeviceRequest, gCallbackCore->orgHostDeviceRequest);

    auto *usbDev = reinterpret_cast<IOUSBHostDevice *>(that);
    const StandardUSB::DeviceDescriptor *dd = usbDev ? usbDev->getDeviceDescriptor() : nullptr;

    if (dd &&
        mt7902::bt::isMT7902BtUsbId(dd->idVendor, dd->idProduct) &&
        mt7902::bt::isClassOutControlRequest(request.bmRequestType) &&
        request.wLength >= sizeof(mt7902::bt::HciCmdHeader)) {

        uint8_t hciBuf[32] {};
        uint32_t readLen = 0;

        if (data != nullptr) {
            readLen = request.wLength < sizeof(hciBuf) ? request.wLength : static_cast<uint32_t>(sizeof(hciBuf));
            memcpy(hciBuf, data, readLen);
        } else if (descriptor != nullptr) {
            bool prepared = false;
            bool canRead = true;
            if (getKernelVersion() >= KernelVersion::Sequoia) {
                if (descriptor->prepare(kIODirectionOut) == kIOReturnSuccess)
                    prepared = true;
                else
                    canRead = false;
            }
            if (canRead && descriptor->getLength() >= sizeof(mt7902::bt::HciCmdHeader)) {
                size_t toRead = descriptor->getLength();
                if (toRead > sizeof(hciBuf))
                    toRead = sizeof(hciBuf);
                readLen = static_cast<uint32_t>(descriptor->readBytes(0, hciBuf, toRead));
            }
            if (prepared)
                descriptor->complete(kIODirectionOut);
        }

        if (readLen >= sizeof(mt7902::bt::HciCmdHeader)) {
            const auto *hdr = reinterpret_cast<const mt7902::bt::HciCmdHeader *>(hciBuf);
            uint16_t opcode = hdr->opcode;

            if (sHciCmdLogCount < 40 || opcode == mt7902::bt::kHciOpLeSetScanParam ||
                opcode == mt7902::bt::kHciOpLeSetExtScanParam ||
                opcode == mt7902::bt::kHciOpLeSetScanEnable ||
                opcode == mt7902::bt::kHciOpLeSetExtScanEnable ||
                opcode == mt7902::bt::kHciOpLeSetRandomAddress ||
                opcode == mt7902::bt::kHciOpReset) {
                if (sHciCmdLogCount < 80) {
                    ++sHciCmdLogCount;
                    SYSLOG(MTKBTP_LOG_TAG,
                           "HCI OUT #%u: bmReq=0x%02x bReq=0x%02x wVal=0x%04x wIdx=0x%04x op=0x%04x plen=%u wLen=%u",
                           sHciCmdLogCount, request.bmRequestType, request.bRequest,
                           request.wValue, request.wIndex, opcode, hdr->paramLen, request.wLength);
                }
            }

            if (sHciState.onOutgoingHciOpcode(opcode)) {
                // Mirror the exact bmRequestType/bRequest/wValue/wIndex used by bluetoothd
                StandardUSB::DeviceRequest randReq = request;
                randReq.wLength = sizeof(mt7902::bt::kStaticRandomAddressHciCmd);

                IOBufferMemoryDescriptor *randDesc = IOBufferMemoryDescriptor::withBytes(
                    mt7902::bt::kStaticRandomAddressHciCmd,
                    sizeof(mt7902::bt::kStaticRandomAddressHciCmd),
                    kIODirectionOut);
                if (randDesc) {
                    uint32_t randTransferred = 0;
                    IOReturn prepRet = randDesc->prepare(kIODirectionOut);
                    IOReturn injRet = kIOReturnError;
                    if (prepRet == kIOReturnSuccess) {
                        injRet = origFn(that, provider, randReq, nullptr, randDesc,
                                        randTransferred, nullptr, timeout ? timeout : 1000);
                        randDesc->complete(kIODirectionOut);
                    }
                    randDesc->release();
                    SYSLOG(MTKBTP_LOG_TAG,
                           "[PATCH] Injected HCI_LE_Set_Random_Address before 0x%04x (bmReq=0x%02x bReq=0x%02x) -> ret=0x%x xfer=%u",
                           opcode, randReq.bmRequestType, randReq.bRequest, injRet, randTransferred);
                }
            }
        }
    }

    return origFn(that, provider, request, data, descriptor, length, completion, timeout);
}
