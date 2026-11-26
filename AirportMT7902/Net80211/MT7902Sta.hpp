// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// 802.11 STA state machine bridging MT7902 HAL to macOS native IO80211 / WCL

#ifndef MT7902_STA_HPP
#define MT7902_STA_HPP

#include "../../include/MT7902Common.hpp"
#include "../HAL/MT7902Device.hpp"
#include "MT7902BeaconParser.hpp"
#include "MT7902Channels.hpp"

namespace mt7902 {

enum StaState : uint32_t {
    kStaInit = 0,
    kStaScanning = 1,
    kStaAuthenticating = 2,
    kStaAssociating = 3,
    kStaAssociated = 4, // Associated; awaiting 802.1X/WPA 4-way handshake if secure
    kStaRun = 5,        // Fully authorized data path active
};

struct AssocRequestParams {
    uint8_t ssid[32] = {};
    uint8_t ssidLen = 0;
    uint8_t bssid[6] = {};
    bool hasBssid = false;
    uint32_t authLower = 1; // OPEN
    uint32_t authUpper = 0; // NONE / WPA2_PSK / WPA3_SAE / ...
    uint8_t rsnIe[257] = {};
    uint16_t rsnIeLen = 0;
    uint8_t channelHint = 0;
};

struct StaLinkInfo {
    StaState state = kStaInit;
    uint8_t ssid[32] = {};
    uint8_t ssidLen = 0;
    uint8_t bssid[6] = {};
    uint8_t channel = 1;
    WifiBand band = kBand2GHz;
    uint8_t bw = CMD_CBW_20MHZ;
    uint16_t aid = 0;
    uint16_t apWcid = 1;
    uint16_t beaconInterval = 100;
    uint8_t dtimPeriod = 1;
    uint16_t capability = 0;
    bool qos = true;
    bool encrypted = false;
    bool ptkInstalled = false;
    bool gtkInstalled = false;
    int32_t rssi = -65;
    int32_t noise = -95;
    uint32_t txRateMbps = 54;
    uint8_t phyMode = MT_PHY_TYPE_HE_SU;
    uint8_t mcs = 7;
    uint8_t nss = 1;
    uint16_t lastAssocStatus = 0;
    uint16_t lastDeauthReason = 0;
    uint32_t authLower = 1;
    uint32_t authUpper = 0;
    uint8_t rsnIe[257] = {};
    uint16_t rsnIeLen = 0;
    uint8_t apIes[kMaxRawIeLen] = {};
    uint16_t apIeLen = 0;
};

class IStaEventListener {
public:
    virtual ~IStaEventListener() = default;
    virtual void onDataFrameReceived(const uint8_t *ethFrame, size_t len) = 0;
    virtual void onScanFinished() = 0;
    virtual void onBssDiscovered(const ScannedBss &bss) = 0;
    virtual void onAssociationCompleted(bool success, uint16_t statusCode) = 0;
    virtual void onLinkStateChanged(bool up) = 0;
    virtual void onDeauthenticated(uint16_t reasonCode, bool isDisassoc) = 0;
    virtual void onAssociatedBeacon(const ScannedBss &bss) = 0;
};

class MT7902Sta : public IMT7902RxCallback {
public:
    explicit MT7902Sta(MT7902Device &dev);

    void setListener(IStaEventListener *listener) { mListener = listener; }

    // Radio power control
    int setPower(bool on);
    bool isPoweredOn() const { return mPowerOn; }

    // Country code / regulatory
    int setCountryCode(const char cc[2]);
    const char *countryCode() const { return mCountryCode; }

    // Scan control
    int startScan(const ScanRequestParams &params);
    int abortScan();
    bool isScanning() const { return mScanning; }

    // Association & disconnection
    int associate(const AssocRequestParams &req);
    int disassociate(uint16_t reasonCode = 8 /* DEAUTH_LEAVING */);

    // Key installation from macOS Apple80211 RSN supplicant (PTK/GTK/WEP)
    int installCipherKey(uint8_t cipher, uint8_t keyIdx, const uint8_t *key,
                         size_t keyLen, bool pairwise, bool setKey);

    // Outbound data path (Ethernet 802.3 frames from macOS network stack, including EAPOL 0x888E)
    int outputEthernetPacket(const uint8_t *ethFrame, size_t len);

    // Raw 802.11 management frame builders (static, directly unit-testable)
    static size_t buildAuthReqFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                    uint16_t seqNum, uint16_t authAlg, uint16_t authSeq,
                                    uint8_t *out, size_t outCap);
    static size_t buildAssocReqFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                     uint16_t seqNum, const uint8_t *ssid, uint8_t ssidLen,
                                     const uint8_t *rsnIe, uint16_t rsnIeLen,
                                     bool ht, bool vht, bool he,
                                     uint8_t *out, size_t outCap);
    static size_t buildDeauthFrame(const uint8_t staMac[6], const uint8_t bssid[6],
                                   uint16_t seqNum, uint16_t reasonCode,
                                   uint8_t *out, size_t outCap);

    // IMT7902RxCallback overrides
    void onRxEthernetFrame(const uint8_t *ethFrame, size_t len, const ParsedRxFrame &meta) override;
    void onRx80211Frame(const uint8_t *dot11Frame, size_t len, const ParsedRxFrame &meta) override;
    void onScanComplete(uint8_t seqNum) override;
    void onBeaconLoss(uint8_t bssIdx, uint8_t reason) override;
    void onFirmwareResetNeeded() override;

    MT7902ScanCache &scanCache() { return mScanCache; }
    const MT7902ScanCache &scanCache() const { return mScanCache; }
    const StaLinkInfo &link() const { return mLink; }
    MT7902Device &device() { return mDev; }

private:
    void handleAuthResp(const uint8_t *frame, size_t len);
    void handleAssocResp(const uint8_t *frame, size_t len);
    void handleDeauthOrDisassoc(const uint8_t *frame, size_t len, bool isDisassoc);
    void handleData80211Frame(const uint8_t *frame, size_t len);
    uint16_t nextMgmtSeq();

    MT7902Device &mDev;
    IStaEventListener *mListener = nullptr;
    MT7902ScanCache mScanCache;
    StaLinkInfo mLink;
    char mCountryCode[3] = "US";
    bool mPowerOn = false;
    bool mScanning = false;
    uint16_t mMgmtSeq = 0;
};

} // namespace mt7902

#endif // MT7902_STA_HPP
