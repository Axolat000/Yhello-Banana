/* $OpenBSD: if_aq_pci.c,v 1.40 2026/10/26 19:37:43 kettenis Exp $ */
/*	$NetBSD: if_aq.c,v 1.27 2021/06/16 00:21:18 riastradh Exp $	*/

/*
 * Copyright (c) 2021 Jonathan Matthew <jonathan@d14n.org>
 * Copyright (c) 2021 Mike Larkin <mlarkin@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   (1) Redistributions of source code must retain the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer.
 *
 *   (2) Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 *   (3) The name of the author may not be used to endorse or promote
 *   products derived from this software without specific prior
 *   written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*-
 * Copyright (c) 2020 Ryo Shimizu <ryo@nerv.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#define AQ_BAR0 				0x10
#define AQ_MAXQ 				8
#define AQ_RSS_KEYSIZE				40
#define AQ_RSS_REDIR_ENTRIES			12

#define AQ_TXD_NUM 				2048
#define AQ_RXD_NUM 				2048

#define AQ_TX_MAX_SEGMENTS			32

#define AQ_LINKSTAT_IRQ				31

#define RPF_ACTION_HOST				1

#define AQ_FW_SOFTRESET_REG			0x0000
#define  AQ_FW_SOFTRESET_DIS			(1 << 14)
#define  AQ_FW_SOFTRESET_RESET			(1 << 15)
#define AQ_FW_VERSION_REG			0x0018
#define AQ_HW_REVISION_REG			0x001c
#define AQ2_HW_FPGA_VERSION_REG			0x00f4
#define AQ_GLB_NVR_INTERFACE1_REG		0x0100
#define AQ_FW_MBOX_CMD_REG			0x0200
#define  AQ_FW_MBOX_CMD_EXECUTE			0x00008000
#define  AQ_FW_MBOX_CMD_BUSY			0x00000100
#define AQ_FW_MBOX_ADDR_REG			0x0208
#define AQ_FW_MBOX_VAL_REG			0x020C
#define AQ_FW_GLB_CPU_SEM_REG(i)		(0x03a0 + (i) * 4)
#define AQ_FW_SEM_RAM_REG			AQ_FW_GLB_CPU_SEM_REG(2)
#define AQ2_ART_SEM_REG				AQ_FW_GLB_CPU_SEM_REG(3)
#define AQ_FW_GLB_CTL2_REG			0x0404
#define AQ_GLB_GENERAL_PROVISIONING9_REG	0x0520
#define AQ_GLB_NVR_PROVISIONING2_REG		0x0534
#define AQ_INTR_STATUS_REG			0x2000  /* intr status */
#define AQ_INTR_STATUS_CLR_REG			0x2050  /* intr status clear */
#define AQ_INTR_MASK_REG			0x2060	/* intr mask set */
#define AQ_INTR_MASK_CLR_REG			0x2070	/* intr mask clear */
#define AQ_INTR_AUTOMASK_REG			0x2090

#define AQ_SMB_PROVISIONING_REG			0x0604
#define AQ_SMB_TX_DATA_REG			0x0608
#define AQ_SMB_BUS_REG				0x0744
#define  AQ_SMB_BUS_XFER_COMPLETE		(1 << 1)
#define  AQ_SMB_BUS_REPEAT_DETECT		(1 << 2)
#define  AQ_SMB_BUS_BUSY			(1 << 7)
#define  AQ_SMB_BUS_RX_ACK			(1 << 8)
#define AQ_SMB_RX_DATA_REG			0x0748

/* AQ_INTR_IRQ_MAP_TXRX_REG 0x2100-0x2140 */
#define AQ_INTR_IRQ_MAP_TXRX_REG(i)		(0x2100 + ((i) / 2) * 4)
#define AQ_INTR_IRQ_MAP_TX_REG(i)		AQ_INTR_IRQ_MAP_TXRX_REG(i)
#define  AQ_INTR_IRQ_MAP_TX_IRQMAP(i)		(0x1FU << (((i) & 1) ? 16 : 24))
#define  AQ_INTR_IRQ_MAP_TX_EN(i)		(1U << (((i) & 1) ? 23 : 31))
#define AQ_INTR_IRQ_MAP_RX_REG(i)		AQ_INTR_IRQ_MAP_TXRX_REG(i)
#define  AQ_INTR_IRQ_MAP_RX_IRQMAP(i)		(0x1FU << (((i) & 1) ? 0 : 8))
#define  AQ_INTR_IRQ_MAP_RX_EN(i)		(1U << (((i) & 1) ? 7 : 15))

/* AQ_GEN_INTR_MAP_REG[AQ_RINGS_NUM] 0x2180-0x2200 */
#define AQ_GEN_INTR_MAP_REG(i)			(0x2180 + (i) * 4)
#define  AQ_B0_ERR_INT				8U

#define AQ_INTR_CTRL_REG			0x2300
#define  AQ_INTR_CTRL_IRQMODE			((1 << 0) | (1 << 1))
#define AQ_INTR_CTRL_IRQMODE_LEGACY		0
#define AQ_INTR_CTRL_IRQMODE_MSI		1
#define AQ_INTR_CTRL_IRQMODE_MSIX		2
#define  AQ_INTR_CTRL_MULTIVEC			(1 << 2)
#define  AQ_INTR_CTRL_RESET_DIS			(1 << 29)
#define  AQ_INTR_CTRL_RESET_IRQ			(1U << 31)
#define AQ_MBOXIF_POWER_GATING_CONTROL_REG	0x32a8

