// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
//
// Comprehensive Host Hardware-Simulation & Protocol Verification Suite.
// Runs natively on Linux / Podman container without requiring physical macOS hardware,
// exercising the REAL embedded MT7902 Wi-Fi & Bluetooth firmware binaries through
// simulated PCIe BAR0/L1 MMIO, WFDMA0 descriptor rings, Connac2 MCU, 802.11 STA,
// macOS Sequoia/Tahoe WCL wire codecs, and USB Bluetooth WMT control transfers.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include <map>
#include <queue>
#include <vector>

#include "../include/MT7902Common.hpp"
#include "../AirportMT7902/PrivateSPI.pch"
#include "../AirportMT7902/Firmware/MT7902FwStore.hpp"
#include "../AirportMT7902/HAL/MT7902Regs.hpp"
#include "../AirportMT7902/HAL/MT7902DmaTypes.hpp"
#include "../AirportMT7902/HAL/MT7902McuTypes.hpp"
#include "../AirportMT7902/HAL/MT7902Bus.hpp"
#include "../AirportMT7902/HAL/MT7902Dma.hpp"
#include "../AirportMT7902/HAL/MT7902Mcu.hpp"
#include "../AirportMT7902/HAL/MT7902Firmware.hpp"
#include "../AirportMT7902/HAL/MT7902Mac.hpp"
#include "../AirportMT7902/HAL/MT7902Device.hpp"
#include "../AirportMT7902/Net80211/MT7902Channels.hpp"
#include "../AirportMT7902/Net80211/MT7902BeaconParser.hpp"
#include "../AirportMT7902/Net80211/MT7902Sta.hpp"
#include "../AirportMT7902/Native/AirportMT7902WCL.hpp"
#include "../MT7902BluetoothFirmware/MT7902BtProtocol.hpp"
#include "../MT7902BluetoothPatcher/MT7902BtPatcherLogic.hpp"

using namespace mt7902;

#define TEST_CHECK(cond, msg, ...)                                                  \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "[FAIL] %s:%d: " msg "\n", __FILE__, __LINE__,          \
                    ##__VA_ARGS__);                                                 \
            exit(1);                                                                \
        }                                                                           \
    } while (0)

// ============================================================================
// Programmable Mock MT7902 PCIe ASIC (BAR0 + L1 Remap + WFDMA0 + Connac2 MCU)
// ============================================================================
class MockMT7902AsicBus : public IMT7902Bus {
public:
    MockMT7902AsicBus() {
        mBar0.resize(0x100000, 0);
        // L1 physical registers
        mL1Regs[MT_HW_CHIPID] = 0x00007902U;
        mL1Regs[MT_HW_REV] = 0x00000010U;
        mL1Regs[MT_WFSYS_SW_RST_B] = WFSYS_SW_RST_B | WFSYS_SW_INIT_DONE;
        mL1Regs[MT_TOP_LPCR_HOST_BAND0] = MT_TOP_LPCR_HOST_FW_OWN;

        // Fixed BAR0 mapped registers: MT_CONN_ON_MISC (0x7c0600f0 -> 0xe00f0)
        writeBarDirect(0xe00f0, MT_TOP_MISC2_FW_PWR_ON);
    }

    ~MockMT7902AsicBus() override {
        for (auto &kv : mDmaMap) {
            free(kv.second.virt);
        }
    }

    uint32_t barRead32(uint32_t barOffset) override {
        TEST_CHECK(barOffset + 4 <= mBar0.size(), "BAR0 read out of range: 0x%x", barOffset);
        if (barOffset >= MT_HIF_REMAP_BASE_L1 && barOffset < MT_HIF_REMAP_BASE_L1 + 0x10000U) {
            uint32_t phys = mCurrentL1Base | (barOffset - MT_HIF_REMAP_BASE_L1);
            auto it = mL1Regs.find(phys);
            return (it != mL1Regs.end()) ? it->second : 0U;
        }
        return readBarDirect(barOffset);
    }

    void barWrite32(uint32_t barOffset, uint32_t val) override {
        TEST_CHECK(barOffset + 4 <= mBar0.size(), "BAR0 write out of range: 0x%x", barOffset);

        if (barOffset == MT_HIF_REMAP_L1) {
            mCurrentL1Base = (val & MT_HIF_REMAP_L1_MASK) << 16;
            writeBarDirect(barOffset, val);
            return;
        }

        if (barOffset >= MT_HIF_REMAP_BASE_L1 && barOffset < MT_HIF_REMAP_BASE_L1 + 0x10000U) {
            uint32_t phys = mCurrentL1Base | (barOffset - MT_HIF_REMAP_BASE_L1);
            mL1Regs[phys] = val;
            if (phys == MT_WFSYS_SW_RST_B && (val & WFSYS_SW_RST_B)) {
                mL1Regs[phys] |= WFSYS_SW_INIT_DONE;
                wfsysResetCount++;
            } else if (phys == MT_TOP_LPCR_HOST_BAND0 && (val & MT_TOP_LPCR_HOST_DRV_OWN)) {
                mL1Regs[phys] = 0; // Clear FW_OWN bit to grant driver ownership
            }
            return;
        }

        // MT_CONN_ON_LPCTL (0x7c060010 -> BAR0 0xe0010)
        if (barOffset == 0xe0010U) {
            if (val & PCIE_LPCR_HOST_SET_OWN)
                writeBarDirect(barOffset, PCIE_LPCR_HOST_OWN_SYNC);
            else if (val & PCIE_LPCR_HOST_CLR_OWN)
                writeBarDirect(barOffset, 0);
            return;
        }

        // MT_WTBL_UPDATE (0x820d4230 -> BAR0 0x34230): clear busy bit immediately
        if (barOffset == 0x34230U) {
            writeBarDirect(barOffset, val & ~MT_WTBL_UPDATE_BUSY);
            wtblUpdates++;
            return;
        }

        // MT_WFDMA0_HOST_INT_STA (write-1-to-clear)
        if (barOffset == MT_WFDMA0_HOST_INT_STA) {
            uint32_t cur = readBarDirect(barOffset);
            writeBarDirect(barOffset, cur & ~val);
            return;
        }

        // WFDMA0 GLO_CFG: never leave DMA_BUSY stuck
        if (barOffset == MT_WFDMA0_GLO_CFG) {
            val &= ~(MT_WFDMA0_GLO_CFG_TX_DMA_BUSY | MT_WFDMA0_GLO_CFG_RX_DMA_BUSY);
            writeBarDirect(barOffset, val);
            return;
        }

        writeBarDirect(barOffset, val);

        // Detect TX Ring CPU_IDX doorbell writes
        const uint32_t tx0Cpu = MT_TX_RING_BASE + MT7902_TXQ_BAND0 * MT_RING_STRIDE + MT_RING_REG_CPU_IDX;
        const uint32_t tx15Cpu = MT_TX_RING_BASE + MT7902_TXQ_MCU_WM * MT_RING_STRIDE + MT_RING_REG_CPU_IDX;
        const uint32_t tx16Cpu = MT_TX_RING_BASE + MT7902_TXQ_FWDL * MT_RING_STRIDE + MT_RING_REG_CPU_IDX;

        if (barOffset == tx0Cpu) {
            tx0DoorbellWrites++;
            processTxRing(MT_TX_RING_BASE + MT7902_TXQ_BAND0 * MT_RING_STRIDE, MT7902_TXQ_BAND0);
        } else if (barOffset == tx15Cpu) {
            processTxRing(MT_TX_RING_BASE + MT7902_TXQ_MCU_WM * MT_RING_STRIDE, MT7902_TXQ_MCU_WM);
        } else if (barOffset == tx16Cpu) {
            processTxRing(MT_TX_RING_BASE + MT7902_TXQ_FWDL * MT_RING_STRIDE, MT7902_TXQ_FWDL);
        } else if (barOffset == MT_RX_DATA_RING_BASE + MT_RING_REG_CPU_IDX) {
            rx2RefillWrites++;
        }
    }

    bool allocDma(size_t size, size_t align, DmaBuffer &out) override {
        (void)align;
        void *mem = calloc(1, size);
        if (!mem)
            return false;
        uint64_t phys = mNextDmaPhys;
        mNextDmaPhys += ((size + 4095ULL) & ~4095ULL);
        TEST_CHECK(phys + size <= 0xffffffffULL, "32-bit DMA mask violated");
        out.virt = mem;
        out.phys = phys;
        out.size = size;
        out.osHandle = nullptr;
        mDmaMap[phys] = out;
        return true;
    }

    void freeDma(DmaBuffer &buf) override {
        auto it = mDmaMap.find(buf.phys);
        if (it != mDmaMap.end()) {
            free(it->second.virt);
            mDmaMap.erase(it);
        }
        buf = {};
    }

    void syncDmaForDevice(const DmaBuffer &) override {}
    void syncDmaForCpu(const DmaBuffer &) override {}
    void delayUs(uint32_t us) override { mNowUs += us; }
    void sleepMs(uint32_t ms) override { mNowUs += static_cast<uint64_t>(ms) * 1000ULL; }
    uint64_t nowMs() override { return mNowUs / 1000ULL; }

    uint16_t pciVendorId() const override { return kMT7902VendorID; }
    uint16_t pciDeviceId() const override { return kMT7902DeviceID; }
    bool isAspmSupported() const override { return true; }

    // Inject raw packet into WFDMA0 RX Ring 0 (MCU event / TX-free) or RX Ring 2 (Data/802.11)
    void injectRxPacket(uint32_t rxRingRegBase, uint32_t irqBit, const void *pkt, size_t len) {
        uint32_t descBase = readBarDirect(rxRingRegBase + MT_RING_REG_DESC_BASE);
        uint32_t ringSize = readBarDirect(rxRingRegBase + MT_RING_REG_RING_SIZE);
        uint32_t dmaIdx = readBarDirect(rxRingRegBase + MT_RING_REG_DMA_IDX);
        TEST_CHECK(descBase != 0 && ringSize > 0, "RX ring 0x%x not initialized", rxRingRegBase);

        auto *descs = static_cast<Mt76Desc *>(lookupDma(descBase, ringSize * sizeof(Mt76Desc)));
        Mt76Desc &d = descs[dmaIdx];
        TEST_CHECK((d.ctrl & MT_DMA_CTL_DMA_DONE) == 0, "RX ring 0x%x descriptor %u overflow", rxRingRegBase, dmaIdx);

        void *rxBuf = lookupDma(d.buf0, len);
        memcpy(rxBuf, pkt, len);
        d.ctrl = fieldPrep(MT_DMA_CTL_SD_LEN0, static_cast<uint32_t>(len)) |
                 MT_DMA_CTL_LAST_SEC0 | MT_DMA_CTL_DMA_DONE;

        writeBarDirect(rxRingRegBase + MT_RING_REG_DMA_IDX, (dmaIdx + 1U) % ringSize);
        uint32_t intSta = readBarDirect(MT_WFDMA0_HOST_INT_STA);
        writeBarDirect(MT_WFDMA0_HOST_INT_STA, intSta | irqBit);
    }

    // Counters & captured frames for assertions
    uint32_t wfsysResetCount = 0;
    uint32_t wtblUpdates = 0;
    size_t fwdlTotalBytes = 0;
    uint32_t fwdlChunks = 0;
    std::vector<uint32_t> mcuCmdHistory;
    std::vector<std::vector<uint8_t>> capturedBand0TxPayloads;
    std::vector<Mt7902Txwi> capturedBand0Txwis;
    uint8_t lastHwScanSeq = 0;
    uint16_t lastHwScanDwell = 0;
    uint16_t lastHwScanTimeout = 0;
    uint8_t lastCancelScanSeq = 0;
    uint8_t lastChanSwitchReason = 0;
    uint8_t lastChanSwitchPrimary = 0;
    uint8_t lastChanSwitchCenter = 0;
    uint8_t lastChanSwitchBw = 0;
    uint32_t chanSwitchCount = 0;
    bool sawStaRecHe = false;
    bool sawStaRecHeV2 = false;
    bool sawStaRecVht = false;
    bool sawStaRecBa = false;
    bool sawStaRecBaTx = false;
    bool sawWtblSmps = false;
    uint32_t tx0DoorbellWrites = 0;
    uint32_t rx2RefillWrites = 0;

private:
    uint32_t readBarDirect(uint32_t off) const {
        return getLe32(&mBar0[off]);
    }

    void writeBarDirect(uint32_t off, uint32_t val) {
        putLe32(&mBar0[off], val);
    }

    void *lookupDma(uint64_t phys, size_t needed) {
        auto it = mDmaMap.upper_bound(phys);
        if (it != mDmaMap.begin()) {
            --it;
            if (phys >= it->first && phys + needed <= it->first + it->second.size) {
                return static_cast<uint8_t *>(it->second.virt) + (phys - it->first);
            }
        }
        TEST_CHECK(false, "DMA address 0x%llx (%zu bytes) not mapped",
                   static_cast<unsigned long long>(phys), needed);
        return nullptr;
    }

