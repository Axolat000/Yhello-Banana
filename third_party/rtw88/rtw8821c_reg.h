/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation
 * Adapted from Linux v6.12 rtw88 hardware routines under BSD terms.
 */
#define REG_SYS_FUNC_EN		0x0002
#define BIT_FEN_EN_25_1		BIT(13)
#define BIT_FEN_ELDR		BIT(12)
#define BIT_FEN_CPUEN		BIT(2)
#define BIT_FEN_BB_GLB_RST	BIT(1)
#define BIT_FEN_BB_RSTB		BIT(0)
#define BIT_R_DIS_PRST		BIT(6)
#define BIT_WLOCK_1C_B6		BIT(5)
#define REG_SYS_PW_CTRL		0x0004
#define BIT_PFM_WOWL		BIT(3)
#define BIT_APFM_OFFMAC		BIT(9)
#define REG_SYS_CLK_CTRL	0x0008
#define BIT_CPU_CLK_EN		BIT(14)
#define REG_SYS_CLKR		0x0008
#define BIT_ANA8M		BIT(1)
#define BIT_WAKEPAD_EN		BIT(3)
#define BIT_LOADER_CLK_EN	BIT(5)
#define REG_RSV_CTRL		0x001C
#define DISABLE_PI		0x3
#define ENABLE_PI		0x2
#define BITS_RFC_DIRECT		(BIT(31) | BIT(30))
#define BIT_WLMCU_IOIF		BIT(0)
#define REG_RF_CTRL		0x001F
#define BIT_RF_SDM_RSTB		BIT(2)
#define BIT_RF_RSTB		BIT(1)
#define BIT_RF_EN		BIT(0)
#define REG_AFE_CTRL1		0x0024
#define BIT_MAC_CLK_SEL		(BIT(20) | BIT(21))
#define REG_EFUSE_CTRL		0x0030
#define BIT_EF_FLAG		BIT(31)
#define BIT_SHIFT_EF_ADDR	8
#define BIT_MASK_EF_ADDR	0x3ff
#define BIT_MASK_EF_DATA	0xff
#define BITS_EF_ADDR		(BIT_MASK_EF_ADDR << BIT_SHIFT_EF_ADDR)
#define BITS_PLL		0xf0
#define REG_AFE_XTAL_CTRL	0x24
#define REG_AFE_PLL_CTRL	0x28
#define REG_AFE_CTRL3		0x2c
#define BIT_MASK_XTAL		0x00FFF000
#define BIT_XTAL_GMP_BIT4	BIT(28)
#define REG_LDO_EFUSE_CTRL	0x0034
#define BIT_MASK_EFUSE_BANK_SEL	(BIT(8) | BIT(9))
#define BIT_LDO25_VOLTAGE_V25	0x03
#define BIT_MASK_LDO25_VOLTAGE	GENMASK(6, 4)
#define BIT_SHIFT_LDO25_VOLTAGE	4
#define BIT_LDO25_EN		BIT(7)
#define REG_GPIO_MUXCFG		0x0040
#define BIT_FSPI_EN		BIT(19)
#define BIT_EN_SIC		BIT(12)
#define BIT_PO_BT_PTA_PINS	BIT(9)
#define BIT_BT_PTA_EN		BIT(5)
#define BIT_WLRFE_4_5_EN	BIT(2)
#define REG_LED_CFG		0x004C
#define BIT_LNAON_SEL_EN	BIT(26)
#define BIT_PAPE_SEL_EN		BIT(25)
#define BIT_DPDT_WL_SEL		BIT(24)
#define BIT_DPDT_SEL_EN		BIT(23)
#define REG_LEDCFG2		0x004E
#define REG_PAD_CTRL1		0x0064
#define BIT_BT_BTG_SEL		BIT(31)
#define BIT_PAPE_WLBT_SEL	BIT(29)
#define BIT_LNAON_WLBT_SEL	BIT(28)
#define BIT_BTGP_JTAG_EN	BIT(24)
#define BIT_BTGP_SPI_EN		BIT(20)
#define BIT_LED1DIS		BIT(15)
#define BIT_SW_DPDT_SEL_DATA	BIT(0)
#define REG_WL_BT_PWR_CTRL	0x0068
#define BIT_BT_FUNC_EN		BIT(18)
#define BIT_BT_DIG_CLK_EN	BIT(8)
#define REG_SYS_SDIO_CTRL	0x0070
#define BIT_DBG_GNT_WL_BT	BIT(27)
#define BIT_LTE_MUX_CTRL_PATH	BIT(26)
#define REG_HCI_OPT_CTRL	0x0074
#define BIT_USB_SUS_DIS		BIT(8)
#define BIT_SDIO_PAD_E5		BIT(18)
#define REG_AFE_CTRL_4		0x0078
#define BIT_CK320M_AFE_EN	BIT(4)
#define BIT_EN_SYN		BIT(15)
#define REG_LDO_SWR_CTRL	0x007C
#define LDO_SEL			0xC3
#define SPS_SEL			0x83
#define BIT_XTA1		BIT(29)
#define BIT_XTA0		BIT(28)
#define REG_MCUFW_CTRL		0x0080
#define BIT_ANA_PORT_EN		BIT(22)
#define BIT_MAC_PORT_EN		BIT(21)
#define BIT_BOOT_FSPI_EN	BIT(20)
#define BIT_ROM_DLEN		BIT(19)
#define BIT_ROM_PGE		GENMASK(18, 16)	/* legacy only */
#define BIT_SHIFT_ROM_PGE	16
#define BIT_FW_INIT_RDY		BIT(15)
#define BIT_FW_DW_RDY		BIT(14)
#define BIT_RPWM_TOGGLE		BIT(7)
#define BIT_RAM_DL_SEL		BIT(7)	/* legacy only */
#define BIT_DMEM_CHKSUM_OK	BIT(6)
#define BIT_WINTINI_RDY		BIT(6)	/* legacy only */
#define BIT_DMEM_DW_OK		BIT(5)
#define BIT_IMEM_CHKSUM_OK	BIT(4)
#define BIT_IMEM_DW_OK		BIT(3)
#define BIT_IMEM_BOOT_LOAD_CHECKSUM_OK BIT(2)
#define BIT_FWDL_CHK_RPT	BIT(2)	/* legacy only */
#define BIT_MCUFWDL_RDY		BIT(1)	/* legacy only */
#define BIT_MCUFWDL_EN		BIT(0)
#define BIT_CHECK_SUM_OK	(BIT(4) | BIT(6))
#define FW_READY		(BIT_FW_INIT_RDY | BIT_FW_DW_RDY |             \
				 BIT_IMEM_DW_OK | BIT_DMEM_DW_OK |             \
				 BIT_CHECK_SUM_OK)
#define FW_READY_LEGACY		(BIT_MCUFWDL_RDY | BIT_FWDL_CHK_RPT |	       \
				 BIT_WINTINI_RDY | BIT_RAM_DL_SEL)
#define FW_READY_MASK		0xffff
#define REG_MCU_TST_CFG		0x84
#define VAL_FW_TRIGGER		0x1
#define REG_PMC_DBG_CTRL1	0xa8
#define BITS_PMC_BT_IQK_STS	GENMASK(22, 21)
#define REG_PAD_CTRL2		0x00C4
#define BIT_RSM_EN_V1		BIT(16)
#define BIT_NO_PDN_CHIPOFF_V1	BIT(17)
#define BIT_MASK_USB23_SW_MODE_V1	GENMASK(19, 18)
#define BIT_USB3_USB2_TRANSITION	BIT(20)
#define BIT_USB_MODE_U2		1
#define BIT_USB_MODE_U3		2
#define REG_EFUSE_ACCESS	0x00CF
#define EFUSE_ACCESS_ON		0x69
#define EFUSE_ACCESS_OFF	0x00
#define REG_WLRF1		0x00EC
#define REG_WIFI_BT_INFO	0x00AA
#define BIT_BT_INT_EN		BIT(15)
#define REG_SYS_CFG1		0x00F0
#define	BIT_RTL_ID		BIT(23)
#define BIT_LDO			BIT(24)
#define BIT_RF_TYPE_ID		BIT(27)
#define BIT_SHIFT_VENDOR_ID	16
#define BIT_MASK_VENDOR_ID	0xf
#define BIT_VENDOR_ID(x) (((x) & BIT_MASK_VENDOR_ID) << BIT_SHIFT_VENDOR_ID)
#define BITS_VENDOR_ID		(BIT_MASK_VENDOR_ID << BIT_SHIFT_VENDOR_ID)
#define BIT_CLEAR_VENDOR_ID(x)	((x) & (~BITS_VENDOR_ID))
#define BIT_GET_VENDOR_ID(x) (((x) >> BIT_SHIFT_VENDOR_ID) & BIT_MASK_VENDOR_ID)
#define BIT_SHIFT_CHIP_VER	12
#define BIT_MASK_CHIP_VER	0xf
#define BIT_CHIP_VER(x)	 (((x) & BIT_MASK_CHIP_VER) << BIT_SHIFT_CHIP_VER)
#define BITS_CHIP_VER		(BIT_MASK_CHIP_VER << BIT_SHIFT_CHIP_VER)
#define BIT_CLEAR_CHIP_VER(x)	((x) & (~BITS_CHIP_VER))
#define BIT_GET_CHIP_VER(x) (((x) >> BIT_SHIFT_CHIP_VER) & BIT_MASK_CHIP_VER)
#define REG_SYS_STATUS1		0x00F4
#define REG_SYS_STATUS2		0x00F8
#define REG_SYS_CFG2		0x00FC
#define BIT_WLRF1_BBRF_EN	(BIT(24) | BIT(25) | BIT(26))
#define REG_CR			0x0100
#define BIT_32K_CAL_TMR_EN	BIT(10)
#define BIT_MAC_SEC_EN		BIT(9)
#define BIT_ENSWBCN		BIT(8)
#define BIT_MACRXEN		BIT(7)
#define BIT_MACTXEN		BIT(6)
#define BIT_SCHEDULE_EN		BIT(5)
#define BIT_PROTOCOL_EN		BIT(4)
#define BIT_RXDMA_EN		BIT(3)
#define BIT_TXDMA_EN		BIT(2)
#define BIT_HCI_RXDMA_EN	BIT(1)
#define BIT_HCI_TXDMA_EN	BIT(0)
#define MAC_TRX_ENABLE	(BIT_HCI_TXDMA_EN | BIT_HCI_RXDMA_EN | BIT_TXDMA_EN | \
			BIT_RXDMA_EN | BIT_PROTOCOL_EN | BIT_SCHEDULE_EN | \
			BIT_MACTXEN | BIT_MACRXEN)
#define BIT_SHIFT_TXDMA_VOQ_MAP	4
#define BIT_MASK_TXDMA_VOQ_MAP	0x3
#define BIT_TXDMA_VOQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_VOQ_MAP) << BIT_SHIFT_TXDMA_VOQ_MAP)
#define BIT_SHIFT_TXDMA_VIQ_MAP	6
#define BIT_MASK_TXDMA_VIQ_MAP	0x3
#define BIT_TXDMA_VIQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_VIQ_MAP) << BIT_SHIFT_TXDMA_VIQ_MAP)
#define REG_TXDMA_PQ_MAP	0x010C
#define BIT_RXDMA_ARBBW_EN	BIT(0)
#define BIT_RXSHFT_EN		BIT(1)
#define BIT_RXDMA_AGG_EN	BIT(2)
#define BIT_TXDMA_BW_EN		BIT(3)
#define BIT_SHIFT_TXDMA_BEQ_MAP	8
#define BIT_MASK_TXDMA_BEQ_MAP	0x3
#define BIT_TXDMA_BEQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_BEQ_MAP) << BIT_SHIFT_TXDMA_BEQ_MAP)
#define BIT_SHIFT_TXDMA_BKQ_MAP	10
#define BIT_MASK_TXDMA_BKQ_MAP	0x3
#define BIT_TXDMA_BKQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_BKQ_MAP) << BIT_SHIFT_TXDMA_BKQ_MAP)
#define BIT_SHIFT_TXDMA_MGQ_MAP	12
#define BIT_MASK_TXDMA_MGQ_MAP	0x3
#define BIT_TXDMA_MGQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_MGQ_MAP) << BIT_SHIFT_TXDMA_MGQ_MAP)
#define BIT_SHIFT_TXDMA_HIQ_MAP	14
#define BIT_MASK_TXDMA_HIQ_MAP	0x3
#define BIT_TXDMA_HIQ_MAP(x)                                                   \
	(((x) & BIT_MASK_TXDMA_HIQ_MAP) << BIT_SHIFT_TXDMA_HIQ_MAP)