#define FW_MPI_MBOX_ADDR_REG			0x0360
#define FW1X_MPI_INIT1_REG			0x0364
#define FW1X_MPI_INIT2_REG			0x0370
#define FW1X_MPI_EFUSEADDR_REG			0x0374

#define FW2X_MPI_EFUSEADDR_REG			0x0364
#define FW2X_MPI_CONTROL_REG			0x0368  /* 64bit */
#define FW2X_MPI_STATE_REG			0x0370  /* 64bit */
#define FW_BOOT_EXIT_CODE_REG			0x0388

#define FW_BOOT_EXIT_CODE_REG			0x0388
#define  RBL_STATUS_DEAD			0x0000dead
#define  RBL_STATUS_SUCCESS			0x0000abba
#define  RBL_STATUS_FAILURE			0x00000bad
#define  RBL_STATUS_HOST_BOOT			0x0000f1a7
#define FW_MPI_DAISY_CHAIN_STATUS_REG		0x0704
#define AQ_PCI_REG_CONTROL_6_REG		0x1014

#define FW_MPI_RESETCTRL_REG			0x4000
#define  FW_MPI_RESETCTRL_RESET_DIS		(1 << 29)

#define RX_SYSCONTROL_REG			0x5000
#define  RX_SYSCONTROL_RESET_DIS		(1 << 29)

#define RX_TCP_RSS_HASH_REG			0x5040
#define  RX_TCP_RSS_HASH_RPF2			(0xf << 16)
#define  RX_TCP_RSS_HASH_TYPE			(0xffff)

#define RPF_L2BC_REG				0x5100
#define  RPF_L2BC_EN				(1 << 0)
#define  RPF_L2BC_PROMISC			(1 << 3)
#define  RPF_L2BC_ACTION			0x7000
#define  RPF_L2BC_THRESHOLD			0xFFFF0000

#define AQ_HW_MAC_OWN				0

/* RPF_L2UC_*_REG[34] (actual [38]?) */
#define RPF_L2UC_LSW_REG(i)                     (0x5110 + (i) * 8)
#define RPF_L2UC_MSW_REG(i)                     (0x5114 + (i) * 8)
#define  RPF_L2UC_MSW_MACADDR_HI		0xFFFF
#define  RPF_L2UC_MSW_ACTION			0x70000
#define  RPF_L2UC_MSW_TAG			0x03c00000
#define  RPF_L2UC_MSW_EN			(1U << 31)
#define AQ_HW_MAC_NUM				34

/* RPF_MCAST_FILTER_REG[8] 0x5250-0x5270 */
#define RPF_MCAST_FILTER_REG(i)			(0x5250 + (i) * 4)
#define  RPF_MCAST_FILTER_EN			(1U << 31)
#define RPF_MCAST_FILTER_MASK_REG		0x5270
#define  RPF_MCAST_FILTER_MASK_ALLMULTI		(1 << 14)

#define RPF_VLAN_MODE_REG			0x5280
#define  RPF_VLAN_MODE_PROMISC			(1 << 1)
#define  RPF_VLAN_MODE_ACCEPT_UNTAGGED		(1 << 2)
#define  RPF_VLAN_MODE_UNTAGGED_ACTION		0x38

#define RPF_VLAN_TPID_REG                       0x5284
#define  RPF_VLAN_TPID_OUTER			0xFFFF0000
#define  RPF_VLAN_TPID_INNER			0xFFFF

/* RPF_ETHERTYPE_FILTER_REG[AQ_RINGS_NUM] 0x5300-0x5380 */
#define RPF_ETHERTYPE_FILTER_REG(i)		(0x5300 + (i) * 4)
#define  RPF_ETHERTYPE_FILTER_EN		(1U << 31)

/* RPF_L3_FILTER_REG[8] 0x5380-0x53a0 */
#define RPF_L3_FILTER_REG(i)			(0x5380 + (i) * 4)
#define  RPF_L3_FILTER_L4_EN			(1U << 31)

#define RX_FLR_RSS_CONTROL1_REG			0x54c0
#define  RX_FLR_RSS_CONTROL1_EN			(1U << 31)

#define RPF_RPB_RX_TC_UPT_REG                   0x54c4
#define  RPF_RPB_RX_TC_UPT_MASK(i)              (0x00000007 << ((i) * 4))

#define RPF_RSS_KEY_ADDR_REG			0x54d0
#define  RPF_RSS_KEY_ADDR			0x1f
#define  RPF_RSS_KEY_WR_EN			(1 << 5)
#define RPF_RSS_KEY_WR_DATA_REG			0x54d4
#define RPF_RSS_KEY_RD_DATA_REG			0x54d8

#define RPF_RSS_REDIR_ADDR_REG			0x54e0
#define  RPF_RSS_REDIR_ADDR			0xf
#define  RPF_RSS_REDIR_WR_EN			(1 << 4)

#define RPF_RSS_REDIR_WR_DATA_REG		0x54e4


#define RPO_HWCSUM_REG				0x5580
#define  RPO_HWCSUM_L4CSUM_EN			(1 << 0)
#define  RPO_HWCSUM_IP4CSUM_EN			(1 << 1)