    void processTxRing(uint32_t ringBase, uint8_t hwIdx) {
        uint32_t descBase = readBarDirect(ringBase + MT_RING_REG_DESC_BASE);
        uint32_t ringSize = readBarDirect(ringBase + MT_RING_REG_RING_SIZE);
        uint32_t cpuIdx = readBarDirect(ringBase + MT_RING_REG_CPU_IDX);
        uint32_t dmaIdx = readBarDirect(ringBase + MT_RING_REG_DMA_IDX);
        if (!descBase || !ringSize)
            return;

        auto *descs = static_cast<Mt76Desc *>(lookupDma(descBase, ringSize * sizeof(Mt76Desc)));
        while (dmaIdx != cpuIdx) {
            Mt76Desc &d = descs[dmaIdx];
            uint32_t len0 = fieldGet(MT_DMA_CTL_SD_LEN0, d.ctrl);
            const auto *buf0 = static_cast<const uint8_t *>(lookupDma(d.buf0, len0));

            if (hwIdx == MT7902_TXQ_FWDL) {
                fwdlTotalBytes += len0;
                fwdlChunks++;
                writeBarDirect(MT_WFDMA0_HOST_INT_STA,
                               readBarDirect(MT_WFDMA0_HOST_INT_STA) | MT_INT_TX_DONE_FWDL);
            } else if (hwIdx == MT7902_TXQ_MCU_WM) {
                handleMcuCommand(buf0, len0);
                writeBarDirect(MT_WFDMA0_HOST_INT_STA,
                               readBarDirect(MT_WFDMA0_HOST_INT_STA) | MT_INT_TX_DONE_MCU_WM);
            } else if (hwIdx == MT7902_TXQ_BAND0) {
                TEST_CHECK(len0 == sizeof(Mt7902Txwi), "Expected 64B TXWI on Band0 ring");
                Mt7902Txwi txwi;
                memcpy(&txwi, buf0, sizeof(txwi));
                capturedBand0Txwis.push_back(txwi);

                uint32_t payPhys = getLe32(&txwi.txp.ptr[0].buf0);
                uint16_t payLen = getLe16(&txwi.txp.ptr[0].len0) & MT_TXD_LEN_MASK;
                const auto *payPtr = static_cast<const uint8_t *>(lookupDma(payPhys, payLen));
                capturedBand0TxPayloads.emplace_back(payPtr, payPtr + payLen);

                writeBarDirect(MT_WFDMA0_HOST_INT_STA,
                               readBarDirect(MT_WFDMA0_HOST_INT_STA) | MT_INT_TX_DONE_BAND0);
            }

            d.ctrl |= MT_DMA_CTL_DMA_DONE;
            dmaIdx = (dmaIdx + 1U) % ringSize;
        }
        writeBarDirect(ringBase + MT_RING_REG_DMA_IDX, dmaIdx);
    }

    void pushMcuResponse(uint8_t seq, uint8_t eid, const void *body, size_t bodyLen,
                         bool statusInRxdTail = false, uint8_t tailStatus = 0) {
        std::vector<uint8_t> pkt(sizeof(Mt76Connac2McuRxd) + bodyLen, 0);
        auto *rxd = reinterpret_cast<Mt76Connac2McuRxd *>(pkt.data());
        uint32_t rxd0 = fieldPrep(MT_RXD0_LENGTH, static_cast<uint32_t>(pkt.size())) |
                        fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_RX_EVENT);
        putLe32(&rxd->rxd[0], rxd0);
        putLe16(&rxd->len, static_cast<uint16_t>(pkt.size() - 24U));
        rxd->eid = eid;
        rxd->seq = seq;
        rxd->option = 0;
        if (statusInRxdTail) {
            pkt[sizeof(Mt76Connac2McuRxd) - 4] = tailStatus;
        }
        if (body && bodyLen > 0) {
            memcpy(pkt.data() + sizeof(Mt76Connac2McuRxd), body, bodyLen);
        }
        injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, pkt.data(), pkt.size());
    }

    void handleMcuCommand(const uint8_t *buf, size_t len) {
        TEST_CHECK(len >= sizeof(Mt76Connac2McuUniTxd), "MCU TX packet too short (%zu)", len);
        // Check pkt_type byte at offset 37: both Mt76Connac2McuTxd and UniTxd place pkt_type=0xa0 at offset 37
        const auto *uni = reinterpret_cast<const Mt76Connac2McuUniTxd *>(buf);
        const auto *stdTxd = reinterpret_cast<const Mt76Connac2McuTxd *>(buf);

        bool isUni = (uni->option == MCU_CMD_UNI_EXT_ACK && len >= sizeof(Mt76Connac2McuUniTxd) &&
                      stdTxd->pq_id != mcuPqId(MT_TX_PORT_IDX_MCU, MT_TX_MCU_PORT_RX_Q0));

        if (isUni) {
            uint8_t cid = static_cast<uint8_t>(getLe16(&uni->cid) & 0xffU);
            uint8_t seq = uni->seq;
            mcuCmdHistory.push_back(MCU_CMD_FIELD_UNI | cid);

            if (cid == 0x27) { // MCU_UNI_CMD_ROC (fire-and-forget; ACQUIRE yields unsolicited ROC grant event)
                const uint8_t *rocPayload = buf + sizeof(Mt76Connac2McuUniTxd);
                size_t rocLen = len - sizeof(Mt76Connac2McuUniTxd);
                if (rocLen >= 6 && getLe16(rocPayload + 4) == UNI_ROC_ACQUIRE) {
                    std::vector<uint8_t> evt(sizeof(Mt76Connac2McuRxd) + 16, 0);
                    auto *erxd = reinterpret_cast<Mt76Connac2McuRxd *>(evt.data());
                    putLe32(&erxd->rxd[0], fieldPrep(MT_RXD0_LENGTH, static_cast<uint32_t>(evt.size())) |
                                           fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_RX_EVENT));
                    erxd->eid = MCU_UNI_EVENT_ROC;
                    erxd->option = MCU_UNI_CMD_UNSOLICITED_EVENT;
                    injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, evt.data(), evt.size());
                }
                return;
            }

            if (cid == 0x03) { // Verify STA_REC_UPDATE carries valid STA_REC_WTBL, STA_REC_KEY_V2, or STA_REC_BA
                const uint8_t *sp = buf + sizeof(Mt76Connac2McuUniTxd);
                size_t slen = len - sizeof(Mt76Connac2McuUniTxd);
                TEST_CHECK(slen >= sizeof(StaReqHdr) + 4, "STA_REC_UPDATE payload too short");
                const auto *sh = reinterpret_cast<const StaReqHdr *>(sp);
                uint16_t ntlvs = getLe16(&sh->tlv_num);
                TEST_CHECK(ntlvs >= 1, "STA_REC_UPDATE must have >=1 TLV");
                size_t off = sizeof(StaReqHdr);
                bool sawValidCoreTlv = false;
                for (uint16_t t = 0; t < ntlvs && off + 4 <= slen; ++t) {
                    uint16_t ttag = getLe16(sp + off);
                    uint16_t tlen = getLe16(sp + off + 2);
                    TEST_CHECK(tlen >= 4 && off + tlen <= slen, "Malformed STA_REC TLV %u (len=%u)", ttag, tlen);
                    if (ttag == STA_REC_WTBL || ttag == STA_REC_KEY_V2 || ttag == STA_REC_BA)
                        sawValidCoreTlv = true;
                    if (ttag == STA_REC_WTBL && tlen >= sizeof(McuTlvHdr) + sizeof(WtblReqHdr)) {
                        const uint8_t *wp = sp + off + sizeof(McuTlvHdr);
                        const auto *wh = reinterpret_cast<const WtblReqHdr *>(wp);
                        uint16_t wn = getLe16(&wh->tlv_num);
                        size_t woff = sizeof(McuTlvHdr) + sizeof(WtblReqHdr);
                        for (uint16_t wt = 0; wt < wn && woff + 4 <= tlen; ++wt) {
                            uint16_t wtag = getLe16(sp + off + woff);
                            uint16_t wlen = getLe16(sp + off + woff + 2);
                            if (wtag == WTBL_SMPS) sawWtblSmps = true;
                            if (wlen < 4) break;
                            woff += wlen;
                        }
                    }
                    if (ttag == STA_REC_HE) sawStaRecHe = true;
                    if (ttag == STA_REC_HE_V2) sawStaRecHeV2 = true;
                    if (ttag == STA_REC_VHT) sawStaRecVht = true;
                    if (ttag == STA_REC_BA) {
                        sawStaRecBa = true;
                        if (tlen >= sizeof(StaRecBaTlv)) {
                            const auto *ba = reinterpret_cast<const StaRecBaTlv *>(sp + off);
                            if (ba->ba_type == MT_BA_TYPE_ORIGINATOR && ba->ba_en != 0)
                                sawStaRecBaTx = true;
                        }
                    }
                    off += tlen;
                }
                TEST_CHECK(sawValidCoreTlv, "STA_REC_UPDATE missing STA_REC_WTBL/KEY_V2/BA");
            }

            Mt76ConnacMcuUniEvent ev = {};
            ev.cid = cid;
            putLe32(&ev.status, 0);
            pushMcuResponse(seq, MCU_EVENT_GENERIC, &ev, sizeof(ev));
            return;
        }

        TEST_CHECK(len >= sizeof(Mt76Connac2McuTxd), "Standard MCU TXD too short");
        uint8_t cid = stdTxd->cid;
        uint8_t extCid = stdTxd->ext_cid;
        uint8_t seq = stdTxd->seq;
        const uint8_t *payload = buf + sizeof(Mt76Connac2McuTxd);
        size_t payloadLen = len - sizeof(Mt76Connac2McuTxd);

        mcuCmdHistory.push_back(cid | (static_cast<uint32_t>(extCid) << 8));

        if (cid == 0x04) { // MCU_CMD_NIC_POWER_CTRL (no response waited)
            return;
        }
        if (cid == 0x10) { // MCU_CMD_PATCH_SEM_CONTROL
            uint32_t op = (payloadLen >= 4) ? getLe32(payload) : 0;
            uint8_t st = (op == PATCH_SEM_GET) ? PATCH_NOT_DL_SEM_SUCCESS : PATCH_REL_SEM_SUCCESS;
            pushMcuResponse(seq, MCU_EVENT_GENERIC, nullptr, 0, true, st);
            return;
        }
        if (cid == 0x07) { // MCU_CMD_PATCH_FINISH_REQ
            pushMcuResponse(seq, MCU_EVENT_GENERIC, nullptr, 0, true, 0);
            return;
        }
        if (cid == 0x02) { // MCU_CMD_FW_START_REQ -> mark N9 firmware ready!
            writeBarDirect(0xe00f0, MT_TOP_MISC2_FW_N9_RDY);
            pushMcuResponse(seq, MCU_EVENT_GENERIC, nullptr, 0);
            return;
        }
        if (cid == 0x8a) { // MCU_CE_CMD_GET_NIC_CAPAB
            uint8_t capBuf[64] = {};
            putLe16(capBuf + 0, 3); // 3 TLVs: MAC_ADDR, PHY, 6G
            size_t p = 4;
            // TLV 1: MAC_ADDR
            putLe32(capBuf + p, MT_NIC_CAP_MAC_ADDR);
            putLe32(capBuf + p + 4, 6);
            const uint8_t kMac[6] = { 0xa0, 0xb1, 0xc2, 0x79, 0x02, 0xef };
            memcpy(capBuf + p + 8, kMac, 6);
            p += 14;
            // TLV 2: PHY (12 bytes)
            putLe32(capBuf + p, MT_NIC_CAP_PHY);
            putLe32(capBuf + p + 4, 12);
            capBuf[p + 8 + 3] = CMD_CBW_80MHZ;
            capBuf[p + 8 + 4] = 1; // 1x1 NSS on MT7902
            capBuf[p + 8 + 10] = 0x03; // 2.4G + 5G
            capBuf[p + 8 + 11] = 1;    // 802.11ax HE
            p += 20;
            // TLV 3: 6GHz (Wi-Fi 6E)
            putLe32(capBuf + p, MT_NIC_CAP_6G);
            putLe32(capBuf + p + 4, 1);
            capBuf[p + 8] = 1;
            p += 9;
            pushMcuResponse(seq, MCU_EVENT_GENERIC, capBuf, p);
            return;
        }
        if (cid == 0x03 && stdTxd->set_query == MCU_Q_SET) { // MCU_CE_CMD_START_HW_SCAN (fire-and-forget)
            if (payloadLen >= sizeof(Mt7902HwScanReq)) {
                const auto *hs = reinterpret_cast<const Mt7902HwScanReq *>(payload);
                lastHwScanSeq = hs->seq_num;
                lastHwScanDwell = getLe16(&hs->channel_dwell_time);
                lastHwScanTimeout = getLe16(&hs->timeout_value);
            } else if (payloadLen >= 1) {
                lastHwScanSeq = payload[0];
            }
            return;
        }
        if (cid == 0x05 && stdTxd->set_query == MCU_Q_SET) { // MCU_CE_CMD_SET_PS_PROFILE (fire-and-forget)
            return;
        }
        if (cid == 0x1b && stdTxd->set_query == MCU_Q_SET) { // MCU_CE_CMD_CANCEL_HW_SCAN
            if (payloadLen >= 1)
                lastCancelScanSeq = payload[0];
            return;
        }
        if (cid == 0x0f || cid == 0x16 || cid == 0x17 || cid == 0x1d) {
            // Fire-and-forget CE commands (SET_CHAN_DOMAIN, SET_BSS_CONNECTED, SET_BSS_ABORT, SET_EDCA_PARMS)
            return;
        }

        if (cid == 0xed && extCid == 0x08 && payloadLen >= sizeof(Mt7902ChanSwitchReq)) {
            const auto *cs = reinterpret_cast<const Mt7902ChanSwitchReq *>(payload);
            lastChanSwitchPrimary = cs->control_ch;
            lastChanSwitchCenter = cs->center_ch;
            lastChanSwitchBw = cs->bw;
            lastChanSwitchReason = cs->switch_reason;
            chanSwitchCount++;
        }

        // Default synchronous ACK (PATCH_START_REQ, TARGET_ADDRESS_LEN_REQ, EXT_CMDs, SET_RX_FILTER)
        pushMcuResponse(seq, MCU_EVENT_GENERIC, nullptr, 0);
    }

    std::vector<uint8_t> mBar0;
    std::map<uint32_t, uint32_t> mL1Regs;
    std::map<uint64_t, DmaBuffer> mDmaMap;
    uint32_t mCurrentL1Base = 0;
    uint64_t mNextDmaPhys = 0x10000000ULL;
    uint64_t mNowUs = 1000000ULL;
};

