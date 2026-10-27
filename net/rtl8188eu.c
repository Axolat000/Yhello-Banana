/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native RTL8188EU USB frontend, experimental legacy 2.4 GHz station.
 * Hardware routines adapted from OpenBSD under the retained ISC terms.
 * See third_party/rtwn/README.md and firmware/realtek/NOTICES.
 */
/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2015 Stefan Sperling <stsp@openbsd.org>
 * Copyright (c) 2016 Nathanial Sloss <nathanialsloss@yahoo.com.au>
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
/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2016 Nathanial Sloss <nathanialsloss@yahoo.com.au>
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
#include "wifi.h"
#include "../usb/usbcore.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"
#define printf klog
#define EINVAL 22
#define ETIMEDOUT 110
#define __packed __attribute__((packed))
#define IEEE80211_ADDR_LEN 6
#define IEEE80211_ADDR_COPY(a,b) memcpy(a,b,6)
#define nitems(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define RTWN_SIGN4TO8(val) (((val)&8)?(val)|0xf0:(val))
#define DPRINTF(...) ((void)0)
#define KASSERT(x) do {if(!(x)){sc->ok=0;return -1;}}while(0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define abs(x) __builtin_abs(x)
#define letoh16(x) (x)
#define letoh32(x) (x)
#define htole16(x) (x)
#define htole32(x) (x)
typedef unsigned int u_int;
typedef unsigned char u_char;
#define R92C_HQ_NPAGES		12
#define R92C_LQ_NPAGES		2
#define R92C_NQ_NPAGES		2
#define R92C_TXPKTBUF_COUNT	256
#define R92C_TX_PAGE_COUNT	248
#define R92C_MAX_RX_DMA_SIZE	0x2800

#define R88E_HQ_NPAGES		0
#define R88E_LQ_NPAGES		9
#define R88E_NQ_NPAGES		0
#define R88E_TXPKTBUF_COUNT	177
#define R88E_TX_PAGE_COUNT	168
#define R88E_MAX_RX_DMA_SIZE	0x2400

#define R88F_HQ_NPAGES		12
#define R88F_LQ_NPAGES		2
#define R88F_NQ_NPAGES		2
#define R88F_TXPKTBUF_COUNT	177
#define R88F_TX_PAGE_COUNT	247
#define R88F_MAX_RX_DMA_SIZE	0x3f80

#define R92E_HQ_NPAGES		16
#define R92E_LQ_NPAGES		16
#define R92E_NQ_NPAGES		16
#define R92E_TX_PAGE_COUNT	248
#define R92E_MAX_RX_DMA_SIZE	0x3fc0

#define R92C_TXDESC_SUMSIZE	32
#define R92C_TXDESC_SUMOFFSET	14


#define RTWN_IQ_CAL_MAX_TOLERANCE 5
#define RTWN_IQ_CAL_NRUN 3
#include "../third_party/rtwn/r92creg.h"
struct ieee80211com { uint8_t ic_myaddr[6]; };
struct ieee80211_channel { unsigned channel,ic_freq; };
static unsigned ieee80211_chan2ieee(struct ieee80211com *ic,struct ieee80211_channel *c) {(void)ic;return c->channel;}
struct rtwn_softc {
 struct ieee80211com sc_ic;
 struct r88e_rom sc_r88e_rom;
 int board_type, crystal_cap, regulatory, iq_ok, fwcur, nrxchains,ntxchains,ntx,ok,sc_flags;
 uint32_t rf_chnlbw[2];
 int (*control)(struct rtwn_softc*,int,uint16_t,void*,unsigned);
 void (*delay)(struct rtwn_softc*,unsigned);
 usb_device_t *dev;
 wifi_radio_t wifi;
 usb_xfer_t rx[2],tx[8],zlp[8];
 uint8_t in,out[3];uint16_t mps[3];
 unsigned rate,reports,retries,failures,good;
 int detached,queued;
 const uint8_t *firmware;size_t firmware_size;
};
#define DELAY(n) sc->delay(sc,n)
static int urtwn_write_region_1(struct rtwn_softc*s,uint16_t a,const uint8_t*b,int n) {
 if(!s->ok)return -1;
 if(n<0||n>254||s->control(s,0,a,(void*)b,n)!=n){s->ok=0;return -1;}return 0;
}
static uint32_t reg_read(struct rtwn_softc*s,uint16_t a,unsigned n) {
 uint8_t b[4]={0};
 if(!s->ok || s->control(s,1,a,b,n)!=(int)n){s->ok=0;return 0;}
 return b[0]|(n>=2?(uint32_t)b[1]<<8:0)|(n==4?(uint32_t)b[2]<<16|(uint32_t)b[3]<<24:0);
}
static uint8_t rtwn_read_1(struct rtwn_softc*s,uint16_t a){return reg_read(s,a,1);}
static uint16_t rtwn_read_2(struct rtwn_softc*s,uint16_t a){return reg_read(s,a,2);}
static uint32_t rtwn_read_4(struct rtwn_softc*s,uint16_t a){return reg_read(s,a,4);}
static void reg_write(struct rtwn_softc*s,uint16_t a,uint32_t v,unsigned n){
 uint8_t b[]={v,v>>8,v>>16,v>>24};urtwn_write_region_1(s,a,b,n);
}
static void rtwn_write_1(struct rtwn_softc*s,uint16_t a,uint8_t v){reg_write(s,a,v,1);}
static void rtwn_write_2(struct rtwn_softc*s,uint16_t a,uint16_t v){reg_write(s,a,v,2);}
static void rtwn_write_4(struct rtwn_softc*s,uint16_t a,uint32_t v){reg_write(s,a,v,4);}
#define rtwn_bb_read rtwn_read_4
#define rtwn_bb_write rtwn_write_4
#define urtwn_read_1 rtwn_read_1
#define urtwn_read_2 rtwn_read_2
#define urtwn_read_4 rtwn_read_4
#define urtwn_write_1 rtwn_write_1
#define urtwn_write_2 rtwn_write_2
#define urtwn_write_4 rtwn_write_4
#define urtwn_bb_read rtwn_read_4
#define urtwn_bb_write rtwn_write_4