#define RPB_RPF_RX_REG				0x5700
#define  RPB_RPF_RX_TC_MODE			(1 << 8)
#define  RPB_RPF_RX_FC_MODE			0x30
#define  RPB_RPF_RX_BUF_EN			(1 << 0)

/* RPB_RXB_BUFSIZE_REG[AQ_TRAFFICCLASS_NUM] 0x5710-0x5790 */
#define RPB_RXB_BUFSIZE_REG(i)			(0x5710 + (i) * 0x10)
#define  RPB_RXB_BUFSIZE			0x1FF
#define RPB_RXB_XOFF_REG(i)			(0x5714 + (i) * 0x10)
#define  RPB_RXB_XOFF_EN			(1U << 31)
#define  RPB_RXB_XOFF_THRESH_HI                 0x3FFF0000
#define  RPB_RXB_XOFF_THRESH_LO                 0x3FFF

#define RX_DMA_DESC_CACHE_INIT_REG		0x5a00
#define  RX_DMA_DESC_CACHE_INIT			(1 << 0)

#define RX_DMA_INT_DESC_WRWB_EN_REG		0x5a30
#define  RX_DMA_INT_DESC_WRWB_EN		(1 << 2)
#define  RX_DMA_INT_DESC_MODERATE_EN		(1 << 3)

#define RX_INTR_MODERATION_CTL_REG(i)		(0x5a40 + (i) * 4)
#define  RX_INTR_MODERATION_CTL_EN		(1 << 1)
#define  RX_INTR_MODERATION_CTL_MIN		(0xFF << 8)
#define  RX_INTR_MODERATION_CTL_MAX		(0x1FF << 16)

#define RX_DMA_DESC_BASE_ADDRLSW_REG(i)		(0x5b00 + (i) * 0x20)
#define RX_DMA_DESC_BASE_ADDRMSW_REG(i)		(0x5b04 + (i) * 0x20)
#define RX_DMA_DESC_REG(i)			(0x5b08 + (i) * 0x20)
#define  RX_DMA_DESC_LEN			(0x3FF << 3)
#define  RX_DMA_DESC_RESET			(1 << 25)
#define  RX_DMA_DESC_HEADER_SPLIT		(1 << 28)
#define  RX_DMA_DESC_VLAN_STRIP			(1 << 29)
#define  RX_DMA_DESC_EN				(1U << 31)
#define RX_DMA_DESC_HEAD_PTR_REG(i)		(0x5b0c + (i) * 0x20)
#define  RX_DMA_DESC_HEAD_PTR			0xFFF
#define RX_DMA_DESC_TAIL_PTR_REG(i)		(0x5b10 + (i) * 0x20)
#define RX_DMA_DESC_BUFSIZE_REG(i)		(0x5b18 + (i) * 0x20)
#define  RX_DMA_DESC_BUFSIZE_DATA		0x000F
#define  RX_DMA_DESC_BUFSIZE_HDR		0x0FF0

#define RX_DMA_DCAD_REG(i)			(0x6100 + (i) * 4)
#define  RX_DMA_DCAD_CPUID			0xFF
#define  RX_DMA_DCAD_PAYLOAD_EN			(1 << 29)
#define  RX_DMA_DCAD_HEADER_EN			(1 << 30)
#define  RX_DMA_DCAD_DESC_EN			(1U << 31)

#define RX_DMA_DCA_REG				0x6180
#define  RX_DMA_DCA_EN				(1U << 31)
#define  RX_DMA_DCA_MODE			0xF

#define TX_SYSCONTROL_REG			0x7000
#define  TX_SYSCONTROL_RESET_DIS		(1 << 29)

#define TX_TPO2_REG				0x7040
#define  TX_TPO2_EN				(1 << 16)

#define TPS_DESC_VM_ARB_MODE_REG		0x7300
#define  TPS_DESC_VM_ARB_MODE			(1 << 0)
#define TPS_DESC_RATE_REG			0x7310
#define  TPS_DESC_RATE_TA_RST			(1U << 31)
#define  TPS_DESC_RATE_LIM			0x7FF
#define TPS_DESC_TC_ARB_MODE_REG		0x7200
#define  TPS_DESC_TC_ARB_MODE			0x3
#define TPS_DATA_TC_ARB_MODE_REG		0x7100
#define  TPS_DATA_TC_ARB_MODE			(1 << 0)

/* TPS_DATA_TCT_REG[AQ_TRAFFICCLASS_NUM] 0x7110-0x7130 */
#define TPS_DATA_TCT_REG(i)			(0x7110 + (i) * 4)
#define  TPS_DATA_TCT_CREDIT_MAX		0xFFF0000
#define  TPS_DATA_TCT_WEIGHT			0x1FF
#define  TPS2_DATA_TCT_CREDIT_MAX		0xFFFF0000
#define  TPS2_DATA_TCT_WEIGHT			0x7FFF
/* TPS_DATA_TCT_REG[AQ_TRAFFICCLASS_NUM] 0x7210-0x7230 */
#define TPS_DESC_TCT_REG(i)			(0x7210 + (i) * 4)
#define  TPS_DESC_TCT_CREDIT_MAX		0xFFF0000
#define  TPS_DESC_TCT_WEIGHT			0x1FF

