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

#ifndef AQC_NATIVE_H
#define AQC_NATIVE_H
#include "netdev.h"
#include "pci.h"
#include "kheap.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"
#include "io.h"
#define printf klog
#define EBUSY 16
#define ETIMEDOUT 110
#define ENOTSUP 95
#define ENXIO 6
#define EINVAL 22
#define offsetof __builtin_offsetof
#define snprintf ksnprintf
#define true 1
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define __BIT(n) (1ULL<<(n))
#define __BITS(h,l) ((~0ULL>>(63-(h)))&(~0ULL<<(l)))
#define __SHIFTIN(v,m) (((uint64_t)(v)<<__builtin_ctzll(m))&(m))
#define __SHIFTOUT(v,m) (((v)&(m))>>__builtin_ctzll(m))
#define DEVNAME(s) "aqc107"
#define DPRINTF(...) ((void)0)
#define ETHER_ADDR_LEN 6
#define ETHERTYPE_QINQ 0x88a8
#define ETHERTYPE_VLAN 0x8100
struct aq_softc;
static uint32_t aq_read(struct aq_softc*,uint32_t);
static void aq_write(struct aq_softc*,uint32_t,uint32_t);
static void aq_delay(struct aq_softc*,uint32_t);
#define AQ_READ_REG(s,r) aq_read(s,r)
#define AQ_WRITE_REG(s,r,v) aq_write(s,r,v)
#define delay(n) aq_delay(sc,n)
#define AQ_MPI_LOCK(s) ((void)0)
#define AQ_MPI_UNLOCK(s) ((void)0)
#define htobe32(x) __builtin_bswap32(x)
#define aq2_filter_art_set(...) ((void)0)
#define arc4random_buf(p,n) memset(p,0,n)
#include "aqreg.h"

struct aq_mailbox_header {
        uint32_t version;
        uint32_t transaction_id;
        int32_t error;
} __packed __aligned(4);

struct aq_hw_stats_s {
        uint32_t uprc;
        uint32_t mprc;
        uint32_t bprc;
        uint32_t erpt;
        uint32_t uptc;
        uint32_t mptc;
        uint32_t bptc;
        uint32_t erpr;
        uint32_t mbtc;
        uint32_t bbtc;
        uint32_t mbrc;
        uint32_t bbrc;
        uint32_t ubrc;
        uint32_t ubtc;
        uint32_t ptc;
        uint32_t prc;
        uint32_t dpc;   /* not exists in fw2x_msm_statistics */
        uint32_t cprc;  /* not exists in fw2x_msm_statistics */
} __packed __aligned(4);

struct aq_fw2x_capabilities {
        uint32_t caps_lo;
        uint32_t caps_hi;
} __packed __aligned(4);

struct aq_fw2x_msm_statistics {
	uint32_t uprc;
	uint32_t mprc;
	uint32_t bprc;
	uint32_t erpt;
	uint32_t uptc;
	uint32_t mptc;
	uint32_t bptc;
	uint32_t erpr;
	uint32_t mbtc;
	uint32_t bbtc;
	uint32_t mbrc;
	uint32_t bbrc;
	uint32_t ubrc;
	uint32_t ubtc;
	uint32_t ptc;
	uint32_t prc;
} __packed __aligned(4);

struct aq_fw2x_phy_cable_diag_data {
	uint32_t lane_data[4];
} __packed __aligned(4);

struct aq_fw2x_mailbox {		/* struct fwHostInterface */
	struct aq_mailbox_header header;
	struct aq_fw2x_msm_statistics msm;	/* msmStatistics_t msm; */

	uint32_t phy_info1;
#define PHYINFO1_FAULT_CODE	__BITS(31,16)
#define PHYINFO1_PHY_H_BIT	__BITS(0,15)
	uint32_t phy_info2;
#define PHYINFO2_TEMPERATURE	__BITS(15,0)
#define PHYINFO2_CABLE_LEN	__BITS(23,16)

