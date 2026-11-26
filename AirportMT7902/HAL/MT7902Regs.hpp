// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2026 MediaTek MT7902 macOS Native Driver Project
// Register definitions derived from Linux mt76 (mt792x_regs.h, mt7921/regs.h, pci.c)

#ifndef MT7902_REGS_HPP
#define MT7902_REGS_HPP

#include "../../include/MT7902Common.hpp"

namespace mt7902 {

// PCIe MAC registers
static constexpr uint32_t MT_PCIE_MAC_BASE = 0x10000;
static constexpr uint32_t MT_PCIE_MAC_INT_ENABLE = MT_PCIE_MAC_BASE + 0x188;
static constexpr uint32_t MT_PCIE_MAC_PM = MT_PCIE_MAC_BASE + 0x194;
static constexpr uint32_t MT_PCIE_MAC_PM_L0S_DIS = MTK_BIT(8);

// Infra L1 dynamic remap window (maps 64 KiB physical pages to BAR0 + 0x40000)
static constexpr uint32_t MT_INFRA_CFG_BASE = 0xfe000;
static constexpr uint32_t MT_HIF_REMAP_L1 = MT_INFRA_CFG_BASE + 0x24c;
static constexpr uint32_t MT_HIF_REMAP_L1_MASK = genmask32(15, 0);
static constexpr uint32_t MT_HIF_REMAP_L1_OFFSET = genmask32(15, 0);
static constexpr uint32_t MT_HIF_REMAP_L1_BASE = genmask32(31, 16);
static constexpr uint32_t MT_HIF_REMAP_BASE_L1 = 0x40000;

// Chip identification & reset registers (accessed via L1 remap)
static constexpr uint32_t MT_HW_BOUND = 0x70010020;
static constexpr uint32_t MT_HW_CHIPID = 0x70010200;
static constexpr uint32_t MT_HW_REV = 0x70010204;

static constexpr uint32_t MT_WFSYS_SW_RST_B = 0x18000140;
static constexpr uint32_t WFSYS_SW_RST_B = MTK_BIT(0);
static constexpr uint32_t WFSYS_SW_INIT_DONE = MTK_BIT(4);

static constexpr uint32_t MT_TOP_BASE = 0x18060000;
static constexpr uint32_t MT_TOP_LPCR_HOST_BAND0 = MT_TOP_BASE + 0x10;
static constexpr uint32_t MT_TOP_LPCR_HOST_FW_OWN = MTK_BIT(0);
static constexpr uint32_t MT_TOP_LPCR_HOST_DRV_OWN = MTK_BIT(1);
static constexpr uint32_t MT_TOP_MISC = MT_TOP_BASE + 0xf0;
static constexpr uint32_t MT_TOP_MISC_FW_STATE = genmask32(2, 0);

static constexpr uint32_t MT_CONN_STATUS = 0x7c053c10;
static constexpr uint32_t MT_WIFI_PATCH_DL_STATE = MTK_BIT(0);

static constexpr uint32_t MT_CONN_ON_LPCTL = 0x7c060010;
static constexpr uint32_t PCIE_LPCR_HOST_SET_OWN = MTK_BIT(0);
static constexpr uint32_t PCIE_LPCR_HOST_CLR_OWN = MTK_BIT(1);
static constexpr uint32_t PCIE_LPCR_HOST_OWN_SYNC = MTK_BIT(2);

static constexpr uint32_t MT_CONN_ON_MISC = 0x7c0600f0;
static constexpr uint32_t MT_TOP_MISC2_FW_PWR_ON = MTK_BIT(0);
static constexpr uint32_t MT_TOP_MISC2_FW_N9_ON = MTK_BIT(1);
static constexpr uint32_t MT_TOP_MISC2_FW_N9_RDY = genmask32(1, 0);

static constexpr uint32_t MT_SWDEF_BASE = 0x41f200;
static constexpr uint32_t MT_SWDEF_MODE = MT_SWDEF_BASE + 0x3c;
static constexpr uint32_t MT_SWDEF_NORMAL_MODE = 0;

static constexpr uint32_t MT_MCU_WPDMA0_BASE = 0x54000000;
static constexpr uint32_t MT_WFDMA_DUMMY_CR = MT_MCU_WPDMA0_BASE + 0x120;
static constexpr uint32_t MT_WFDMA_NEED_REINIT = MTK_BIT(1);

// WFDMA0 registers (BAR0 direct: 0xd4000)
static constexpr uint32_t MT_WFDMA0_BASE = 0xd4000;
static constexpr uint32_t MT_WFDMA0(uint32_t ofs) { return MT_WFDMA0_BASE + ofs; }

static constexpr uint32_t MT_WFDMA0_RST = MT_WFDMA0(0x100);
static constexpr uint32_t MT_WFDMA0_RST_LOGIC_RST = MTK_BIT(4);
static constexpr uint32_t MT_WFDMA0_RST_DMASHDL_ALL_RST = MTK_BIT(5);

static constexpr uint32_t MT_WFDMA0_BUSY_ENA = MT_WFDMA0(0x13c);

static constexpr uint32_t MT_MCU_CMD = MT_WFDMA0(0x1f0);
static constexpr uint32_t MT_MCU_CMD_WAKE_RX_PCIE = MTK_BIT(0);
static constexpr uint32_t MT_MCU_CMD_STOP_DMA_FW_RELOAD = MTK_BIT(1);
static constexpr uint32_t MT_MCU_CMD_STOP_DMA = MTK_BIT(2);
static constexpr uint32_t MT_MCU_CMD_RESET_DONE = MTK_BIT(3);
static constexpr uint32_t MT_MCU_CMD_RECOVERY_DONE = MTK_BIT(4);
static constexpr uint32_t MT_MCU_CMD_NORMAL_STATE = MTK_BIT(5);
static constexpr uint32_t MT_MCU_CMD_ERROR_MASK = genmask32(5, 1);

static constexpr uint32_t MT_MCU2HOST_SW_INT_ENA = MT_WFDMA0(0x1f4);

static constexpr uint32_t MT_WFDMA0_HOST_INT_STA = MT_WFDMA0(0x200);
static constexpr uint32_t MT_WFDMA0_HOST_INT_ENA = MT_WFDMA0(0x204);

static constexpr uint32_t HOST_RX_DONE_INT_ENA0 = MTK_BIT(0);   // RX Ring 0 (MCU event + TX-free on MT7902)
static constexpr uint32_t HOST_RX_DONE_INT_ENA1 = MTK_BIT(1);
static constexpr uint32_t HOST_RX_DONE_INT_ENA2 = MTK_BIT(2);   // RX Ring 2 (Data/Mgmt)
static constexpr uint32_t HOST_RX_DONE_INT_ENA3 = MTK_BIT(3);
static constexpr uint32_t HOST_TX_DONE_INT_ENA0 = MTK_BIT(4);   // TX Ring 0 (Band 0 Data)
static constexpr uint32_t HOST_TX_DONE_INT_ENA15 = MTK_BIT(19);
static constexpr uint32_t HOST_RX_DONE_INT_ENA4 = MTK_BIT(22);  // Unused on MT7902 (no WA ring)
static constexpr uint32_t HOST_TX_DONE_INT_ENA16 = MTK_BIT(26); // FWDL TX done
static constexpr uint32_t HOST_TX_DONE_INT_ENA17 = MTK_BIT(27); // MCU-WM TX done
static constexpr uint32_t MCU2HOST_SW_INT_ENA = MTK_BIT(29);

static constexpr uint32_t MT_INT_MCU_CMD = MCU2HOST_SW_INT_ENA;
static constexpr uint32_t MT_INT_RX_DONE_DATA = HOST_RX_DONE_INT_ENA2;
static constexpr uint32_t MT_INT_RX_DONE_WM = HOST_RX_DONE_INT_ENA0;
// MT7902 does NOT use MCU-WA ring (wm2_complete_mask = 0)
static constexpr uint32_t MT_INT_RX_DONE_MT7902_ALL = MT_INT_RX_DONE_DATA | MT_INT_RX_DONE_WM;
static constexpr uint32_t MT_INT_TX_DONE_MCU_WM = HOST_TX_DONE_INT_ENA17;
static constexpr uint32_t MT_INT_TX_DONE_FWDL = HOST_TX_DONE_INT_ENA16;
static constexpr uint32_t MT_INT_TX_DONE_BAND0 = HOST_TX_DONE_INT_ENA0;
static constexpr uint32_t MT_INT_TX_DONE_MCU = MT_INT_TX_DONE_MCU_WM | MT_INT_TX_DONE_FWDL;
static constexpr uint32_t MT_INT_TX_DONE_ALL =
    MT_INT_TX_DONE_MCU_WM | MT_INT_TX_DONE_BAND0 | genmask32(18, 4) | HOST_TX_DONE_INT_ENA15 | MT_INT_TX_DONE_FWDL;

static constexpr uint32_t MT_WFDMA0_GLO_CFG = MT_WFDMA0(0x208);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_TX_DMA_EN = MTK_BIT(0);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_TX_DMA_BUSY = MTK_BIT(1);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_RX_DMA_EN = MTK_BIT(2);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_RX_DMA_BUSY = MTK_BIT(3);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_DMA_SIZE = genmask32(5, 4);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_TX_WB_DDONE = MTK_BIT(6);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_FW_DWLD_BYPASS_DMASHDL = MTK_BIT(9);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_FIFO_DIS_CHECK = MTK_BIT(11);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_FIFO_LITTLE_ENDIAN = MTK_BIT(12);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_RX_WB_DDONE = MTK_BIT(13);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_CSR_DISP_BASE_PTR_CHAIN_EN = MTK_BIT(15);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_OMIT_RX_INFO_PFET2 = MTK_BIT(21);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_OMIT_RX_INFO = MTK_BIT(27);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_OMIT_TX_INFO = MTK_BIT(28);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_CLK_GAT_DIS = MTK_BIT(30);

static constexpr uint32_t MT_WFDMA0_RST_DTX_PTR = MT_WFDMA0(0x20c);
static constexpr uint32_t MT_WFDMA0_RST_DRX_PTR = MT_WFDMA0(0x280);
static constexpr uint32_t MT_WFDMA0_GLO_CFG_EXT0 = MT_WFDMA0(0x2b0);
static constexpr uint32_t MT_WFDMA0_CSR_TX_DMASHDL_ENABLE = MTK_BIT(6);
static constexpr uint32_t MT_WFDMA0_PRI_DLY_INT_CFG0 = MT_WFDMA0(0x2f0);

// Ring register bases (stride = 0x10 per ring: desc_base, ring_size, cpu_idx, dma_idx)
static constexpr uint32_t MT_TX_RING_BASE = MT_WFDMA0(0x300);
static constexpr uint32_t MT_RX_EVENT_RING_BASE = MT_WFDMA0(0x500);
static constexpr uint32_t MT_RX_DATA_RING_BASE = MT_WFDMA0(0x520);
static constexpr uint32_t MT_RING_STRIDE = 0x10;

// WFDMA0 Prefetch Extended Control registers
static constexpr uint32_t MT_WFDMA0_TX_RING0_EXT_CTRL = MT_WFDMA0(0x600);
static constexpr uint32_t MT_WFDMA0_TX_RING1_EXT_CTRL = MT_WFDMA0(0x604);
static constexpr uint32_t MT_WFDMA0_TX_RING2_EXT_CTRL = MT_WFDMA0(0x608);
static constexpr uint32_t MT_WFDMA0_TX_RING3_EXT_CTRL = MT_WFDMA0(0x60c);
static constexpr uint32_t MT_WFDMA0_TX_RING4_EXT_CTRL = MT_WFDMA0(0x610);
static constexpr uint32_t MT_WFDMA0_TX_RING5_EXT_CTRL = MT_WFDMA0(0x614);
static constexpr uint32_t MT_WFDMA0_TX_RING6_EXT_CTRL = MT_WFDMA0(0x618);
static constexpr uint32_t MT_WFDMA0_TX_RING15_EXT_CTRL = MT_WFDMA0(0x63c);
static constexpr uint32_t MT_WFDMA0_TX_RING16_EXT_CTRL = MT_WFDMA0(0x640);
static constexpr uint32_t MT_WFDMA0_TX_RING17_EXT_CTRL = MT_WFDMA0(0x644);

static constexpr uint32_t MT_WFDMA0_RX_RING0_EXT_CTRL = MT_WFDMA0(0x680);
static constexpr uint32_t MT_WFDMA0_RX_RING1_EXT_CTRL = MT_WFDMA0(0x684);
static constexpr uint32_t MT_WFDMA0_RX_RING2_EXT_CTRL = MT_WFDMA0(0x688);
static constexpr uint32_t MT_WFDMA0_RX_RING3_EXT_CTRL = MT_WFDMA0(0x68c);

static constexpr uint32_t mtPrefetch(uint16_t base, uint16_t depth) {
    return (static_cast<uint32_t>(base) << 16) | depth;
}

// DMASHDL
static constexpr uint32_t MT_DMASHDL_SW_CONTROL = 0x7c026004;
static constexpr uint32_t MT_DMASHDL_DMASHDL_BYPASS = MTK_BIT(28);

// MDP / MAC / WTBL / MIB / TMAC / RMAC / ARB / AGG / LPON
static constexpr uint32_t MT_MDP_BASE = 0x820cd000;
static constexpr uint32_t MT_MDP_DCR0 = MT_MDP_BASE + 0x000;
static constexpr uint32_t MT_MDP_DCR0_DAMSDU_EN = MTK_BIT(15);
static constexpr uint32_t MT_MDP_DCR0_RX_HDR_TRANS_EN = MTK_BIT(19);
static constexpr uint32_t MT_MDP_DCR1 = MT_MDP_BASE + 0x004;
static constexpr uint32_t MT_MDP_DCR1_MAX_RX_LEN = genmask32(15, 3);

static constexpr uint32_t MT_WTBLON_TOP_BASE = 0x820d4000;
static constexpr uint32_t MT_WTBL_UPDATE = MT_WTBLON_TOP_BASE + 0x230;
static constexpr uint32_t MT_WTBL_UPDATE_WLAN_IDX = genmask32(9, 0);
static constexpr uint32_t MT_WTBL_UPDATE_ADM_COUNT_CLEAR = MTK_BIT(12);
static constexpr uint32_t MT_WTBL_UPDATE_BUSY = MTK_BIT(31);

static constexpr uint32_t MT_WF_TMAC(uint8_t band, uint32_t ofs) {
    return (band ? 0x820f4000U : 0x820e4000U) + ofs;
}
static constexpr uint32_t MT_TMAC_CDTR(uint8_t b) { return MT_WF_TMAC(b, 0x090); }
static constexpr uint32_t MT_TMAC_ODTR(uint8_t b) { return MT_WF_TMAC(b, 0x094); }
static constexpr uint32_t MT_TMAC_ICR0(uint8_t b) { return MT_WF_TMAC(b, 0x0a4); }
static constexpr uint32_t MT_TMAC_CTCR0(uint8_t b) { return MT_WF_TMAC(b, 0x0f4); }
static constexpr uint32_t MT_TMAC_CTCR0_INS_DDLMT_REFTIME = genmask32(5, 0);
static constexpr uint32_t MT_TMAC_CTCR0_INS_DDLMT_EN = MTK_BIT(17);
static constexpr uint32_t MT_TMAC_CTCR0_INS_DDLMT_VHT_SMPDU_EN = MTK_BIT(18);

static constexpr uint32_t MT_WF_RMAC(uint8_t band, uint32_t ofs) {
    return (band ? 0x820f5000U : 0x820e5000U) + ofs;
}
static constexpr uint32_t MT_WF_RFCR(uint8_t b) { return MT_WF_RMAC(b, 0x000); }
static constexpr uint32_t MT_WF_RFCR_DROP_OTHER_BEACON = MTK_BIT(11);
static constexpr uint32_t MT_WF_RMAC_MIB_AIRTIME0(uint8_t b) { return MT_WF_RMAC(b, 0x380); }
static constexpr uint32_t MT_WF_RMAC_MIB_TIME0(uint8_t b) { return MT_WF_RMAC(b, 0x3c4); }
static constexpr uint32_t MT_WF_RMAC_MIB_RXTIME_EN = MTK_BIT(30);
static constexpr uint32_t MT_WF_RMAC_MIB_RXTIME_CLR = MTK_BIT(31);

static constexpr uint32_t MT_WF_MIB(uint8_t band, uint32_t ofs) {
    return (band ? 0x820fd000U : 0x820ed000U) + ofs;
}
static constexpr uint32_t MT_MIB_SCR1(uint8_t b) { return MT_WF_MIB(b, 0x004); }
static constexpr uint32_t MT_MIB_TXDUR_EN = MTK_BIT(8);
static constexpr uint32_t MT_MIB_RXDUR_EN = MTK_BIT(9);
static constexpr uint32_t MT_MIB_SDR3(uint8_t b) { return MT_WF_MIB(b, 0x698); }
static constexpr uint32_t MT_MIB_SDR3_FCS_ERR_MASK = genmask32(31, 16);
static constexpr uint32_t MT_MIB_SDR9(uint8_t b) { return MT_WF_MIB(b, 0x02c); }

static constexpr uint32_t MT_WF_DMA(uint8_t band, uint32_t ofs) {
    return (band ? 0x820f7000U : 0x820e7000U) + ofs;
}
static constexpr uint32_t MT_DMA_DCR0(uint8_t b) { return MT_WF_DMA(b, 0x000); }
static constexpr uint32_t MT_DMA_DCR0_MAX_RX_LEN = genmask32(15, 3);
static constexpr uint32_t MT_DMA_DCR0_RXD_G5_EN = MTK_BIT(23);

static constexpr uint32_t MT_WF_ARB(uint8_t band, uint32_t ofs) {
    return (band ? 0x820f3000U : 0x820e3000U) + ofs;
}
static constexpr uint32_t MT_ARB_SCR(uint8_t b) { return MT_WF_ARB(b, 0x080); }
static constexpr uint32_t MT_ARB_SCR_TX_DISABLE = MTK_BIT(8);
static constexpr uint32_t MT_ARB_SCR_RX_DISABLE = MTK_BIT(9);

static constexpr uint32_t MT_WF_LPON(uint8_t band, uint32_t ofs) {
    return (band ? 0x820fb000U : 0x820eb000U) + ofs;
}
static constexpr uint32_t MT_LPON_UTTR0(uint8_t b) { return MT_WF_LPON(b, 0x080); }
static constexpr uint32_t MT_LPON_UTTR1(uint8_t b) { return MT_WF_LPON(b, 0x084); }
static constexpr uint32_t MT_LPON_TCR(uint8_t b, uint32_t n) { return MT_WF_LPON(b, 0x0a8 + n * 4); }
static constexpr uint32_t MT_LPON_TCR_SW_MODE = genmask32(1, 0);

// MT7902 Ring indices & sizes (from upstream Linux mt7921/pci.c & mt7921.h)
static constexpr uint8_t MT7902_TXQ_BAND0 = 0;
static constexpr uint8_t MT7902_TXQ_MCU_WM = 15; // MT7902 quirk: ring 15 (MT7921 uses 17)
static constexpr uint8_t MT7902_TXQ_FWDL = 16;

static constexpr uint8_t MT7902_RXQ_MCU_WM = 0;  // Shared for MCU events & TX-free notifications
static constexpr uint8_t MT7902_RXQ_DATA = 2;    // RX Ring 2 (0xd4520)

static constexpr uint16_t MT7902_TX_RING_SIZE = 512;      // HW supports up to 2048
static constexpr uint16_t MT7902_TX_MCU_RING_SIZE = 256;
static constexpr uint16_t MT7902_TX_FWDL_RING_SIZE = 128;
static constexpr uint16_t MT7902_RX_MCU_RING_SIZE = 512;  // MT7902 quirk: 512 (MT7921 uses 8)
static constexpr uint16_t MT7902_RX_DATA_RING_SIZE = 512; // HW supports up to 1536
static constexpr uint32_t MT7902_RX_BUF_SIZE = 2048;
static constexpr uint32_t MT7902_WTBL_SIZE = 288;
static constexpr uint32_t MT7902_TOKEN_SIZE = 1024;

// Fixed BAR0 address translation entry
struct RegMapEntry {
    uint32_t phys;
    uint32_t maps;
    uint32_t size;
};

static constexpr RegMapEntry kFixedRegMap[] = {
    { 0x820d0000, 0x30000, 0x10000 }, // WF_LMAC_TOP (WF_WTBLON)
    { 0x820ed000, 0x24800, 0x00800 }, // WF_LMAC_TOP BN0 (WF_MIB)
    { 0x820e4000, 0x21000, 0x00400 }, // WF_LMAC_TOP BN0 (WF_TMAC)
    { 0x820e7000, 0x21e00, 0x00200 }, // WF_LMAC_TOP BN0 (WF_DMA)
    { 0x820eb000, 0x24200, 0x00400 }, // WF_LMAC_TOP BN0 (WF_LPON)
    { 0x820e2000, 0x20800, 0x00400 }, // WF_LMAC_TOP BN0 (WF_AGG)
    { 0x820e3000, 0x20c00, 0x00400 }, // WF_LMAC_TOP BN0 (WF_ARB)
    { 0x820e5000, 0x21400, 0x00800 }, // WF_LMAC_TOP BN0 (WF_RMAC)
    { 0x00400000, 0x80000, 0x10000 }, // WF_MCU_SYSRAM
    { 0x00410000, 0x90000, 0x10000 }, // WF_MCU_SYSRAM (configure)
    { 0x40000000, 0x70000, 0x10000 }, // WF_UMAC_SYSRAM
    { 0x54000000, 0x02000, 0x01000 }, // WFDMA PCIE0 MCU DMA0
    { 0x55000000, 0x03000, 0x01000 }, // WFDMA PCIE0 MCU DMA1
    { 0x58000000, 0x06000, 0x01000 }, // WFDMA PCIE1 MCU DMA0
    { 0x59000000, 0x07000, 0x01000 }, // WFDMA PCIE1 MCU DMA1
    { 0x7c000000, 0xf0000, 0x10000 }, // CONN_INFRA
    { 0x7c020000, 0xd0000, 0x10000 }, // CONN_INFRA, WFDMA
    { 0x7c060000, 0xe0000, 0x10000 }, // CONN_INFRA, conn_host_csr_top
    { 0x80020000, 0xb0000, 0x10000 }, // WF_TOP_MISC_OFF
    { 0x81020000, 0xc0000, 0x10000 }, // WF_TOP_MISC_ON
    { 0x820c0000, 0x08000, 0x04000 }, // WF_UMAC_TOP (PLE)
    { 0x820c8000, 0x0c000, 0x02000 }, // WF_UMAC_TOP (PSE)
    { 0x820cc000, 0x0e000, 0x01000 }, // WF_UMAC_TOP (PP)
    { 0x820cd000, 0x0f000, 0x01000 }, // WF_MDP_TOP
    { 0x74030000, 0x10000, 0x10000 }, // PCIE_MAC_IREG
    { 0x820ce000, 0x21c00, 0x00200 }, // WF_LMAC_TOP (WF_SEC)
    { 0x820cf000, 0x22000, 0x01000 }, // WF_LMAC_TOP (WF_PF)
    { 0x820e0000, 0x20000, 0x00400 }, // WF_LMAC_TOP BN0 (WF_CFG)
    { 0x820e1000, 0x20400, 0x00200 }, // WF_LMAC_TOP BN0 (WF_TRB)
    { 0x820e9000, 0x23400, 0x00200 }, // WF_LMAC_TOP BN0 (WF_WTBLOFF)
    { 0x820ea000, 0x24000, 0x00200 }, // WF_LMAC_TOP BN0 (WF_ETBF)
    { 0x820ec000, 0x24600, 0x00200 }, // WF_LMAC_TOP BN0 (WF_INT)
    { 0x820f0000, 0xa0000, 0x00400 }, // WF_LMAC_TOP BN1 (WF_CFG)
    { 0x820f1000, 0xa0600, 0x00200 }, // WF_LMAC_TOP BN1 (WF_TRB)
    { 0x820f2000, 0xa0800, 0x00400 }, // WF_LMAC_TOP BN1 (WF_AGG)
    { 0x820f3000, 0xa0c00, 0x00400 }, // WF_LMAC_TOP BN1 (WF_ARB)
    { 0x820f4000, 0xa1000, 0x00400 }, // WF_LMAC_TOP BN1 (WF_TMAC)
    { 0x820f5000, 0xa1400, 0x00800 }, // WF_LMAC_TOP BN1 (WF_RMAC)
    { 0x820f7000, 0xa1e00, 0x00200 }, // WF_LMAC_TOP BN1 (WF_DMA)
    { 0x820f9000, 0xa3400, 0x00200 }, // WF_LMAC_TOP BN1 (WF_WTBLOFF)
    { 0x820fa000, 0xa4000, 0x00200 }, // WF_LMAC_TOP BN1 (WF_ETBF)
    { 0x820fb000, 0xa4200, 0x00400 }, // WF_LMAC_TOP BN1 (WF_LPON)
    { 0x820fc000, 0xa4600, 0x00200 }, // WF_LMAC_TOP BN1 (WF_INT)
    { 0x820fd000, 0xa4800, 0x00800 }, // WF_LMAC_TOP BN1 (WF_MIB)
};

static inline bool mt7902NeedsL1Remap(uint32_t addr) {
    return (addr >= 0x18000000U && addr < 0x18c00000U) ||
           (addr >= 0x70000000U && addr < 0x78000000U) ||
           (addr >= 0x7c000000U && addr < 0x7c400000U);
}

static inline bool mt7902FixedMapLookup(uint32_t addr, uint32_t &barOffset) {
    if (addr < 0x100000U) {
        barOffset = addr;
        return true;
    }
    for (size_t i = 0; i < sizeof(kFixedRegMap) / sizeof(kFixedRegMap[0]); ++i) {
        if (addr >= kFixedRegMap[i].phys) {
            uint32_t ofs = addr - kFixedRegMap[i].phys;
            if (ofs <= kFixedRegMap[i].size) {
                barOffset = kFixedRegMap[i].maps + ofs;
                return true;
            }
        }
    }
    return false;
}

} // namespace mt7902

#endif // MT7902_REGS_HPP