#define AQ_HW_TXBUF_MAX         160
#define AQ_HW_RXBUF_MAX         320
#define AQ2_HW_TXBUF_MAX	128
#define AQ2_HW_RXBUF_MAX	192

#define TPO_HWCSUM_REG				0x7800
#define  TPO_HWCSUM_L4CSUM_EN			(1 << 0)
#define  TPO_HWCSUM_IP4CSUM_EN			(1 << 1)

#define THM_LSO_TCP_FLAG1_REG			0x7820
#define  THM_LSO_TCP_FLAG1_FIRST		0xFFF
#define  THM_LSO_TCP_FLAG1_MID			0xFFF0000
#define THM_LSO_TCP_FLAG2_REG			0x7824
#define  THM_LSO_TCP_FLAG2_LAST			0xFFF

#define TPB_TX_BUF_REG				0x7900
#define  TPB_TX_BUF_EN				(1 << 0)
#define  TPB_TX_BUF_SCP_INS_EN			(1 << 2)
#define  TPB_TX_BUF_CLK_GATE_EN			(1 << 5)
#define  TPB_TX_BUF_TC_MODE_EN			(1 << 8)
#define  TPB_TX_BUF_TC_Q_RAND_MAP_EN		(1 << 9)


/* TPB_TXB_BUFSIZE_REG[AQ_TRAFFICCLASS_NUM] 0x7910-7990 */
#define TPB_TXB_BUFSIZE_REG(i)			(0x7910 + (i) * 0x10)
#define  TPB_TXB_BUFSIZE                        (0xFF)
#define TPB_TXB_THRESH_REG(i)                   (0x7914 + (i) * 0x10)
#define  TPB_TXB_THRESH_HI                      0x1FFF0000
#define  TPB_TXB_THRESH_LO                      0x1FFF

#define AQ_HW_TX_DMA_TOTAL_REQ_LIMIT_REG	0x7b20

#define TX_DMA_INT_DESC_WRWB_EN_REG		0x7b40
#define  TX_DMA_INT_DESC_WRWB_EN		(1 << 1)
#define  TX_DMA_INT_DESC_MODERATE_EN		(1 << 4)

#define TX_DMA_DESC_BASE_ADDRLSW_REG(i)		(0x7c00 + (i) * 0x40)
#define TX_DMA_DESC_BASE_ADDRMSW_REG(i)		(0x7c04 + (i) * 0x40)
#define TX_DMA_DESC_REG(i)			(0x7c08 + (i) * 0x40)
#define  TX_DMA_DESC_LEN			0x00000FF8
#define  TX_DMA_DESC_EN				0x80000000
#define TX_DMA_DESC_HEAD_PTR_REG(i)		(0x7c0c + (i) * 0x40)
#define  TX_DMA_DESC_HEAD_PTR			0x00000FFF
#define TX_DMA_DESC_TAIL_PTR_REG(i)		(0x7c10 + (i) * 0x40)
#define TX_DMA_DESC_WRWB_THRESH_REG(i)		(0x7c18 + (i) * 0x40)
#define  TX_DMA_DESC_WRWB_THRESH		0x00003F00

#define TDM_DCAD_REG(i)				(0x8400 + (i) * 4)
#define  TDM_DCAD_CPUID				0x7F
#define  TDM_DCAD_CPUID_EN			0x80000000

#define TDM_DCA_REG				0x8480
#define  TDM_DCA_EN				(1U << 31)
#define  TDM_DCA_MODE				0xF

#define TX_INTR_MODERATION_CTL_REG(i)		(0x8980 + (i) * 4)
#define  TX_INTR_MODERATION_CTL_EN		(1 << 1)
#define  TX_INTR_MODERATION_CTL_MIN		(0xFF << 8)
#define  TX_INTR_MODERATION_CTL_MAX		(0x1FF << 16)

/* AQ2 registers */

#define AQ2_MIF_HOST_FINISHED_STATUS_WRITE_REG	0x0e00
#define AQ2_MIF_HOST_FINISHED_STATUS_READ_REG	0x0e04
#define  AQ2_MIF_HOST_FINISHED_STATUS_ACK	(1 << 0)

#define AQ2_MCP_HOST_REQ_INT_REG		0x0f00
#define  AQ2_MCP_HOST_REQ_INT_READY		(1 << 0)
#define AQ2_MCP_HOST_REQ_INT_SET_REG		0x0f04
#define AQ2_MCP_HOST_REQ_INT_CLR_REG		0x0f08

#define AQ2_MIF_BOOT_REG			0x3040
#define  AQ2_MIF_BOOT_HOST_DATA_LOADED		(1 << 16)
#define  AQ2_MIF_BOOT_BOOT_STARTED		(1 << 24)
#define  AQ2_MIF_BOOT_CRASH_INIT		(1 << 27)
#define  AQ2_MIF_BOOT_BOOT_CODE_FAILED		(1 << 28)
#define  AQ2_MIF_BOOT_FW_INIT_FAILED		(1 << 29)
#define  AQ2_MIF_BOOT_FW_INIT_COMP_SUCCESS	(1U << 31)