	struct aq_fw2x_phy_cable_diag_data diag_data;
	uint32_t reserved[8];

	struct aq_fw2x_capabilities caps;

	/* ... */
} __packed __aligned(4);

struct aq_rx_desc_read {
	uint64_t		buf_addr;
	uint64_t		hdr_addr;
} __packed;

struct aq_rx_desc_wb {
	uint32_t		type;
#define AQ_RXDESC_TYPE_RSSTYPE	0x000f
#define AQ_RXDESC_TYPE_ETHER	0x0030
#define AQ_RXDESC_TYPE_PROTO	0x01c0
#define AQ_RXDESC_TYPE_VLAN	(1 << 9)
#define AQ_RXDESC_TYPE_VLAN2	(1 << 10)
#define AQ_RXDESC_TYPE_DMA_ERR	(1 << 12)
#define AQ_RXDESC_TYPE_V4_SUM	(1 << 19)
#define AQ_RXDESC_TYPE_L4_SUM	(1 << 20)
	uint32_t		rss_hash;
	uint16_t		status;
#define AQ_RXDESC_STATUS_DD	(1 << 0)
#define AQ_RXDESC_STATUS_EOP	(1 << 1)
#define AQ_RXDESC_STATUS_MACERR (1 << 2)
#define AQ_RXDESC_STATUS_V4_SUM_NG (1 << 3)
#define AQ_RXDESC_STATUS_L4_SUM_ERR (1 << 4)
#define AQ_RXDESC_STATUS_L4_SUM_OK (1 << 5)
	uint16_t		pkt_len;
	uint16_t		next_desc_ptr;
	uint16_t		vlan;
} __packed;

struct aq_tx_desc {
	uint64_t		buf_addr;
	uint32_t		ctl1;
#define AQ_TXDESC_CTL1_TYPE_TXD	0x00000001
#define AQ_TXDESC_CTL1_TYPE_TXC	0x00000002
#define AQ_TXDESC_CTL1_BLEN_SHIFT 4
#define AQ_TXDESC_CTL1_VLAN_SHIFT 4
#define AQ_TXDESC_CTL1_DD	(1 << 20)
#define AQ_TXDESC_CTL1_CMD_EOP	(1 << 21)
#define AQ_TXDESC_CTL1_CMD_VLAN	(1 << 22)
#define AQ_TXDESC_CTL1_CMD_FCS	(1 << 23)
#define AQ_TXDESC_CTL1_CMD_IP4CSUM (1 << 24)
#define AQ_TXDESC_CTL1_CMD_L4CSUM (1 << 25)
#define AQ_TXDESC_CTL1_CMD_WB	(1 << 27)

#define AQ_TXDESC_CTL1_VID_SHIFT 4
	uint32_t		ctl2;
#define AQ_TXDESC_CTL2_LEN_SHIFT 14
#define AQ_TXDESC_CTL2_CTX_EN	(1 << 13)
} __packed;



struct aq_softc {
 uint32_t sc_mbox_addr,sc_fw_version,sc_features;
 int sc_rbl_enabled,sc_fast_start_enabled,sc_flash_present,sc_nqueues,sc_linkstat_irq;
 uint64_t sc_fw_caps;
 struct {uint8_t ether_addr_octet[6];} sc_enaddr;
 uint32_t (*read)(struct aq_softc*,uint32_t);
 void (*write)(struct aq_softc*,uint32_t,uint32_t);
 void (*wait)(struct aq_softc*,uint32_t);
 int ok;
 volatile uint8_t *mmio;
};
static uint32_t aq_read(struct aq_softc*s,uint32_t r){return s->ok?s->read(s,r):0;}
static void aq_write(struct aq_softc*s,uint32_t r,uint32_t v){if(s->ok)s->write(s,r,v);}
static void aq_delay(struct aq_softc*s,uint32_t n){s->wait(s,n);}

#include "aq_hal.inc"

#endif