#define BIT_SHIFT_TXSC_40M	4
#define BIT_MASK_TXSC_40M	0xf
#define BIT_TXSC_40M(x)							       \
	(((x) & BIT_MASK_TXSC_40M) << BIT_SHIFT_TXSC_40M)
#define BIT_SHIFT_TXSC_20M	0
#define BIT_MASK_TXSC_20M	0xf
#define BIT_TXSC_20M(x)							       \
	(((x) & BIT_MASK_TXSC_20M) << BIT_SHIFT_TXSC_20M)
#define BIT_SHIFT_MAC_CLK_SEL	20
#define MAC_CLK_HW_DEF_80M	0
#define MAC_CLK_HW_DEF_40M	1
#define MAC_CLK_HW_DEF_20M	2
#define MAC_CLK_SPEED		80
#define REG_TRXFF_BNDY		0x0114
#define REG_RXFF_BNDY		0x011C
#define REG_FE1IMR		0x0120
#define BIT_FS_RXDONE		BIT(16)
#define REG_CPWM		0x012C
#define REG_FWIMR		0x0130
#define BIT_FS_H2CCMD_INT_EN	BIT(4)
#define BIT_FS_HRCV_INT_EN	BIT(5)
#define REG_FWISR		0x0134
#define BIT_FS_H2CCMD_INT	BIT(4)
#define BIT_FS_HRCV_INT		BIT(5)
#define REG_PKTBUF_DBG_CTRL	0x0140
#define REG_C2HEVT		0x01A0
#define REG_MCUTST_1		0x01C0
#define REG_MCUTST_II		0x01C4
#define REG_WOWLAN_WAKE_REASON	0x01C7
#define REG_HMETFR		0x01CC
#define BIT_INT_BOX0		BIT(0)
#define BIT_INT_BOX1		BIT(1)
#define BIT_INT_BOX2		BIT(2)
#define BIT_INT_BOX3		BIT(3)
#define BIT_INT_BOX_ALL		(BIT_INT_BOX0 | BIT_INT_BOX1 | BIT_INT_BOX2 | \
				 BIT_INT_BOX3)
#define REG_HMEBOX0		0x01D0
#define REG_HMEBOX1		0x01D4
#define REG_HMEBOX2		0x01D8
#define REG_HMEBOX3		0x01DC
#define REG_HMEBOX0_EX		0x01F0
#define REG_HMEBOX1_EX		0x01F4
#define REG_HMEBOX2_EX		0x01F8
#define REG_HMEBOX3_EX		0x01FC
#define REG_RQPN		0x0200
#define BIT_MASK_HPQ		0xff
#define BIT_SHIFT_HPQ		0
#define BIT_RQPN_HPQ(x)		(((x) & BIT_MASK_HPQ) << BIT_SHIFT_HPQ)
#define BIT_MASK_LPQ		0xff
#define BIT_SHIFT_LPQ		8
#define BIT_RQPN_LPQ(x)		(((x) & BIT_MASK_LPQ) << BIT_SHIFT_LPQ)
#define BIT_MASK_PUBQ		0xff
#define BIT_SHIFT_PUBQ		16
#define BIT_RQPN_PUBQ(x)	(((x) & BIT_MASK_PUBQ) << BIT_SHIFT_PUBQ)
#define BIT_RQPN_HLP(h, l, p)	(BIT_LD_RQPN | BIT_RQPN_HPQ(h) |	       \
				 BIT_RQPN_LPQ(l) | BIT_RQPN_PUBQ(p))
#define REG_FIFOPAGE_CTRL_2	0x0204
#define BIT_BCN_VALID_V1	BIT(15)
#define BIT_MASK_BCN_HEAD_1_V1	0xfff
#define REG_AUTO_LLT_V1		0x0208
#define BIT_AUTO_INIT_LLT_V1	BIT(0)
#define BIT_MASK_BLK_DESC_NUM	GENMASK(7, 4)
#define REG_DWBCN0_CTRL		0x0208
#define BIT_BCN_VALID		BIT(16)
#define REG_TXDMA_OFFSET_CHK	0x020C
#define BIT_DROP_DATA_EN	BIT(9)
#define REG_TXDMA_STATUS	0x0210
#define BTI_PAGE_OVF		BIT(2)
#define REG_RQPN_NPQ		0x0214
#define BIT_MASK_NPQ		0xff
#define BIT_SHIFT_NPQ		0
#define BIT_MASK_EPQ		0xff
#define BIT_SHIFT_EPQ		16
#define BIT_RQPN_NPQ(x)		(((x) & BIT_MASK_NPQ) << BIT_SHIFT_NPQ)
#define BIT_RQPN_EPQ(x)		(((x) & BIT_MASK_EPQ) << BIT_SHIFT_EPQ)
#define BIT_RQPN_NE(n, e)	(BIT_RQPN_NPQ(n) | BIT_RQPN_EPQ(e))
#define REG_AUTO_LLT		0x0224
#define BIT_AUTO_INIT_LLT	BIT(16)
#define REG_RQPN_CTRL_1		0x0228
#define REG_RQPN_CTRL_2		0x022C
#define BIT_LD_RQPN		BIT(31)
#define REG_FIFOPAGE_INFO_1	0x0230
#define REG_FIFOPAGE_INFO_2	0x0234
#define REG_FIFOPAGE_INFO_3	0x0238
#define REG_FIFOPAGE_INFO_4	0x023C
#define REG_FIFOPAGE_INFO_5	0x0240
#define REG_H2C_HEAD		0x0244
#define REG_H2C_TAIL		0x0248
#define REG_H2C_READ_ADDR	0x024C
#define REG_H2C_INFO		0x0254
#define REG_RXDMA_AGG_PG_TH	0x0280
#define BIT_RXDMA_AGG_PG_TH	GENMASK(7, 0)
#define BIT_DMA_AGG_TO_V1	GENMASK(15, 8)
#define BIT_EN_PRE_CALC		BIT(29)
#define REG_RXPKT_NUM		0x0284
#define BIT_RXDMA_REQ		BIT(19)
#define BIT_RW_RELEASE		BIT(18)
#define BIT_RXDMA_IDLE		BIT(17)
#define REG_RXDMA_STATUS	0x0288
#define REG_RXDMA_DPR		0x028C
#define REG_RXDMA_MODE		0x0290
#define BIT_DMA_MODE		BIT(1)
#define BIT_DMA_BURST_CNT	GENMASK(3, 2)
#define BIT_DMA_BURST_SIZE	GENMASK(5, 4)
#define BIT_DMA_BURST_SIZE_64	2
#define BIT_DMA_BURST_SIZE_512	1
#define BIT_DMA_BURST_SIZE_1024	0
#define REG_RXPKTNUM		0x02B0
#define REG_INT_MIG		0x0304
#define REG_HCI_MIX_CFG		0x03FC
#define BIT_PCIE_EMAC_PDN_AUX_TO_FAST_CLK BIT(26)
#define REG_BCNQ_INFO		0x0418
#define BIT_MGQ_CPU_EMPTY	BIT(24)
#define REG_FWHW_TXQ_CTRL	0x0420
#define BIT_EN_BCNQ_DL		BIT(22)
#define BIT_EN_WR_FREE_TAIL	BIT(20)
#define REG_HWSEQ_CTRL		0x0423
#define REG_BCNQ_BDNY_V1	0x0424
#define REG_BCNQ_BDNY		0x0424
#define REG_MGQ_BDNY		0x0425
#define REG_LIFETIME_EN		0x0426
#define BIT_BA_PARSER_EN	BIT(5)
#define REG_SPEC_SIFS		0x0428
#define REG_RETRY_LIMIT		0x042a
#define REG_DARFRC		0x0430
#define REG_DARFRCH		0x0434
#define REG_RARFRCH		0x043C
#define REG_RRSR		0x0440
#define BITS_RRSR_RSC		GENMASK(22, 21)
#define REG_ARFR0		0x0444
#define REG_ARFRH0		0x0448
#define REG_ARFR1_V1		0x044C
#define REG_ARFRH1_V1		0x0450
#define REG_CCK_CHECK		0x0454
#define BIT_CHECK_CCK_EN	BIT(7)
#define REG_AMPDU_MAX_TIME_V1	0x0455
#define REG_BCNQ1_BDNY_V1	0x0456
#define REG_AMPDU_MAX_TIME	0x0456
#define REG_WMAC_LBK_BF_HD	0x045D
#define REG_TX_HANG_CTRL	0x045E
#define BIT_EN_GNT_BT_AWAKE	BIT(3)
#define BIT_EN_EOF_V1		BIT(2)
#define REG_DATA_SC		0x0483
#define REG_ARFR2_V1		0x048C
#define REG_ARFRH2_V1		0x0490
#define REG_ARFR3_V1		0x0494
#define BIT_EXC_CODE		GENMASK(6, 2)
#define REG_ARFRH3_V1		0x0498
#define REG_ARFR4		0x049C
#define BIT_WL_RFK		BIT(0)
#define REG_ARFRH4		0x04A0
#define REG_ARFR5		0x04A4
#define REG_ARFRH5		0x04A8
#define REG_SW_AMPDU_BURST_MODE_CTRL 0x04BC
#define BIT_PRE_TX_CMD		BIT(6)
#define REG_QUEUE_CTRL		0x04C6
#define BIT_PTA_WL_TX_EN	BIT(4)
#define BIT_PTA_EDCCA_EN	BIT(5)
#define REG_SINGLE_AMPDU_CTRL	0x04C7
#define BIT_EN_SINGLE_APMDU	BIT(7)
#define REG_PROT_MODE_CTRL	0x04C8
#define REG_MAX_AGGR_NUM	0x04CA
#define REG_BAR_MODE_CTRL	0x04CC
#define REG_PRECNT_CTRL		0x04E5
#define BIT_BTCCA_CTRL		(BIT(0) | BIT(1))
#define BIT_EN_PRECNT		BIT(11)
#define REG_DUMMY_PAGE4_V1	0x04FC
#define REG_EDCA_VO_PARAM	0x0500
#define REG_EDCA_VI_PARAM	0x0504
#define REG_EDCA_BE_PARAM	0x0508
#define REG_EDCA_BK_PARAM	0x050C
#define BIT_MASK_TXOP_LMT	GENMASK(26, 16)
#define BIT_MASK_CWMAX		GENMASK(15, 12)
#define BIT_MASK_CWMIN		GENMASK(11, 8)
#define BIT_MASK_AIFS		GENMASK(7, 0)
#define REG_PIFS		0x0512
#define REG_SIFS		0x0514
#define BIT_SHIFT_SIFS_OFDM_CTX	8
#define BIT_SHIFT_SIFS_CCK_TRX	16
#define BIT_SHIFT_SIFS_OFDM_TRX	24
#define REG_AGGR_BREAK_TIME	0x051A
#define REG_SLOT		0x051B
#define REG_TX_PTCL_CTRL	0x0520
#define BIT_DIS_EDCCA		BIT(15)
#define BIT_SIFS_BK_EN		BIT(12)
#define REG_TXPAUSE		0x0522
#define BIT_AC_QUEUE		GENMASK(7, 0)
#define BIT_HIGH_QUEUE		BIT(5)
#define REG_RD_CTRL		0x0524
#define BIT_EDCCA_MSK_CNTDOWN_EN BIT(11)
#define BIT_DIS_TXOP_CFE	BIT(10)
#define BIT_DIS_LSIG_CFE	BIT(9)
#define BIT_DIS_STBC_CFE	BIT(8)
#define REG_TBTT_PROHIBIT	0x0540
#define BIT_SHIFT_TBTT_HOLD_TIME_AP 8
#define REG_RD_NAV_NXT		0x0544
#define REG_NAV_PROT_LEN	0x0546
#define REG_BCN_CTRL		0x0550
#define BIT_DIS_TSF_UDT		BIT(4)
#define BIT_EN_BCN_FUNCTION	BIT(3)
#define BIT_EN_TXBCN_RPT	BIT(2)
#define REG_BCN_CTRL_CLINT0	0x0551
#define REG_DRVERLYINT		0x0558
#define REG_BCNDMATIM		0x0559
#define REG_ATIMWND		0x055A
#define REG_USTIME_TSF		0x055C
#define REG_BCN_MAX_ERR		0x055D
#define REG_RXTSF_OFFSET_CCK	0x055E
#define REG_MISC_CTRL		0x0577
#define BIT_EN_FREE_CNT		BIT(3)
#define BIT_DIS_SECOND_CCA	(BIT(0) | BIT(1))
#define REG_HIQ_NO_LMT_EN	0x5A7
#define REG_DTIM_COUNTER_ROOT	0x5A8
#define BIT_HIQ_NO_LMT_EN_ROOT	BIT(0)
#define REG_TIMER0_SRC_SEL	0x05B4
#define BIT_TSFT_SEL_TIMER0	(BIT(4) | BIT(5) | BIT(6))
#define REG_TCR			0x0604
#define BIT_PWRMGT_HWDATA_EN	BIT(7)
#define BIT_TCR_UPDATE_TIMIE	BIT(5)
#define BIT_TCR_UPDATE_HGQMD	BIT(4)
#define REG_RCR			0x0608
#define BIT_APP_FCS		BIT(31)
#define BIT_APP_MIC		BIT(30)
#define BIT_APP_ICV		BIT(29)
#define BIT_APP_PHYSTS		BIT(28)
#define BIT_APP_BASSN		BIT(27)
#define BIT_VHT_DACK		BIT(26)
#define BIT_TCPOFLD_EN		BIT(25)
#define BIT_ENMBID		BIT(24)
#define BIT_LSIGEN		BIT(23)
#define BIT_MFBEN		BIT(22)
#define BIT_DISCHKPPDLLEN	BIT(21)
#define BIT_PKTCTL_DLEN		BIT(20)
#define BIT_DISGCLK		BIT(19)
#define BIT_TIM_PARSER_EN	BIT(18)
#define BIT_BC_MD_EN		BIT(17)
#define BIT_UC_MD_EN		BIT(16)
#define BIT_RXSK_PERPKT		BIT(15)
#define BIT_HTC_LOC_CTRL	BIT(14)
#define BIT_RPFM_CAM_ENABLE	BIT(12)
#define BIT_TA_BCN		BIT(11)
#define BIT_RCR_ADF		BIT(11)
#define BIT_DISDECMYPKT		BIT(10)
#define BIT_AICV		BIT(9)
#define BIT_ACRC32		BIT(8)
#define BIT_CBSSID_BCN		BIT(7)
#define BIT_CBSSID_DATA		BIT(6)
#define BIT_APWRMGT		BIT(5)
#define BIT_ADD3		BIT(4)
#define BIT_AB			BIT(3)
#define BIT_AM			BIT(2)
#define BIT_APM			BIT(1)
#define BIT_AAP			BIT(0)
#define REG_RX_PKT_LIMIT	0x060C
#define REG_RX_DRVINFO_SZ	0x060F
#define REG_MAR			0x0620
#define REG_USTIME_EDCA		0x0638
#define REG_ACKTO_CCK		0x0639
#define REG_MAC_SPEC_SIFS	0x063A
#define REG_RESP_SIFS_CCK	0x063C
#define REG_RESP_SIFS_OFDM	0x063E
#define REG_ACKTO		0x0640
#define REG_EIFS		0x0642
#define REG_NAV_CTRL		0x0650
#define REG_WMAC_TRXPTCL_CTL	0x0668
#define BIT_RFMOD		(BIT(7) | BIT(8))
#define BIT_RFMOD_80M		BIT(8)
#define BIT_RFMOD_40M		BIT(7)
#define REG_WMAC_TRXPTCL_CTL_H	0x066C
#define REG_WKFMCAM_CMD		0x0698
#define BIT_WKFCAM_POLLING_V1	BIT(31)
#define BIT_WKFCAM_CLR_V1	BIT(30)
#define BIT_WKFCAM_WE		BIT(16)
#define BIT_SHIFT_WKFCAM_ADDR_V2	8
#define BIT_MASK_WKFCAM_ADDR_V2		0xff
#define BIT_WKFCAM_ADDR_V2(x)						       \
	(((x) & BIT_MASK_WKFCAM_ADDR_V2) << BIT_SHIFT_WKFCAM_ADDR_V2)