/* AQ2 action resolver table */
#define AQ2_ART_ACTION_ACT_SHIFT		8
#define AQ2_ART_ACTION_RSS			0x0080
#define AQ2_ART_ACTION_INDEX_SHIFT		2
#define AQ2_ART_ACTION_ENABLE			0x0001
#define AQ2_ART_ACTION(act, rss, idx, en)		\
	(((act) << AQ2_ART_ACTION_ACT_SHIFT) |		\
	((rss) ? AQ2_ART_ACTION_RSS : 0) |		\
	((idx) << AQ2_ART_ACTION_INDEX_SHIFT) |		\
	((en) ? AQ2_ART_ACTION_ENABLE : 0))
#define AQ2_ART_ACTION_DROP			AQ2_ART_ACTION(0, 0, 0, 1)
#define AQ2_ART_ACTION_DISABLE			AQ2_ART_ACTION(0, 0, 0, 0)
#define AQ2_ART_ACTION_ASSIGN_QUEUE(q)		AQ2_ART_ACTION(1, 0, (q), 1)
#define AQ2_ART_ACTION_ASSIGN_TC(tc)		AQ2_ART_ACTION(1, 1, (tc), 1)

#define AQ2_RPF_TAG_PCP_MASK			0xe0000000
#define AQ2_RPF_TAG_PCP_SHIFT			29
#define AQ2_RPF_TAG_FLEX_MASK			0x18000000
#define AQ2_RPF_TAG_UNKNOWN_MASK		0x07000000
#define AQ2_RPF_TAG_L4_MASK			0x00e00000
#define AQ2_RPF_TAG_L3_V6_MASK			0x001c0000
#define AQ2_RPF_TAG_L3_V4_MASK			0x00038000
#define AQ2_RPF_TAG_UNTAG_MASK			0x00004000
#define AQ2_RPF_TAG_VLAN_MASK			0x00003c00
#define AQ2_RPF_TAG_ET_MASK			0x00000380
#define AQ2_RPF_TAG_ALLMC_MASK			0x00000040
#define AQ2_RPF_TAG_UC_MASK			0x0000002f

/* index of aq2_filter_art_set() */
#define AQ2_RPF_INDEX_L2_PROMISC_OFF		0
#define AQ2_RPF_INDEX_VLAN_PROMISC_OFF		1
#define AQ2_RPF_INDEX_L3L4_USER			8
#define AQ2_RPF_INDEX_ET_PCP_USER		24
#define AQ2_RPF_INDEX_VLAN_USER			40
#define AQ2_RPF_INDEX_PCP_TO_TC			56

#define AQ2_RPF_L2BC_TAG_REG			0x50f0
#define  AQ2_RPF_L2BC_TAG_MASK			0x0000003f

#define AQ2_RPF_NEW_CTRL_REG			0x5104
#define  AQ2_RPF_NEW_CTRL_ENABLE		(1 << 11)

#define AQ2_RPF_REDIR2_REG			0x54c8
#define  AQ2_RPF_REDIR2_INDEX			(1 << 12)
#define  AQ2_RPF_REDIR2_HASHTYPE		0x000001FF
#define  AQ2_RPF_REDIR2_HASHTYPE_NONE		0
#define  AQ2_RPF_REDIR2_HASHTYPE_IP		(1 << 0)
#define  AQ2_RPF_REDIR2_HASHTYPE_TCP4		(1 << 1)
#define  AQ2_RPF_REDIR2_HASHTYPE_UDP4		(1 << 2)
#define  AQ2_RPF_REDIR2_HASHTYPE_IP6		(1 << 3)
#define  AQ2_RPF_REDIR2_HASHTYPE_TCP6		(1 << 4)
#define  AQ2_RPF_REDIR2_HASHTYPE_UDP6		(1 << 5)
#define  AQ2_RPF_REDIR2_HASHTYPE_IP6EX		(1 << 6)
#define  AQ2_RPF_REDIR2_HASHTYPE_TCP6EX		(1 << 7)
#define  AQ2_RPF_REDIR2_HASHTYPE_UDP6EX		(1 << 8)
#define  AQ2_RPF_REDIR2_HASHTYPE_ALL		0x000001FF

#define AQ2_RX_Q_TC_MAP_REG(i)			(0x5900 + (i) * 4)
#define AQ2_TX_Q_TC_MAP_REG(i)			(0x799c + (i) * 4)

#define AQ2_RPF_RSS_REDIR_MAX			64
#define AQ2_RPF_RSS_REDIR_REG(tc, i)		\
	 (0x6200 + (0x100 * ((tc) >> 2)) + (i) * 4)
#define AQ2_RPF_RSS_REDIR_TC_MASK(tc)		\
	 (0x1f << (5 * ((tc) & 3)))

#define AQ2_RPF_REC_TAB_ENABLE_REG		0x6ff0
#define  AQ2_RPF_REC_TAB_ENABLE_MASK		0x0000ffff

#define AQ2_LAUNCHTIME_CTRL_REG			0x7a1c
#define  AQ2_LAUNCHTIME_CTRL_RATIO		0x0000ff00
#define  AQ2_LAUNCHTIME_CTRL_RATIO_SPEED_QUARTER 4
#define  AQ2_LAUNCHTIME_CTRL_RATIO_SPEED_HALF	2
#define  AQ2_LAUNCHTIME_CTRL_RATIO_SPEED_FULL	1

#define AQ2_TX_INTR_MODERATION_CTL_REG(i)	(0x7c28 + (i) * 0x40)
#define  AQ2_TX_INTR_MODERATION_CTL_EN		(1 << 1)
#define  AQ2_TX_INTR_MODERATION_CTL_MIN		0x0000ff00
#define  AQ2_TX_INTR_MODERATION_CTL_MAX		0x01ff0000