// ============================================================================
// Mock USB Bluetooth Control Transport for MT7902 (13d3:3579)
// ============================================================================
class MockBtUsbTransport : public bt::IBtUsbTransport {
public:
    int vendorReadReg32(uint32_t reg, uint32_t &outVal) override {
        vendorReads.push_back(reg);
        if (reg == bt::MTK_BT_CHIPID_REG) {
            outVal = 0x00007902U;
            return kOk;
        }
        if (reg == bt::MTK_BT_FW_VER_REG) {
            outVal = 0x8a100001U;
            return kOk;
        }
        outVal = 0;
        return kOk;
    }

    int uhwWriteReg32(uint32_t reg, uint32_t val) override {
        uhwWrites.emplace_back(reg, val);
        return kOk;
    }

    int sendHciCmdControl(const uint8_t *hciCmdPkt, size_t len) override {
        TEST_CHECK(len >= 3 + sizeof(bt::BtmtkWmtHdr), "HCI WMT cmd too short");
        TEST_CHECK(getLe16(hciCmdPkt) == bt::HCI_OP_MTK_WMT, "Unexpected HCI opcode");
        const auto *whdr = reinterpret_cast<const bt::BtmtkWmtHdr *>(hciCmdPkt + 3);
        uint16_t payloadLen = static_cast<uint16_t>(getLe16(&whdr->dlen) - 1U);

        std::vector<uint8_t> evt(2 + sizeof(bt::BtmtkWmtHdr), 0);
        evt[0] = bt::HCI_EV_WMT;
        evt[1] = sizeof(bt::BtmtkWmtHdr);
        auto *ew = reinterpret_cast<bt::BtmtkWmtHdr *>(evt.data() + 2);
        ew->dir = 2;
        ew->op = whdr->op;
        putLe16(&ew->dlen, 1);

        if (whdr->op == bt::BTMTK_WMT_PATCH_DWNLD) {
            if (whdr->flag == 0) {
                TEST_CHECK(payloadLen == 53, "Expected 53B section descriptor");
                sectionDescriptorsSent++;
                ew->flag = 0; // BTMTK_WMT_PATCH_UNDONE
            } else {
                TEST_CHECK(payloadLen <= 250, "WMT chunk exceeds 250B limit: %u", payloadLen);
                downloadedPayloadBytes += payloadLen;
                wmtChunksSent++;
                ew->flag = (whdr->flag == bt::BTMTK_WMT_PKT_END) ? 2 : 0;
            }
        } else if (whdr->op == bt::BTMTK_WMT_FUNC_CTRL) {
            funcCtrlCalled = true;
            evt.resize(2 + sizeof(bt::BtmtkWmtHdr) + 2, 0);
            evt[1] = sizeof(bt::BtmtkWmtHdr) + 2;
            putBe16(evt.data() + 2 + sizeof(bt::BtmtkWmtHdr), 0x0404); // BTMTK_WMT_ON_DONE
        }
        mEvents.push(evt);
        return kOk;
    }

    int pollWmtEventControl(uint8_t *outBuf, size_t maxLen, size_t &actualLen) override {
        if (mEvents.empty()) {
            actualLen = 0;
            return kOk;
        }
        const auto &front = mEvents.front();
        TEST_CHECK(front.size() <= maxLen, "WMT event buffer too small");
        memcpy(outBuf, front.data(), front.size());
        actualLen = front.size();
        mEvents.pop();
        return kOk;
    }

    void delayUs(uint32_t) override {}
    void sleepMs(uint32_t) override {}

    std::vector<uint32_t> vendorReads;
    std::vector<std::pair<uint32_t, uint32_t>> uhwWrites;
    uint32_t sectionDescriptorsSent = 0;
    uint32_t wmtChunksSent = 0;
    size_t downloadedPayloadBytes = 0;
    bool funcCtrlCalled = false;

private:
    std::queue<std::vector<uint8_t>> mEvents;
};

// ============================================================================
// Mock STA Event Listener
// ============================================================================
class MockStaListener : public IStaEventListener {
public:
    void onDataFrameReceived(const uint8_t *ethFrame, size_t len) override {
        rxFrames.emplace_back(ethFrame, ethFrame + len);
    }
    void onScanFinished() override { scanDoneCount++; }
    void onBssDiscovered(const ScannedBss &bss) override { discovered.push_back(bss); }
    void onAssociationCompleted(bool success, uint16_t statusCode) override {
        assocDone = true;
        assocOk = success;
        assocStatus = statusCode;
    }
    void onLinkStateChanged(bool up) override {
        linkUp = up;
        linkTransitions++;
    }
    void onDeauthenticated(uint16_t reasonCode, bool) override {
        lastDeauth = reasonCode;
    }
    void onAssociatedBeacon(const ScannedBss &) override { assocBeacons++; }

    std::vector<ScannedBss> discovered;
    std::vector<std::vector<uint8_t>> rxFrames;
    uint32_t scanDoneCount = 0;
    bool assocDone = false;
    bool assocOk = false;
    uint16_t assocStatus = 0xffff;
    bool linkUp = false;
    uint32_t linkTransitions = 0;
    uint16_t lastDeauth = 0;
    uint32_t assocBeacons = 0;
};

// ============================================================================
// Test Suites
// ============================================================================
static void testEmbeddedFirmwareIntegrity() {
    printf("[TEST 1/7] Verifying embedded MT7902 Wi-Fi & Bluetooth firmware blobs...\n");
    TEST_CHECK(gWifiFirmwareCount == 2, "Expected 2 embedded Wi-Fi blobs, got %zu", gWifiFirmwareCount);
    TEST_CHECK(gBtFirmwareCount == 1, "Expected 1 embedded BT blob, got %zu", gBtFirmwareCount);

    FirmwareBuffer patchBuf, ramBuf, btBuf;
    TEST_CHECK(loadEmbeddedFirmware(gWifiFirmwareTable, gWifiFirmwareCount, kMT7902PatchFwName, patchBuf),
               "Failed to decompress %s", kMT7902PatchFwName);
    TEST_CHECK(loadEmbeddedFirmware(gWifiFirmwareTable, gWifiFirmwareCount, kMT7902RamFwName, ramBuf),
               "Failed to decompress %s", kMT7902RamFwName);
    TEST_CHECK(loadEmbeddedFirmware(gBtFirmwareTable, gBtFirmwareCount, bt::kMT7902BtFwName, btBuf),
               "Failed to decompress %s", bt::kMT7902BtFwName);

    uint32_t patchCrc = static_cast<uint32_t>(crc32(0L, patchBuf.data, static_cast<uInt>(patchBuf.size)));
    uint32_t ramCrc = static_cast<uint32_t>(crc32(0L, ramBuf.data, static_cast<uInt>(ramBuf.size)));
    uint32_t btCrc = static_cast<uint32_t>(crc32(0L, btBuf.data, static_cast<uInt>(btBuf.size)));
    TEST_CHECK(patchCrc == gWifiFirmwareTable[0].crc32, "Wi-Fi patch CRC32 mismatch");
    TEST_CHECK(ramCrc == gWifiFirmwareTable[1].crc32, "Wi-Fi RAM CRC32 mismatch");
    TEST_CHECK(btCrc == gBtFirmwareTable[0].crc32, "BT RAM CRC32 mismatch");

    ParsedPatchImage patchImg;
    TEST_CHECK(MT7902Firmware::parsePatchImage(patchBuf.data, patchBuf.size, patchImg) == kOk,
               "parsePatchImage failed");
    TEST_CHECK(strcmp(patchImg.platform, "ALPS") == 0, "Unexpected patch platform: %s", patchImg.platform);
    TEST_CHECK(patchImg.hwSwVer == 0x8a108a10U, "Unexpected patch hwSwVer: 0x%x", patchImg.hwSwVer);
    TEST_CHECK(patchImg.regionCount >= 1 && patchImg.sections[0].len > 64000, "Invalid patch section");

    ParsedRamImage ramImg;
    TEST_CHECK(MT7902Firmware::parseRamImage(ramBuf.data, ramBuf.size, ramImg) == kOk,
               "parseRamImage failed");
    TEST_CHECK(ramImg.regionCount >= 3, "Expected >=3 WM RAM regions, got %u", ramImg.regionCount);

    bt::ParsedBtFirmware btImg;
    TEST_CHECK(bt::MT7902BtLoader::parseFirmware79xx(btBuf.data, btBuf.size, btImg) == kOk,
               "parseFirmware79xx failed");
    TEST_CHECK(strcmp(btImg.platform, "ALPS") == 0, "Unexpected BT platform: %s", btImg.platform);
    TEST_CHECK(btImg.hwVer == 0x008a && btImg.swVer == 0x008a, "Unexpected BT hw/sw ver");
    TEST_CHECK(btImg.sectionCount >= 2, "Expected >=2 BT sections, got %u", btImg.sectionCount);

    patchBuf.release();
    ramBuf.release();
    btBuf.release();
}

static void testRegisterRemapping() {
    printf("[TEST 2/7] Verifying MT7902 PCIe BAR0 fixed table & L1 dynamic window remap...\n");
    uint32_t barOfs = 0;
    TEST_CHECK(mt7902FixedMapLookup(MT_WFDMA0_GLO_CFG, barOfs) && barOfs == 0xd4208U, "WFDMA0 map mismatch");
    TEST_CHECK(mt7902FixedMapLookup(MT_MDP_DCR0, barOfs) && barOfs == 0x0f000U, "MDP map mismatch");
    TEST_CHECK(mt7902FixedMapLookup(MT_WTBL_UPDATE, barOfs) && barOfs == 0x34230U, "WTBL map mismatch");
    TEST_CHECK(mt7902FixedMapLookup(MT_TMAC_CTCR0(0), barOfs) && barOfs == 0x210f4U, "BN0 TMAC map mismatch");
    TEST_CHECK(mt7902FixedMapLookup(MT_TMAC_CTCR0(1), barOfs) && barOfs == 0xa10f4U, "BN1 TMAC map mismatch");
    TEST_CHECK(mt7902FixedMapLookup(MT_CONN_ON_LPCTL, barOfs) && barOfs == 0xe0010U, "LPCTL map mismatch");

    TEST_CHECK(mt7902NeedsL1Remap(MT_HW_CHIPID), "MT_HW_CHIPID must use L1 remap");
    TEST_CHECK(mt7902NeedsL1Remap(MT_WFSYS_SW_RST_B), "MT_WFSYS_SW_RST_B must use L1 remap");

    MockMT7902AsicBus bus;
    TEST_CHECK((bus.l1Rr(MT_HW_CHIPID) & 0xffffU) == 0x7902U, "Mock L1 chip ID read failed");
    TEST_CHECK((bus.barRead32(MT_HIF_REMAP_L1) & 0xffffU) == 0x7001U, "L1 base register not programmed");
}

static void testFullDeviceProbeFwBootAndDmaQuirks(MockMT7902AsicBus &bus, MT7902Device &dev) {
    printf("[TEST 3/7] Simulating full MT7902Device::probeAndInit() + real firmware scatter...\n");
    int err = dev.probeAndInit();
    TEST_CHECK(err == kOk, "probeAndInit() failed with %d", err);
    TEST_CHECK(dev.isInitialized(), "Device should be marked initialized");
    TEST_CHECK((dev.chipRev() >> 16) == 0x7902U, "Unexpected chipRev 0x%08x", dev.chipRev());

    // Verify MT7902-specific hardware quirks vs MT7921:
    TEST_CHECK(dev.dma().ring(kTxMcuWm).hwIdx == 15, "MT7902 MCU-WM TX ring MUST be index 15");
    TEST_CHECK(dev.dma().ring(kTxFwdl).hwIdx == 16, "MT7902 FWDL TX ring MUST be index 16");
    TEST_CHECK(dev.dma().ring(kRxMcuEvent).ndesc == 512, "MT7902 RX0 shared ring MUST have 512 descs");
    TEST_CHECK((dev.dma().irqMask() & HOST_RX_DONE_INT_ENA4) == 0,
               "MT7902 has NO MCU-WA ring; RX_DONE_INT_ENA4 must be disabled");
    TEST_CHECK(bus.fwdlTotalBytes > 800000, "Expected >800 KB of real patch+RAM scattered, got %zu",
               bus.fwdlTotalBytes);
    TEST_CHECK(bus.wtblUpdates == MT7902_WTBL_SIZE, "Expected %u WTBL clears, got %u",
               MT7902_WTBL_SIZE, bus.wtblUpdates);

    const auto &cap = dev.capability();
    TEST_CHECK(cap.macValid && cap.macAddr[3] == 0x79 && cap.macAddr[4] == 0x02, "NIC MAC parse failed");
    TEST_CHECK(cap.has2Ghz && cap.has5Ghz && cap.has6Ghz && cap.supportsHe, "Wi-Fi 6E capabilities missing");

    TEST_CHECK(dev.startRadio() == kOk && dev.isRadioRunning(), "startRadio() failed");
    TEST_CHECK(dev.suspend() == kOk, "suspend() failed");
    TEST_CHECK(dev.resume() == kOk, "resume() failed");
}