#define REG_WKFMCAM_RWD         0x069C
#define BIT_WKFMCAM_VALID	BIT(31)
#define BIT_WKFMCAM_BC		BIT(26)
#define BIT_WKFMCAM_MC		BIT(25)
#define BIT_WKFMCAM_UC		BIT(24)
#define REG_RXFLTMAP0		0x06A0
#define REG_RXFLTMAP1		0x06A2
#define REG_RXFLTMAP2		0x06A4
#define REG_RXFLTMAP4		0x068A
#define REG_BT_COEX_TABLE0	0x06C0
#define REG_BT_COEX_TABLE1	0x06C4
#define REG_BT_COEX_BRK_TABLE	0x06C8
#define REG_BT_COEX_TABLE_H	0x06CC
#define REG_BT_COEX_TABLE_H1	0x06CD
#define REG_BT_COEX_TABLE_H2	0x06CE
#define REG_BT_COEX_TABLE_H3	0x06CF
#define REG_BBPSF_CTRL		0x06DC
#define REG_BT_COEX_V2		0x0762
#define BIT_GNT_BT_POLARITY	BIT(12)
#define BIT_LTE_COEX_EN		BIT(7)
#define REG_BT_COEX_ENH_INTR_CTRL	0x76E
#define BIT_R_GRANTALL_WLMASK	BIT(3)
#define BIT_STATIS_BT_EN	BIT(2)
#define REG_BT_ACT_STATISTICS	0x0770
#define REG_BT_ACT_STATISTICS_1	0x0774
#define REG_BT_STAT_CTRL	0x0778
#define REG_BT_TDMA_TIME	0x0790
#define BIT_MASK_SAMPLE_RATE	GENMASK(5, 0)
#define REG_LTR_IDLE_LATENCY	0x0798
#define REG_LTR_ACTIVE_LATENCY	0x079C
#define REG_LTR_CTRL_BASIC	0x07A4
#define REG_WMAC_OPTION_FUNCTION 0x07D0
#define REG_WMAC_OPTION_FUNCTION_1 0x07D4
#define REG_FPGA0_RFMOD		0x0800
#define BIT_CCKEN		BIT(24)
#define BIT_OFDMEN		BIT(25)
#define REG_RX_GAIN_EN		0x081c
#define REG_RFE_CTRL_E		0x0974
#define REG_2ND_CCA_CTRL	0x0976
#define REG_CCK0_FAREPORT	0xa2c
#define BIT_CCK0_2RX		BIT(18)
#define BIT_CCK0_MRC		BIT(22)
#define REG_DIS_DPD		0x0a70
#define DIS_DPD_MASK		GENMASK(9, 0)
#define DIS_DPD_RATE6M		BIT(0)
#define DIS_DPD_RATE9M		BIT(1)
#define DIS_DPD_RATEMCS0	BIT(2)
#define DIS_DPD_RATEMCS1	BIT(3)
#define DIS_DPD_RATEMCS8	BIT(4)
#define DIS_DPD_RATEMCS9	BIT(5)
#define DIS_DPD_RATEVHT1SS_MCS0	BIT(6)
#define DIS_DPD_RATEVHT1SS_MCS1	BIT(7)
#define DIS_DPD_RATEVHT2SS_MCS0	BIT(8)
#define DIS_DPD_RATEVHT2SS_MCS1	BIT(9)
#define DIS_DPD_RATEALL		GENMASK(9, 0)
#define REG_RFE_CTRL8		0x0cb4
#define BIT_MASK_RFE_SEL89	GENMASK(7, 0)
#define REG_RFE_INV8		0x0cbd
#define BIT_MASK_RFE_INV89	GENMASK(1, 0)
#define REG_RFE_INV16		0x0cbe
#define BIT_RFE_BUF_EN		BIT(3)
#define REG_ANAPARSW_MAC_0	0x1010
#define BIT_CF_L_V2		GENMASK(29, 28)
#define REG_ANAPAR_XTAL_0	0x1040
#define BIT_XCAP_0		GENMASK(23, 10)
#define REG_CPU_DMEM_CON	0x1080
#define BIT_WL_PLATFORM_RST	BIT(16)
#define BIT_WL_SECURITY_CLK	BIT(15)
#define BIT_DDMA_EN		BIT(8)
#define REG_SW_MDIO		0x10C0
#define REG_H2C_PKT_READADDR	0x10D0
#define REG_H2C_PKT_WRITEADDR	0x10D4
#define REG_FW_DBG6		0x10F8
#define REG_FW_DBG7		0x10FC
#define FW_KEY_MASK		0xffffff00
#define REG_CR_EXT		0x1100
#define REG_FT1IMR		0x1138
#define BIT_FS_H2C_CMD_OK_INT_EN BIT(25)
#define REG_FT1ISR		0x113c
#define BIT_FS_H2C_CMD_OK_INT	BIT(25)
#define REG_DDMA_CH0SA		0x1200
#define REG_DDMA_CH0DA		0x1204
#define REG_DDMA_CH0CTRL	0x1208
#define BIT_DDMACH0_OWN		BIT(31)
#define BIT_DDMACH0_CHKSUM_EN	BIT(29)
#define BIT_DDMACH0_CHKSUM_STS	BIT(27)
#define BIT_DDMACH0_DDMA_MODE	BIT(26)
#define BIT_DDMACH0_RESET_CHKSUM_STS BIT(25)
#define BIT_DDMACH0_CHKSUM_CONT	BIT(24)
#define BIT_MASK_DDMACH0_DLEN	0x3ffff
#define REG_H2CQ_CSR		0x1330
#define BIT_H2CQ_FULL		BIT(31)
#define REG_FAST_EDCA_VOVI_SETTING 0x1448
#define REG_FAST_EDCA_BEBK_SETTING 0x144C
#define REG_RXPSF_CTRL		0x1610
#define BIT_RXGCK_FIFOTHR_EN	BIT(28)
#define BIT_SHIFT_RXGCK_VHT_FIFOTHR 26
#define BIT_MASK_RXGCK_VHT_FIFOTHR 0x3
#define BIT_RXGCK_VHT_FIFOTHR(x)                                               \
	(((x) & BIT_MASK_RXGCK_VHT_FIFOTHR) << BIT_SHIFT_RXGCK_VHT_FIFOTHR)
#define BITS_RXGCK_VHT_FIFOTHR                                                 \
	(BIT_MASK_RXGCK_VHT_FIFOTHR << BIT_SHIFT_RXGCK_VHT_FIFOTHR)