#define AQ2_FW_INTERFACE_IN_MTU_REG		0x12000
#define AQ2_FW_INTERFACE_IN_MAC_ADDRESS_REG	0x12008

#define AQ2_FW_INTERFACE_IN_LINK_CONTROL_REG	0x12010
#define  AQ2_FW_INTERFACE_IN_LINK_CONTROL_MODE	0x0000000f
#define  AQ2_FW_INTERFACE_IN_LINK_CONTROL_MODE_INVALID	0
#define  AQ2_FW_INTERFACE_IN_LINK_CONTROL_MODE_ACTIVE	1
#define  AQ2_FW_INTERFACE_IN_LINK_CONTROL_MODE_SLEEP_PROXY 2
#define  AQ2_FW_INTERFACE_IN_LINK_CONTROL_MODE_LOWPOWER	3
#define  AQ2_FW_INTERFACE_IN_LINK_CONTROL_MODE_SHUTDOWN	4

#define AQ2_FW_INTERFACE_IN_LINK_OPTIONS_REG	0x12018
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_DOWNSHIFT	(1 << 27)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_PAUSE_TX	(1 << 25)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_PAUSE_RX	(1 << 24)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_EEE_10G	(1 << 20)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_EEE_5G	(1 << 19)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_EEE_2G5	(1 << 18)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_EEE_1G	(1 << 17)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_EEE_100M	(1 << 16)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_10G	(1 << 15)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_N5G	(1 << 14)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_5G	(1 << 13)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_N2G5	(1 << 12)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_2G5	(1 << 11)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_1G	(1 << 10)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_100M	(1 << 9)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_10M	(1 << 8)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_1G_HD	(1 << 7)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_100M_HD	(1 << 6)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_RATE_10M_HD	(1 << 5)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_EXTERNAL_LOOPBACK (1 << 4)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_INTERNAL_LOOPBACK (1 << 3)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_MINIMAL_LINK_SPEED (1 << 2)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_LINK_RENEGOTIATE (1 << 1)
#define  AQ2_FW_INTERFACE_IN_LINK_OPTIONS_LINK_UP	(1 << 0)

#define AQ2_FW_INTERFACE_IN_REQUEST_POLICY_REG	0x12a58
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_MCAST_QUEUE_OR_TC		0x00800000
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_MCAST_RX_QUEUE_TC_INDEX	0x007c0000
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_MCAST_ACCEPT		0x00010000
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_BCAST_QUEUE_OR_TC		0x00008000
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_BCAST_RX_QUEUE_TC_INDEX	0x00007c00
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_BCAST_ACCEPT		0x00000100
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_PROMISC_QUEUE_OR_TC		0x00000080
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_PROMISC_RX_QUEUE_TX_INDEX	0x0000007c
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_PROMISC_MCAST		0x00000002
#define  AQ2_FW_INTERFACE_IN_REQUEST_POLICY_PROMISC_ALL			0x00000001

#define AQ2_FW_INTERFACE_OUT_TRANSACTION_ID_REG	0x13000
#define  AQ2_FW_INTERFACE_OUT_TRANSACTION_ID_B	0xffff0000
#define  AQ2_FW_INTERFACE_OUT_TRANSACTION_ID_B_S 16
#define  AQ2_FW_INTERFACE_OUT_TRANSACTION_ID_A	0x0000ffff
#define  AQ2_FW_INTERFACE_OUT_TRANSACTION_ID_A_S 0

#define AQ2_FW_INTERFACE_OUT_VERSION_BUNDLE_REG	0x13004
#define AQ2_FW_INTERFACE_OUT_VERSION_MAC_REG	0x13008

#define AQ2_FW_INTERFACE_OUT_VERSION_PHY_REG	0x1300c
#define  AQ2_FW_INTERFACE_OUT_VERSION_BUILD	0xffff0000
#define  AQ2_FW_INTERFACE_OUT_VERSION_BUILD_S	16
#define  AQ2_FW_INTERFACE_OUT_VERSION_MINOR	0x0000ff00
#define  AQ2_FW_INTERFACE_OUT_VERSION_MINOR_S	8
#define  AQ2_FW_INTERFACE_OUT_VERSION_MAJOR	0x000000ff
#define  AQ2_FW_INTERFACE_OUT_VERSION_MAJOR_S	0

#define AQ2_FW_INTERFACE_OUT_VERSION_IFACE_REG	0x13010
#define  AQ2_FW_INTERFACE_OUT_VERSION_IFACE_VER	0x0000000f
#define  AQ2_FW_INTERFACE_OUT_VERSION_IFACE_VER_A0 0
#define  AQ2_FW_INTERFACE_OUT_VERSION_IFACE_VER_B0 1

#define AQ2_FW_INTERFACE_OUT_LINK_STATUS_REG	0x13014
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_DUPLEX	(1 << 11)
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_EEE		(1 << 10)
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_PAUSE_RX	(1 << 9)
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_PAUSE_TX	(1 << 8)
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE		0x000000f0
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_S	4
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_10G	6
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_5G	5
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_2G5	4
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_1G	3
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_100M	2
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_10M	1
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_RATE_INVALID	0
#define  AQ2_FW_INTERFACE_OUT_LINK_STATUS_STATE		0x0000000f