static void testTxwiTokensTxFreeAndRxdDecoding(MockMT7902AsicBus &bus, MT7902Device &dev) {
    printf("[TEST 4/7] Verifying TXWI+HW_TXP, TX token table, PKT_TYPE_TXRX_NOTIFY & RXD/P-RXV...\n");
    bus.capturedBand0Txwis.clear();
    bus.capturedBand0TxPayloads.clear();

    uint8_t dummyEth[60] = {};
    memset(dummyEth, 0x11, 6);
    memset(dummyEth + 6, 0x22, 6);
    dummyEth[12] = 0x08;
    dummyEth[13] = 0x00; // IPv4

    TEST_CHECK(dev.sendEthernetFrame(dummyEth, sizeof(dummyEth), 1, true, true) == kOk, "TX 1 failed");
    TEST_CHECK(dev.sendEthernetFrame(dummyEth, sizeof(dummyEth), 1, true, true) == kOk, "TX 2 failed");
    TEST_CHECK(dev.mac().activeTokens() == 2, "Expected 2 active TX tokens, got %u", dev.mac().activeTokens());
    TEST_CHECK(bus.capturedBand0Txwis.size() == 2, "Expected 2 Band0 TXWIs");

    // Verify Connac2 HW_TXP len0 sets ONLY MT_TXD_LEN_LAST (never MT_TXD_LEN_MSDU_LAST)
    uint16_t rawLen0 = getLe16(&bus.capturedBand0Txwis[0].txp.ptr[0].len0);
    TEST_CHECK((rawLen0 & MT_TXD_LEN_LAST) != 0 && (rawLen0 & MT_TXD_LEN_MSDU_LAST) == 0,
               "Connac2 HW_TXP len0 must set MT_TXD_LEN_LAST without MT_TXD_LEN_MSDU_LAST (got 0x%04x)", rawLen0);

    uint16_t tok0 = getLe16(&bus.capturedBand0Txwis[0].txp.msdu_id[0]) & ~MT_MSDU_ID_VALID;
    uint16_t tok1 = getLe16(&bus.capturedBand0Txwis[1].txp.msdu_id[0]) & ~MT_MSDU_ID_VALID;

    // Build PKT_TYPE_TXRX_NOTIFY (Mt76ConnacTxFree + PAIR marker + 2 token entries) on RX Ring 0
    // In Connac2 hardware, DW0 = rx_byte_cnt[15:0] | (msdu_cnt[9:0] << 16) | (PKT_TYPE_TXRX_NOTIFY << 27)
    uint8_t txFreePkt[sizeof(Mt76ConnacTxFree) + 12] = {};
    uint32_t tfDw0 = fieldPrep(MT_RXD0_LENGTH, sizeof(txFreePkt)) |
                     (fieldPrep(MT_TX_FREE_MSDU_CNT, 2) << 16) |
                     fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_TXRX_NOTIFY);
    putLe32(txFreePkt + 0, tfDw0);
    putLe32(txFreePkt + 8, MT_TX_FREE_PAIR | fieldPrep(MT_TX_FREE_WLAN_ID, 1));
    putLe32(txFreePkt + 12, fieldPrep(MT_TX_FREE_MSDU_ID, tok0) | fieldPrep(MT_TX_FREE_COUNT, 1));
    putLe32(txFreePkt + 16, fieldPrep(MT_TX_FREE_MSDU_ID, tok1) | fieldPrep(MT_TX_FREE_COUNT, 2));

    bus.injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, txFreePkt, sizeof(txFreePkt));
    dev.serviceInterrupt();
    TEST_CHECK(dev.mac().activeTokens() == 0, "TX tokens not freed by PKT_TYPE_TXRX_NOTIFY");
    TEST_CHECK(dev.mac().stats().txRetries == 1, "TX retry stat mismatch");

    // Verify RXD + Group 3 P-RXV decoding (HE_SU MCS 11, BW 80MHz, RSSI -52 dBm)
    uint8_t rxRaw[24 + 8 + 60] = {};
    putLe32(rxRaw + 0, fieldPrep(MT_RXD0_LENGTH, sizeof(rxRaw)) |
                       fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_NORMAL));
    putLe32(rxRaw + 4, fieldPrep(MT_RXD1_NORMAL_WLAN_IDX, 1) |
                       MT_RXD1_NORMAL_GROUP_3 |
                       fieldPrep(MT_RXD1_NORMAL_SEC_MODE, 5));
    putLe32(rxRaw + 8, MT_RXD2_NORMAL_HDR_TRANS);
    putLe32(rxRaw + 12, fieldPrep(MT_RXD3_NORMAL_CH_FREQ, 36) |
                        fieldPrep(MT_RXD3_NORMAL_ADDR_TYPE, MT_RXD3_NORMAL_U2M));
    // Group 3 (P-RXV: 2 dwords at offset 24)
    uint32_t prxv0 = fieldPrep(MT_PRXV_TX_MODE, MT_PHY_TYPE_HE_SU) |
                     fieldPrep(MT_PRXV_TX_RATE, 11) |
                     fieldPrep(MT_PRXV_FRAME_MODE, CMD_CBW_80MHZ);
    uint32_t prxv1 = fieldPrep(MT_PRXV_RCPI0, rssiToRcpi(-52));
    putLe32(rxRaw + 24, prxv0);
    putLe32(rxRaw + 28, prxv1);
    memcpy(rxRaw + 32, dummyEth, sizeof(dummyEth));

    ParsedRxFrame parsed;
    TEST_CHECK(MT7902Mac::parseRxDescriptor(rxRaw, sizeof(rxRaw), 0x1, parsed) && parsed.valid,
               "parseRxDescriptor failed");
    TEST_CHECK(parsed.is8023 && parsed.decrypted && parsed.unicast, "RXD flags mismatch");
    TEST_CHECK(parsed.rssi == -52, "Expected RSSI -52 dBm, got %d", parsed.rssi);
    TEST_CHECK(parsed.phyMode == MT_PHY_TYPE_HE_SU && parsed.mcs == 11 && parsed.bw == CMD_CBW_80MHZ,
               "P-RXV PHY rate decode mismatch");
    TEST_CHECK(MT7902Mac::estimateRateMbps(parsed.phyMode, parsed.mcs, parsed.nss, parsed.bw) >= 400,
               "Expected 80MHz HE MCS11 >= 400 Mbps");

    // Verify Connac2 OFDM hardware rate code 0x0b decodes to index 0 (6 Mbps)
    putLe32(rxRaw + 24, fieldPrep(MT_PRXV_TX_MODE, MT_PHY_TYPE_OFDM) |
                        fieldPrep(MT_PRXV_TX_RATE, 0x0b));
    TEST_CHECK(MT7902Mac::parseRxDescriptor(rxRaw, sizeof(rxRaw), 0x1, parsed) &&
                   parsed.rateValid && parsed.mcs == 0 &&
                   MT7902Mac::estimateRateMbps(parsed.phyMode, parsed.mcs, parsed.nss, parsed.bw) == 6,
               "Connac2 OFDM 0x0b must decode to 6 Mbps (index 0)");

    // Verify Connac2 A-MSDU subframe P-RXV fill pattern (0x55555555) is rejected by rateValid
    putLe32(rxRaw + 24, 0x55555555U);
    TEST_CHECK(MT7902Mac::parseRxDescriptor(rxRaw, sizeof(rxRaw), 0x1, parsed) &&
                   parsed.valid && !parsed.rateValid,
               "P-RXV fill pattern 0x55555555 must mark rateValid=false");
}

static void inject80211FrameOnRx2(MockMT7902AsicBus &bus, const uint8_t *dot11, size_t dot11Len,
                                  uint8_t ch = 36, int32_t rssi = -48) {
    std::vector<uint8_t> raw(32 + dot11Len, 0);
    putLe32(raw.data() + 0, fieldPrep(MT_RXD0_LENGTH, static_cast<uint32_t>(raw.size())) |
                            fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_NORMAL));
    putLe32(raw.data() + 4, MT_RXD1_NORMAL_GROUP_3);
    putLe32(raw.data() + 12, fieldPrep(MT_RXD3_NORMAL_CH_FREQ, ch));
    putLe32(raw.data() + 24, fieldPrep(MT_PRXV_TX_MODE, MT_PHY_TYPE_OFDM));
    putLe32(raw.data() + 28, fieldPrep(MT_PRXV_RCPI0, rssiToRcpi(rssi)));
    memcpy(raw.data() + 32, dot11, dot11Len);
    bus.injectRxPacket(MT_RX_DATA_RING_BASE, MT_INT_RX_DONE_DATA, raw.data(), raw.size());
}

static void testStaScanWpa2AssocKeyInstallAndData(MockMT7902AsicBus &bus, MT7902Device &dev) {
    printf("[TEST 5/7] Simulating 802.11ax Scan -> WPA2 Auth -> Assoc -> EAPOL -> PTK/GTK -> Data...\n");
    MT7902Sta sta(dev);
    MockStaListener listener;
    sta.setListener(&listener);

    TEST_CHECK(sta.setPower(true) == kOk && sta.isPoweredOn(), "sta.setPower(true) failed");
    TEST_CHECK(sta.setCountryCode("VN") == kOk, "setCountryCode failed");

    ScanRequestParams scanReq;
    scanReq.active = true;
    TEST_CHECK(sta.startScan(scanReq) == kOk && sta.isScanning(), "startScan failed");

    // Synthesize a 5 GHz Wi-Fi 6 WPA2-PSK Beacon frame
    const uint8_t apBssid[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    const char *ssidStr = "MT7902-Filogic-6E";
    const uint8_t rsnIe[] = {
        48, 20, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,             // Group: CCMP-128
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, // Pairwise: CCMP-128
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, // AKM: PSK
        0x00, 0x00
    };
    const uint8_t heExtIe[] = { 255, 4, 35, 0x05, 0x00, 0x00 };

    std::vector<uint8_t> beacon(36, 0);
    putLe16(beacon.data() + 0, 0x0080); // Beacon FC
    memset(beacon.data() + 4, 0xff, 6);
    memcpy(beacon.data() + 10, apBssid, 6);
    memcpy(beacon.data() + 16, apBssid, 6);
    putLe16(beacon.data() + 32, 100);    // Beacon interval
    putLe16(beacon.data() + 34, 0x0431); // ESS + Privacy + ShortSlot
    beacon.push_back(0);
    beacon.push_back(static_cast<uint8_t>(strlen(ssidStr)));
    beacon.insert(beacon.end(), ssidStr, ssidStr + strlen(ssidStr));
    beacon.push_back(3); // DS Param Set
    beacon.push_back(1);
    beacon.push_back(36);
    beacon.insert(beacon.end(), rsnIe, rsnIe + sizeof(rsnIe));
    beacon.insert(beacon.end(), heExtIe, heExtIe + sizeof(heExtIe));

    inject80211FrameOnRx2(bus, beacon.data(), beacon.size(), 36, -46);

    // Inject unsolicited MCU_EVENT_SCAN_DONE on RX Ring 0
    uint8_t scanDoneEvt[sizeof(Mt76Connac2McuRxd) + 4] = {};
    auto *srxd = reinterpret_cast<Mt76Connac2McuRxd *>(scanDoneEvt);
    putLe32(&srxd->rxd[0], fieldPrep(MT_RXD0_LENGTH, sizeof(scanDoneEvt)) |
                           fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_RX_EVENT));
    srxd->eid = MCU_EVENT_SCAN_DONE;
    srxd->option = MCU_UNI_CMD_UNSOLICITED_EVENT;
    scanDoneEvt[sizeof(Mt76Connac2McuRxd)] = bus.lastHwScanSeq;
    bus.injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, scanDoneEvt, sizeof(scanDoneEvt));

    dev.serviceInterrupt();
    TEST_CHECK(!sta.isScanning() && listener.scanDoneCount == 1, "Scan completion not handled");
    TEST_CHECK(listener.discovered.size() == 1, "Expected 1 discovered BSS");
    TEST_CHECK(listener.discovered[0].hasRsn && listener.discovered[0].hasPsk && listener.discovered[0].hasHe,
               "Beacon RSN/PSK/HE IE parse mismatch");

    // Initiate WPA2 association
    bus.capturedBand0TxPayloads.clear();
    bus.capturedBand0Txwis.clear();
    AssocRequestParams areq;
    areq.ssidLen = static_cast<uint8_t>(strlen(ssidStr));
    memcpy(areq.ssid, ssidStr, areq.ssidLen);
    memcpy(areq.bssid, apBssid, 6);
    areq.hasBssid = true;
    areq.authLower = 1;
    areq.authUpper = 8; // WPA2-PSK
    memcpy(areq.rsnIe, rsnIe, sizeof(rsnIe));
    areq.rsnIeLen = sizeof(rsnIe);

    TEST_CHECK(sta.associate(areq) == kOk, "sta.associate() failed");
    TEST_CHECK(sta.link().state == kStaAuthenticating, "Expected kStaAuthenticating");
    TEST_CHECK(bus.capturedBand0TxPayloads.size() == 1 &&
               getLe16(bus.capturedBand0TxPayloads[0].data()) == 0x00b0, "Expected 802.11 Auth Req");
    TEST_CHECK(fieldGet(MT_TXD1_WLAN_IDX, getLe32(&bus.capturedBand0Txwis[0].txd[1])) == sta.link().apWcid,
               "Auth Req must use peer apWcid (not 0)");
    TEST_CHECK(fieldGet(MT_TXD6_TX_RATE, getLe32(&bus.capturedBand0Txwis[0].txd[6])) ==
                   ((static_cast<uint32_t>(MT_PHY_TYPE_OFDM) << 6) | 0x0bU),
               "5 GHz Auth Req must use OFDM 6 Mbps (0x004b), not CCK");

    // Inject 802.11 Auth Response (status = 0)
    uint8_t authResp[30] = {};
    putLe16(authResp + 0, 0x00b0);
    memcpy(authResp + 4, dev.capability().macAddr, 6);
    memcpy(authResp + 10, apBssid, 6);
    memcpy(authResp + 16, apBssid, 6);
    putLe16(authResp + 24, 0);
    putLe16(authResp + 26, 2);
    putLe16(authResp + 28, 0);
    inject80211FrameOnRx2(bus, authResp, sizeof(authResp));
    dev.serviceInterrupt();
    TEST_CHECK(sta.link().state == kStaAssociating, "Expected kStaAssociating");
    TEST_CHECK(bus.capturedBand0TxPayloads.size() == 2 &&
               getLe16(bus.capturedBand0TxPayloads[1].data()) == 0x0000, "Expected 802.11 Assoc Req");

    // Inject 802.11 Assoc Response (status = 0, AID = 0xc007)
    uint8_t assocResp[32] = {};
    putLe16(assocResp + 0, 0x0010);
    memcpy(assocResp + 4, dev.capability().macAddr, 6);
    memcpy(assocResp + 10, apBssid, 6);
    memcpy(assocResp + 16, apBssid, 6);
    putLe16(assocResp + 24, 0x0431);
    putLe16(assocResp + 26, 0);      // Status success
    putLe16(assocResp + 28, 0xc007); // AID = 7
    inject80211FrameOnRx2(bus, assocResp, sizeof(assocResp));
    dev.serviceInterrupt();

    TEST_CHECK(listener.assocDone && listener.assocOk && sta.link().aid == 7, "Assoc completion failed");
    TEST_CHECK(sta.link().state == kStaAssociated && !listener.linkUp,
               "WPA2 STA must wait for PTK before reporting linkUp");

    // EAPOL (0x888e) must pass unencrypted during 4-way handshake, while IPv4 (0x0800) must be gated
    uint8_t eapolPkt[64] = {};
    memcpy(eapolPkt + 0, apBssid, 6);
    memcpy(eapolPkt + 6, dev.capability().macAddr, 6);
    eapolPkt[12] = 0x88;
    eapolPkt[13] = 0x8e;
    uint8_t ipv4Pkt[64] = {};
    memcpy(ipv4Pkt + 0, apBssid, 6);
    memcpy(ipv4Pkt + 6, dev.capability().macAddr, 6);
    ipv4Pkt[12] = 0x08;
    ipv4Pkt[13] = 0x00;

    bus.capturedBand0Txwis.clear();
    TEST_CHECK(sta.outputEthernetPacket(eapolPkt, sizeof(eapolPkt)) == kOk, "EAPOL TX failed");
    TEST_CHECK((getLe32(&bus.capturedBand0Txwis.back().txd[3]) & MT_TXD3_PROTECT_FRAME) == 0,
               "Initial EAPOL 2/4 must NOT set HW protect bit before PTK");
    TEST_CHECK(sta.outputEthernetPacket(ipv4Pkt, sizeof(ipv4Pkt)) == kErrIO,
               "IPv4 must be blocked before PTK installation");

    // Install CCMP-128 PTK & GTK
    const uint8_t ptk[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
    const uint8_t gtk[16] = { 16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1 };
    TEST_CHECK(sta.installCipherKey(MCU_CIPHER_AES_CCMP, 0, ptk, 16, true, true) == kOk, "PTK install failed");
    TEST_CHECK(sta.installCipherKey(MCU_CIPHER_AES_CCMP, 1, gtk, 16, false, true) == kOk, "GTK install failed");
    TEST_CHECK(sta.link().state == kStaRun && listener.linkUp, "STA did not enter kStaRun after PTK");

    TEST_CHECK(sta.outputEthernetPacket(ipv4Pkt, sizeof(ipv4Pkt)) == kOk, "Encrypted IPv4 TX failed");
    TEST_CHECK((getLe32(&bus.capturedBand0Txwis.back().txd[3]) & MT_TXD3_PROTECT_FRAME) != 0,
               "Encrypted IPv4 TX must set MT_TXD3_PROTECT_FRAME");

    // Verify Private Wi-Fi Address rotation cleanly tears down active STA/BSS before MAC update
    const uint8_t rotatedMac[6] = { 0xee, 0xf0, 0xe1, 0x85, 0x44, 0xac };
    TEST_CHECK(sta.setMacAddress(rotatedMac) == kOk && !listener.linkUp && sta.link().state == kStaInit,
               "setMacAddress() must cleanly disassociate active link before updating MAC");
    TEST_CHECK(memcmp(dev.capability().macAddr, rotatedMac, 6) == 0,
               "setMacAddress() did not update device MAC");

    // Verify Auth rejection (status=30 retries once after comeback, second reject resets cleanly)
    bus.capturedBand0TxPayloads.clear();
    TEST_CHECK(sta.associate(areq) == kOk && sta.link().state == kStaAuthenticating,
               "Re-associate after MAC rotation failed");
    uint8_t authReject[30] = {};
    putLe16(authReject + 0, 0x00b0);
    memcpy(authReject + 4, rotatedMac, 6);
    memcpy(authReject + 10, apBssid, 6);
    memcpy(authReject + 16, apBssid, 6);
    putLe16(authReject + 24, 0);
    putLe16(authReject + 26, 2);
    putLe16(authReject + 28, 30); // STATUS_ASSOC_REJECTED_TEMPORARILY
    inject80211FrameOnRx2(bus, authReject, sizeof(authReject), 36, -50);
    dev.serviceInterrupt();
    TEST_CHECK(sta.link().state == kStaAuthenticating && bus.capturedBand0TxPayloads.size() == 2,
               "First status=30 should perform one Auth comeback retry");
    inject80211FrameOnRx2(bus, authReject, sizeof(authReject), 36, -50);
    dev.serviceInterrupt();
    TEST_CHECK(sta.link().state == kStaInit && sta.link().lastAssocStatus == 30,
               "Second Auth reject (status=30) did not reset STA to kStaInit");
    TEST_CHECK(sta.associate(areq) == kOk && sta.link().state == kStaAuthenticating,
               "Retry associate after Auth reject failed");
    TEST_CHECK(sta.disassociate(8) == kOk && !listener.linkUp, "disassociate() failed");
    dev.mac().releaseAllTokens();
}