#define BIT_SHIFT_RXGCK_HT_FIFOTHR 24
#define BIT_MASK_RXGCK_HT_FIFOTHR 0x3
#define BIT_RXGCK_HT_FIFOTHR(x)                                                \
	(((x) & BIT_MASK_RXGCK_HT_FIFOTHR) << BIT_SHIFT_RXGCK_HT_FIFOTHR)
#define BITS_RXGCK_HT_FIFOTHR                                                  \
	(BIT_MASK_RXGCK_HT_FIFOTHR << BIT_SHIFT_RXGCK_HT_FIFOTHR)
#define BIT_SHIFT_RXGCK_OFDM_FIFOTHR 22
#define BIT_MASK_RXGCK_OFDM_FIFOTHR 0x3
#define BIT_RXGCK_OFDM_FIFOTHR(x)                                              \
	(((x) & BIT_MASK_RXGCK_OFDM_FIFOTHR) << BIT_SHIFT_RXGCK_OFDM_FIFOTHR)
#define BITS_RXGCK_OFDM_FIFOTHR                                                \
	(BIT_MASK_RXGCK_OFDM_FIFOTHR << BIT_SHIFT_RXGCK_OFDM_FIFOTHR)
#define BIT_SHIFT_RXGCK_CCK_FIFOTHR 20
#define BIT_MASK_RXGCK_CCK_FIFOTHR 0x3
#define BIT_RXGCK_CCK_FIFOTHR(x)                                               \
	(((x) & BIT_MASK_RXGCK_CCK_FIFOTHR) << BIT_SHIFT_RXGCK_CCK_FIFOTHR)
#define BITS_RXGCK_CCK_FIFOTHR                                                 \
	(BIT_MASK_RXGCK_CCK_FIFOTHR << BIT_SHIFT_RXGCK_CCK_FIFOTHR)
#define BIT_RXGCK_OFDMCCA_EN BIT(16)
#define BIT_SHIFT_RXPSF_PKTLENTHR 13
#define BIT_MASK_RXPSF_PKTLENTHR 0x7
#define BIT_RXPSF_PKTLENTHR(x)                                                 \
	(((x) & BIT_MASK_RXPSF_PKTLENTHR) << BIT_SHIFT_RXPSF_PKTLENTHR)
#define BITS_RXPSF_PKTLENTHR                                                   \
	(BIT_MASK_RXPSF_PKTLENTHR << BIT_SHIFT_RXPSF_PKTLENTHR)
#define BIT_CLEAR_RXPSF_PKTLENTHR(x) ((x) & (~BITS_RXPSF_PKTLENTHR))
#define BIT_SET_RXPSF_PKTLENTHR(x, v)                                          \
	(BIT_CLEAR_RXPSF_PKTLENTHR(x) | BIT_RXPSF_PKTLENTHR(v))
#define BIT_RXPSF_CTRLEN	BIT(12)
#define BIT_RXPSF_VHTCHKEN	BIT(11)
#define BIT_RXPSF_HTCHKEN	BIT(10)
#define BIT_RXPSF_OFDMCHKEN	BIT(9)
#define BIT_RXPSF_CCKCHKEN	BIT(8)
#define BIT_RXPSF_OFDMRST	BIT(7)
#define BIT_RXPSF_CCKRST	BIT(6)
#define BIT_RXPSF_MHCHKEN	BIT(5)
#define BIT_RXPSF_CONT_ERRCHKEN	BIT(4)
#define BIT_RXPSF_ALL_ERRCHKEN	BIT(3)
#define BIT_SHIFT_RXPSF_ERRTHR 0
#define BIT_MASK_RXPSF_ERRTHR 0x7
#define BIT_RXPSF_ERRTHR(x)                                                    \
	(((x) & BIT_MASK_RXPSF_ERRTHR) << BIT_SHIFT_RXPSF_ERRTHR)
#define BITS_RXPSF_ERRTHR (BIT_MASK_RXPSF_ERRTHR << BIT_SHIFT_RXPSF_ERRTHR)
#define BIT_CLEAR_RXPSF_ERRTHR(x) ((x) & (~BITS_RXPSF_ERRTHR))
#define BIT_GET_RXPSF_ERRTHR(x)                                                \
	(((x) >> BIT_SHIFT_RXPSF_ERRTHR) & BIT_MASK_RXPSF_ERRTHR)
#define BIT_SET_RXPSF_ERRTHR(x, v)                                             \
	(BIT_CLEAR_RXPSF_ERRTHR(x) | BIT_RXPSF_ERRTHR(v))
#define REG_RXPSF_TYPE_CTRL	0x1614
#define REG_GENERAL_OPTION	0x1664
#define BIT_DUMMY_FCS_READY_MASK_EN BIT(9)
#define REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1		0x1700
#define REG_WL2LTECOEX_INDIRECT_ACCESS_WRITE_DATA_V1	0x1704
#define REG_WL2LTECOEX_INDIRECT_ACCESS_READ_DATA_V1	0x1708
#define LTECOEX_READY		BIT(29)
#define LTECOEX_ACCESS_CTRL REG_WL2LTECOEX_INDIRECT_ACCESS_CTRL_V1
#define LTECOEX_WRITE_DATA REG_WL2LTECOEX_INDIRECT_ACCESS_WRITE_DATA_V1
#define LTECOEX_READ_DATA REG_WL2LTECOEX_INDIRECT_ACCESS_READ_DATA_V1
#define REG_IGN_GNT_BT1	0x1860
#define REG_RFESEL_CTRL	0x1990
#define REG_NOMASK_TXBT	0x1ca7
#define REG_ANAPAR	0x1c30
#define BIT_ANAPAR_BTPS	BIT(22)
#define REG_RSTB_SEL	0x1c38
#define BIT_DAC_OFF_ENABLE	BIT(4)
#define BIT_PI_IGNORE_GNT_BT	BIT(3)
#define BIT_NOMASK_TXBT_ENABLE	BIT(3)
#define REG_HRCV_MSG	0x1cf
#define REG_EDCCA_REPORT	0x2d38
#define BIT_EDCCA_FLAG		BIT(24)
#define REG_IGN_GNTBT4	0x4160
#define RF_MODE		0x00
#define RF_MODOPT	0x01
#define RF_WLINT	0x01
#define RF_WLSEL	0x02
#define RF_DTXLOK	0x08
#define RF_CFGCH	0x18
#define BIT_BAND	GENMASK(18, 16)
#define RF_RCK		0x1d
#define RF_LUTWA	0x33
#define RF_LUTWD1	0x3e
#define RF_LUTWD0	0x3f
#define BIT_GAIN_EXT	BIT(12)
#define BIT_DATA_L	GENMASK(11, 0)
#define RF_T_METER	0x42
#define RF_BSPAD	0x54
#define RF_GAINTX	0x56
#define RF_TXATANK	0x64
#define RF_TRXIQ	0x66
#define RF_RXIQGEN	0x8d
#define RF_SYN_PFD	0xb0
#define RF_XTALX2	0xb8
#define RF_SYN_CTRL	0xbb
#define RF_MALSEL	0xbe
#define RF_SYN_AAC	0xc9
#define RF_AAC_CTRL	0xca
#define RF_FAST_LCK	0xcc
#define RF_RCKD		0xde
#define RF_TXADBG	0xde
#define RF_LUTDBG	0xdf
#define BIT_TXA_TANK	BIT(4)
#define RF_LUTWE2	0xee
#define RF_LUTWE	0xef
#define LTE_COEX_CTRL	0x38
#define LTE_WL_TRX_CTRL	0xa0
#define LTE_BT_TRX_CTRL	0xa4
#define RCR_VHT_ACK		BIT(26)
#define BIT_FEN_PCIEA BIT(6)
#define WLAN_SLOT_TIME		0x09
#define WLAN_PIFS_TIME		0x19
#define WLAN_SIFS_CCK_CONT_TX	0xA
#define WLAN_SIFS_OFDM_CONT_TX	0xE
#define WLAN_SIFS_CCK_TRX	0x10
#define WLAN_SIFS_OFDM_TRX	0x10
#define WLAN_VO_TXOP_LIMIT	0x186
#define WLAN_VI_TXOP_LIMIT	0x3BC
#define WLAN_RDG_NAV		0x05
#define WLAN_TXOP_NAV		0x1B
#define WLAN_CCK_RX_TSF		0x30
#define WLAN_OFDM_RX_TSF	0x30
#define WLAN_TBTT_PROHIBIT	0x04
#define WLAN_TBTT_HOLD_TIME	0x064
#define WLAN_DRV_EARLY_INT	0x04
#define WLAN_BCN_DMA_TIME	0x02
#define WLAN_RX_FILTER0		0x0FFFFFFF
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_RCR_CFG		0xE400220E
#define WLAN_RXPKT_MAX_SZ	12288
#define WLAN_RXPKT_MAX_SZ_512	(WLAN_RXPKT_MAX_SZ >> 9)
#define WLAN_AMPDU_MAX_TIME		0x70
#define WLAN_RTS_LEN_TH			0xFF
#define WLAN_RTS_TX_TIME_TH		0x08
#define WLAN_MAX_AGG_PKT_LIMIT		0x20
#define WLAN_RTS_MAX_AGG_PKT_LIMIT	0x20
#define FAST_EDCA_VO_TH		0x06
#define FAST_EDCA_VI_TH		0x06
#define FAST_EDCA_BE_TH		0x06
#define FAST_EDCA_BK_TH		0x06
#define WLAN_BAR_RETRY_LIMIT		0x01
#define WLAN_RA_TRY_RATE_AGG_LIMIT	0x08
#define WLAN_TX_FUNC_CFG1		0x30
#define WLAN_TX_FUNC_CFG2		0x30
#define WLAN_MAC_OPT_NORM_FUNC1		0x98
#define WLAN_MAC_OPT_LB_FUNC1		0x80
#define WLAN_MAC_OPT_FUNC2		0xb0810041
#define WLAN_SIFS_CFG	(WLAN_SIFS_CCK_CONT_TX | \
			(WLAN_SIFS_OFDM_CONT_TX << BIT_SHIFT_SIFS_OFDM_CTX) | \
			(WLAN_SIFS_CCK_TRX << BIT_SHIFT_SIFS_CCK_TRX) | \
			(WLAN_SIFS_OFDM_TRX << BIT_SHIFT_SIFS_OFDM_TRX))
#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))
#define WLAN_NAV_CFG		(WLAN_RDG_NAV | (WLAN_TXOP_NAV << 16))
#define WLAN_RX_TSF_CFG		(WLAN_CCK_RX_TSF | (WLAN_OFDM_RX_TSF) << 8)
#define WLAN_PRE_TXCNT_TIME_TH		0x1E4
#define GET_PHY_STAT_P0_PWDB(phy_stat)                                         \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))
#define GET_PHY_STAT_P0_VGA(phy_stat)                                          \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), GENMASK(12, 8))
#define GET_PHY_STAT_P0_LNA_L(phy_stat)                                        \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), GENMASK(15, 13))
#define GET_PHY_STAT_P0_LNA_H(phy_stat)                                        \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), BIT(23))
#define BIT_LNA_H_MASK BIT(3)
#define BIT_LNA_L_MASK GENMASK(2, 0)
#define GET_PHY_STAT_P1_PWDB_A(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(15, 8))
#define GET_PHY_STAT_P1_PWDB_B(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x00), GENMASK(23, 16))
#define GET_PHY_STAT_P1_RF_MODE(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x03), GENMASK(29, 28))
#define GET_PHY_STAT_P1_L_RXSC(phy_stat)                                       \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x01), GENMASK(11, 8))
#define GET_PHY_STAT_P1_HT_RXSC(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x01), GENMASK(15, 12))
#define GET_PHY_STAT_P1_RXEVM_A(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x04), GENMASK(7, 0))
#define GET_PHY_STAT_P1_RXEVM_B(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x04), GENMASK(15, 8))
#define GET_PHY_STAT_P1_CFO_TAIL_A(phy_stat)                                 \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x05), GENMASK(7, 0))
#define GET_PHY_STAT_P1_CFO_TAIL_B(phy_stat)                                 \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x05), GENMASK(15, 8))
#define GET_PHY_STAT_P1_RXSNR_A(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x06), GENMASK(7, 0))
#define GET_PHY_STAT_P1_RXSNR_B(phy_stat)                                      \
	le32_get_bits(*((__le32 *)(phy_stat) + 0x06), GENMASK(15, 8))