#define AQ2_FW_INTERFACE_OUT_FILTER_CAPS_REG	0x13774
#define  AQ2_FW_INTERFACE_OUT_FILTER_CAPS3_RESOLVER_BASE_INDEX 0x00ff0000
#define  AQ2_FW_INTERFACE_OUT_FILTER_CAPS3_RESOLVER_BASE_INDEX_SHIFT 16

#define AQ2_RPF_ACT_ART_REQ_TAG_REG(i)		(0x14000 + (i) * 0x10)
#define AQ2_RPF_ACT_ART_REQ_MASK_REG(i)		(0x14004 + (i) * 0x10)
#define AQ2_RPF_ACT_ART_REQ_ACTION_REG(i)	(0x14008 + (i) * 0x10)

#define __LOWEST_SET_BIT(__mask) (((((uint32_t)__mask) - 1) & ((uint32_t)__mask)) ^ ((uint32_t)__mask))

#define AQ_WRITE_REG_BIT(sc, reg, mask, val)                    \
	do {                                                    \
		uint32_t _v;                                    \
		_v = AQ_READ_REG((sc), (reg));                  \
		_v &= ~(mask);                                  \
		if ((val) != 0)                                 \
			_v |= __SHIFTIN((val), (mask));         \
		AQ_WRITE_REG((sc), (reg), _v);                  \
	} while (/* CONSTCOND */ 0)

#define AQ_READ64_REG(sc, reg)					\
	((uint64_t)AQ_READ_REG(sc, reg) |			\
	(((uint64_t)AQ_READ_REG(sc, (reg) + 4)) << 32))

#define AQ_WRITE64_REG(sc, reg, val)				\
	do {							\
		AQ_WRITE_REG(sc, reg, (uint32_t)val);		\
		AQ_WRITE_REG(sc, reg + 4, (uint32_t)(val >> 32)); \
	} while (/* CONSTCOND */0)

#define WAIT_FOR(expr, us, n, errp)                             \
	do {                                                    \
		unsigned int _n;                                \
		for (_n = n; (!(expr)) && _n != 0; --_n) {      \
			aq_delay(sc,(us));                            \
		}                                               \
		if ((errp != NULL)) {                           \
			if (_n == 0)                            \
				*(errp) = ETIMEDOUT;            \
			else                                    \
				*(errp) = 0;                    \
		}                                               \
	} while (/* CONSTCOND */ 0)

#define FW_VERSION_MAJOR(sc)	(((sc)->sc_fw_version >> 24) & 0xff)
#define FW_VERSION_MINOR(sc)	(((sc)->sc_fw_version >> 16) & 0xff)
#define FW_VERSION_BUILD(sc)	((sc)->sc_fw_version & 0xffff)

#define FEATURES_MIPS		0x00000001
#define FEATURES_TPO2		0x00000002
#define FEATURES_RPF2		0x00000004
#define FEATURES_MPI_AQ		0x00000008
#define FEATURES_AQ1_REV_A0	0x01000000
#define FEATURES_AQ1_REV_A	(FEATURES_AQ1_REV_A0)
#define FEATURES_AQ1_REV_B0	0x02000000
#define FEATURES_AQ1_REV_B1	0x04000000
#define FEATURES_AQ1_REV_B	(FEATURES_AQ1_REV_B0|FEATURES_AQ1_REV_B1)
#define FEATURES_AQ1		(FEATURES_AQ1_REV_A|FEATURES_AQ1_REV_B)
#define FEATURES_AQ2		0x10000000
#define FEATURES_AQ2_IFACE_A0	0x20000000
#define FEATURES_AQ2_IFACE_B0	0x40000000
#define HWTYPE_AQ1_P(sc) 1
#define HWTYPE_AQ2_P(sc) 0

/* lock for firmware interface */

