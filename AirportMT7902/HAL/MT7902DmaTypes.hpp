// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Wire-format WFDMA0 descriptor, Connac2 TXD, HW TXP, RXD, TXS, TX_FREE definitions

#ifndef MT7902_DMA_TYPES_HPP
#define MT7902_DMA_TYPES_HPP

#include "../../include/MT7902Common.hpp"

namespace mt7902 {

// WFDMA0 16-byte ring descriptor (little-endian on wire)
struct __attribute__((packed, aligned(4))) Mt76Desc {
    uint32_t buf0;
    uint32_t ctrl;
    uint32_t buf1;
    uint32_t info;
};
static_assert(sizeof(Mt76Desc) == 16, "WFDMA0 descriptor must be 16 bytes");

// Mt76Desc::ctrl bits
static constexpr uint32_t MT_DMA_CTL_SD_LEN1 = genmask32(13, 0);
static constexpr uint32_t MT_DMA_CTL_LAST_SEC1 = MTK_BIT(14);
static constexpr uint32_t MT_DMA_CTL_BURST = MTK_BIT(15);
static constexpr uint32_t MT_DMA_CTL_SD_LEN0 = genmask32(29, 16);
static constexpr uint32_t MT_DMA_CTL_LAST_SEC0 = MTK_BIT(30);
static constexpr uint32_t MT_DMA_CTL_DMA_DONE = MTK_BIT(31);
static constexpr uint32_t MT_DMA_CTL_TO_HOST_A = MTK_BIT(12);
static constexpr uint32_t MT_DMA_CTL_DROP = MTK_BIT(14);
static constexpr uint32_t MT_DMA_CTL_SDP0_H = genmask32(3, 0);
static constexpr uint32_t MT_DMA_CTL_SDP1_H = genmask32(19, 16);

// Ring register offsets inside each 16-byte WFDMA0 ring block
static constexpr uint32_t MT_RING_REG_DESC_BASE = 0x00;
static constexpr uint32_t MT_RING_REG_RING_SIZE = 0x04;
static constexpr uint32_t MT_RING_REG_CPU_IDX = 0x08;
static constexpr uint32_t MT_RING_REG_DMA_IDX = 0x0c;

// Connac2 TXD (32 bytes = 8 dwords)
static constexpr size_t MT_TXD_SIZE = 32;

enum TxHeaderFormat : uint8_t {
    MT_HDR_FORMAT_802_3 = 0,
    MT_HDR_FORMAT_CMD = 1,
    MT_HDR_FORMAT_802_11 = 2,
    MT_HDR_FORMAT_802_11_EXT = 3,
};

enum TxPktType : uint8_t {
    MT_TX_TYPE_CT = 0,  // Cut-through (PCIe data/mgmt)
    MT_TX_TYPE_SF = 1,  // Store-and-forward
    MT_TX_TYPE_CMD = 2, // MCU command
    MT_TX_TYPE_FW = 3,  // Firmware offload (beacon)
};

enum TxLmacQueue : uint8_t {
    MT_LMAC_AC00 = 0x00,
    MT_LMAC_AC01 = 0x01,
    MT_LMAC_AC02 = 0x02,
    MT_LMAC_AC03 = 0x03,
    MT_LMAC_ALTX0 = 0x10,
    MT_LMAC_BMC0 = 0x11,
    MT_LMAC_BCN0 = 0x12,
};

enum TxMcuPortQueue : uint8_t {
    MT_TX_MCU_PORT_RX_Q0 = 0x20,
    MT_TX_MCU_PORT_RX_FWDL = 0x3e,
};

enum TxPortIdx : uint8_t {
    MT_TX_PORT_IDX_LMAC = 0,
    MT_TX_PORT_IDX_MCU = 1,
};

// TXD DW0
static constexpr uint32_t MT_TXD0_Q_IDX = genmask32(31, 25);
static constexpr uint32_t MT_TXD0_PKT_FMT = genmask32(24, 23);
static constexpr uint32_t MT_TXD0_ETH_TYPE_OFFSET = genmask32(22, 16);
static constexpr uint32_t MT_TXD0_TX_BYTES = genmask32(15, 0);

// TXD DW1
static constexpr uint32_t MT_TXD1_LONG_FORMAT = MTK_BIT(31);
static constexpr uint32_t MT_TXD1_TGID = MTK_BIT(30);
static constexpr uint32_t MT_TXD1_OWN_MAC = genmask32(29, 24);
static constexpr uint32_t MT_TXD1_AMSDU = MTK_BIT(23);
static constexpr uint32_t MT_TXD1_TID = genmask32(22, 20);
static constexpr uint32_t MT_TXD1_HDR_PAD = genmask32(19, 18);
static constexpr uint32_t MT_TXD1_HDR_FORMAT = genmask32(17, 16);
static constexpr uint32_t MT_TXD1_HDR_INFO = genmask32(15, 11);
static constexpr uint32_t MT_TXD1_ETH_802_3 = MTK_BIT(15);
static constexpr uint32_t MT_TXD1_VTA = MTK_BIT(10);
static constexpr uint32_t MT_TXD1_WLAN_IDX = genmask32(9, 0);

// TXD DW2
static constexpr uint32_t MT_TXD2_FIX_RATE = MTK_BIT(31);
static constexpr uint32_t MT_TXD2_HTC_VLD = MTK_BIT(13);
static constexpr uint32_t MT_TXD2_BIP = MTK_BIT(11);
static constexpr uint32_t MT_TXD2_MULTICAST = MTK_BIT(10);
static constexpr uint32_t MT_TXD2_FRAME_TYPE = genmask32(5, 4);
static constexpr uint32_t MT_TXD2_SUB_TYPE = genmask32(3, 0);

// TXD DW3
static constexpr uint32_t MT_TXD3_SN_VALID = MTK_BIT(31);
static constexpr uint32_t MT_TXD3_SW_POWER_MGMT = MTK_BIT(29);
static constexpr uint32_t MT_TXD3_BA_DISABLE = MTK_BIT(28);
static constexpr uint32_t MT_TXD3_SEQ = genmask32(27, 16);
static constexpr uint32_t MT_TXD3_REM_TX_COUNT = genmask32(15, 11);
static constexpr uint32_t MT_TXD3_PROTECT_FRAME = MTK_BIT(1);
static constexpr uint32_t MT_TXD3_NO_ACK = MTK_BIT(0);

// TXD DW5
static constexpr uint32_t MT_TXD5_ADD_BA = MTK_BIT(14);
static constexpr uint32_t MT_TXD5_TX_STATUS_HOST = MTK_BIT(10);
static constexpr uint32_t MT_TXD5_PID = genmask32(7, 0);

// TXD DW6
static constexpr uint32_t MT_TXD6_TX_RATE = genmask32(29, 16);
static constexpr uint32_t MT_TXD6_FIXED_BW = MTK_BIT(2);

// TXD DW7
static constexpr uint32_t MT_TXD7_TYPE = genmask32(21, 20);
static constexpr uint32_t MT_TXD7_SUB_TYPE = genmask32(19, 16);
static constexpr uint32_t MT_TXD7_SPE_IDX = genmask32(15, 11);
static constexpr uint32_t MT_TXD7_HW_AMSDU = MTK_BIT(10);

// Hardware TXP appended after 32-byte TXD for PCIe MT7902 (is_mt76_fw_txp == false)
static constexpr size_t MT_HW_TXP_MAX_MSDU_NUM = 4;
static constexpr size_t MT_HW_TXP_MAX_BUF_NUM = 4;
static constexpr uint16_t MT_MSDU_ID_VALID = static_cast<uint16_t>(MTK_BIT(15));
static constexpr uint16_t MT_TXD_LEN_MASK = 0x0fffU;
static constexpr uint16_t MT_TXD_LEN_MSDU_LAST = static_cast<uint16_t>(MTK_BIT(14));
static constexpr uint16_t MT_TXD_LEN_LAST = static_cast<uint16_t>(MTK_BIT(15));

struct __attribute__((packed, aligned(4))) Mt76ConnacTxpPtr {
    uint32_t buf0;
    uint16_t len0;
    uint16_t len1;
    uint32_t buf1;
};

struct __attribute__((packed, aligned(4))) Mt76ConnacHwTxp {
    uint16_t msdu_id[MT_HW_TXP_MAX_MSDU_NUM];
    Mt76ConnacTxpPtr ptr[MT_HW_TXP_MAX_BUF_NUM / 2];
};
static_assert(sizeof(Mt76ConnacHwTxp) == 32, "Connac2 HW TXP must be 32 bytes");

struct __attribute__((packed, aligned(4))) Mt7902Txwi {
    uint32_t txd[8];
    Mt76ConnacHwTxp txp;
};
static_assert(sizeof(Mt7902Txwi) == 64, "MT7902 TXWI (TXD + HW_TXP) must be 64 bytes");

// RX Packet Types (RXD0[31:27])
enum RxPktType : uint8_t {
    PKT_TYPE_TXS = 0,
    PKT_TYPE_TXRXV = 1,
    PKT_TYPE_NORMAL = 2,
    PKT_TYPE_RX_DUP_RFB = 3,
    PKT_TYPE_RX_TMR = 4,
    PKT_TYPE_RETRIEVE = 5,
    PKT_TYPE_TXRX_NOTIFY = 6,
    PKT_TYPE_RX_EVENT = 7,
    PKT_TYPE_NORMAL_MCU = 8,
    PKT_TYPE_RX_FW_MONITOR = 0x0c,
};

// RXD DW0
static constexpr uint32_t MT_RXD0_LENGTH = genmask32(15, 0);
static constexpr uint32_t MT_RXD0_PKT_FLAG = genmask32(19, 16);
static constexpr uint32_t MT_RXD0_NORMAL_IP_SUM = MTK_BIT(23);
static constexpr uint32_t MT_RXD0_NORMAL_UDP_TCP_SUM = MTK_BIT(24);
static constexpr uint32_t MT_RXD0_PKT_TYPE = genmask32(31, 27);

// RXD DW1
static constexpr uint32_t MT_RXD1_NORMAL_WLAN_IDX = genmask32(9, 0);
static constexpr uint32_t MT_RXD1_NORMAL_GROUP_1 = MTK_BIT(11);
static constexpr uint32_t MT_RXD1_NORMAL_GROUP_2 = MTK_BIT(12);
static constexpr uint32_t MT_RXD1_NORMAL_GROUP_3 = MTK_BIT(13);
static constexpr uint32_t MT_RXD1_NORMAL_GROUP_4 = MTK_BIT(14);
static constexpr uint32_t MT_RXD1_NORMAL_GROUP_5 = MTK_BIT(15);
static constexpr uint32_t MT_RXD1_NORMAL_SEC_MODE = genmask32(20, 16);
static constexpr uint32_t MT_RXD1_NORMAL_KEY_ID = genmask32(22, 21);
static constexpr uint32_t MT_RXD1_NORMAL_CM = MTK_BIT(23);
static constexpr uint32_t MT_RXD1_NORMAL_CLM = MTK_BIT(24);
static constexpr uint32_t MT_RXD1_NORMAL_ICV_ERR = MTK_BIT(25);
static constexpr uint32_t MT_RXD1_NORMAL_TKIP_MIC_ERR = MTK_BIT(26);
static constexpr uint32_t MT_RXD1_NORMAL_FCS_ERR = MTK_BIT(27);
static constexpr uint32_t MT_RXD1_NORMAL_BAND_IDX = MTK_BIT(28);
static constexpr uint32_t MT_RXD1_NORMAL_SEC_DONE = MTK_BIT(31);

// RXD DW2
static constexpr uint32_t MT_RXD2_NORMAL_BSSID = genmask32(5, 0);
static constexpr uint32_t MT_RXD2_NORMAL_MAC_HDR_LEN = genmask32(12, 8);
static constexpr uint32_t MT_RXD2_NORMAL_HDR_TRANS = MTK_BIT(13);
static constexpr uint32_t MT_RXD2_NORMAL_HDR_OFFSET = genmask32(15, 14);
static constexpr uint32_t MT_RXD2_NORMAL_TID = genmask32(19, 16);
static constexpr uint32_t MT_RXD2_NORMAL_AMSDU_ERR = MTK_BIT(23);
static constexpr uint32_t MT_RXD2_NORMAL_MAX_LEN_ERROR = MTK_BIT(24);
static constexpr uint32_t MT_RXD2_NORMAL_FRAG = MTK_BIT(27);
static constexpr uint32_t MT_RXD2_NORMAL_NON_AMPDU = MTK_BIT(30);

// RXD DW3
static constexpr uint32_t MT_RXD3_NORMAL_CH_FREQ = genmask32(15, 8);
static constexpr uint32_t MT_RXD3_NORMAL_ADDR_TYPE = genmask32(17, 16);
static constexpr uint32_t MT_RXD3_NORMAL_U2M = MTK_BIT(0);

// RXD DW4
static constexpr uint32_t MT_RXD4_NORMAL_PAYLOAD_FORMAT = genmask32(1, 0);

// P-RXV DW0 / DW1 (Group 3)
static constexpr uint32_t MT_PRXV_TX_RATE = genmask32(6, 0);
static constexpr uint32_t MT_PRXV_NSTS = genmask32(9, 7);
static constexpr uint32_t MT_PRXV_FRAME_MODE = genmask32(14, 12);
static constexpr uint32_t MT_PRXV_HT_SGI = genmask32(16, 15);
static constexpr uint32_t MT_PRXV_HT_STBC = genmask32(23, 22);
static constexpr uint32_t MT_PRXV_TX_MODE = genmask32(27, 24);
static constexpr uint32_t MT_PRXV_RCPI0 = genmask32(7, 0);
static constexpr uint32_t MT_PRXV_RCPI1 = genmask32(15, 8);
static constexpr uint32_t MT_PRXV_RCPI2 = genmask32(23, 16);
static constexpr uint32_t MT_PRXV_RCPI3 = genmask32(31, 24);

static inline int8_t rcpiToRssi(uint32_t rcpi) {
    return static_cast<int8_t>((static_cast<int32_t>(rcpi) - 220) / 2);
}

static inline uint8_t rssiToRcpi(int32_t rssi) {
    int32_t v = 2 * rssi + 220;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return static_cast<uint8_t>(v);
}

// TX Free notification (PKT_TYPE_TXRX_NOTIFY)
struct __attribute__((packed, aligned(4))) Mt76ConnacTxFree {
    uint16_t rx_byte_cnt;
    uint16_t ctrl;
    uint32_t txd;
};
static_assert(sizeof(Mt76ConnacTxFree) == 8, "TX free header must be 8 bytes");

static constexpr uint32_t MT_TX_FREE_MSDU_CNT = genmask32(9, 0);
static constexpr uint32_t MT_TX_FREE_WLAN_ID = genmask32(23, 14);
static constexpr uint32_t MT_TX_FREE_COUNT = genmask32(12, 0);
static constexpr uint32_t MT_TX_FREE_STATUS = genmask32(14, 13);
static constexpr uint32_t MT_TX_FREE_MSDU_ID = genmask32(30, 16);
static constexpr uint32_t MT_TX_FREE_PAIR = MTK_BIT(31);

// TXS (PKT_TYPE_TXS)
static constexpr uint32_t MT_TXS0_TXS_FORMAT = genmask32(24, 23);
static constexpr uint32_t MT_TXS0_TX_RATE = genmask32(13, 0);
static constexpr uint32_t MT_TXS2_WCID = genmask32(25, 16);
static constexpr uint32_t MT_TXS3_PID = genmask32(31, 24);

static constexpr uint32_t MT_TX_RATE_STBC = MTK_BIT(13);
static constexpr uint32_t MT_TX_RATE_NSS = genmask32(12, 10);
static constexpr uint32_t MT_TX_RATE_MODE = genmask32(9, 6);
static constexpr uint32_t MT_TX_RATE_IDX = genmask32(5, 0);

enum MtPhyType : uint8_t {
    MT_PHY_TYPE_CCK = 0,
    MT_PHY_TYPE_OFDM = 1,
    MT_PHY_TYPE_HT = 2,
    MT_PHY_TYPE_HT_GF = 3,
    MT_PHY_TYPE_VHT = 4,
    MT_PHY_TYPE_HE_SU = 8,
    MT_PHY_TYPE_HE_EXT_SU = 9,
    MT_PHY_TYPE_HE_TB = 10,
    MT_PHY_TYPE_HE_MU = 11,
};

} // namespace mt7902

#endif // MT7902_DMA_TYPES_HPP