#define REG_SYS_CTRL	0x000
#define BIT_FEN_EN	BIT(26)
#define REG_INIRTS_RATE_SEL 0x0480
#define REG_HTSTFWT	0x800
#define REG_RXPSEL	0x808
#define BIT_RX_PSEL_RST		(BIT(28) | BIT(29))
#define REG_TXPSEL	0x80c
#define REG_RXCCAMSK	0x814
#define REG_CCASEL	0x82c
#define REG_PDMFTH	0x830
#define REG_CCA2ND	0x838
#define REG_L1WT	0x83c
#define REG_L1PKWT	0x840
#define REG_MRC		0x850
#define REG_CLKTRK	0x860
#define REG_ADCCLK	0x8ac
#define REG_ADC160	0x8c4
#define REG_ADC40	0x8c8
#define REG_CHFIR	0x8f0
#define REG_CDDTXP	0x93c
#define REG_TXPSEL1	0x940
#define REG_ACBB0	0x948
#define REG_ACBBRXFIR	0x94c
#define REG_ACGG2TBL	0x958
#define REG_FAS		0x9a4
#define REG_RXSB	0xa00
#define REG_ADCINI	0xa04
#define REG_PWRTH	0xa08
#define REG_CCA_FLTR	0xa20
#define REG_TXSF2	0xa24
#define REG_TXSF6	0xa28
#define REG_FA_CCK	0xa5c
#define REG_RXDESC	0xa2c
#define REG_ENTXCCK	0xa80
#define BTG_LNA		0xfc84
#define WLG_LNA		0x7532
#define REG_ENRXCCA	0xa84
#define BTG_CCA		0x0e
#define WLG_CCA		0x12
#define REG_PWRTH2	0xaa8
#define REG_CSRATIO	0xaaa
#define REG_TXFILTER	0xaac
#define REG_CNTRST	0xb58
#define REG_AGCTR_A	0xc08
#define REG_TXSCALE_A	0xc1c
#define REG_TXDFIR	0xc20
#define REG_RXIGI_A	0xc50
#define REG_TXAGCIDX	0xc94
#define REG_TRSW	0xca0
#define REG_RFESEL0	0xcb0
#define REG_RFESEL8	0xcb4
#define REG_RFECTL	0xcb8
#define B_BTG_SWITCH	BIT(16)
#define B_CTRL_SWITCH	BIT(18)
#define B_WL_SWITCH	(BIT(20) | BIT(22))
#define B_WLG_SWITCH	BIT(21)
#define B_WLA_SWITCH	BIT(23)
#define REG_RFEINV	0xcbc
#define REG_AGCTR_B	0xe08
#define REG_RXIGI_B	0xe50
#define REG_CRC_CCK	0xf04
#define REG_CRC_OFDM	0xf14
#define REG_CRC_HT	0xf10
#define REG_CRC_VHT	0xf0c
#define REG_CCA_OFDM	0xf08
#define REG_FA_OFDM	0xf48
#define REG_CCA_CCK	0xfcc
#define REG_DMEM_CTRL	0x1080
#define BIT_WL_RST	BIT(16)
#define REG_ANTWT	0x1904
#define REG_IQKFAILMSK	0x1bf0
#define BIT_MASK_R_RFE_SEL_15	GENMASK(31, 28)
#define BIT_SDIO_INT BIT(18)
#define BT_CNT_ENABLE	0x1
#define BIT_BCN_QUEUE	BIT(3)
#define BCN_PRI_EN	0x1
#define PTA_CTRL_PIN	0x66
#define DPDT_CTRL_PIN	0x77
#define ANTDIC_CTRL_PIN	0x88
#define REG_CTRL_TYPE	0x67
#define BIT_CTRL_TYPE1	BIT(5)
#define BIT_CTRL_TYPE2	BIT(4)
#define CTRL_TYPE_MASK	GENMASK(15, 8)
#define RF18_BAND_MASK		(BIT(16) | BIT(9) | BIT(8))
#define RF18_BAND_2G		(0)
#define RF18_BAND_5G		(BIT(16) | BIT(8))
#define RF18_CHANNEL_MASK	(MASKBYTE0)
#define RF18_RFSI_MASK		(BIT(18) | BIT(17))
#define RF18_RFSI_GE		(BIT(17))
#define RF18_RFSI_GT		(BIT(18))
#define RF18_BW_MASK		(BIT(11) | BIT(10))
#define RF18_BW_20M		(BIT(11) | BIT(10))
#define RF18_BW_40M		(BIT(11))
#define RF18_BW_80M		(BIT(10))
#define RTW_HW_PORT_NUM		5
#define cut_version_to_mask(cut) (0x1 << ((cut) + 1))
#define DDMA_POLLING_COUNT	1000
#define C2H_PKT_BUF		256
#define REPORT_BUF		128
#define PHY_STATUS_SIZE		4
#define ILLEGAL_KEY_GROUP	0xFAAAAA00
#define OCPBASE_RXBUF_FW_88XX		0x18680000
#define OCPBASE_TXBUF_88XX		0x18780000
#define OCPBASE_ROM_88XX		0x00000000
#define OCPBASE_IMEM_88XX		0x00030000
#define OCPBASE_DMEM_88XX		0x00200000
#define OCPBASE_EMEM_88XX		0x00100000
#define RSVD_PG_DRV_NUM			16
#define RSVD_PG_H2C_EXTRAINFO_NUM	24
#define RSVD_PG_H2C_STATICINFO_NUM	8
#define RSVD_PG_H2CQ_NUM		8
#define RSVD_PG_CPU_INSTRUCTION_NUM	0
#define RSVD_PG_FW_TXBUF_NUM		4
#define H2C_PKT_SIZE		32
#define H2C_PKT_HDR_SIZE	8
#define FW_HDR_SIZE			64
#define FW_HDR_CHKSUM_SIZE		8
#define FW_NLO_INFO_CHECK_SIZE		4
#define FIFO_PAGE_SIZE_SHIFT		12
#define FIFO_PAGE_SIZE			4096
#define FIFO_DUMP_ADDR			0x8000
#define DLFW_PAGE_SIZE_SHIFT_LEGACY	12
#define DLFW_PAGE_SIZE_LEGACY		0x1000
#define DLFW_BLK_SIZE_SHIFT_LEGACY	2
#define DLFW_BLK_SIZE_LEGACY		4
#define FW_START_ADDR_LEGACY		0x1000
#define BCN_LOSS_CNT			10
#define BCN_FILTER_NOTIFY_SIGNAL_CHANGE	0
#define BCN_FILTER_CONNECTION_LOSS	1
#define BCN_FILTER_CONNECTED		2
#define BCN_FILTER_NOTIFY_BEACON_LOSS	3
#define RTW_DEFAULT_CQM_THOLD		-70
#define RTW_DEFAULT_CQM_HYST		4
#define SCAN_NOTIFY_TIMEOUT  msecs_to_jiffies(10)
#define RTW_CHANNEL_TIME		45
#define RTW_OFF_CHAN_TIME		100
#define RTW_PASS_CHAN_TIME		105
#define RTW_DFS_CHAN_TIME		20
#define RTW_CH_INFO_SIZE		4
#define RTW_EX_CH_INFO_SIZE		3
#define RTW_EX_CH_INFO_HDR_SIZE		2
#define RTW_SCAN_WIDTH			0
#define RTW_PRI_CH_IDX			1
#define RTW_OLD_PROBE_PG_CNT		2
#define RTW_PROBE_PG_CNT		4
#define RTW_DEBUG_DUMP_TIMES		10
#define RTW_H2C_W0_CMDID		GENMASK(7, 0)
#define RTW_H2C_DEFAULT_PORT_W0_PORTID	GENMASK(15, 8)
#define RTW_H2C_DEFAULT_PORT_W0_MACID	GENMASK(23, 16)
#define RTW_FW_VER_CODE(ver, sub_ver, idx)	\
	(((ver) << 16) | ((sub_ver) << 8) | (idx))
#define RTW_FW_SUIT_VER_CODE(s)	\
	RTW_FW_VER_CODE((s).version, (s).sub_version, (s).sub_index)
#define GET_CCX_REPORT_SEQNUM_V0(c2h_payload)	(c2h_payload[6] & 0xfc)
#define GET_CCX_REPORT_STATUS_V0(c2h_payload)	(c2h_payload[0] & 0xc0)
#define GET_CCX_REPORT_SEQNUM_V1(c2h_payload)	(c2h_payload[8] & 0xfc)
#define GET_CCX_REPORT_STATUS_V1(c2h_payload)	(c2h_payload[9] & 0xc0)
#define GET_SCAN_REPORT_RETURN_CODE(c2h_payload)	(c2h_payload[2] & 0xff)
#define GET_CHAN_SWITCH_CENTRAL_CH(c2h_payload)	(c2h_payload[2])
#define GET_CHAN_SWITCH_ID(c2h_payload)		(c2h_payload[3])
#define GET_CHAN_SWITCH_STATUS(c2h_payload)	(c2h_payload[4])
#define GET_RA_REPORT_RATE(c2h_payload)		(c2h_payload[0] & 0x7f)
#define GET_RA_REPORT_SGI(c2h_payload)		((c2h_payload[0] & 0x80) >> 7)
#define GET_RA_REPORT_BW(c2h_payload)		(c2h_payload[6])
#define GET_RA_REPORT_MACID(c2h_payload)	(c2h_payload[1])
#define GET_BCN_FILTER_NOTIFY_TYPE(c2h_payload)	(c2h_payload[1] & 0xf)
#define GET_BCN_FILTER_NOTIFY_EVENT(c2h_payload)	(c2h_payload[1] & 0x10)
#define GET_BCN_FILTER_NOTIFY_RSSI(c2h_payload)	(c2h_payload[2] - 100)
#define H2C_PKT_CMD_ID 0xFF
#define H2C_PKT_CATEGORY 0x01
#define H2C_PKT_GENERAL_INFO 0x0D
#define H2C_PKT_PHYDM_INFO 0x11
#define H2C_PKT_IQK 0x0E
#define H2C_PKT_CH_SWITCH 0x02
#define H2C_PKT_UPDATE_PKT 0x0C
#define H2C_PKT_SCAN_OFFLOAD 0x19
#define H2C_PKT_CH_SWITCH_LEN 0x20
#define H2C_PKT_UPDATE_PKT_LEN 0x4
#define SET_PKT_H2C_CATEGORY(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(6, 0))
#define SET_PKT_H2C_CMD_ID(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_PKT_H2C_SUB_CMD_ID(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 16))
#define SET_PKT_H2C_TOTAL_LEN(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 0))
#define FW_OFFLOAD_H2C_SET_SEQ_NUM(h2c_pkt, value)                             \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(31, 16))
#define GENERAL_INFO_SET_FW_TX_BOUNDARY(h2c_pkt, value)                        \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define PHYDM_INFO_SET_REF_TYPE(h2c_pkt, value)                                \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(7, 0))
#define PHYDM_INFO_SET_RF_TYPE(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 8))
#define PHYDM_INFO_SET_CUT_VER(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define PHYDM_INFO_SET_RX_ANT_STATUS(h2c_pkt, value)                           \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(27, 24))
#define PHYDM_INFO_SET_TX_ANT_STATUS(h2c_pkt, value)                           \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(31, 28))
#define IQK_SET_CLEAR(h2c_pkt, value)                                          \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(0))
#define IQK_SET_SEGMENT_IQK(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(1))
#define CHSW_INFO_SET_CH(pkt, value)					       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(7, 0))
#define CHSW_INFO_SET_PRI_CH_IDX(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(11, 8))
#define CHSW_INFO_SET_BW(pkt, value)					       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(15, 12))
#define CHSW_INFO_SET_TIMEOUT(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(23, 16))
#define CHSW_INFO_SET_ACTION_ID(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, GENMASK(30, 24))
#define CHSW_INFO_SET_EXTRA_INFO(pkt, value)				       \
	le32p_replace_bits((__le32 *)(pkt) + 0x00, value, BIT(31))