static void test5Ghz80MhzBaPostScanAndReentrancy(MockMT7902AsicBus &bus, MT7902Device &dev) {
    printf("[10/10] Testing 5 GHz DFS 80 MHz, HE/VHT TLVs, ADDBA, post-scan restore & duplicate Deauth...\n");

    // 1. Verify Apple80211 channel flags match include/Airport/apple80211_var.h
    TEST_CHECK(kAppleChanFlagActive == 0x0080U && kAppleChanFlag80Mhz == 0x0400U &&
                   kAppleChanFlag160Mhz == 0x0800U,
               "Apple80211 channel flag constants mismatch");
    const ChannelInfoEntry *ch116 = findChannel(116, kBand5GHz);
    TEST_CHECK(ch116 != nullptr && ch116->dfs && ch116->allow80, "Channel 116 lookup failed");
    uint32_t f116 = channelToAppleFlags(*ch116, CMD_CBW_80MHZ);
    TEST_CHECK((f116 & kAppleChanFlag5Ghz) && (f116 & kAppleChanFlag80Mhz) &&
                   (f116 & kAppleChanFlagDfs) && !(f116 & kAppleChanFlag160Mhz),
               "Channel 116 80MHz Apple flags mismatch (0x%x)", f116);

    // 2. Parse 5 GHz DFS ch 116 80 MHz (center=122) VHT+HE beacon
    MT7902Sta sta(dev);
    MockStaListener listener;
    sta.setListener(&listener);
    TEST_CHECK(sta.setPower(true) == kOk, "sta.setPower failed");

    const uint8_t apBssid[6] = { 0x7c, 0x7b, 0x68, 0x01, 0x74, 0x51 };
    const char *ssidStr = "TestDFS5G";
    std::vector<uint8_t> beacon(36, 0);
    putLe16(beacon.data() + 0, 0x0080);
    memset(beacon.data() + 4, 0xff, 6);
    memcpy(beacon.data() + 10, apBssid, 6);
    memcpy(beacon.data() + 16, apBssid, 6);
    putLe16(beacon.data() + 32, 100);
    putLe16(beacon.data() + 34, 0x0401);
    beacon.push_back(0); beacon.push_back(static_cast<uint8_t>(strlen(ssidStr)));
    beacon.insert(beacon.end(), ssidStr, ssidStr + strlen(ssidStr));
    // HT Operation (ID=61): primary=116, SCA + 40MHz allowed (0x05)
    beacon.push_back(61); beacon.push_back(22);
    beacon.push_back(116); beacon.push_back(0x05);
    beacon.insert(beacon.end(), 20, 0);
    // VHT Capabilities (ID=191)
    beacon.push_back(191); beacon.push_back(12);
    beacon.insert(beacon.end(), 12, 0);
    // VHT Operation (ID=192): width=1 (80MHz), seg0=122, seg1=0
    beacon.push_back(192); beacon.push_back(5);
    beacon.push_back(1); beacon.push_back(122); beacon.push_back(0); beacon.push_back(0); beacon.push_back(0);
    // HE Capabilities (ID=255, Ext=35) + HE Operation (ID=255, Ext=36, BSS Color=7)
    beacon.push_back(255); beacon.push_back(4); beacon.push_back(35);
    beacon.push_back(0x05); beacon.push_back(0); beacon.push_back(0);
    beacon.push_back(255); beacon.push_back(7); beacon.push_back(36);
    beacon.push_back(0xf4); beacon.push_back(0x3f); beacon.push_back(0x00);
    beacon.push_back(7); beacon.push_back(0); beacon.push_back(0);

    inject80211FrameOnRx2(bus, beacon.data(), beacon.size(), 116, -45);
    dev.serviceInterrupt();
    const ScannedBss *found = sta.scanCache().findByBssid(apBssid);
    TEST_CHECK(found && found->channel == 116 && found->centerChannel == 122 &&
                   found->bw == CMD_CBW_80MHZ && found->hasVht && found->hasHe &&
                   found->bssColor == 7,
               "5 GHz 80 MHz VHT+HE+BSSColor beacon parse failed");

    // 3. Associate to open 5 GHz DFS ch 116 -> verify CH_SWITCH_DFS + STA_REC_VHT/HE/HE_V2
    bus.sawStaRecHe = bus.sawStaRecHeV2 = bus.sawStaRecVht = bus.sawStaRecBa = false;
    AssocRequestParams areq;
    areq.ssidLen = static_cast<uint8_t>(strlen(ssidStr));
    memcpy(areq.ssid, ssidStr, areq.ssidLen);
    memcpy(areq.bssid, apBssid, 6);
    areq.hasBssid = true;
    areq.authLower = 1;
    areq.authUpper = 0; // Open -> enters kStaRun directly on Assoc Resp
    TEST_CHECK(sta.associate(areq) == kOk, "5G DFS associate failed");
    TEST_CHECK(bus.lastChanSwitchPrimary == 116 && bus.lastChanSwitchReason == CH_SWITCH_DFS,
               "DFS ch 116 must use CH_SWITCH_DFS (got reason=%u)", bus.lastChanSwitchReason);

    uint8_t authResp[30] = {};
    putLe16(authResp + 0, 0x00b0);
    memcpy(authResp + 4, dev.capability().macAddr, 6);
    memcpy(authResp + 10, apBssid, 6);
    memcpy(authResp + 16, apBssid, 6);
    putLe16(authResp + 26, 2);
    inject80211FrameOnRx2(bus, authResp, sizeof(authResp), 116, -45);
    dev.serviceInterrupt();

    uint8_t assocResp[32] = {};
    putLe16(assocResp + 0, 0x0010);
    memcpy(assocResp + 4, dev.capability().macAddr, 6);
    memcpy(assocResp + 10, apBssid, 6);
    memcpy(assocResp + 16, apBssid, 6);
    putLe16(assocResp + 24, 0x0401);
    putLe16(assocResp + 28, 0xc003);
    inject80211FrameOnRx2(bus, assocResp, sizeof(assocResp), 116, -45);
    dev.serviceInterrupt();

    TEST_CHECK(sta.link().state == kStaRun && sta.link().bw == CMD_CBW_80MHZ &&
                   sta.link().centerChannel == 122,
               "5G 80 MHz link state mismatch");
    TEST_CHECK(bus.lastChanSwitchCenter == 122 && bus.lastChanSwitchBw == CMD_CBW_80MHZ &&
                   bus.lastChanSwitchReason == CH_SWITCH_DFS,
               "Associated 5G DFS 80MHz channel switch mismatch");
    TEST_CHECK(bus.sawStaRecVht && bus.sawStaRecHe && bus.sawStaRecHeV2,
               "Associated 5G HE/VHT STA_REC TLVs missing");
    // Verify 5 GHz MAC timing programmed SIFS = 16 us (not 2.4 GHz 10 us!)
    uint32_t icr0 = bus.rr(MT_TMAC_ICR0(0));
    TEST_CHECK(fieldGet(MT_IFS_SIFS, icr0) == 16U,
               "5 GHz MT_TMAC_ICR0 SIFS must be 16 us (got %u)", fieldGet(MT_IFS_SIFS, icr0));

    // 4. ADDBA Request -> ADDBA Response + STA_REC_BA
    bus.capturedBand0TxPayloads.clear();
    uint8_t addbaReq[33] = {};
    putLe16(addbaReq + 0, 0x00d0); // Action
    memcpy(addbaReq + 4, dev.capability().macAddr, 6);
    memcpy(addbaReq + 10, apBssid, 6);
    memcpy(addbaReq + 16, apBssid, 6);
    addbaReq[24] = 3; // Block Ack
    addbaReq[25] = 0; // ADDBA Request
    addbaReq[26] = 7; // Dialog token
    putLe16(addbaReq + 27, static_cast<uint16_t>((64U << 6) | (0U << 2) | (1U << 1) | 1U));
    putLe16(addbaReq + 29, 0);
    putLe16(addbaReq + 31, static_cast<uint16_t>(12U << 4));
    inject80211FrameOnRx2(bus, addbaReq, sizeof(addbaReq), 116, -45);
    dev.serviceInterrupt();
    TEST_CHECK(bus.sawStaRecBa, "ADDBA Request must program STA_REC_BA");
    TEST_CHECK(bus.capturedBand0TxPayloads.size() == 1 &&
                   bus.capturedBand0TxPayloads[0].size() == 33 &&
                   bus.capturedBand0TxPayloads[0][24] == 3 &&
                   bus.capturedBand0TxPayloads[0][25] == 1 &&
                   bus.capturedBand0TxPayloads[0][26] == 7,
               "ADDBA Response not sent properly");

    // 5. Connected scan completion restores operating 5 GHz 80 MHz channel, and DFS batches boost dwell+timeout
    uint32_t preScanSwitches = bus.chanSwitchCount;
    ScanRequestParams sreq;
    sreq.active = true;
    sreq.channelCount = 2;
    sreq.channels[0] = { 1, 7 };
    sreq.channels[1] = { 2, 116 };
    TEST_CHECK(sta.startScan(sreq) == kOk && sta.isScanning(), "Connected DFS startScan failed");
    TEST_CHECK(bus.lastHwScanDwell == 120U && bus.lastHwScanTimeout == 600U,
               "Short DFS batch must set dwell=120, timeout=600 (got %u/%u)",
               bus.lastHwScanDwell, bus.lastHwScanTimeout);
    TEST_CHECK(sta.abortScan(true) == kOk && !sta.isScanning() &&
                   bus.lastCancelScanSeq == (bus.lastHwScanSeq & 0x7fU),
               "abortScan must pass active scanSeq to CANCEL_HW_SCAN");

    ScanRequestParams active2g;
    active2g.active = true;
    active2g.channelCount = 2;
    active2g.channels[0] = { 1, 1 };
    active2g.channels[1] = { 1, 6 };
    TEST_CHECK(sta.startScan(active2g) == kOk && sta.isScanning(), "Connected active 2G startScan failed");
    TEST_CHECK(bus.lastHwScanDwell == 0U && bus.lastHwScanTimeout == 0U,
               "Active non-DFS batch must use firmware default dwell=0, timeout=0");
    uint8_t scanDoneEvt[sizeof(Mt76Connac2McuRxd) + 4] = {};
    auto *srxd = reinterpret_cast<Mt76Connac2McuRxd *>(scanDoneEvt);
    putLe32(&srxd->rxd[0], fieldPrep(MT_RXD0_LENGTH, sizeof(scanDoneEvt)) |
                           fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_RX_EVENT));
    srxd->eid = MCU_EVENT_SCAN_DONE;
    srxd->option = MCU_UNI_CMD_UNSOLICITED_EVENT;
    scanDoneEvt[sizeof(Mt76Connac2McuRxd)] = bus.lastHwScanSeq;
    bus.injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, scanDoneEvt, sizeof(scanDoneEvt));
    dev.serviceInterrupt();
    TEST_CHECK(!sta.isScanning() && bus.chanSwitchCount > preScanSwitches &&
                   bus.lastChanSwitchPrimary == 116 && bus.lastChanSwitchCenter == 122 &&
                   bus.lastChanSwitchBw == CMD_CBW_80MHZ,
               "onScanComplete() must restore connected 5 GHz 80 MHz operating channel");

    // 6. Back-to-back duplicate AP Deauth frames on RX Ring 0 must NOT re-enter cleanup
    uint8_t deauth[26] = {};
    putLe16(deauth + 0, 0x00c0);
    memcpy(deauth + 4, dev.capability().macAddr, 6);
    memcpy(deauth + 10, apBssid, 6);
    memcpy(deauth + 16, apBssid, 6);
    putLe16(deauth + 24, 7);
    std::vector<uint8_t> rawDeauth(32 + sizeof(deauth), 0);
    putLe32(rawDeauth.data() + 0, fieldPrep(MT_RXD0_LENGTH, static_cast<uint32_t>(rawDeauth.size())) |
                                  fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_NORMAL));
    putLe32(rawDeauth.data() + 4, MT_RXD1_NORMAL_GROUP_3);
    putLe32(rawDeauth.data() + 12, fieldPrep(MT_RXD3_NORMAL_CH_FREQ, 116));
    putLe32(rawDeauth.data() + 28, fieldPrep(MT_PRXV_RCPI0, rssiToRcpi(-45)));
    memcpy(rawDeauth.data() + 32, deauth, sizeof(deauth));

    uint32_t preDeauthTransitions = listener.linkTransitions;
    bus.injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, rawDeauth.data(), rawDeauth.size());
    bus.injectRxPacket(MT_RX_EVENT_RING_BASE, MT_INT_RX_DONE_WM, rawDeauth.data(), rawDeauth.size());
    dev.serviceInterrupt();
    TEST_CHECK(sta.link().state == kStaInit && !listener.linkUp &&
                   listener.lastDeauth == 7 &&
                   listener.linkTransitions == preDeauthTransitions + 1,
               "Duplicate AP Deauth must transition link down exactly once without re-entrancy");
    dev.mac().releaseAllTokens();
}