#define RTWN_FLAG_CCK_HIPWR		0x01
#define RTWN_FLAG_BUSY			0x02
#define RTWN_FLAG_FORCE_RAID_11B	0x04
#define RTWN_FLAG_EXT_HDR		0x08
#define RTWN_CHIP_92C		0x00000001
#define RTWN_CHIP_92C_1T2R	0x00000002
#define RTWN_CHIP_UMC		0x00000004
#define RTWN_CHIP_UMC_A_CUT	0x00000008
#define RTWN_CHIP_88C		0x00000010
#define RTWN_CHIP_88E		0x00000020
#define RTWN_CHIP_92E		0x00000040
#define RTWN_CHIP_23A		0x00000080
#define RTWN_CHIP_23B		0x00000100
#define RTWN_CHIP_88F		0x00000200
#define RTWN_CHIP_PCI		0x40000000
#define RTWN_CHIP_USB		0x80000000
#define RTWN_RIDX_CCK1		0
#define RTWN_RIDX_CCK2		1
#define RTWN_RIDX_CCK11		3
#define RTWN_RIDX_OFDM6		4
#define RTWN_RIDX_OFDM54	11
#define RTWN_RIDX_MCS0		12
#define RTWN_RIDX_MCS8		(RTWN_RIDX_MCS0 + 8)
#define RTWN_RIDX_MCS15		27
#define RTWN_RIDX_MAX		27
#define RTWN_POWER_CCK1		0
#define RTWN_POWER_CCK2		1
#define RTWN_POWER_CCK55	2
#define RTWN_POWER_CCK11	3
#define RTWN_POWER_OFDM6	4
#define RTWN_POWER_OFDM9	5
#define RTWN_POWER_OFDM12	6
#define RTWN_POWER_OFDM18	7
#define RTWN_POWER_OFDM24	8
#define RTWN_POWER_OFDM36	9
#define RTWN_POWER_OFDM48	10
#define RTWN_POWER_OFDM54	11
#define RTWN_POWER_MCS(mcs)	(12 + (mcs))
#define RTWN_POWER_COUNT	28
struct rtwn_iq_cal_regs {
	uint32_t	adda[16];
	uint8_t		txpause;
	uint8_t		bcn_ctrl;
	uint8_t		bcn_ctrl1;
	uint32_t	gpio_muxcfg;
	uint32_t	ofdm0_trxpathena;
	uint32_t	ofdm0_trmuxpar;
	uint32_t	fpga0_rfifacesw0;
	uint32_t	fpga0_rfifacesw1;
	uint32_t	fpga0_rfifaceoe0;
	uint32_t	fpga0_rfifaceoe1;
	uint32_t	config_ant_a;
	uint32_t	config_ant_b;
	uint32_t	cck0_afesetting;
};
static int rtl_firmware(struct rtwn_softc *s,uint8_t **fw,size_t *n) {
 *fw=(uint8_t*)s->firmware;*n=s->firmware_size;return !*fw;
}
#include "../third_party/rtwn/rtl8188eu_hal.inc"
static int
rtwn_load_firmware(struct rtwn_softc *sc)
{
	const struct r92c_fw_hdr *hdr;
	u_char *fw, *ptr;
	size_t len0, len;
	uint32_t reg;
	int mlen, ntries, page, error;

	/* Read firmware image from the filesystem. */
	error = rtl_firmware(sc, &fw, &len0);
	if (error)
		return (error);
	len = len0;
    if (len < sizeof(*hdr) || len > 32768 ||
        (fw[0] | (uint16_t)fw[1] << 8) >> 4 != 0x88e ||
        (unsigned)(fw[12] | (uint16_t)fw[13] << 8) != len-sizeof(*hdr))
        { return -1; }
	ptr = fw;
	hdr = (const struct r92c_fw_hdr *)ptr;
	/* Check if there is a valid FW header and skip it. */
	if ((letoh16(hdr->signature) >> 4) == 0x230 ||
	    (letoh16(hdr->signature) >> 4) == 0x88c ||
	    (letoh16(hdr->signature) >> 4) == 0x88e ||
	    (letoh16(hdr->signature) >> 4) == 0x88f ||
	    (letoh16(hdr->signature) >> 4) == 0x92c ||
	    (letoh16(hdr->signature) >> 4) == 0x92e) {
		DPRINTF(("FW V%d.%d %02d-%02d %02d:%02d\n",
		    letoh16(hdr->version), letoh16(hdr->subversion),
		    hdr->month, hdr->date, hdr->hour, hdr->minute));
		ptr += sizeof(*hdr);
		len -= sizeof(*hdr);
	}

	if (rtwn_read_1(sc, R92C_MCUFWDL) & R92C_MCUFWDL_RAM_DL_SEL) {
		rtwn_write_1(sc, R92C_MCUFWDL, 0);
		rtwn_r88e_fw_reset(sc);
	}

	if (((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_PCI) || ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)) {
		rtwn_write_2(sc, R92C_SYS_FUNC_EN,
		    rtwn_read_2(sc, R92C_SYS_FUNC_EN) | R92C_SYS_FUNC_EN_CPUEN);
	}

	/* Enable FW download. */
	rtwn_write_1(sc, R92C_MCUFWDL,
	    rtwn_read_1(sc, R92C_MCUFWDL) | R92C_MCUFWDL_EN);
	rtwn_write_4(sc, R92C_MCUFWDL,
	    rtwn_read_4(sc, R92C_MCUFWDL) & ~R92C_MCUFWDL_ROM_DLEN);

	/* Reset the FWDL checksum. */
	rtwn_write_1(sc, R92C_MCUFWDL,
	    rtwn_read_1(sc, R92C_MCUFWDL) | R92C_MCUFWDL_CHKSUM_RPT);

	DELAY(50);
	for (page = 0; len > 0; page++) {
		mlen = MIN(len, R92C_FW_PAGE_SIZE);
		error = urtwn_fw_loadpage(sc, page, ptr,
		    mlen);
		if (error != 0) {
			printf("%s: could not load firmware page %d\n",
			    "rtl8188eu", page);
			goto fail;
		}
		ptr += mlen;
		len -= mlen;
	}

	/* Wait for checksum report. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (rtwn_read_4(sc, R92C_MCUFWDL) & R92C_MCUFWDL_CHKSUM_RPT)
			break;
		DELAY(5);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for checksum report\n",
		    "rtl8188eu");
		error = ETIMEDOUT;
		goto fail;
	}

	/* Disable FW download. */
	rtwn_write_1(sc, R92C_MCUFWDL,
	    rtwn_read_1(sc, R92C_MCUFWDL) & ~R92C_MCUFWDL_EN);

	/* Reserved for fw extension. */
	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88F | RTWN_CHIP_92E)))
		rtwn_write_1(sc, R92C_MCUFWDL + 1, 0);

	reg = rtwn_read_4(sc, R92C_MCUFWDL);
	reg = (reg & ~R92C_MCUFWDL_WINTINI_RDY) | R92C_MCUFWDL_RDY;
	rtwn_write_4(sc, R92C_MCUFWDL, reg);
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_92C | RTWN_CHIP_88C | RTWN_CHIP_23A)) {
		reg = rtwn_read_2(sc, R92C_SYS_FUNC_EN);
		rtwn_write_2(sc, R92C_SYS_FUNC_EN,
		    reg & ~R92C_SYS_FUNC_EN_CPUEN);
		rtwn_write_2(sc, R92C_SYS_FUNC_EN,
		    reg | R92C_SYS_FUNC_EN_CPUEN);
	} else
		rtwn_r88e_fw_reset(sc);
	/* Wait for firmware readiness. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (rtwn_read_4(sc, R92C_MCUFWDL) & R92C_MCUFWDL_WINTINI_RDY)
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		printf("%s: timeout waiting for firmware readiness\n",
		    "rtl8188eu");
		error = ETIMEDOUT;
		goto fail;
	}
fail:
	/* Firmware is immutable storage owned by the caller. */
	/* Init H2C command. */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)
		rtwn_write_1(sc, R92C_HMETFR, 0xf);
	return (error);
}
static int rtl_hw_init(struct rtwn_softc *sc) {
 sc->ntxchains=sc->nrxchains=1;
 sc->sc_flags=RTWN_FLAG_EXT_HDR;
 rtwn_r88e_read_rom(sc);
 if(!sc->ok || sc->sc_r88e_rom.id!=0x8129 ||
    sc->sc_r88e_rom.r88eu_rom.vid!=0x0bda ||
    sc->sc_r88e_rom.r88eu_rom.pid!=0x8179)return -1;
 uint8_t any=0;for(int i=0;i<6;i++)any|=sc->sc_ic.ic_myaddr[i];
 if(!any || (sc->sc_ic.ic_myaddr[0]&1))return -1;
 /* Never invent power calibration when the OTP map is absent/corrupt. */
 for(int i=0;i<6;i++)if(sc->sc_r88e_rom.txpwr.cck_tx_pwr[i]>63)return -1;
 for(int i=0;i<5;i++)if(sc->sc_r88e_rom.txpwr.ht40_tx_pwr[i]>63)return -1;
 if(sc->sc_r88e_rom.xtal==0xff)return -1;
 if(urtwn_r88e_power_on(sc) || !sc->ok)return -1;
 if(urtwn_dma_init(sc) || !sc->ok)return -1;
 rtwn_write_1(sc,R92C_RX_DRVINFO_SZ,4);
 /* Poll bulk RX for both data and firmware notifications. */
 rtwn_write_4(sc,R88E_HISR,0xffffffff);
 rtwn_write_4(sc,R88E_HIMR,R88E_HIMR_CPWM|R88E_HIMR_CPWM2|R88E_HIMR_TBDER|R88E_HIMR_PSTIMEOUT);
 rtwn_write_4(sc,R88E_HIMRE,R88E_HIMRE_RXFOVW|R88E_HIMRE_TXFOVW|R88E_HIMRE_RXERR|R88E_HIMRE_TXERR);
 rtwn_write_1(sc,R92C_USB_SPECIAL_OPTION,rtwn_read_1(sc,R92C_USB_SPECIAL_OPTION)|R92C_USB_SPECIAL_OPTION_INT_BULK_SEL);
 for(int i=0;i<6;i++)rtwn_write_1(sc,R92C_MACID+i,sc->sc_ic.ic_myaddr[i]);
 rtwn_write_1(sc,R92C_MSR,0);
 rtwn_rxfilter_init(sc);
 rtwn_write_4(sc,R92C_RRSR,RW(rtwn_read_4(sc,R92C_RRSR),R92C_RRSR_RATE_BITMAP,R92C_RRSR_RATE_CCK_ONLY_1M));
 rtwn_write_2(sc,R92C_RL,SM(R92C_RL_SRL,0x30)|SM(R92C_RL_LRL,0x30));
 rtwn_write_2(sc,R92C_SPEC_SIFS,0x100a);rtwn_write_2(sc,R92C_MAC_SPEC_SIFS,0x100a);
 rtwn_write_2(sc,R92C_SIFS_CCK,0x100a);rtwn_write_2(sc,R92C_SIFS_OFDM,0x100a);
 rtwn_write_2(sc,R92C_RESP_SIFS_CCK,0x100a);rtwn_write_2(sc,R92C_RESP_SIFS_OFDM,0x100a);
 rtwn_write_4(sc,R92C_EDCA_VO_PARAM,0x002f3217);
 rtwn_write_4(sc,R92C_EDCA_VI_PARAM,0x005e4317);
 rtwn_write_4(sc,R92C_EDCA_BE_PARAM,0x00105320);
 rtwn_write_4(sc,R92C_EDCA_BK_PARAM,0x0000a444);
 rtwn_write_1(sc,R92C_FWHW_TXQ_CTRL,rtwn_read_1(sc,R92C_FWHW_TXQ_CTRL)|R92C_FWHW_TXQ_CTRL_AMPDU_RTY_NEW);
 rtwn_write_1(sc,R92C_ACKTO,0x40);
 rtwn_write_4(sc,R92C_TDECTRL,RW(rtwn_read_4(sc,R92C_TDECTRL),R92C_TDECTRL_BLK_DESC_NUM,1));
 rtwn_write_1(sc,R92C_TRXDMA_CTRL,rtwn_read_1(sc,R92C_TRXDMA_CTRL)|R92C_TRXDMA_CTRL_RXDMA_AGG_EN);
 rtwn_write_1(sc,R92C_RXDMA_AGG_PG_TH,48);rtwn_write_1(sc,R92C_RXDMA_AGG_PG_TH+1,4);
 rtwn_write_4(sc,R92C_TXDMA_OFFSET_CHK,rtwn_read_4(sc,R92C_TXDMA_OFFSET_CHK)|R92C_TXDMA_OFFSET_CHK_DROP_DATA_EN);
 rtwn_write_2(sc,R92C_BCN_CTRL,(R92C_BCN_CTRL_DIS_TSF_UDT0<<8)|R92C_BCN_CTRL_DIS_TSF_UDT0);
 rtwn_write_2(sc,R92C_TBTT_PROHIBIT,0x6404);
 rtwn_write_1(sc,R92C_DRVERLYINT,R92C_DRVERLYINT_INIT_TIME);
 rtwn_write_1(sc,R92C_BCNDMATIM,R92C_BCNDMATIM_INIT_TIME);
 rtwn_write_2(sc,R92C_BCNTCFG,0x660f);
 if(rtwn_load_firmware(sc) || !sc->ok)return -1;
 urtwn_mac_init(sc);urtwn_bb_init(sc);rtwn_rf_init(sc);
 if(!sc->ok)return -1;
 rtwn_write_2(sc,R92C_CR,rtwn_read_2(sc,R92C_CR)|R92C_CR_MACTXEN|R92C_CR_MACRXEN);
 rtwn_bb_write(sc,R92C_FPGA0_RFMOD,rtwn_bb_read(sc,R92C_FPGA0_RFMOD)|R92C_RFMOD_CCK_EN|R92C_RFMOD_OFDM_EN);
 rtwn_cam_init(sc);
 /* The shared station layer owns CCMP and sequence numbers. */
 rtwn_write_2(sc,R92C_SECCFG,0);
 rtwn_write_1(sc,R92C_HWSEQ_CTRL,0);
 rtwn_iq_calib(sc);if(!sc->iq_ok || !sc->ok)return -1;
 rtwn_lc_calib(sc);
 rtwn_write_1(sc,R92C_GPIO_MUXCFG,rtwn_read_1(sc,R92C_GPIO_MUXCFG)&~R92C_GPIO_MUXCFG_ENBT);
 struct ieee80211_channel ch={1,2412};rtwn_set_chan(sc,&ch,NULL);
 return sc->ok?0:-1;
}