#define CH_INFO_SET_CH(pkt, value)					       \
	u8p_replace_bits((u8 *)(pkt) + 0x00, value, GENMASK(7, 0))
#define CH_INFO_SET_PRI_CH_IDX(pkt, value)				       \
	u8p_replace_bits((u8 *)(pkt) + 0x01, value, GENMASK(3, 0))
#define CH_INFO_SET_BW(pkt, value)					       \
	u8p_replace_bits((u8 *)(pkt) + 0x01, value, GENMASK(7, 4))
#define CH_INFO_SET_TIMEOUT(pkt, value)					       \
	u8p_replace_bits((u8 *)(pkt) + 0x02, value, GENMASK(7, 0))
#define CH_INFO_SET_ACTION_ID(pkt, value)				       \
	u8p_replace_bits((u8 *)(pkt) + 0x03, value, GENMASK(6, 0))
#define CH_INFO_SET_EXTRA_INFO(pkt, value)				       \
	u8p_replace_bits((u8 *)(pkt) + 0x03, value, BIT(7))
#define EXTRA_CH_INFO_SET_ID(pkt, value)				       \
	u8p_replace_bits((u8 *)(pkt) + 0x04, value, GENMASK(6, 0))
#define EXTRA_CH_INFO_SET_INFO(pkt, value)				       \
	u8p_replace_bits((u8 *)(pkt) + 0x04, value, BIT(7))
#define EXTRA_CH_INFO_SET_SIZE(pkt, value)				       \
	u8p_replace_bits((u8 *)(pkt) + 0x05, value, GENMASK(7, 0))
#define EXTRA_CH_INFO_SET_DFS_EXT_TIME(pkt, value)			       \
	u8p_replace_bits((u8 *)(pkt) + 0x06, value, GENMASK(7, 0))
#define UPDATE_PKT_SET_SIZE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 0))
#define UPDATE_PKT_SET_PKT_ID(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define UPDATE_PKT_SET_LOCATION(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(31, 24))
#define CH_SWITCH_SET_START(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(0))
#define CH_SWITCH_SET_DEST_CH_EN(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(1))
#define CH_SWITCH_SET_ABSOLUTE_TIME(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(2))
#define CH_SWITCH_SET_PERIODIC_OPT(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(4, 3))
#define CH_SWITCH_SET_SCAN_MODE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(5))
#define CH_SWITCH_SET_BACK_OP_EN(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(6))
#define CH_SWITCH_SET_INFO_LOC(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 8))
#define CH_SWITCH_SET_CH_NUM(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(23, 16))
#define CH_SWITCH_SET_PRI_CH_IDX(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(27, 24))
#define CH_SWITCH_SET_DEST_BW(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(31, 28))
#define CH_SWITCH_SET_DEST_CH(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(7, 0))
#define CH_SWITCH_SET_NORMAL_PERIOD(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(13, 8))
#define CH_SWITCH_SET_NORMAL_PERIOD_SEL(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(15, 14))
#define CH_SWITCH_SET_SLOW_PERIOD(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(21, 16))
#define CH_SWITCH_SET_SLOW_PERIOD_SEL(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(23, 22))
#define CH_SWITCH_SET_NORMAL_CYCLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(31, 24))
#define CH_SWITCH_SET_TSF_HIGH(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x04, value, GENMASK(31, 0))
#define CH_SWITCH_SET_TSF_LOW(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x05, value, GENMASK(31, 0))
#define CH_SWITCH_SET_INFO_SIZE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x06, value, GENMASK(15, 0))
#define SCAN_OFFLOAD_SET_START(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(0))
#define SCAN_OFFLOAD_SET_BACK_OP_EN(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(1))
#define SCAN_OFFLOAD_SET_RANDOM_SEQ_EN(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(2))
#define SCAN_OFFLOAD_SET_NO_CCK_EN(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(3))
#define SCAN_OFFLOAD_SET_VERBOSE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, BIT(4))
#define SCAN_OFFLOAD_SET_CH_NUM(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(15, 8))
#define SCAN_OFFLOAD_SET_CH_INFO_SIZE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x02, value, GENMASK(31, 16))
#define SCAN_OFFLOAD_SET_CH_INFO_LOC(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(7, 0))
#define SCAN_OFFLOAD_SET_OP_CH(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(15, 8))
#define SCAN_OFFLOAD_SET_OP_PRI_CH_IDX(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(19, 16))
#define SCAN_OFFLOAD_SET_OP_BW(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(23, 20))
#define SCAN_OFFLOAD_SET_OP_PORT_ID(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x03, value, GENMASK(26, 24))
#define SCAN_OFFLOAD_SET_OP_DWELL_TIME(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x04, value, GENMASK(15, 0))
#define SCAN_OFFLOAD_SET_OP_GAP_TIME(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x04, value, GENMASK(31, 16))
#define SCAN_OFFLOAD_SET_MODE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x05, value, GENMASK(3, 0))
#define SCAN_OFFLOAD_SET_SSID_NUM(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x05, value, GENMASK(7, 4))
#define SCAN_OFFLOAD_SET_PKT_LOC(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x05, value, GENMASK(15, 8))
#define H2C_CMD_RSVD_PAGE		0x0
#define H2C_CMD_MEDIA_STATUS_RPT	0x01
#define H2C_CMD_SET_PWR_MODE		0x20
#define H2C_CMD_LPS_PG_INFO		0x2b
#define H2C_CMD_DEFAULT_PORT		0x2c
#define H2C_CMD_RA_INFO			0x40
#define H2C_CMD_RSSI_MONITOR		0x42
#define H2C_CMD_BCN_FILTER_OFFLOAD_P0	0x56
#define H2C_CMD_BCN_FILTER_OFFLOAD_P1	0x57
#define H2C_CMD_WL_PHY_INFO		0x58
#define H2C_CMD_SCAN			0x59
#define H2C_CMD_ADAPTIVITY		0x5A
#define H2C_CMD_COEX_TDMA_TYPE		0x60
#define H2C_CMD_QUERY_BT_INFO		0x61
#define H2C_CMD_FORCE_BT_TX_POWER	0x62
#define H2C_CMD_IGNORE_WLAN_ACTION	0x63
#define H2C_CMD_WL_CH_INFO		0x66
#define H2C_CMD_QUERY_BT_MP_INFO	0x67
#define H2C_CMD_BT_WIFI_CONTROL		0x69
#define H2C_CMD_WIFI_CALIBRATION	0x6d
#define H2C_CMD_QUERY_BT_HID_INFO	0x73
#define H2C_CMD_KEEP_ALIVE		0x03
#define H2C_CMD_DISCONNECT_DECISION	0x04
#define H2C_CMD_WOWLAN			0x80
#define H2C_CMD_REMOTE_WAKE_CTRL	0x81
#define H2C_CMD_AOAC_GLOBAL_INFO	0x82
#define H2C_CMD_NLO_INFO		0x8C
#define H2C_CMD_RECOVER_BT_DEV		0xD1
#define SET_H2C_CMD_ID_CLASS(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(7, 0))
#define MEDIA_STATUS_RPT_SET_OP_MODE(h2c_pkt, value)                           \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define MEDIA_STATUS_RPT_SET_MACID(h2c_pkt, value)                             \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_WL_PHY_INFO_TX_TP(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(17, 8))
#define SET_WL_PHY_INFO_RX_TP(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(27, 18))
#define SET_WL_PHY_INFO_TX_RATE_DESC(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_WL_PHY_INFO_RX_RATE_DESC(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_WL_PHY_INFO_RX_EVM(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(23, 16))
#define SET_BCN_FILTER_OFFLOAD_P1_MACID(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_BCN_FILTER_OFFLOAD_P1_ENABLE(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(16))
#define SET_BCN_FILTER_OFFLOAD_P1_HYST(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(20, 17))
#define SET_BCN_FILTER_OFFLOAD_P1_OFFLOAD_MODE(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 21))
#define SET_BCN_FILTER_OFFLOAD_P1_THRESHOLD(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BCN_FILTER_OFFLOAD_P1_BCN_LOSS_CNT(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(3, 0))
#define SET_BCN_FILTER_OFFLOAD_P1_BCN_INTERVAL(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(13, 4))
#define SET_SCAN_START(h2c_pkt, value)					       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_ADAPTIVITY_MODE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(11, 8))
#define SET_ADAPTIVITY_OPTION(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 12))
#define SET_ADAPTIVITY_IGI(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_ADAPTIVITY_L2H(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_ADAPTIVITY_DENSITY(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_PWR_MODE_SET_MODE(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(14, 8))
#define SET_PWR_MODE_SET_RLBM(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(19, 16))
#define SET_PWR_MODE_SET_SMART_PS(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 20))
#define SET_PWR_MODE_SET_AWAKE_INTERVAL(h2c_pkt, value)                        \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_PWR_MODE_SET_PORT_ID(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 5))
#define SET_PWR_MODE_SET_PWR_STATE(h2c_pkt, value)                             \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define LPS_PG_INFO_LOC(h2c_pkt, value)                                        \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define LPS_PG_DPK_LOC(h2c_pkt, value)                                         \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define LPS_PG_SEC_CAM_EN(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define LPS_PG_PATTERN_CAM_EN(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_RSSI_INFO_MACID(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_RSSI_INFO_RSSI(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_RSSI_INFO_STBC(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, BIT(1))
#define SET_RA_INFO_MACID(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_RA_INFO_RATE_ID(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(20, 16))
#define SET_RA_INFO_INIT_RA_LVL(h2c_pkt, value)                                \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(22, 21))
#define SET_RA_INFO_SGI_EN(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(23))
#define SET_RA_INFO_BW_MODE(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(25, 24))
#define SET_RA_INFO_LDPC(h2c_pkt, value)                                       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(26))
#define SET_RA_INFO_NO_UPDATE(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(27))
#define SET_RA_INFO_VHT_EN(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(29, 28))
#define SET_RA_INFO_DIS_PT(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(30))
#define SET_RA_INFO_RA_MASK0(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_RA_INFO_RA_MASK1(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_RA_INFO_RA_MASK2(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(23, 16))
#define SET_RA_INFO_RA_MASK3(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(31, 24))
#define SET_QUERY_BT_INFO(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_WL_CH_INFO_LINK(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_WL_CH_INFO_CHNL(h2c_pkt, value)                                    \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_WL_CH_INFO_BW(h2c_pkt, value)                                      \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BT_MP_INFO_SEQ(h2c_pkt, value)                                     \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 12))
#define SET_BT_MP_INFO_OP_CODE(h2c_pkt, value)                                 \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_BT_MP_INFO_PARA1(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BT_MP_INFO_PARA2(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_BT_MP_INFO_PARA3(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_BT_TX_POWER_INDEX(h2c_pkt, value)                                  \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_IGNORE_WLAN_ACTION_EN(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_COEX_TDMA_TYPE_PARA1(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_COEX_TDMA_TYPE_PARA2(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_COEX_TDMA_TYPE_PARA3(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_COEX_TDMA_TYPE_PARA4(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_COEX_TDMA_TYPE_PARA5(h2c_pkt, value)                               \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_BT_WIFI_CONTROL_OP_CODE(h2c_pkt, value)                            \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_BT_WIFI_CONTROL_DATA1(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_BT_WIFI_CONTROL_DATA2(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_BT_WIFI_CONTROL_DATA3(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(7, 0))
#define SET_BT_WIFI_CONTROL_DATA4(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(15, 8))
#define SET_BT_WIFI_CONTROL_DATA5(h2c_pkt, value)                              \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x01, value, GENMASK(23, 16))
#define SET_COEX_QUERY_HID_INFO_SUBID(h2c_pkt, value)                          \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_COEX_QUERY_HID_INFO_DATA1(h2c_pkt, value)                          \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_KEEP_ALIVE_ENABLE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_KEEP_ALIVE_ADOPT(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_KEEP_ALIVE_PKT_TYPE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_KEEP_ALIVE_CHECK_PERIOD(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_DISCONNECT_DECISION_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_DISCONNECT_DECISION_ADOPT(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_DISCONNECT_DECISION_CHECK_PERIOD(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_DISCONNECT_DECISION_TRY_PKT_NUM(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(31, 24))
#define SET_WOWLAN_FUNC_ENABLE(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_WOWLAN_PATTERN_MATCH_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_WOWLAN_MAGIC_PKT_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_WOWLAN_UNICAST_PKT_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(11))
#define SET_WOWLAN_REKEY_WAKEUP_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(14))
#define SET_WOWLAN_DEAUTH_WAKEUP_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(15))
#define SET_REMOTE_WAKECTRL_ENABLE(h2c_pkt, value)			       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_REMOTE_WAKE_CTRL_NLO_OFFLOAD_EN(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(12))
#define SET_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(15, 8))
#define SET_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(h2c_pkt, value)		       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_NLO_FUN_EN(h2c_pkt, value)                                         \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define SET_NLO_PS_32K(h2c_pkt, value)                                         \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(9))
#define SET_NLO_IGNORE_SECURITY(h2c_pkt, value)                                \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(10))
#define SET_NLO_LOC_NLO_INFO(h2c_pkt, value)                                   \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, GENMASK(23, 16))
#define SET_RECOVER_BT_DEV_EN(h2c_pkt, value)				       \
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define GET_FW_DUMP_LEN(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x00), GENMASK(15, 0))
#define GET_FW_DUMP_SEQ(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x00), GENMASK(22, 16))
#define GET_FW_DUMP_MORE(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x00), BIT(23))
#define GET_FW_DUMP_VERSION(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x00), GENMASK(31, 24))
#define GET_FW_DUMP_TLV_TYPE(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x01), GENMASK(15, 0))
#define GET_FW_DUMP_TLV_LEN(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x01), GENMASK(31, 16))
#define GET_FW_DUMP_TLV_VAL(_header)					\
	le32_get_bits(*((__le32 *)(_header) + 0x02), GENMASK(31, 0))