static void inject8023QosRx2(MockMT7902AsicBus &bus, const uint8_t dstMac[6], const uint8_t srcMac[6],
                             uint16_t seqNo, uint8_t tid, uint8_t amsduFmt, uint16_t tagId) {
    uint8_t eth[60] = {};
    memcpy(eth + 0, dstMac, 6);
    memcpy(eth + 6, srcMac, 6);
    putBe16(eth + 12, 0x0800);
    putLe16(eth + 14, tagId);

    // Base RXD (24B) + Group 4 (16B) + Group 3 (8B) + Ethernet payload
    std::vector<uint8_t> raw(24 + 16 + 8 + sizeof(eth), 0);
    putLe32(raw.data() + 0, fieldPrep(MT_RXD0_LENGTH, static_cast<uint32_t>(raw.size())) |
                            fieldPrep(MT_RXD0_PKT_TYPE, PKT_TYPE_NORMAL));
    putLe32(raw.data() + 4, fieldPrep(MT_RXD1_NORMAL_WLAN_IDX, 1) |
                            MT_RXD1_NORMAL_GROUP_4 | MT_RXD1_NORMAL_GROUP_3 |
                            fieldPrep(MT_RXD1_NORMAL_SEC_MODE, 5));
    putLe32(raw.data() + 8, MT_RXD2_NORMAL_HDR_TRANS | fieldPrep(MT_RXD2_NORMAL_TID, tid));
    putLe32(raw.data() + 12, fieldPrep(MT_RXD3_NORMAL_CH_FREQ, 116) |
                             fieldPrep(MT_RXD3_NORMAL_ADDR_TYPE, MT_RXD3_NORMAL_U2M));
    putLe32(raw.data() + 16, fieldPrep(MT_RXD4_NORMAL_PAYLOAD_FORMAT, amsduFmt));
    // Group 4 at offset 24: v0 = FC (QoS Data = 0x0288 FromDS), v2 = seqCtrl | (qosCtrl << 16)
    putLe32(raw.data() + 24, fieldPrep(MT_RXD6_FRAME_CONTROL, 0x0288U));
    putLe32(raw.data() + 32, fieldPrep(MT_RXD8_SEQ_CTRL, static_cast<uint32_t>((seqNo & 0x0fffU) << 4)) |
                             fieldPrep(MT_RXD8_QOS_CTL, static_cast<uint32_t>(tid & 0x0fU)));
    // Group 3 at offset 40: HE_SU MCS 7, 80 MHz, RSSI -60 dBm
    putLe32(raw.data() + 40, fieldPrep(MT_PRXV_TX_MODE, MT_PHY_TYPE_HE_SU) |
                             fieldPrep(MT_PRXV_TX_RATE, 7) |
                             fieldPrep(MT_PRXV_FRAME_MODE, CMD_CBW_80MHZ));
    putLe32(raw.data() + 44, fieldPrep(MT_PRXV_RCPI0, rssiToRcpi(-60)));
    memcpy(raw.data() + 48, eth, sizeof(eth));
    bus.injectRxPacket(MT_RX_DATA_RING_BASE, MT_INT_RX_DONE_DATA, raw.data(), raw.size());
}

static void testRxReorderAmsduTxAddbaAndBatching(MockMT7902AsicBus &bus, MT7902Device &dev) {
    printf("[11/11] Testing RX A-MPDU reorder (OOO, A-MSDU, wrap, BAR, timeout), TX ADDBA & DMA batching...\n");

    TEST_CHECK(dev.dma().ring(kRxData).ndesc == 1024, "Expected 1024-entry RX data ring");
    TEST_CHECK(dev.dma().ring(kTxBand0).ndesc == 1024, "Expected 1024-entry TX data ring");
    TEST_CHECK(dev.dma().txFreeCount(kTxBand0) == 1023 && dev.mac().freeTokens() == MT7902_TOKEN_SIZE,
               "Initial TX ring/token free counts mismatch");

    MT7902Sta sta(dev);
    MockStaListener listener;
    sta.setListener(&listener);
    TEST_CHECK(sta.setPower(true) == kOk, "sta.setPower failed");

    // Associate to a 5 GHz 80 MHz open AP
    const uint8_t apBssid[6] = { 0x7c, 0x7b, 0x68, 0xaa, 0xbb, 0xcc };
    const char *ssidStr = "HighSpeed5G";
    std::vector<uint8_t> beacon(36, 0);
    putLe16(beacon.data() + 0, 0x0080);
    memset(beacon.data() + 4, 0xff, 6);
    memcpy(beacon.data() + 10, apBssid, 6);
    memcpy(beacon.data() + 16, apBssid, 6);
    putLe16(beacon.data() + 32, 100);
    putLe16(beacon.data() + 34, 0x0401);
    beacon.push_back(0); beacon.push_back(static_cast<uint8_t>(strlen(ssidStr)));
    beacon.insert(beacon.end(), ssidStr, ssidStr + strlen(ssidStr));
    beacon.push_back(61); beacon.push_back(22); beacon.push_back(116); beacon.push_back(0x05);
    beacon.insert(beacon.end(), 20, 0);
    beacon.push_back(191); beacon.push_back(12); beacon.insert(beacon.end(), 12, 0);
    beacon.push_back(192); beacon.push_back(5); beacon.push_back(1); beacon.push_back(122);
    beacon.push_back(0); beacon.push_back(0); beacon.push_back(0);
    beacon.push_back(255); beacon.push_back(4); beacon.push_back(35);
    beacon.push_back(0x05); beacon.push_back(0); beacon.push_back(0);

    inject80211FrameOnRx2(bus, beacon.data(), beacon.size(), 116, -55);
    dev.serviceInterrupt();

    bus.sawWtblSmps = false;
    AssocRequestParams areq;
    areq.ssidLen = static_cast<uint8_t>(strlen(ssidStr));
    memcpy(areq.ssid, ssidStr, areq.ssidLen);
    memcpy(areq.bssid, apBssid, 6);
    areq.hasBssid = true;
    areq.authLower = 1;
    areq.authUpper = 0;
    TEST_CHECK(sta.associate(areq) == kOk, "associate failed");

    uint8_t authResp[30] = {};
    putLe16(authResp + 0, 0x00b0);
    memcpy(authResp + 4, dev.capability().macAddr, 6);
    memcpy(authResp + 10, apBssid, 6);
    memcpy(authResp + 16, apBssid, 6);
    putLe16(authResp + 26, 2);
    inject80211FrameOnRx2(bus, authResp, sizeof(authResp), 116, -55);
    dev.serviceInterrupt();

    uint8_t assocResp[32] = {};
    putLe16(assocResp + 0, 0x0010);
    memcpy(assocResp + 4, dev.capability().macAddr, 6);
    memcpy(assocResp + 10, apBssid, 6);
    memcpy(assocResp + 16, apBssid, 6);
    putLe16(assocResp + 24, 0x0401);
    putLe16(assocResp + 28, 0xc009);
    inject80211FrameOnRx2(bus, assocResp, sizeof(assocResp), 116, -55);
    dev.serviceInterrupt();
    TEST_CHECK(sta.link().state == kStaRun, "Expected kStaRun");
    TEST_CHECK(bus.sawWtblSmps, "WTBL_SMPS TLV must be sent in uniUpdateSta");

    // 1. AP sends RX ADDBA Request (TID=0, SSN=4094, win=64, amsdu=1)
    uint8_t addbaReq[33] = {};
    putLe16(addbaReq + 0, 0x00d0);
    memcpy(addbaReq + 4, dev.capability().macAddr, 6);
    memcpy(addbaReq + 10, apBssid, 6);
    memcpy(addbaReq + 16, apBssid, 6);
    addbaReq[24] = 3; addbaReq[25] = 0; addbaReq[26] = 5;
    putLe16(addbaReq + 27, static_cast<uint16_t>((64U << 6) | (0U << 2) | (1U << 1) | 1U));
    putLe16(addbaReq + 31, static_cast<uint16_t>(4094U << 4));
    inject80211FrameOnRx2(bus, addbaReq, sizeof(addbaReq), 116, -55);
    dev.serviceInterrupt();

    // 2. Inject out-of-order burst across 12-bit SN wrap (4094 -> 4095[A-MSDU 3 subframes] -> 0 -> 1) + duplicate 4094
    // Arrival order: 4094 (in-order), 0 (OOO), 1 (OOO), 4094 (dup), 4095(FIRST/MID/LAST A-MSDU)
    listener.rxFrames.clear();
    uint32_t refillsBefore = bus.rx2RefillWrites;
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 4094, 0, 0, 1001);
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 0,    0, 0, 1005);
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 1,    0, 0, 1006);
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 4094, 0, 0, 9999); // dup!
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 4095, 0, MT_RXD4_FIRST_AMSDU_FRAME, 1002);
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 4095, 0, MT_RXD4_MID_AMSDU_FRAME,   1003);
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 4095, 0, MT_RXD4_LAST_AMSDU_FRAME,  1004);
    dev.serviceInterrupt();

    // Verify batched RX refill: 7 packets must require only 1 BAR0 CPU_IDX refill write!
    TEST_CHECK(bus.rx2RefillWrites - refillsBefore == 1,
               "Expected 1 batched RX refill write for 7 packets, got %u",
               bus.rx2RefillWrites - refillsBefore);

    // Verify exact reassembled order: 1001, 1002, 1003, 1004, 1005, 1006 (dup 9999 dropped)
    TEST_CHECK(listener.rxFrames.size() == 6,
               "Expected 6 reordered frames, got %zu", listener.rxFrames.size());
    for (size_t i = 0; i < 6; ++i) {
        uint16_t got = getLe16(listener.rxFrames[i].data() + 14);
        uint16_t exp = static_cast<uint16_t>(1001 + i);
        TEST_CHECK(got == exp, "Reorder idx %zu: expected tag %u, got %u", i, exp, got);
    }
    TEST_CHECK(sta.rxReorderStats().dupsDropped == 1, "Duplicate frame was not dropped");

    // 3. Test OOO multi-subframe A-MSDU + timeout release (seq 2 missing, seq 3 is 2-subframe A-MSDU)
    listener.rxFrames.clear();
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 3, 0, MT_RXD4_FIRST_AMSDU_FRAME, 2001);
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 3, 0, MT_RXD4_LAST_AMSDU_FRAME,  2002);
    dev.serviceInterrupt();
    TEST_CHECK(listener.rxFrames.empty() && sta.hasPendingReorderFrames(),
               "OOO A-MSDU seq 3 must wait for missing seq 2");

    bus.sleepMs(100);
    sta.pollRxReorderTimeouts();
    TEST_CHECK(listener.rxFrames.size() == 2 &&
                   getLe16(listener.rxFrames[0].data() + 14) == 2001 &&
                   getLe16(listener.rxFrames[1].data() + 14) == 2002 &&
                   !sta.hasPendingReorderFrames(),
               "Timeout flush did not release buffered OOO A-MSDU subframes in order");

    // 4. Test BAR Control frame flushing buffered seq 6 when seq 4 & 5 are missing
    listener.rxFrames.clear();
    inject8023QosRx2(bus, dev.capability().macAddr, apBssid, 6, 0, 0, 3001);
    dev.serviceInterrupt();
    TEST_CHECK(listener.rxFrames.empty(), "Seq 6 should buffer while waiting for seq 4");

    uint8_t bar[20] = {};
    putLe16(bar + 0, 0x0084); // Control: Block Ack Request
    memcpy(bar + 4, dev.capability().macAddr, 6);
    memcpy(bar + 10, apBssid, 6);
    putLe16(bar + 16, 0x0004); // TID=0, Compressed BAR
    putLe16(bar + 18, static_cast<uint16_t>(6U << 4)); // SSN=6
    inject80211FrameOnRx2(bus, bar, sizeof(bar), 116, -55);
    dev.serviceInterrupt();
    TEST_CHECK(listener.rxFrames.size() == 1 && getLe16(listener.rxFrames[0].data() + 14) == 3001,
               "BAR control frame must advance head and deliver buffered seq 6");

    // 5. Test TX Originator ADDBA Request -> ADDBA Response -> STA_REC_BA(tx=true) + MT_TXD7_HW_AMSDU + batched TX doorbell
    bus.capturedBand0TxPayloads.clear();
    bus.capturedBand0Txwis.clear();
    bus.sawStaRecBaTx = false;
    uint8_t txEth[64] = {};
    memcpy(txEth + 0, apBssid, 6);
    memcpy(txEth + 6, dev.capability().macAddr, 6);
    putBe16(txEth + 12, 0x0800);

    // First unicast data frame triggers automatic TX ADDBA Request
    TEST_CHECK(sta.outputEthernetPacket(txEth, sizeof(txEth), true) == kOk, "First unicast TX failed");
    TEST_CHECK(bus.capturedBand0TxPayloads.size() == 2 &&
                   bus.capturedBand0TxPayloads[0].size() == 33 &&
                   bus.capturedBand0TxPayloads[0][24] == 3 &&
                   bus.capturedBand0TxPayloads[0][25] == 0,
               "Expected automatic TX ADDBA Request before first unicast QoS data frame");
    uint8_t txAddbaTok = bus.capturedBand0TxPayloads[0][26];

    // AP replies with ADDBA Response (status=0, tid=0, win=64, amsdu=1)
    uint8_t addbaResp[33] = {};
    putLe16(addbaResp + 0, 0x00d0);
    memcpy(addbaResp + 4, dev.capability().macAddr, 6);
    memcpy(addbaResp + 10, apBssid, 6);
    memcpy(addbaResp + 16, apBssid, 6);
    addbaResp[24] = 3; addbaResp[25] = 1; addbaResp[26] = txAddbaTok;
    putLe16(addbaResp + 27, 0); // Status = Success
    putLe16(addbaResp + 29, static_cast<uint16_t>((64U << 6) | (0U << 2) | (1U << 1) | 1U));
    inject80211FrameOnRx2(bus, addbaResp, sizeof(addbaResp), 116, -55);
    dev.serviceInterrupt();
    TEST_CHECK(bus.sawStaRecBaTx && (sta.txBaActiveMask() & 1U) != 0,
               "ADDBA Response must activate TX Originator BA in hardware");

    // Batched TX: submit 4 frames with kick=false, then 1 flushTx() -> exactly 1 TX0 doorbell write + HW_AMSDU set!
    bus.capturedBand0Txwis.clear();
    uint32_t txKicksBefore = bus.tx0DoorbellWrites;
    for (int i = 0; i < 4; ++i)
        TEST_CHECK(sta.outputEthernetPacket(txEth, sizeof(txEth), false) == kOk, "Batched TX failed");
    TEST_CHECK(bus.tx0DoorbellWrites == txKicksBefore, "kick=false must not ring TX0 doorbell prematurely");
    sta.flushTx();
    TEST_CHECK(bus.tx0DoorbellWrites == txKicksBefore + 1, "flushTx() must ring TX0 doorbell once");
    TEST_CHECK(bus.capturedBand0Txwis.size() == 4, "Expected 4 batched TXWIs processed by single doorbell");
    TEST_CHECK((getLe32(&bus.capturedBand0Txwis[0].txd[7]) & MT_TXD7_HW_AMSDU) != 0,
               "TXWI DW7 must set MT_TXD7_HW_AMSDU once TX BA with A-MSDU is active");

    (void)sta.disassociate(8);
    dev.mac().releaseAllTokens();
}

