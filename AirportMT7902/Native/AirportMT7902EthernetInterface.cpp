// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project

#if defined(KERNEL) && defined(IO80211FAMILY_V2)

#include "AirportMT7902EthernetInterface.hpp"
#include <IOKit/network/IOEthernetController.h>
#include <IOKit/network/IONetworkStats.h>
#include <net/if.h>
#include <sys/kpi_mbuf.h>

#ifndef ETHER_HDR_LEN
#define ETHER_HDR_LEN 14
#endif
#ifndef ETHER_ALIGN
#define ETHER_ALIGN 2
#endif

#ifdef AIRPORT_WCL
extern IOCommandGate *gMT7902CommandGate;
#endif

#define super IOEthernetInterface
OSDefineMetaClassAndStructors(AirportMT7902EthernetInterface, IOEthernetInterface);

bool AirportMT7902EthernetInterface::initWithSkywalkInterfaceAndProvider(
    IONetworkController *controller, IO80211SkywalkInterface *interface) {
    if (!controller || !interface)
        return false;
    bool ret = super::init(controller);
    if (ret)
        mSkywalkIf = interface;
    mAttached = false;
    return ret;
}

#ifdef AIRPORT_WCL
IOReturn AirportMT7902EthernetInterface::prepareBSDGated(
    OSObject *, void *arg0, void *, void *, void *) {
    auto *self = static_cast<AirportMT7902EthernetInterface *>(arg0);
    auto *controller = OSDynamicCast(IO80211Controller, self->getController());
    auto *queue = controller ? controller->getWorkQueue() : nullptr;
    if (!queue || !queue->inGate() || queue->onThread())
        return kIOReturnNotPermitted;
    return self->mSkywalkIf->prepareBSDInterface(self->getIfnet(), 0);
}
#endif

IOReturn AirportMT7902EthernetInterface::attachToDataLinkLayer(IOOptionBits options, void *parameter) {
    char infName[IFNAMSIZ] = {};
#ifdef AIRPORT_WCL
    if (!setInterfaceSubType(3))
        return kIOReturnNotReady;
#endif
    IOReturn ret = super::attachToDataLinkLayer(options, parameter);
    if (ret == kIOReturnSuccess && mSkywalkIf) {
        UInt8 builtIn = 0;
        IOEthernetAddress addr = {};
        if (OSData *biData = OSData::withBytes(&builtIn, sizeof(builtIn))) {
            mSkywalkIf->setProperty("built-in", biData);
            biData->release();
        }
        snprintf(infName, sizeof(infName), "%s%u", ifnet_name(getIfnet()), ifnet_unit(getIfnet()));
        if (OSString *nameStr = OSString::withCString(infName)) {
            mSkywalkIf->setProperty("IOInterfaceName", nameStr);
            nameStr->release();
        }
        if (OSNumber *unitNum = OSNumber::withNumber(ifnet_unit(getIfnet()), 8)) {
            mSkywalkIf->setProperty(kIOInterfaceUnit, unitNum);
            unitNum->release();
        }
        if (OSString *prefixStr = OSString::withCString(ifnet_name(getIfnet()))) {
            mSkywalkIf->setProperty(kIOInterfaceNamePrefix, prefixStr);
            prefixStr->release();
        }
        if (auto *ethCtrl = OSDynamicCast(IOEthernetController, getController())) {
            if (ethCtrl->getHardwareAddress(&addr) == kIOReturnSuccess)
                setProperty(kIOMACAddress, &addr, kIOEthernetAddressSize);
        }
#ifdef AIRPORT_WCL
        ret = gMT7902CommandGate ? gMT7902CommandGate->runAction(prepareBSDGated, this)
                                 : kIOReturnNotReady;
        if (ret != kIOReturnSuccess) {
            super::detachFromDataLinkLayer(options, parameter);
            mAttached = false;
            return ret;
        }
        mAttached = true;
        mSkywalkIf->registerService();
#else
        mSkywalkIf->registerService();
        mSkywalkIf->prepareBSDInterface(getIfnet(), 0);
        mAttached = true;
#endif
    }
    return ret;
}

void AirportMT7902EthernetInterface::detachFromDataLinkLayer(IOOptionBits options, void *parameter) {
    super::detachFromDataLinkLayer(options, parameter);
    mAttached = false;
}

IOService *AirportMT7902EthernetInterface::getProvider() const {
    return mAttached ? static_cast<IOService *>(mSkywalkIf) : super::getProvider();
}

bool AirportMT7902EthernetInterface::setLinkState(IO80211LinkState state) {
    if (!getIfnet())
        return false;
    if (state == kIO80211NetworkLinkUp) {
        ifnet_set_flags(getIfnet(),
                        ifnet_flags(getIfnet()) | (IFF_UP | IFF_RUNNING),
                        (IFF_UP | IFF_RUNNING));
    } else {
        ifnet_set_flags(getIfnet(),
                        ifnet_flags(getIfnet()) & ~(IFF_UP | IFF_RUNNING),
                        0);
    }
    return true;
}

#ifdef AIRPORT_WCL
static mbuf_t normalizeInputPacket(mbuf_t packet, UInt32 length) {
    const size_t packetLength = length ? length : mbuf_pkthdr_len(packet);
    if (!(mbuf_flags(packet) & MBUF_PKTHDR) || packetLength < ETHER_HDR_LEN) {
        mbuf_freem(packet);
        return nullptr;
    }
    if (!mbuf_next(packet) && mbuf_len(packet) >= packetLength)
        return packet;

    unsigned int maxChunks = 1;
    mbuf_t linear = nullptr;
    if (mbuf_allocpacket(MBUF_DONTWAIT, packetLength + ETHER_ALIGN, &maxChunks, &linear)) {
        mbuf_freem(packet);
        return nullptr;
    }
    UInt8 *data = static_cast<UInt8 *>(mbuf_data(linear)) + ETHER_ALIGN;
    if (mbuf_copydata(packet, 0, packetLength, data) ||
        mbuf_copy_pkthdr(linear, packet) ||
        mbuf_setdata(linear, data, packetLength)) {
        mbuf_freem(linear);
        mbuf_freem(packet);
        return nullptr;
    }
    mbuf_pkthdr_setlen(linear, packetLength);
    mbuf_pkthdr_setheader(linear, nullptr);
    mbuf_freem(packet);
    return linear;
}
#endif

UInt32 AirportMT7902EthernetInterface::inputPacket(mbuf_t packet, UInt32 length,
                                                   IOOptionBits options, void *param) {
#ifdef AIRPORT_WCL
    if (!packet)
        return 0;
    if (!getIfnet()) {
        mbuf_freem(packet);
        return 0;
    }
    const bool directInput = (param == packet);
    packet = normalizeInputPacket(packet, length);
    if (!packet) {
        IONetworkData *data = getParameter(kIONetworkStatsKey);
        IONetworkStats *stats = data ? static_cast<IONetworkStats *>(const_cast<void *>(data->getBuffer())) : nullptr;
        if (stats)
            stats->inputErrors++;
        return 0;
    }
    if (directInput)
        param = packet;
#else
    if (!packet || !mAttached) {
        if (packet)
            mbuf_freem(packet);
        return 0;
    }
#endif
    return super::inputPacket(packet, length, options, param);
}

#endif // KERNEL && IO80211FAMILY_V2