#define RFK_SET_INFORM_START(h2c_pkt, value)				\
	le32p_replace_bits((__le32 *)(h2c_pkt) + 0x00, value, BIT(8))
#define RTW_MAX_MAC_ID_NUM		32
#define RTW_MAX_SEC_CAM_NUM		32
#define MAX_PG_CAM_BACKUP_NUM		8
#define RTW_SCAN_MAX_SSIDS		4
#define RTW_MAX_PATTERN_NUM		12
#define RTW_MAX_PATTERN_MASK_SIZE	16
#define RTW_MAX_PATTERN_SIZE		128
#define RTW_WATCH_DOG_DELAY_TIME	round_jiffies_relative(HZ * 2)
#define RFREG_MASK			0xfffff
#define INV_RF_DATA			0xffffffff
#define TX_PAGE_SIZE_SHIFT		7
#define TX_PAGE_SIZE			(1 << TX_PAGE_SIZE_SHIFT)
#define RTW_CHANNEL_WIDTH_MAX		3
#define RTW_RF_PATH_MAX			4
#define HW_FEATURE_LEN			13
#define RTW_TP_SHIFT			18 /* bytes/2s --> Mbps */
#define RTW_MAX_CHANNEL_NUM_2G 14
#define RTW_MAX_CHANNEL_NUM_5G 49
#define IS_CH_5G_BAND_1(channel) ((channel) >= 36 && (channel <= 48))
#define IS_CH_5G_BAND_2(channel) ((channel) >= 52 && (channel <= 64))
#define IS_CH_5G_BAND_3(channel) ((channel) >= 100 && (channel <= 144))
#define IS_CH_5G_BAND_4(channel) ((channel) >= 149 && (channel <= 177))
#define IS_CH_5G_BAND_MID(channel) \
	(IS_CH_5G_BAND_2(channel) || IS_CH_5G_BAND_3(channel))
#define IS_CH_2G_BAND(channel) ((channel) <= 14)
#define IS_CH_5G_BAND(channel) \
	(IS_CH_5G_BAND_1(channel) || IS_CH_5G_BAND_2(channel) || \
	 IS_CH_5G_BAND_3(channel) || IS_CH_5G_BAND_4(channel))
#define RTW_MAX_CHANNEL_WIDTH RTW_CHANNEL_WIDTH_80
#define HT_STBC_EN	BIT(0)
#define VHT_STBC_EN	BIT(1)
#define HT_LDPC_EN	BIT(0)
#define VHT_LDPC_EN	BIT(1)
#define RTW_REG_DOMAIN_MAC32	0
#define RTW_REG_DOMAIN_MAC16	1
#define RTW_REG_DOMAIN_MAC8	2
#define RTW_REG_DOMAIN_RF_A	3
#define RTW_REG_DOMAIN_RF_B	4
#define RTW_REG_DOMAIN_NL	0xFF
#define RTW_PWR_POLLING_CNT	20000
#define RTW_PWR_CMD_READ	0x00
#define RTW_PWR_CMD_WRITE	0x01
#define RTW_PWR_CMD_POLLING	0x02
#define RTW_PWR_CMD_DELAY	0x03
#define RTW_PWR_CMD_END		0x04
#define RTW_PWR_ADDR_MAC	0x00
#define RTW_PWR_ADDR_USB	0x01
#define RTW_PWR_ADDR_PCIE	0x02
#define RTW_PWR_ADDR_SDIO	0x03
#define RTW_PWR_INTF_SDIO_MSK	BIT(0)
#define RTW_PWR_INTF_USB_MSK	BIT(1)
#define RTW_PWR_INTF_PCI_MSK	BIT(2)
#define RTW_PWR_INTF_ALL_MSK	(BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define RTW_PWR_CUT_TEST_MSK	BIT(0)
#define RTW_PWR_CUT_A_MSK	BIT(1)
#define RTW_PWR_CUT_B_MSK	BIT(2)
#define RTW_PWR_CUT_C_MSK	BIT(3)
#define RTW_PWR_CUT_D_MSK	BIT(4)
#define RTW_PWR_CUT_E_MSK	BIT(5)
#define RTW_PWR_CUT_F_MSK	BIT(6)
#define RTW_PWR_CUT_G_MSK	BIT(7)
#define RTW_PWR_CUT_ALL_MSK	0xFF
#define RTW_INTF_PHY_PLATFORM_ALL 0
#define RTW_DEF_RFE(chip, bb_pg, pwrlmt) {				  \
	.phy_pg_tbl = &rtw ## chip ## _bb_pg_type ## bb_pg ## _tbl,	  \
	.txpwr_lmt_tbl = &rtw ## chip ## _txpwr_lmt_type ## pwrlmt ## _tbl, \
	}
#define RTW_DEF_RFE_EXT(chip, bb_pg, pwrlmt, btg) {			  \
	.phy_pg_tbl = &rtw ## chip ## _bb_pg_type ## bb_pg ## _tbl,	  \
	.txpwr_lmt_tbl = &rtw ## chip ## _txpwr_lmt_type ## pwrlmt ## _tbl, \
	.agc_btg_tbl = &rtw ## chip ## _agc_btg_type ## btg ## _tbl, \
	}
#define RTW_PWR_TRK_5G_1		0
#define RTW_PWR_TRK_5G_2		1
#define RTW_PWR_TRK_5G_3		2
#define RTW_PWR_TRK_5G_NUM		3
#define RTW_PWR_TRK_TBL_SZ		30
#define COEX_WL_TDMA_PARA_LENGTH	5
#define COEX_BTINFO_SRC_WL_FW	0x0
#define COEX_BTINFO_SRC_BT_RSP	0x1
#define COEX_BTINFO_SRC_BT_ACT	0x2
#define COEX_BTINFO_SRC_BT_IQK	0x3
#define COEX_BTINFO_SRC_BT_SCBD	0x4
#define COEX_BTINFO_SRC_H2C60	0x5
#define COEX_BTINFO_SRC_MAX	0x6
#define COEX_INFO_FTP		BIT(7)
#define COEX_INFO_A2DP		BIT(6)
#define COEX_INFO_HID		BIT(5)
#define COEX_INFO_SCO_BUSY	BIT(4)
#define COEX_INFO_ACL_BUSY	BIT(3)
#define COEX_INFO_INQ_PAGE	BIT(2)
#define COEX_INFO_SCO_ESCO	BIT(1)
#define COEX_INFO_CONNECTION	BIT(0)
#define COEX_BTINFO_LENGTH_MAX	10
#define COEX_BTINFO_LENGTH	7
#define COEX_BT_HIDINFO_LIST	0x0
#define COEX_BT_HIDINFO_A	0x1
#define COEX_BT_HIDINFO_NAME	3
#define COEX_BT_HIDINFO_LENGTH	6
#define COEX_BT_HIDINFO_HANDLE_NUM	4
#define COEX_BT_HIDINFO_C2H_HANDLE	0
#define COEX_BT_HIDINFO_C2H_VENDOR	1
#define COEX_BT_BLE_HANDLE_THRS	0x10
#define COEX_BT_HIDINFO_NOTCON	0xff
#define DPK_RF_REG_NUM 7
#define DPK_RF_PATH_NUM 2
#define DPK_BB_REG_NUM 18
#define DPK_CHANNEL_WIDTH_80 1
#define DACK_MSBK_BACKUP_NUM	0xf
#define DACK_DCK_BACKUP_NUM	0x2
#define RF_GAIN_NUM 11
#define RF_HW_OFFSET_NUM 10
#define EDCCA_TH_L2H_IDX 0
#define EDCCA_TH_H2L_IDX 1
#define EDCCA_TH_L2H_LB 48
#define EDCCA_ADC_BACKOFF 12
#define EDCCA_IGI_BASE 50
#define EDCCA_IGI_L2H_DIFF 8
#define EDCCA_L2H_H2L_DIFF 7
#define EDCCA_L2H_H2L_DIFF_NORMAL 8
#define RRSR_INIT_2G 0x15f
#define RRSR_INIT_5G 0x150
	#define INTF_PCIE	BIT(0)
	#define INTF_USB	BIT(1)
	#define INTF_SDIO	BIT(2)
	#define BRANCH_IF	0
	#define BRANCH_ELIF	1
	#define BRANCH_ELSE	2
	#define BRANCH_ENDIF	3
#define FW_CD_TYPE 0xffff
#define FW_CD_LEN 4
#define FW_CD_VAL 0xaabbccdd
#define RTW_DECL_TABLE_PHY_COND_CORE(name, cfg, path)	\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw_parse_tbl_phy_cond,		\
	.do_cfg = cfg,					\
	.rf_path = path,				\
}
#define RTW_DECL_TABLE_PHY_COND(name, cfg)		\
	RTW_DECL_TABLE_PHY_COND_CORE(name, cfg, 0)