#define FW2X_CTRL_10BASET_HD			(1 << 0)
#define FW2X_CTRL_10BASET_FD			(1 << 1)
#define FW2X_CTRL_100BASETX_HD			(1 << 2)
#define FW2X_CTRL_100BASET4_HD			(1 << 3)
#define FW2X_CTRL_100BASET2_HD			(1 << 4)
#define FW2X_CTRL_100BASETX_FD			(1 << 5)
#define FW2X_CTRL_100BASET2_FD			(1 << 6)
#define FW2X_CTRL_1000BASET_HD			(1 << 7)
#define FW2X_CTRL_1000BASET_FD			(1 << 8)
#define FW2X_CTRL_2P5GBASET_FD			(1 << 9)
#define FW2X_CTRL_5GBASET_FD			(1 << 10)
#define FW2X_CTRL_10GBASET_FD			(1 << 11)
#define FW2X_CTRL_RESERVED1			(1ULL << 32)
#define FW2X_CTRL_10BASET_EEE			(1ULL << 33)
#define FW2X_CTRL_RESERVED2			(1ULL << 34)
#define FW2X_CTRL_PAUSE				(1ULL << 35)
#define FW2X_CTRL_ASYMMETRIC_PAUSE		(1ULL << 36)
#define FW2X_CTRL_100BASETX_EEE			(1ULL << 37)
#define FW2X_CTRL_RESERVED3			(1ULL << 38)
#define FW2X_CTRL_RESERVED4			(1ULL << 39)
#define FW2X_CTRL_1000BASET_FD_EEE		(1ULL << 40)
#define FW2X_CTRL_2P5GBASET_FD_EEE		(1ULL << 41)
#define FW2X_CTRL_5GBASET_FD_EEE		(1ULL << 42)
#define FW2X_CTRL_10GBASET_FD_EEE		(1ULL << 43)
#define FW2X_CTRL_RESERVED5			(1ULL << 44)
#define FW2X_CTRL_RESERVED6			(1ULL << 45)
#define FW2X_CTRL_RESERVED7			(1ULL << 46)
#define FW2X_CTRL_RESERVED8			(1ULL << 47)
#define FW2X_CTRL_RESERVED9			(1ULL << 48)
#define FW2X_CTRL_CABLE_DIAG			(1ULL << 49)
#define FW2X_CTRL_TEMPERATURE			(1ULL << 50)
#define FW2X_CTRL_DOWNSHIFT			(1ULL << 51)
#define FW2X_CTRL_PTP_AVB_EN			(1ULL << 52)
#define FW2X_CTRL_MEDIA_DETECT			(1ULL << 53)
#define FW2X_CTRL_LINK_DROP			(1ULL << 54)
#define FW2X_CTRL_SLEEP_PROXY			(1ULL << 55)
#define FW2X_CTRL_WOL				(1ULL << 56)
#define FW2X_CTRL_MAC_STOP			(1ULL << 57)
#define FW2X_CTRL_EXT_LOOPBACK			(1ULL << 58)
#define FW2X_CTRL_INT_LOOPBACK			(1ULL << 59)
#define FW2X_CTRL_EFUSE_AGENT			(1ULL << 60)
#define FW2X_CTRL_WOL_TIMER			(1ULL << 61)
#define FW2X_CTRL_STATISTICS			(1ULL << 62)
#define FW2X_CTRL_TRANSACTION_ID		(1ULL << 63)

#define FW2X_CTRL_RATE_100M			FW2X_CTRL_100BASETX_FD
#define FW2X_CTRL_RATE_1G			FW2X_CTRL_1000BASET_FD
#define FW2X_CTRL_RATE_2G5			FW2X_CTRL_2P5GBASET_FD
#define FW2X_CTRL_RATE_5G			FW2X_CTRL_5GBASET_FD
#define FW2X_CTRL_RATE_10G			FW2X_CTRL_10GBASET_FD
#define FW2X_CTRL_RATE_MASK		\
	(FW2X_CTRL_RATE_100M |		\
	 FW2X_CTRL_RATE_1G |		\
	 FW2X_CTRL_RATE_2G5 |		\
	 FW2X_CTRL_RATE_5G |		\
	 FW2X_CTRL_RATE_10G)
#define FW2X_CTRL_EEE_MASK		\
	(FW2X_CTRL_10BASET_EEE |	\
	 FW2X_CTRL_100BASETX_EEE |	\
	 FW2X_CTRL_1000BASET_FD_EEE |	\
	 FW2X_CTRL_2P5GBASET_FD_EEE |	\
	 FW2X_CTRL_5GBASET_FD_EEE |	\
	 FW2X_CTRL_10GBASET_FD_EEE)

enum aq_hwtype {
	HWTYPE_AQ1,
	HWTYPE_AQ2
};

enum aq_fw_bootloader_mode {
	FW_BOOT_MODE_UNKNOWN = 0,
	FW_BOOT_MODE_FLB,
	FW_BOOT_MODE_RBL_FLASH,
	FW_BOOT_MODE_RBL_HOST_BOOTLOAD
};

enum aq_media_type {
	AQ_MEDIA_TYPE_UNKNOWN = 0,
	AQ_MEDIA_TYPE_FIBRE,
	AQ_MEDIA_TYPE_TP
};

enum aq_link_speed {
	AQ_LINK_NONE    = 0,
	AQ_LINK_10M	= (1 << 0),
	AQ_LINK_100M    = (1 << 1),
	AQ_LINK_1G      = (1 << 2),
	AQ_LINK_2G5     = (1 << 3),
	AQ_LINK_5G      = (1 << 4),
	AQ_LINK_10G     = (1 << 5)
};

#define AQ_LINK_ALL	(AQ_LINK_100M | AQ_LINK_1G | AQ_LINK_2G5 | \
			    AQ_LINK_5G | AQ_LINK_10G )
#define AQ_LINK_AUTO	AQ_LINK_ALL

enum aq_link_eee {
	AQ_EEE_DISABLE = 0,
	AQ_EEE_ENABLE = 1
};

enum aq_hw_fw_mpi_state {
	MPI_DEINIT      = 0,
	MPI_RESET       = 1,
	MPI_INIT        = 2,
	MPI_POWER       = 4
};

enum aq_link_fc {
        AQ_FC_NONE = 0,
        AQ_FC_RX = (1 << 0),
        AQ_FC_TX = (1 << 1),
        AQ_FC_ALL = (AQ_FC_RX | AQ_FC_TX)
};

#define AQ_SMB_START_TRANSMIT		0x5001
#define AQ_SMB_START_READ_TRANSMIT	0x5101
#define AQ_SMB_STOP_TRANSMIT		0x3001
#define AQ_SMB_REPEAT_TRANSMIT		0x1001
#define AQ_SMB_REPEAT_NACK_TRANSMIT	0x1011