static uint16_t eu16(const uint8_t*b){return b[0]|(uint16_t)b[1]<<8;}
static uint32_t eu32(const uint8_t*b){return eu16(b)|(uint32_t)eu16(b+2)<<16;}
static void eup16(uint8_t*b,uint16_t v){b[0]=v;b[1]=v>>8;}
static void eup32(uint8_t*b,uint32_t v){eup16(b,v);eup16(b+2,v>>16);}
static int eu_control(struct rtwn_softc*s,int rd,uint16_t a,void*b,unsigned n){
 if(s->detached || !s->dev->present)return -1;
 return usb_control(s->dev,USB_TYPE_VENDOR|(rd?USB_DIR_IN:0),0x05,a,0,b,n);
}
static void eu_delay(struct rtwn_softc*s,unsigned us){(void)s;usb_delay_ms((us+999)/1000);}
static int eu_channel(wifi_radio_t*r,unsigned n){
 struct rtwn_softc*s=r->hw;
 if(!s->ok||s->detached||n<1||n>11)return -1;
 struct ieee80211_channel c={n,2407+5*n};rtwn_set_chan(s,&c,NULL);
 return s->ok?0:-1;
}
static int eu_rate(wifi_radio_t*r,unsigned rate){
 static const uint8_t rates[]={2,4,11,22,12,18,24,36,48,72,96,108};
 if(rate>=12)return 0;
 for(unsigned i=0;i<r->ap.nrates;i++)if((r->ap.rates[i]&127)==rates[rate])return 1;
 return !r->ap.nrates && !rate;
}
static void eu_edca(struct rtwn_softc*s,unsigned slot){
 rtwn_write_1(s,R92C_SLOT,slot);
 /* Legacy station: default contention parameters, no negotiated WMM/HT. */
 rtwn_write_4(s,R92C_EDCA_BE_PARAM,0xa400|(10+3*slot));
 rtwn_write_4(s,R92C_EDCA_BK_PARAM,0xa400|(10+7*slot));
 rtwn_write_4(s,R92C_EDCA_VI_PARAM,0x005e4300|(10+2*slot));
 rtwn_write_4(s,R92C_EDCA_VO_PARAM,0x002f3200|(10+2*slot));
}
static int eu_station(wifi_radio_t*r,const uint8_t*bssid,unsigned aid){
 struct rtwn_softc*s=r->hw;if(!s->ok||s->detached)return -1;
 rtwn_write_1(s,R92C_MSR,rtwn_read_1(s,R92C_MSR)&~R92C_MSR_NETTYPE_MASK);
 uint32_t cr=rtwn_read_4(s,R92C_RCR)&~(R92C_RCR_CBSSID_DATA|R92C_RCR_CBSSID_BCN);
 for(int i=0;i<6;i++)rtwn_write_1(s,R92C_BSSID+i,bssid?bssid[i]:0);
 rtwn_write_1(s,R92C_TXPAUSE,0);
 eu_edca(s,bssid&&(r->ap.capability&0x400)?9:20);
 s->rate=0;while(s->rate<11&&!eu_rate(r,s->rate))s->rate++;
 s->reports=s->retries=s->failures=s->good=0;
 if(bssid){
  uint32_t rates=0,basic=0;
  static const uint8_t values[]={2,4,11,22,12,18,24,36,48,72,96,108};
  for(unsigned i=0;i<12;i++)if(eu_rate(r,i)){
   rates|=1u<<i;
   for(unsigned j=0;j<r->ap.nrates;j++)if(r->ap.rates[j]==(values[i]|128))basic|=1u<<i;
  }
  rtwn_write_4(s,R92C_RRSR,RW(rtwn_read_4(s,R92C_RRSR),R92C_RRSR_RATE_BITMAP,basic?basic:rates));
  rtwn_bb_write(s,R92C_OFDM0_AGCCORE1(0),RW(rtwn_bb_read(s,R92C_OFDM0_AGCCORE1(0)),R92C_OFDM0_AGCCORE1_GAIN,0x32));
 }
 if(aid&&bssid){
  rtwn_write_1(s,R92C_MSR,(rtwn_read_1(s,R92C_MSR)&~R92C_MSR_NETTYPE_MASK)|R92C_MSR_NETTYPE_INFRA);
  rtwn_write_2(s,R92C_BCN_INTERVAL,r->ap.beacon_ms?r->ap.beacon_ms:100);
  cr|=R92C_RCR_CBSSID_DATA|R92C_RCR_CBSSID_BCN;
  rtwn_write_1(s,R92C_INIRTS_RATE_SEL,s->rate<4?0:3);
 }
 rtwn_write_4(s,R92C_RCR,cr);
 return s->ok?0:-1;
}
static int eu_send(wifi_radio_t*r,const uint8_t*f,uint32_t n){
 struct rtwn_softc*s=r->hw;
 if(!s->ok||s->detached||!r->nd.present||n<24||n>WIFI_FRAME_MAX)return -1;
 unsigned i;for(i=0;i<8;i++)if(s->tx[i].status!=USB_XFER_PENDING&&s->zlp[i].status!=USB_XFER_PENDING)break;
 if(i==8)return -1;
 int data=(f[0]&0x0c)==8 && !(f[4]&1);
 unsigned pipe=data?(unsigned)s->ntx-1:0;
 usb_xfer_t*x=&s->tx[i];x->ep=s->out[pipe];s->zlp[i].ep=x->ep;
 uint8_t*b=x->buf;memset(b,0,32);
 unsigned rate=data?s->rate:0;
 if(!eu_rate(r,rate)){for(rate=0;rate<11&&!eu_rate(r,rate);rate++);}
 eup32(b,SM(R92C_TXDW0_PKTLEN,n)|SM(R92C_TXDW0_OFFSET,32)|
     R92C_TXDW0_OWN|R92C_TXDW0_FSG|R92C_TXDW0_LSG|((f[4]&1)?R92C_TXDW0_BMCAST:0));
 eup32(b+4,SM(R88E_TXDW1_MACID,data?R92C_MACID_BSS:0)|
     SM(R92C_TXDW1_QSEL,data?R92C_TXDW1_QSEL_BE:R92C_TXDW1_QSEL_MGNT)|
     SM(R92C_TXDW1_RAID,R92C_RAID_11BG));
 if(data)eup32(b+8,R88E_TXDW2_AGGBK|R92C_TXDW2_CCX_RPT);
 eup16(b+14,eu16(f+22)>>4);
 eup32(b+16,R92C_TXDW4_DRVRATE);
 eup32(b+20,0x0001ff00|SM(R92C_TXDW5_DATARATE,rate));
 uint16_t sum=0;for(unsigned j=0;j<32;j+=2)sum^=eu16(b+j);
 eup16(b+28,sum);
 /* No HWSEQ / cipher flags: preserve native sequence, CCMP IV and MIC. */
 memcpy(b+32,f,n);x->len=32+n;
 if(usb_submit(x)<0)return -1;
 s->queued=1;
 if(!(x->len%s->mps[pipe])){s->zlp[i].len=0;if(usb_submit(&s->zlp[i])<0)return -1;}
 return 0;
}
static void eu_report(struct rtwn_softc*s,const uint8_t*b,unsigned n){
 if(n<8 || (b[1]&R88E_RPTB1_MACID_M)!=R92C_MACID_BSS || (b[1]&R88E_RPTB1_BMC))return;
 s->reports++;s->retries+=(b[2]&R88E_RPTB2_RETRY_CNT_M)!=0;
 s->failures+=!(b[1]&R88E_RPTB1_PKT_OK);
 if(s->reports<16)return;
 static const unsigned order[]={0,1,2,4,5,3,6,7,8,9,10,11};
 int pos=0;while(pos<11&&order[pos]!=s->rate)pos++;
 if((s->retries+s->failures)*3>s->reports){
  while(--pos>=0)if(eu_rate(&s->wifi,order[pos])){s->rate=order[pos];break;}
  s->good=0;
 }else if((s->retries+s->failures)*10<s->reports&&++s->good>=2){
  while(++pos<12)if(eu_rate(&s->wifi,order[pos])){s->rate=order[pos];break;}
  s->good=0;
 }
 s->reports=s->retries=s->failures=0;
}
static int eu_receive(struct rtwn_softc*s,const uint8_t*b,unsigned n){
 if(n<24||n>16384)goto bad;
 unsigned reports=MS(eu32(b+12),R88E_RXDW3_RPT);
 if(reports){
  unsigned len=MS(eu32(b),R92C_RXDW0_PKTLEN);
  if(len>n-24)goto bad;
  if(reports==R88E_RXDW3_RPT_TX1)eu_report(s,b+24,len);
  return 0;
 }
 unsigned count=MS(eu32(b+8),R92C_RXDW2_PKTCNT);
 if(!count||count>128)goto bad;
 /* Validate every DMA span before delivering any frame from the aggregate. */
 for(unsigned pass=0;pass<2;pass++){
  unsigned off=0;
  for(unsigned i=0;i<count;i++){
   if(n-off<24)goto bad;
   uint32_t dw=eu32(b+off),dw3=eu32(b+off+12);
   unsigned len=MS(dw,R92C_RXDW0_PKTLEN),info=MS(dw,R92C_RXDW0_INFOSZ)*8;
   unsigned shift=MS(dw,R92C_RXDW0_SHIFT),span=24+info+shift+len;
   if(len<24||len>WIFI_FRAME_MAX||span>n-off||MS(dw3,R88E_RXDW3_RPT))goto bad;
   if(pass){
    if(dw&R92C_RXDW0_CRCERR)s->wifi.nd.rx_errors++;
    else{
     int rssi=-100;
     if((dw&R92C_RXDW0_PHYST)&&info>=sizeof(struct r88e_rx_phystat)){
      const uint8_t*phy=b+off+24;
      unsigned rate=MS(dw3,R92C_RXDW3_RATE);
      if(rate<=3){static const int offsets[]={20,14,10,-4,-16,-22,-38,-40};unsigned agc=phy[5];
       rssi=(agc&31)>27?-94:offsets[agc>>5]-2*(int)(agc&31);
      }else rssi=((phy[4]>>1)&127)-110;
     }
     /* Software CCMP validates protected payloads, independently of HW flags. */
     wifi_rx(&s->wifi,b+off+24+info+shift,len,rssi);
    }
   }
   unsigned advance=(span+127)&~127u;
   if(i+1<count){if(advance>n-off)goto bad;off+=advance;}
   else if(span<=n-off){off+=span;}
  }
 }
 return (int)count;
bad:s->wifi.nd.rx_errors++;return -1;
}
static int eu_poll(wifi_radio_t*r){
 struct rtwn_softc*s=r->hw;if(s->detached||!s->dev->present)return 0;
 usb_poll();
 if(!s->ok){if(r->state!=WIFI_DOWN)wifi_disconnect(r);return 0;}
 int total=0;
 for(unsigned i=0;i<2;i++){
  usb_xfer_t*x=&s->rx[i];
  if(x->status==USB_XFER_PENDING||x->status==USB_XFER_GONE)continue;
  if(x->status==USB_XFER_OK&&x->actual&&x->actual<=16384){int k=eu_receive(s,x->buf,x->actual);if(k>0)total+=k;}
  else if(x->status<0)r->nd.rx_errors++;
  x->len=16384;if(usb_submit(x)<0)x->status=USB_XFER_ERROR;
 }
 return total;
}
static const wifi_ops_t eu_ops={eu_poll,eu_send,eu_channel,eu_station};
typedef struct {usb_ep_desc_t in,out[3];unsigned ntx,nrx,seen;int iface,bad;} eu_eps_t;
static int eu_ep(const uint8_t*b,void*ctx){
 eu_eps_t*e=ctx;
 if(b[1]==USB_DT_INTERFACE&&b[0]>=sizeof(usb_iface_desc_t)){
  if(e->iface)return 1;
  const usb_iface_desc_t*i=(const usb_iface_desc_t*)b;
  e->iface=i->bInterfaceClass==0xff&&!i->bAlternateSetting&&i->bInterfaceNumber==0;
 }else if(b[1]==USB_DT_ENDPOINT&&b[0]>=sizeof(usb_ep_desc_t)&&e->iface){
  const usb_ep_desc_t*ep=(const usb_ep_desc_t*)b;
  if((ep->bmAttributes&3)!=USB_EP_BULK)return 0;
  unsigned addr=ep->bEndpointAddress,bit=(addr&15)+((addr&0x80)?16:0),mps=ep->wMaxPacketSize&0x7ff;
  if(!(addr&15)||(addr&0x70)||(ep->wMaxPacketSize&0xf800)||
     (mps!=8&&mps!=16&&mps!=32&&mps!=64&&mps!=512)||(e->seen&(1u<<bit))){e->bad=1;return 1;}
  e->seen|=1u<<bit;
  if(addr&0x80){if(e->nrx++)e->bad=1;else e->in=*ep;}
  else if(e->ntx<3)e->out[e->ntx++]=*ep;else e->bad=1;
 }
 return e->bad;
}
static int eu_config(usb_device_t*d,int cfg,eu_eps_t*e){
 memset(e,0,sizeof(*e));int rc=usb_walk_config(d,cfg,eu_ep,e);
 if(rc<0||!e->iface||e->nrx!=1||!e->ntx||e->bad)return -1;
 if(d->speed!=USB_SPEED_FULL&&d->speed!=USB_SPEED_HIGH)return -1;
 unsigned expected=d->speed==USB_SPEED_HIGH?512:64;
 if((e->in.wMaxPacketSize&0x7ff)>expected)return -1;
 for(unsigned i=0;i<e->ntx;i++)if((e->out[i].wMaxPacketSize&0x7ff)>expected)return -1;
 return 0;
}
static int eu_probe(usb_device_t*d){
 if(d->desc.idVendor!=0x0bda||d->desc.idProduct!=0x8179)return -1;
 for(int i=0;i<USB_MAX_CONFIGS;i++){eu_eps_t e;if(d->configs[i]&&!eu_config(d,i,&e))return i;}
 return -1;
}
static int eu_attach(usb_device_t*d){
 eu_eps_t e;if(eu_config(d,d->active_config,&e))return -1;
 struct rtwn_softc*s=kzalloc(sizeof(*s));if(!s)return -1;
 s->dev=d;s->ok=1;s->control=eu_control;s->delay=eu_delay;s->ntx=e.ntx;
 s->wifi.ops=&eu_ops;s->wifi.hw=s;
 s->wifi.nd.name="rtl8188eu";s->wifi.nd.model="Realtek RTL8188EUS (experimental 802.11b/g)";
 s->wifi.nd.is_usb=1;s->wifi.nd.irq=0xff;s->in=e.in.bEndpointAddress;
 if(usb_open_endpoint(d,&e.in))goto fail;
 for(unsigned i=0;i<e.ntx;i++){
  s->out[i]=e.out[i].bEndpointAddress;s->mps[i]=e.out[i].wMaxPacketSize&0x7ff;
  if(usb_open_endpoint(d,&e.out[i]))goto fail;
 }
 extern const uint8_t eu_fw_start[],eu_fw_end[];
 s->firmware=eu_fw_start;s->firmware_size=eu_fw_end-eu_fw_start;
 if(rtl_hw_init(s))goto fail;
 memcpy(s->wifi.nd.mac,s->sc_ic.ic_myaddr,6);
 eu_edca(s,20);
 rtwn_write_1(s,R88E_TX_RPT_CTRL,rtwn_read_1(s,R88E_TX_RPT_CTRL)|R88E_TX_RPT_CTRL_EN);
 if(!s->ok)goto fail;
 for(unsigned i=0;i<8;i++){
  s->tx[i].dev=s->zlp[i].dev=d;
  s->tx[i].buf=usb_dma_alloc(2112);s->zlp[i].buf=s->tx[i].buf;
  s->tx[i].status=s->zlp[i].status=USB_XFER_OK;
  if(!s->tx[i].buf)goto fail;
 }
 for(unsigned i=0;i<2;i++){
  s->rx[i].dev=d;s->rx[i].ep=s->in;s->rx[i].len=16384;s->rx[i].buf=usb_dma_alloc(16384);
  if(!s->rx[i].buf)goto fail;
 }
 for(unsigned i=0;i<2;i++){
  if(usb_submit(&s->rx[i])<0)goto fail;
  s->queued=1;
 }
 if(wifi_register(&s->wifi))goto fail;
 d->drvdata=s;
 klog("rtl8188eu: firmware and calibration ready; physical validation pending\n");return 0;
fail:
 /* No network interface is advertised on partial initialization. USB DMA
  * contexts/buffers have no cancellation/free API and are retained if queued. */
 if(s->ok){rtwn_write_2(s,R92C_CR,0);rtwn_write_1(s,R92C_TXPAUSE,0xff);}
 if(!s->queued)kfree(s);
 klog("rtl8188eu: initialization failed; no Wi-Fi interface registered\n");return -1;
}
static void eu_detach(usb_device_t*d){
 struct rtwn_softc*s=d->drvdata;if(!s)return;
 s->detached=1;wifi_unregister(&s->wifi);d->drvdata=NULL;
}
const usb_driver_t rtl8188eu_driver={"rtl8188eu",eu_probe,eu_attach,NULL,eu_detach};

__asm__(".pushsection .rodata\n.balign 16\n.global eu_fw_start\neu_fw_start:\n"
        ".incbin \"firmware/realtek/rtl8188eufw.bin\"\n"
        " .global eu_fw_end\neu_fw_end:\n"
        ".global rtl8188eu_notice_start\nrtl8188eu_notice_start:\n"
        ".incbin \"firmware/realtek/NOTICES\"\n"
        ".global rtl8188eu_notice_end\nrtl8188eu_notice_end:\n.popsection\n");