#define RTW_DECL_TABLE_RF_RADIO(name, path)		\
	RTW_DECL_TABLE_PHY_COND_CORE(name, rtw_phy_cfg_rf, RF_PATH_ ## path)
#define RTW_DECL_TABLE_BB_PG(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw_parse_tbl_bb_pg,			\
}
#define RTW_DECL_TABLE_TXPWR_LMT(name)			\
const struct rtw_table name ## _tbl = {			\
	.data = name,					\
	.size = ARRAY_SIZE(name),			\
	.parse = rtw_parse_tbl_txpwr_lmt,		\
}
#define	MASKBYTE0		0xff
#define	MASKBYTE1		0xff00
#define	MASKBYTE2		0xff0000
#define	MASKBYTE3		0xff000000
#define	MASKHWORD		0xffff0000
#define	MASKLWORD		0x0000ffff
#define	MASKDWORD		0xffffffff
#define	MASK7BITS		0x7f
#define	MASK12BITS		0xfff
#define	MASKH4BITS		0xf0000000
#define	MASK20BITS		0xfffff
#define	MASK24BITS		0xffffff
#define MASKH3BYTES		0xffffff00
#define MASKL3BYTES		0x00ffffff
#define MASKBYTE2HIGHNIBBLE	0x00f00000
#define MASKBYTE3LOWNIBBLE	0x0f000000
#define CCK_FA_AVG_RESET 0xffffffff
#define LSSI_READ_ADDR_MASK	0x7f800000
#define LSSI_READ_EDGE_MASK	0x80000000
#define LSSI_READ_DATA_MASK	0xfffff
#define RRSR_RATE_ORDER_MAX	0xfffff
#define RRSR_RATE_ORDER_CCK_LEN	4
#define SDIO_LOCAL_OFFSET			0x10250000
#define WLAN_IOREG_OFFSET			0x10260000
#define FIRMWARE_FIFO_OFFSET			0x10270000
#define TX_HIQ_OFFSET				0x10310000
#define TX_MIQ_OFFSET				0x10320000
#define TX_LOQ_OFFSET				0x10330000
#define TX_EPQ_OFFSET				0x10350000
#define RX_RX0FF_OFFSET				0x10340000
#define RTW_SDIO_BUS_MSK			0xffff0000
#define SDIO_LOCAL_REG_MSK			0x00000fff
#define WLAN_IOREG_REG_MSK			0x0000ffff
#define REG_SDIO_TX_CTRL			(SDIO_LOCAL_OFFSET + 0x0000)
#define REG_SDIO_TIMEOUT			(SDIO_LOCAL_OFFSET + 0x0002)
#define REG_SDIO_HIMR				(SDIO_LOCAL_OFFSET + 0x0014)
#define REG_SDIO_HIMR_RX_REQUEST		BIT(0)
#define REG_SDIO_HIMR_AVAL			BIT(1)
#define REG_SDIO_HIMR_TXERR			BIT(2)
#define REG_SDIO_HIMR_RXERR			BIT(3)
#define REG_SDIO_HIMR_TXFOVW			BIT(4)
#define REG_SDIO_HIMR_RXFOVW			BIT(5)
#define REG_SDIO_HIMR_TXBCNOK			BIT(6)
#define REG_SDIO_HIMR_TXBCNERR			BIT(7)
#define REG_SDIO_HIMR_BCNERLY_INT		BIT(16)
#define REG_SDIO_HIMR_C2HCMD			BIT(17)
#define REG_SDIO_HIMR_CPWM1			BIT(18)
#define REG_SDIO_HIMR_CPWM2			BIT(19)
#define REG_SDIO_HIMR_HSISR_IND			BIT(20)
#define REG_SDIO_HIMR_GTINT3_IND		BIT(21)
#define REG_SDIO_HIMR_GTINT4_IND		BIT(22)
#define REG_SDIO_HIMR_PSTIMEOUT			BIT(23)
#define REG_SDIO_HIMR_OCPINT			BIT(24)
#define REG_SDIO_HIMR_ATIMEND			BIT(25)
#define REG_SDIO_HIMR_ATIMEND_E			BIT(26)
#define REG_SDIO_HIMR_CTWEND			BIT(27)
#define REG_SDIO_HIMR_MCU_ERR			BIT(28)
#define REG_SDIO_HIMR_TSF_BIT32_TOGGLE		BIT(29)
#define REG_SDIO_HISR				(SDIO_LOCAL_OFFSET + 0x0018)
#define REG_SDIO_HISR_RX_REQUEST		BIT(0)
#define REG_SDIO_HISR_AVAL			BIT(1)
#define REG_SDIO_HISR_TXERR			BIT(2)
#define REG_SDIO_HISR_RXERR			BIT(3)
#define REG_SDIO_HISR_TXFOVW			BIT(4)
#define REG_SDIO_HISR_RXFOVW			BIT(5)
#define REG_SDIO_HISR_TXBCNOK			BIT(6)
#define REG_SDIO_HISR_TXBCNERR			BIT(7)
#define REG_SDIO_HISR_BCNERLY_INT		BIT(16)
#define REG_SDIO_HISR_C2HCMD			BIT(17)
#define REG_SDIO_HISR_CPWM1			BIT(18)
#define REG_SDIO_HISR_CPWM2			BIT(19)
#define REG_SDIO_HISR_HSISR_IND			BIT(20)
#define REG_SDIO_HISR_GTINT3_IND		BIT(21)
#define REG_SDIO_HISR_GTINT4_IND		BIT(22)
#define REG_SDIO_HISR_PSTIMEOUT			BIT(23)
#define REG_SDIO_HISR_OCPINT			BIT(24)
#define REG_SDIO_HISR_ATIMEND			BIT(25)
#define REG_SDIO_HISR_ATIMEND_E			BIT(26)
#define REG_SDIO_HISR_CTWEND			BIT(27)
#define REG_SDIO_HISR_MCU_ERR			BIT(28)
#define REG_SDIO_HISR_TSF_BIT32_TOGGLE		BIT(29)
#define REG_SDIO_HCPWM				(SDIO_LOCAL_OFFSET + 0x0019)
#define REG_SDIO_RX0_REQ_LEN			(SDIO_LOCAL_OFFSET + 0x001C)
#define REG_SDIO_OQT_FREE_PG			(SDIO_LOCAL_OFFSET + 0x001E)
#define REG_SDIO_FREE_TXPG			(SDIO_LOCAL_OFFSET + 0x0020)
#define REG_SDIO_HCPWM1				(SDIO_LOCAL_OFFSET + 0x0024)
#define REG_SDIO_HCPWM2				(SDIO_LOCAL_OFFSET + 0x0026)
#define REG_SDIO_FREE_TXPG_SEQ			(SDIO_LOCAL_OFFSET + 0x0028)
#define REG_SDIO_HTSFR_INFO			(SDIO_LOCAL_OFFSET + 0x0030)
#define REG_SDIO_HCPWM1_V2			(SDIO_LOCAL_OFFSET + 0x0038)
#define REG_SDIO_H2C				(SDIO_LOCAL_OFFSET + 0x0060)
#define REG_SDIO_HRPWM1				(SDIO_LOCAL_OFFSET + 0x0080)
#define REG_SDIO_HRPWM2				(SDIO_LOCAL_OFFSET + 0x0082)
#define REG_SDIO_HPS_CLKR			(SDIO_LOCAL_OFFSET + 0x0084)
#define REG_SDIO_HSUS_CTRL			(SDIO_LOCAL_OFFSET + 0x0086)
#define BIT_HCI_SUS_REQ				BIT(0)
#define BIT_HCI_RESUME_RDY			BIT(1)
#define REG_SDIO_HIMR_ON			(SDIO_LOCAL_OFFSET + 0x0090)
#define REG_SDIO_HISR_ON			(SDIO_LOCAL_OFFSET + 0x0091)
#define REG_SDIO_INDIRECT_REG_CFG		(SDIO_LOCAL_OFFSET + 0x0040)
#define BIT_SDIO_INDIRECT_REG_CFG_WORD		BIT(16)
#define BIT_SDIO_INDIRECT_REG_CFG_DWORD		BIT(17)
#define BIT_SDIO_INDIRECT_REG_CFG_WRITE		BIT(18)
#define BIT_SDIO_INDIRECT_REG_CFG_READ		BIT(19)
#define BIT_SDIO_INDIRECT_REG_CFG_UNK20		BIT(20)
#define REG_SDIO_INDIRECT_REG_DATA		(SDIO_LOCAL_OFFSET + 0x0044)
#define REG_SDIO_CMD_ADDR_MSK			GENMASK(16, 13)
#define REG_SDIO_CMD_ADDR_SDIO_REG		0
#define REG_SDIO_CMD_ADDR_MAC_REG		8
#define REG_SDIO_CMD_ADDR_TXFF_HIGH		4
#define REG_SDIO_CMD_ADDR_TXFF_LOW		6
#define REG_SDIO_CMD_ADDR_TXFF_NORMAL		5
#define REG_SDIO_CMD_ADDR_TXFF_EXTRA		7
#define REG_SDIO_CMD_ADDR_RXFF			7
#define RTW_SDIO_BLOCK_SIZE			512
#define RTW_SDIO_ADDR_RX_RX0FF_GEN(_id)		(0x0e000 | ((_id) & 0x3))
#define RTW_SDIO_DATA_PTR_ALIGN			8
#define REG_TXBF_CTRL		0x042C
#define REG_NDPA_OPT_CTRL	0x045F
#define REG_ASSOCIATED_BFMER0_INFO	0x06E4
#define REG_ASSOCIATED_BFMER1_INFO	0x06EC
#define REG_TX_CSI_RPT_PARAM_BW20	0x06F4
#define REG_SND_PTCL_CTRL		0x0718
#define BIT_DIS_CHK_VHTSIGB_CRC		BIT(6)
#define BIT_DIS_CHK_VHTSIGA_CRC		BIT(5)
#define BIT_MASK_BEAMFORM		(GENMASK(4, 0) | BIT(7))
#define REG_MU_TX_CTL			0x14C0
#define REG_MU_STA_GID_VLD		0x14C4
#define REG_MU_STA_USER_POS_INFO	0x14C8
#define REG_CSI_RRSR			0x1678
#define REG_WMAC_MU_BF_OPTION		0x167C
#define REG_WMAC_MU_BF_CTL		0x1680
#define BIT_WMAC_USE_NDPARATE			BIT(30)
#define BIT_WMAC_TXMU_ACKPOLICY_EN		BIT(6)
#define BIT_USE_NDPA_PARAMETER			BIT(30)
#define BIT_MU_P1_WAIT_STATE_EN			BIT(16)
#define BIT_EN_MU_MIMO				BIT(7)
#define R_MU_RL				0xf
#define BIT_SHIFT_R_MU_RL		12
#define BIT_SHIFT_WMAC_TXMU_ACKPOLICY	4
#define BIT_SHIFT_CSI_RATE		24
#define BIT_MASK_R_MU_RL (R_MU_RL << BIT_SHIFT_R_MU_RL)
#define BIT_MASK_R_MU_TABLE_VALID	0x3f
#define BIT_MASK_CSI_RATE_VAL		0x3F
#define BIT_MASK_CSI_RATE (BIT_MASK_CSI_RATE_VAL << BIT_SHIFT_CSI_RATE)
#define BIT_RXFLTMAP0_ACTIONNOACK	BIT(14)
#define BIT_RXFLTMAP1_BF		(BIT(4) | BIT(5))
#define BIT_RXFLTMAP1_BF_REPORT_POLL	BIT(4)
#define BIT_RXFLTMAP4_BF_REPORT_POLL	BIT(4)
#define RTW_NDP_RX_STANDBY_TIME	0x70
#define RTW_SND_CTRL_REMOVE	0x98
#define RTW_SND_CTRL_SOUNDING	0x9B