static void testMacOSWclWireCodecs() {
    printf("[TEST 6/7] Verifying macOS Sequoia 15.2 / Tahoe 26 native WCL wire structures...\n");
    std::vector<uint8_t> scanBuf(0x58 + 2 * 12, 0);
    putLe32(scanBuf.data() + 0x00, 42); // identifier
    putLe32(scanBuf.data() + 0x1c, 6);  // ssidLen
    memcpy(scanBuf.data() + 0x20, "MacWiF", 6);
    putLe32(scanBuf.data() + 0x40, 1);  // scan type
    putLe32(scanBuf.data() + 0x54, 2);  // 2 channels
    putLe32(scanBuf.data() + 0x58 + 4, 6);
    putLe32(scanBuf.data() + 0x58 + 8, 0x0008); // 2.4 GHz band flag
    putLe32(scanBuf.data() + 0x58 + 12 + 4, 36);
    putLe32(scanBuf.data() + 0x58 + 12 + 8, 0x2010); // 5 GHz band flag (0x2000 | 0x10)

    wcl::DecodedScanRequest dscan;
    TEST_CHECK(wcl::decodeScan(scanBuf.data(), scanBuf.size(), dscan), "wcl::decodeScan failed");
    TEST_CHECK(dscan.identifier == 42 && dscan.ssidLength == 6, "WCL scan header mismatch");
    TEST_CHECK((dscan.channels[6 / 8] & (1U << (6 % 8))) != 0, "WCL ch 6 bit missing");
    TEST_CHECK((dscan.channels[36 / 8] & (1U << (36 % 8))) != 0, "WCL ch 36 bit missing");

    std::vector<uint8_t> joinBuf(0x218 + 18, 0);
    putLe32(joinBuf.data() + 0x10, 1);  // lowerAuth = OPEN
    putLe32(joinBuf.data() + 0x14, 8);  // upperAuth = WPA2_PSK
    putLe32(joinBuf.data() + 0x1c, 6);  // ssidLen
    memcpy(joinBuf.data() + 0x20, "MacWiF", 6);
    putLe16(joinBuf.data() + 0xd4, 4);  // rsnLen
    putLe32(joinBuf.data() + 0x214, 1); // BSSID count = 1
    const uint8_t bssid[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
    memcpy(joinBuf.data() + 0x21c, bssid, 6);
    putLe16(joinBuf.data() + 0x228, 0xd024); // 5GHz ch 36

    wcl::DecodedJoinRequest djoin;
    TEST_CHECK(wcl::decodeJoin(joinBuf.data(), joinBuf.size(), djoin), "wcl::decodeJoin failed");
    TEST_CHECK(djoin.upperAuth == 8 && (djoin.channelSpec & 0xffU) == 36, "WCL join decode mismatch");

    wcl::NativeChannelInfo nci = {};
    TEST_CHECK(wcl::appendNativeChannel(nci, 1, false, false, false, true, false), "append 2G ch failed");
    TEST_CHECK(wcl::appendNativeChannel(nci, 36, true, false, false, true, true), "append 5G ch failed");
    TEST_CHECK(nci.count == 2 && nci.channelSpec[1] == 0xd024U, "NativeChannelInfo spec mismatch");
}

static void testUsbBluetoothFirmwareLoader() {
    printf("[TEST 7/7] Simulating full MT7902 USB Bluetooth 79xx WMT firmware load (13d3:3579)...\n");
    MockBtUsbTransport mockUsb;
    int err = bt::MT7902BtLoader::setupDevice(mockUsb);
    TEST_CHECK(err == kOk, "MT7902BtLoader::setupDevice failed with %d", err);
    TEST_CHECK(mockUsb.sectionDescriptorsSent >= 2, "Expected >=2 BT section descriptors");
    TEST_CHECK(mockUsb.downloadedPayloadBytes > 400000, "Expected >400 KB BT RAM payload, got %zu",
               mockUsb.downloadedPayloadBytes);
    TEST_CHECK(mockUsb.wmtChunksSent > 1600, "Expected >1600 WMT 250B chunks, got %u",
               mockUsb.wmtChunksSent);
    TEST_CHECK(mockUsb.uhwWrites.size() == 1 &&
               mockUsb.uhwWrites[0].first == bt::MTK_EP_RST_OPT &&
               mockUsb.uhwWrites[0].second == bt::MTK_EP_RST_IN_OUT_OPT,
               "USB endpoint reset (0x74011890 = 0x00010001) missing");
    TEST_CHECK(mockUsb.funcCtrlCalled, "BTMTK_WMT_FUNC_CTRL enable not sent");
}

static void testWclWpa2FourWayHandshakeEndToEnd(MockMT7902AsicBus &bus, MT7902Device &dev) {
    printf("[TEST 8/8] Verifying in-kernel WPA2-PSK 4-Way EAPOL + RFC 3394 Key Wrap + Transition IE...\n");

    // 1. RFC 3394 AES-128 Key Wrap / Unwrap test vector (128-bit KEK, 128-bit Key Data)
    const uint8_t rfcKek[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    const uint8_t rfcPlain[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    const uint8_t rfcExpectedWrap[24] = {
        0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,0xae,0xf3,0x4b,0xd8,
        0xfb,0x5a,0x7b,0x82,0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5
    };
    uint8_t wrapped[32] = {};
    size_t wrappedLen = 0;
    TEST_CHECK(MT7902Wpa2::aesKeyWrap(rfcKek, rfcPlain, 16, wrapped, wrappedLen) && wrappedLen == 24,
               "RFC 3394 aesKeyWrap failed");
    TEST_CHECK(memcmp(wrapped, rfcExpectedWrap, 24) == 0, "RFC 3394 ciphertext mismatch");

    uint8_t unwrapped[32] = {};
    size_t unwrappedLen = 0;
    TEST_CHECK(MT7902Wpa2::aesKeyUnwrap(rfcKek, wrapped, wrappedLen, unwrapped, unwrappedLen) && unwrappedLen == 16,
               "RFC 3394 aesKeyUnwrap failed");
    TEST_CHECK(memcmp(unwrapped, rfcPlain, 16) == 0, "RFC 3394 plaintext mismatch");

    // 2. WPA2/WPA3 Transition-mode beacon IE downgrade (strip SAE + RSNXE)
    uint8_t transIes[] = {
        48, 24, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,                         // Group CCMP
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,             // Pairwise CCMP
        0x02, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x0f, 0xac, 0x08, // PSK + SAE
        0x80, 0x00,                                     // MFPC=1, MFPR=0
        244, 1, 0x20                                    // RSNXE (SAE H2E)
    };
    size_t downgradedLen = wcl::selectWPA2TransitionMode(transIes, sizeof(transIes));
    TEST_CHECK(downgradedLen == 22 && transIes[0] == 48 && transIes[1] == 20 && transIes[14] == 1,
               "selectWPA2TransitionMode failed (got len=%zu)", downgradedLen);

    // 3. Full MT7902Sta WCL PMK association -> EAPOL Msg 1/2/3/4 -> hardware PTK/GTK -> kStaRun
    MT7902Sta sta(dev);
    MockStaListener listener;
    sta.setListener(&listener);
    TEST_CHECK(sta.setPower(true) == kOk, "sta.setPower failed");

    const uint8_t apBssid[6] = { 0x06, 0x22, 0x33, 0x44, 0x55, 0x66 };
    const char *ssidStr = "SequoiaWCLTest";
    const uint8_t rsnIe[] = {
        48, 20, 0x01, 0x00,
        0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
        0x01, 0x00, 0x00, 0x0f, 0xac, 0x02,
        0x00, 0x00
    };
    std::vector<uint8_t> beacon(36, 0);
    putLe16(beacon.data() + 0, 0x0080);
    memset(beacon.data() + 4, 0xff, 6);
    memcpy(beacon.data() + 10, apBssid, 6);
    memcpy(beacon.data() + 16, apBssid, 6);
    putLe16(beacon.data() + 32, 100);
    putLe16(beacon.data() + 34, 0x0431);
    beacon.push_back(0);
    beacon.push_back(static_cast<uint8_t>(strlen(ssidStr)));
    beacon.insert(beacon.end(), ssidStr, ssidStr + strlen(ssidStr));
    beacon.push_back(3); beacon.push_back(1); beacon.push_back(6);
    beacon.insert(beacon.end(), rsnIe, rsnIe + sizeof(rsnIe));
    inject80211FrameOnRx2(bus, beacon.data(), beacon.size(), 6, -42);
    dev.serviceInterrupt();

    const uint8_t pmk[32] = {
        0xa1,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,
        0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8
    };
    AssocRequestParams areq;
    areq.ssidLen = static_cast<uint8_t>(strlen(ssidStr));
    memcpy(areq.ssid, ssidStr, areq.ssidLen);
    memcpy(areq.bssid, apBssid, 6);
    areq.hasBssid = true;
    areq.authLower = 1;
    areq.authUpper = 8;
    memcpy(areq.pmk, pmk, 32);
    areq.hasPmk = true;
    TEST_CHECK(sta.associate(areq) == kOk, "WCL PMK associate failed");

    uint8_t authResp[30] = {};
    putLe16(authResp + 0, 0x00b0);
    memcpy(authResp + 4, dev.capability().macAddr, 6);
    memcpy(authResp + 10, apBssid, 6);
    memcpy(authResp + 16, apBssid, 6);
    putLe16(authResp + 26, 2);
    inject80211FrameOnRx2(bus, authResp, sizeof(authResp), 6);
    dev.serviceInterrupt();

    uint8_t assocResp[32] = {};
    putLe16(assocResp + 0, 0x0010);
    memcpy(assocResp + 4, dev.capability().macAddr, 6);
    memcpy(assocResp + 10, apBssid, 6);
    memcpy(assocResp + 16, apBssid, 6);
    putLe16(assocResp + 24, 0x0431);
    putLe16(assocResp + 28, 0xc001);
    inject80211FrameOnRx2(bus, assocResp, sizeof(assocResp), 6);
    dev.serviceInterrupt();
    TEST_CHECK(sta.link().state == kStaAssociated && !listener.linkUp, "Expected kStaAssociated");

    // Simulate AP sending EAPOL-Key Msg 1/4 (Pairwise=1, Ack=1, Ver=2 -> keyInfo = 0x008a)
    bus.capturedBand0TxPayloads.clear();
    uint8_t anonce[32];
    for (int i = 0; i < 32; ++i) anonce[i] = static_cast<uint8_t>(0x50 + i);

    uint8_t msg1[14 + 99] = {};
    memcpy(msg1 + 0, dev.capability().macAddr, 6);
    memcpy(msg1 + 6, apBssid, 6);
    putBe16(msg1 + 12, 0x888e);
    uint8_t *e1 = msg1 + 14;
    e1[0] = 2; e1[1] = 3; putBe16(e1 + 2, 95);
    e1[4] = 2;
    putBe16(e1 + 5, 0x008a); // KeyType=1, Ack=1, Ver=2
    putBe16(e1 + 7, 16);
    e1[16] = 1; // Replay counter = 1
    memcpy(e1 + 17, anonce, 32);

    ParsedRxFrame rxMeta = {};
    rxMeta.valid = true;
    rxMeta.is8023 = true;
    rxMeta.rssi = -42;
    sta.onRxEthernetFrame(msg1, sizeof(msg1), rxMeta);
    // Simulate AP retransmitting Msg 1/4 with identical ANonce — STA must reuse SNonce so Msg 3/4 MIC matches!
    sta.onRxEthernetFrame(msg1, sizeof(msg1), rxMeta);

    TEST_CHECK(bus.capturedBand0TxPayloads.size() == 2, "STA did not transmit EAPOL Msg 2/4 on retry");
    const auto &msg2Eth = bus.capturedBand0TxPayloads[0];
    TEST_CHECK(msg2Eth.size() >= 14 + 99 && getBe16(msg2Eth.data() + 12) == 0x888e, "Invalid Msg 2/4 eth hdr");
    TEST_CHECK(memcmp(bus.capturedBand0TxPayloads[0].data() + 14 + 17,
                      bus.capturedBand0TxPayloads[1].data() + 14 + 17, 32) == 0,
               "Retransmitted Msg 1/4 must reuse identical SNonce");
    const uint8_t *snonce = msg2Eth.data() + 14 + 17;

    // Derive expected PTK on AP side and verify STA's Msg 2/4 MIC
    Wpa2Ptk apPtk = {};
    MT7902Wpa2::derivePtk(pmk, apBssid, dev.capability().macAddr, anonce, snonce, 2, apPtk);
    std::vector<uint8_t> msg2Copy(msg2Eth.begin() + 14, msg2Eth.end());
    uint8_t rxMic2[16], calcMic2[16];
    memcpy(rxMic2, msg2Copy.data() + 81, 16);
    memset(msg2Copy.data() + 81, 0, 16);
    TEST_CHECK(MT7902Wpa2::computeKeyMic(apPtk.kck, 2, msg2Copy.data(), msg2Copy.size(), calcMic2) &&
               memcmp(rxMic2, calcMic2, 16) == 0, "STA Msg 2/4 MIC verification failed on AP");

    // Build AP EAPOL-Key Msg 3/4 with AES-Key-Wrapped GTK KDE + RSN IE
    const uint8_t apGtk[16] = { 0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
    uint8_t plainKeyData[48] = {};
    size_t kdLen = 0;
    memcpy(plainKeyData + kdLen, rsnIe, sizeof(rsnIe));
    kdLen += sizeof(rsnIe);
    // GTK KDE: dd 16 00 0f ac 01 <keyid=1> 00 <16B GTK>
    plainKeyData[kdLen++] = 0xdd;
    plainKeyData[kdLen++] = 22;
    plainKeyData[kdLen++] = 0x00; plainKeyData[kdLen++] = 0x0f;
    plainKeyData[kdLen++] = 0xac; plainKeyData[kdLen++] = 0x01;
    plainKeyData[kdLen++] = 0x01; // KeyIdx = 1
    plainKeyData[kdLen++] = 0x00;
    memcpy(plainKeyData + kdLen, apGtk, 16);
    kdLen += 16;
    plainKeyData[kdLen++] = 0xdd; // IEEE 802.11i pad to multiple of 8 (44 -> 48)
    while (kdLen % 8 != 0) plainKeyData[kdLen++] = 0x00;

    uint8_t encKeyData[64] = {};
    size_t encKdLen = 0;
    TEST_CHECK(MT7902Wpa2::aesKeyWrap(apPtk.kek, plainKeyData, kdLen, encKeyData, encKdLen), "AP wrap failed");

    std::vector<uint8_t> msg3(14 + 99 + encKdLen, 0);
    memcpy(msg3.data() + 0, dev.capability().macAddr, 6);
    memcpy(msg3.data() + 6, apBssid, 6);
    putBe16(msg3.data() + 12, 0x888e);
    uint8_t *e3 = msg3.data() + 14;
    e3[0] = 2; e3[1] = 3; putBe16(e3 + 2, static_cast<uint16_t>(95 + encKdLen));
    e3[4] = 2;
    // KeyType=1(0x08) | Install=1(0x40) | Ack=1(0x80) | MIC=1(0x100) | Secure=1(0x200) | EncKeyData=1(0x1000) | Ver=2
    putBe16(e3 + 5, 0x13ca);
    putBe16(e3 + 7, 16);
    e3[16] = 2; // Replay counter = 2
    memcpy(e3 + 17, anonce, 32);
    putBe16(e3 + 97, static_cast<uint16_t>(encKdLen));
    memcpy(e3 + 99, encKeyData, encKdLen);
    uint8_t mic3[16];
    TEST_CHECK(MT7902Wpa2::computeKeyMic(apPtk.kck, 2, e3, 99 + encKdLen, mic3), "AP Msg 3 MIC failed");
    memcpy(e3 + 81, mic3, 16);

    bus.capturedBand0TxPayloads.clear();
    sta.onRxEthernetFrame(msg3.data(), msg3.size(), rxMeta);

    TEST_CHECK(bus.capturedBand0TxPayloads.size() == 1, "STA did not transmit EAPOL Msg 4/4");
    TEST_CHECK(sta.link().ptkInstalled && sta.link().gtkInstalled, "PTK/GTK not installed after Msg 3/4");
    TEST_CHECK(sta.link().state == kStaRun && listener.linkUp, "STA did not reach kStaRun after 4-way EAPOL");

    (void)sta.disassociate(8);
    dev.mac().releaseAllTokens();
}

static void testBluetoothPatcherLogic() {
    printf("[9/9] Testing MT7902BluetoothPatcher HCI scan state machine & VM bypass...\n");
    using namespace mt7902::bt;

    // 1. USB VID/PID allowlist
    TEST_CHECK(isMT7902BtUsbId(0x13D3, 0x3579), "AzureWave MT7902 BT 13d3:3579 must match");
    TEST_CHECK(isMT7902BtUsbId(0x0489, 0xE0F2), "Foxconn MT7902 BT 0489:e0f2 must match");
    TEST_CHECK(isMT7902BtUsbId(0x0E8D, 0x7902), "MediaTek MT7902 BT 0e8d:7902 must match");
    TEST_CHECK(!isMT7902BtUsbId(0x05AC, 0x8290), "Apple BT USB must not match");
    TEST_CHECK(!isMT7902BtUsbId(0x0627, 0x0001), "QEMU USB tablet must not match");

    // 2. Class-OUT control request check
    TEST_CHECK(isClassOutControlRequest(0x20), "Device Class-OUT must match");
    TEST_CHECK(isClassOutControlRequest(0x21), "Interface Class-OUT must match");
    TEST_CHECK(!isClassOutControlRequest(0xA0), "Class-IN must not match");
    TEST_CHECK(!isClassOutControlRequest(0xC0), "Vendor-IN must not match");
    TEST_CHECK(!isClassOutControlRequest(0x00), "Standard-OUT must not match");

    // 3. VM bypass process filter
    TEST_CHECK(isBluetoothVmBypassProcess("bluetoothd"), "bluetoothd must bypass VM check");
    TEST_CHECK(isBluetoothVmBypassProcess("BlueTool"), "BlueTool must bypass VM check");
    TEST_CHECK(!isBluetoothVmBypassProcess("kernel_task"), "kernel_task must not bypass VM check");
    TEST_CHECK(!isBluetoothVmBypassProcess("softwareupdated"), "softwareupdated must not bypass VM check");
    TEST_CHECK(!isBluetoothVmBypassProcess(nullptr), "nullptr must not match");

    // 4. Static Random Address HCI command payload (opcode 0x2005 LE, plen 6, MSB bits[7:6]==11)
    TEST_CHECK(sizeof(kStaticRandomAddressHciCmd) == 9, "HCI_LE_Set_Random_Address must be 9 bytes");
    TEST_CHECK(getLe16(kStaticRandomAddressHciCmd) == kHciOpLeSetRandomAddress, "Opcode must be 0x2005");
    TEST_CHECK(kStaticRandomAddressHciCmd[2] == 6, "Param length must be 6");
    TEST_CHECK((kStaticRandomAddressHciCmd[8] & 0xC0U) == 0xC0U, "Static random addr MSB bits [7:6] must be 11");

    // 5. State machine: inject once before first LE scan param, re-arm after HCI Reset
    HciRandomAddrPatcherState st;
    TEST_CHECK(!st.isRandomAddrSet(), "Initial state must be unset");
    TEST_CHECK(st.onOutgoingHciOpcode(kHciOpLeSetScanParam), "First LE_Set_Scan_Param must inject random addr");
    TEST_CHECK(st.isRandomAddrSet(), "State must be set after injection");
    TEST_CHECK(!st.onOutgoingHciOpcode(kHciOpLeSetScanParam), "Second LE_Set_Scan_Param must not re-inject");
    TEST_CHECK(!st.onOutgoingHciOpcode(kHciOpLeSetExtScanParam), "Ext scan param after set must not re-inject");
    TEST_CHECK(!st.onOutgoingHciOpcode(kHciOpReset), "HCI Reset itself must not inject");
    TEST_CHECK(!st.isRandomAddrSet(), "HCI Reset must clear random addr state");
    TEST_CHECK(st.onOutgoingHciOpcode(kHciOpLeSetExtScanParam), "First LE_Set_Ext_Scan_Param after reset must inject");
    st.reset();
    TEST_CHECK(!st.onOutgoingHciOpcode(kHciOpLeSetRandomAddress), "Host-sent LE_Set_Random_Address marks initialized");
    TEST_CHECK(!st.onOutgoingHciOpcode(kHciOpLeSetScanParam), "No duplicate injection when host already set random addr");
}

int main() {
    printf("======================================================================\n");
    printf(" MediaTek MT7902 macOS Native Driver — Hardware Simulation Test Suite\n");
    printf("======================================================================\n");

    testEmbeddedFirmwareIntegrity();
    testRegisterRemapping();

    {
        MockMT7902AsicBus mockBus;
        MT7902Device dev(mockBus);
        testFullDeviceProbeFwBootAndDmaQuirks(mockBus, dev);
        testTxwiTokensTxFreeAndRxdDecoding(mockBus, dev);
        testStaScanWpa2AssocKeyInstallAndData(mockBus, dev);
        testWclWpa2FourWayHandshakeEndToEnd(mockBus, dev);
        test5Ghz80MhzBaPostScanAndReentrancy(mockBus, dev);
        testRxReorderAmsduTxAddbaAndBatching(mockBus, dev);
        dev.shutdown();
    }

    testMacOSWclWireCodecs();
    testUsbBluetoothFirmwareLoader();
    testBluetoothPatcherLogic();

    printf("======================================================================\n");
    printf(" [PASS] All 11 MT7902 test suites completed with 0 errors!\n");
    printf("======================================================================\n");
    return 0;
}
