/*
 * Copyright (c) 2008-2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2013-2014 Kevin Lo
 * Copyright (c) 2021 James Hastings
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
#define IEEE80211_T_DS 1
#define IEEE80211_T_OFDM 2
#include "mt7601_regs.h"
#define nitems(a) ((int)(sizeof(a)/sizeof((a)[0])))
#define DPRINTF(...) ((void)0)
#define DPRINTFN(...) ((void)0)

/* Firmware is shipped verbatim with its own redistribution licence. */
#ifndef MT7601_MODEL
__asm__(".pushsection .rodata\n.balign 16\n"
        ".global mt7601_fw_start\nmt7601_fw_start:\n"
        ".incbin \"firmware/mediatek/mt7601u.bin\"\n"
        ".global mt7601_fw_end\nmt7601_fw_end:\n"
        ".global mt7601_notices_start\nmt7601_notices_start:\n"
        ".incbin \"firmware/mediatek/NOTICES\"\n"
        ".global mt7601_notices_end\nmt7601_notices_end:\n.popsection\n");
extern const uint8_t mt7601_fw_start[],mt7601_fw_end[];
#endif
#define MTW_RX_SIZE 4096
#define MTW_TX_SIZE 4096
#define MTW_TX_SLOTS 8
struct mtw_softc {
    wifi_radio_t wifi;
    usb_device_t* sc_udev;
    int ok,queued,detached,sc_rf_calibrated,sc_bw_calibrated,sc_chan_group;
    uint16_t asic_ver,asic_rev,mac_ver,mac_rev,rf_rev;
    uint8_t ntxchains,nrxchains,rf_freq_offset,rfswitch,ext_2ghz_lna,ext_5ghz_lna;
    uint8_t calib_2ghz,calib_5ghz,txmixgain_2ghz,txmixgain_5ghz,lna[4];
    int8_t txpow1[54],txpow2[54],rssi_2ghz[3],rssi_5ghz[3],bbp_temp;
    uint32_t txpow20mhz[5],txpow40mhz_2ghz[5],txpow40mhz_5ghz[5],stats_ms;
    uint8_t in[2],out[6];uint16_t mps[6];
    usb_xfer_t cmd,rx[3],tx[MTW_TX_SLOTS],zlp[MTW_TX_SLOTS];
    uint8_t raw_frame[WIFI_FRAME_MAX];
    unsigned rate,good_windows;
};
static uint16_t mt_le16(const uint8_t* b) { return b[0]|(uint16_t)b[1]<<8; }
static uint32_t mt_le32(const uint8_t* b) { return mt_le16(b)|(uint32_t)mt_le16(b+2)<<16; }
static void mt_put16(uint8_t* b,uint16_t v) { b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8); }
static void mt_put32(uint8_t* b,uint32_t v) { mt_put16(b,(uint16_t)v);mt_put16(b+2,(uint16_t)(v>>16)); }
static int mt_ctl(struct mtw_softc* sc,int read,uint8_t req,uint16_t value,
                  uint16_t index,void* b,uint16_t n) {
    if(!sc->ok || sc->detached || !sc->sc_udev->present) return -1;
    int got=usb_control(sc->sc_udev,USB_TYPE_VENDOR|(read?USB_DIR_IN:0),req,value,index,b,n);
    if(got!=n) { sc->ok=0;return -1; } return 0;
}
static int mtw_read_region_1(struct mtw_softc* s,uint16_t reg,uint8_t* b,int n) {
    return mt_ctl(s,1,MTW_READ_REGION_1,0,reg,b,(uint16_t)n);
}
static int mtw_read(struct mtw_softc* s,uint16_t reg,uint32_t* val) {
    uint8_t b[4];*val=0xffffffffu;
    if(mtw_read_region_1(s,reg,b,4)) return -1;
    *val=mt_le32(b);return 0;
}
static int mtw_read_cfg(struct mtw_softc* s,uint16_t reg,uint32_t* val) {
    uint8_t b[4];*val=0xffffffffu;
    if(mt_ctl(s,1,MTW_READ_CFG,0,reg,b,4)) return -1;
    *val=mt_le32(b);return 0;
}
static int mtw_write(struct mtw_softc* s,uint16_t reg,uint32_t v) {
    if(mt_ctl(s,0,MTW_WRITE_2,(uint16_t)v,reg,0,0)) return -1;
    return mt_ctl(s,0,MTW_WRITE_2,(uint16_t)(v>>16),reg+2,0,0);
}
static int mtw_write_cfg(struct mtw_softc* s,uint16_t reg,uint32_t v) {
    uint8_t b[4];mt_put32(b,v);return mt_ctl(s,0,MTW_WRITE_CFG,0,reg,b,4);
}
static int mtw_set_region_4(struct mtw_softc* s,uint16_t reg,uint32_t v,int n) {
    for(int i=0;i<n;i++) if(mtw_write(s,reg+i*4,v)) return -1;
    return 0;
}
static int mtw_usb_dma_write(struct mtw_softc* s,uint32_t v) { return mtw_write(s,MTW_USB_DMA_CFG,v); }
/* Persistent transfer storage survives timeout: never free/overwrite a DMA
 * buffer the controller may still own. No rescan/driver callbacks here:
 * attach itself can run under usbcore's polling guard. */
static int mt_bulk(struct mtw_softc* s,uint32_t n) {
    if(!s->ok || s->cmd.status==USB_XFER_PENDING) return -1;
    s->cmd.len=n;
    if(usb_submit(&s->cmd)<0) { s->ok=0;return -1; }
    s->queued=1;
    uint32_t start=timer_ms();
    while(s->cmd.status==USB_XFER_PENDING && timer_ms()-start<5000) {
        usb_hc_t* hc=s->sc_udev->hc;
        if(hc && hc->ops->poll) hc->ops->poll(hc);
        usb_wait();
    }
    if(s->cmd.status!=USB_XFER_OK || s->cmd.actual!=n) { s->ok=0;return -1; }
    return 0;
}
static int mtw_mcu_cmd(struct mtw_softc* s,int command,const void* data,unsigned n) {
    if(n>32 || s->cmd.status==USB_XFER_PENDING) return -1;
    uint8_t* b=s->cmd.buf;memset(b,0,n+8);
    mt_put16(b,(uint16_t)n);
    /* Sequence zero means no MCU response, as in the reference driver. */
    mt_put16(b+2,MTW_TXD_CMD|MTW_TXD_MCU|((command&0x7f)<<MTW_TXD_CMD_SHIFT));
    memcpy(b+4,data,n);return mt_bulk(s,n+8);
}
static int mtw_mcu_calibrate(struct mtw_softc* s,int func,uint32_t value) {
    uint8_t b[8];mt_put32(b,func);mt_put32(b+4,value);return mtw_mcu_cmd(s,31,b,8);
}
static int mtw_mcu_radio(struct mtw_softc* s,int func,uint32_t value) {
    uint8_t b[16]={0};mt_put32(b,func);mt_put32(b+4,value);return mtw_mcu_cmd(s,20,b,16);
}

static const struct {
	uint32_t	reg;
	uint32_t	val;
} mt7601_def_mac[] = {
	MT7601_DEF_MAC
};
static const struct {
	uint8_t		reg;
	uint8_t		val;
} mt7601_def_bbp[] = {
	MT7601_DEF_BBP
};
static const struct {
	unsigned		chan;
	uint8_t		r17, r18, r19, r20;
} mt7601_rf_chan[] = {
	MT7601_RF_CHAN
};
static const struct {
	uint8_t		reg;
	uint8_t		val;
} mt7601_rf_bank0[] = {
	MT7601_BANK0_RF
},mt7601_rf_bank4[] = {
	MT7601_BANK4_RF
},mt7601_rf_bank5[] = {
	MT7601_BANK5_RF
};

static int mtw_efuse_read_2(struct mtw_softc *sc, uint16_t addr, uint16_t *val);
static int mtw_rf_read(struct mtw_softc *sc, uint8_t bank, uint8_t reg, uint8_t *val);
static int mtw_rf_write(struct mtw_softc *sc, uint8_t bank, uint8_t reg, uint8_t val);
static int mtw_bbp_read(struct mtw_softc *sc, uint8_t reg, uint8_t *val);
static int mtw_bbp_write(struct mtw_softc *sc, uint8_t reg, uint8_t val);
static __inline uint32_t b4inc(uint32_t b32, int8_t delta);
static void mtw_get_txpower(struct mtw_softc *sc);
static int mtw_read_eeprom(struct mtw_softc *sc);
static void mtw_select_chan_group(struct mtw_softc *sc, int group);
static void mt7601_set_agc(struct mtw_softc *sc, uint8_t agc);
static void mt7601_set_chan(struct mtw_softc *sc, unsigned chan);
static void mtw_enable_mrr(struct mtw_softc *sc);
static void mtw_set_txrts(struct mtw_softc *sc);
static void mtw_abort_tsf_sync(struct mtw_softc *sc);
static void mtw_set_leds(struct mtw_softc *sc, uint16_t which);
static void mtw_set_bssid(struct mtw_softc *sc, const uint8_t *bssid);
static void mtw_set_macaddr(struct mtw_softc *sc, const uint8_t *addr);
static int mt7601_bbp_init(struct mtw_softc *sc);
static int mt7601_rf_init(struct mtw_softc *sc);
static int mt7601_rf_setup(struct mtw_softc *sc);
static int mt7601_rf_temperature(struct mtw_softc *sc, int8_t *val);
static int mt7601_r49_read(struct mtw_softc *sc, uint8_t flag, int8_t *val);
static int mt7601_rxdc_cal(struct mtw_softc *sc);
static int mtw_wlan_enable(struct mtw_softc *sc, int enable);
static int mtw_txrx_enable(struct mtw_softc *sc);
static int
mtw_efuse_read_2(struct mtw_softc *sc, uint16_t addr, uint16_t *val)
{
    *val=0;
	uint32_t tmp;
	uint16_t reg;
	int error, ntries;

	if ((error = mtw_read(sc, MTW_EFUSE_CTRL, &tmp)) != 0)
		return error;

	addr *= 2;
	/*
	 * Read one 16-byte block into registers EFUSE_DATA[0-3]:
	 * DATA0: 3 2 1 0
	 * DATA1: 7 6 5 4
	 * DATA2: B A 9 8
	 * DATA3: F E D C
	 */
	tmp &= ~(MTW_EFSROM_MODE_MASK | MTW_EFSROM_AIN_MASK);
	tmp |= (addr & ~0xf) << MTW_EFSROM_AIN_SHIFT | MTW_EFSROM_KICK;
	mtw_write(sc, MTW_EFUSE_CTRL, tmp);
	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_EFUSE_CTRL, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_EFSROM_KICK))
			break;
		usb_delay_ms(1);
	}
	if (ntries == 100)
		return -1;

	if ((tmp & MTW_EFUSE_AOUT_MASK) == MTW_EFUSE_AOUT_MASK) {
		*val = 0xffff;	/* address not found */
		return 0;
	}
	/* determine to which 32-bit register our 16-bit word belongs */
	reg = MTW_EFUSE_DATA0 + (addr & 0xc);
	if ((error = mtw_read(sc, reg, &tmp)) != 0)
		return error;

	*val = (addr & 2) ? tmp >> 16 : tmp & 0xffff;
	return 0;
}

static int
mtw_rf_read(struct mtw_softc *sc, uint8_t bank, uint8_t reg, uint8_t *val)
{
    *val=0;
	uint32_t tmp;
	int error, ntries, shift;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_RF_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_RF_CSR_KICK))
			break;
	}
	if (ntries == 100)
		return -1;

	if (sc->mac_ver == 0x7601)
		shift = MT7601_BANK_SHIFT;
	else
		shift = MT7610_BANK_SHIFT;

	tmp = MTW_RF_CSR_KICK | (bank & 0xf) << shift | reg << 8;
	if ((error = mtw_write(sc, MTW_RF_CSR, tmp)) != 0)
		return error;

	for (ntries = 0; ntries < 100; ntries++) {
		if ((error = mtw_read(sc, MTW_RF_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_RF_CSR_KICK))
			break;
	}
	if (ntries == 100)
		return -1;

	*val = tmp & 0xff;
	return 0;
}

static int
mtw_rf_write(struct mtw_softc *sc, uint8_t bank, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int error, ntries, shift;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_RF_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_RF_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return -1;

	if (sc->mac_ver == 0x7601)
		shift = MT7601_BANK_SHIFT;
	else
		shift = MT7610_BANK_SHIFT;

	tmp = MTW_RF_CSR_WRITE | MTW_RF_CSR_KICK | (bank & 0xf) << shift |
	    reg << 8 | val;
	return mtw_write(sc, MTW_RF_CSR, tmp);
}

static int
mtw_bbp_read(struct mtw_softc *sc, uint8_t reg, uint8_t *val)
{
    *val=0;
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_BBP_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return -1;

	tmp = MTW_BBP_CSR_READ | MTW_BBP_CSR_KICK | reg << MTW_BBP_ADDR_SHIFT;
	if ((error = mtw_write(sc, MTW_BBP_CSR, tmp)) != 0)
		return error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_BBP_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return -1;

	*val = tmp & 0xff;
	return 0;
}

static int
mtw_bbp_write(struct mtw_softc *sc, uint8_t reg, uint8_t val)
{
	uint32_t tmp;
	int ntries, error;

	for (ntries = 0; ntries < 10; ntries++) {
		if ((error = mtw_read(sc, MTW_BBP_CSR, &tmp)) != 0)
			return error;
		if (!(tmp & MTW_BBP_CSR_KICK))
			break;
	}
	if (ntries == 10)
		return -1;

	tmp = MTW_BBP_CSR_KICK | reg << MTW_BBP_ADDR_SHIFT | val;
	return mtw_write(sc, MTW_BBP_CSR, tmp);
}

static __inline uint32_t
b4inc(uint32_t b32, int8_t delta)
{
	int8_t i, b4;

	for (i = 0; i < 8; i++) {
		b4 = b32 & 0xf;
		b4 += delta;
		if (b4 < 0)
			b4 = 0;
		else if (b4 > 0xf)
			b4 = 0xf;
		b32 = b32 >> 4 | (uint32_t)(uint8_t)b4 << 28;
	}
	return b32;
}

static void
mtw_get_txpower(struct mtw_softc *sc)
{
	uint16_t val;
	int i;

	/* Read power settings for 2GHz channels. */
	for (i = 0; i < 14; i += 2) {
		mtw_efuse_read_2(sc, MTW_EEPROM_PWR2GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 0] = (int8_t)(val & 0xff);
		sc->txpow1[i + 1] = (int8_t)(val >> 8);
		mtw_efuse_read_2(sc, MTW_EEPROM_PWR2GHZ_BASE2 + i / 2, &val);
		sc->txpow2[i + 0] = (int8_t)(val & 0xff);
		sc->txpow2[i + 1] = (int8_t)(val >> 8);
	}
	/* Fix broken Tx power entries. */
	for (i = 0; i < 14; i++) {
		if (sc->txpow1[i] < 0 || sc->txpow1[i] > 27)
			sc->txpow1[i] = 5;
		if (sc->txpow2[i] < 0 || sc->txpow2[i] > 27)
			sc->txpow2[i] = 5;
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    mt7601_rf_chan[i].chan, sc->txpow1[i], sc->txpow2[i]));
	}
#if 0
	/* Read power settings for 5GHz channels. */
	for (i = 0; i < 40; i += 2) {
		mtw_efuse_read_2(sc, MTW_EEPROM_PWR5GHZ_BASE1 + i / 2, &val);
		sc->txpow1[i + 14] = (int8_t)(val & 0xff);
		sc->txpow1[i + 15] = (int8_t)(val >> 8);

		mtw_efuse_read_2(sc, MTW_EEPROM_PWR5GHZ_BASE2 + i / 2, &val);
		sc->txpow2[i + 14] = (int8_t)(val & 0xff);
		sc->txpow2[i + 15] = (int8_t)(val >> 8);
	}
	/* Fix broken Tx power entries. */
	for (i = 0; i < 40; i++ ) {
		if (sc->mac_ver != 0x5592) {
			if (sc->txpow1[14 + i] < -7 || sc->txpow1[14 + i] > 15)
				sc->txpow1[14 + i] = 5;
			if (sc->txpow2[14 + i] < -7 || sc->txpow2[14 + i] > 15)
				sc->txpow2[14 + i] = 5;
		}
		DPRINTF(("chan %d: power1=%d, power2=%d\n",
		    mt7601_rf_chan[14 + i].chan, sc->txpow1[14 + i],
		    sc->txpow2[14 + i]));
	}
#endif
}

static int
mtw_read_eeprom(struct mtw_softc *sc)
{
	int8_t delta_2ghz, delta_5ghz;
	uint16_t val;
	int ridx, ant;



	/* read RF information */
	mtw_efuse_read_2(sc, MTW_EEPROM_CHIPID, &val);
	sc->rf_rev = val;
	mtw_efuse_read_2(sc, MTW_EEPROM_ANTENNA, &val);
	sc->ntxchains = (val >> 4) & 0xf;
	sc->nrxchains = val & 0xf;
	DPRINTF(("EEPROM RF rev=0x%02x chains=%dT%dR\n",
	    sc->rf_rev, sc->ntxchains, sc->nrxchains));

	/* read ROM version */
	mtw_efuse_read_2(sc, MTW_EEPROM_VERSION, &val);
	DPRINTF(("EEPROM rev=%d, FAE=%d\n", val & 0xff, val >> 8));

	/* read MAC address */
	mtw_efuse_read_2(sc, MTW_EEPROM_MAC01, &val);
	sc->wifi.nd.mac[0] = val & 0xff;
	sc->wifi.nd.mac[1] = val >> 8;
	mtw_efuse_read_2(sc, MTW_EEPROM_MAC23, &val);
	sc->wifi.nd.mac[2] = val & 0xff;
	sc->wifi.nd.mac[3] = val >> 8;
	mtw_efuse_read_2(sc, MTW_EEPROM_MAC45, &val);
	sc->wifi.nd.mac[4] = val & 0xff;
	sc->wifi.nd.mac[5] = val >> 8;
#if 0
	klog("eFUSE ROM\n00: ");
	for (int i = 0; i < 256; i++) {
		if (((i % 8) == 0) && i > 0)
			klog("\n%02x: ", i);
		mtw_efuse_read_2(sc, i, &val);
		klog(" %04x", val);
	}
	klog("\n");
#endif
	/* check if RF supports automatic Tx access gain control */
	mtw_efuse_read_2(sc, MTW_EEPROM_CONFIG, &val);
	DPRINTF(("EEPROM CFG 0x%04x\n", val));
	if ((val & 0xff) != 0xff) {
		sc->ext_5ghz_lna = (val >> 3) & 1;
		sc->ext_2ghz_lna = (val >> 2) & 1;
		/* check if RF supports automatic Tx access gain control */
		sc->calib_2ghz = sc->calib_5ghz = (val >> 1) & 1;
		/* check if we have a hardware radio switch */
		sc->rfswitch = val & 1;
	}

	/* read RF frequency offset from EEPROM */
	mtw_efuse_read_2(sc, MTW_EEPROM_FREQ_OFFSET, &val);
	if ((val & 0xff) != 0xff)
		sc->rf_freq_offset = val;
	else
		sc->rf_freq_offset = 0;
	DPRINTF(("frequency offset 0x%x\n", sc->rf_freq_offset));

	/* Read Tx power settings. */
	mtw_get_txpower(sc);

	/* read Tx power compensation for each Tx rate */
	mtw_efuse_read_2(sc, MTW_EEPROM_DELTAPWR, &val);
	delta_2ghz = delta_5ghz = 0;
	if ((val & 0xff) != 0xff && (val & 0x80)) {
		delta_2ghz = val & 0xf;
		if (!(val & 0x40))	/* negative number */
			delta_2ghz = -delta_2ghz;
	}
	val >>= 8;
	if ((val & 0xff) != 0xff && (val & 0x80)) {
		delta_5ghz = val & 0xf;
		if (!(val & 0x40))	/* negative number */
			delta_5ghz = -delta_5ghz;
	}
	DPRINTF(("power compensation=%d (2GHz), %d (5GHz)\n",
	    delta_2ghz, delta_5ghz));

	for (ridx = 0; ridx < 5; ridx++) {
		uint32_t reg;

		mtw_efuse_read_2(sc, MTW_EEPROM_RPWR + ridx * 2, &val);
		reg = val;
		mtw_efuse_read_2(sc, MTW_EEPROM_RPWR + ridx * 2 + 1, &val);
		reg |= (uint32_t)val << 16;

		sc->txpow20mhz[ridx] = reg;
		sc->txpow40mhz_2ghz[ridx] = b4inc(reg, delta_2ghz);
		sc->txpow40mhz_5ghz[ridx] = b4inc(reg, delta_5ghz);

		DPRINTF(("ridx %d: power 20MHz=0x%08x, 40MHz/2GHz=0x%08x, "
		    "40MHz/5GHz=0x%08x\n", ridx, sc->txpow20mhz[ridx],
		    sc->txpow40mhz_2ghz[ridx], sc->txpow40mhz_5ghz[ridx]));
	}

	/* read RSSI offsets and LNA gains from EEPROM */
	val = 0;
	mtw_efuse_read_2(sc, MTW_EEPROM_RSSI1_2GHZ, &val);
	sc->rssi_2ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_2ghz[1] = val >> 8;	/* Ant B */
	mtw_efuse_read_2(sc, MTW_EEPROM_RSSI2_2GHZ, &val);
	/*
	 * On RT3070 chips (limited to 2 Rx chains), this ROM
	 * field contains the Tx mixer gain for the 2GHz band.
	 */
	if ((val & 0xff) != 0xff)
		sc->txmixgain_2ghz = val & 0x7;
	DPRINTF(("tx mixer gain=%u (2GHz)\n", sc->txmixgain_2ghz));
	sc->lna[2] = val >> 8;		/* channel group 2 */
	mtw_efuse_read_2(sc, MTW_EEPROM_RSSI1_5GHZ, &val);
	sc->rssi_5ghz[0] = val & 0xff;	/* Ant A */
	sc->rssi_5ghz[1] = val >> 8;	/* Ant B */
	mtw_efuse_read_2(sc, MTW_EEPROM_RSSI2_5GHZ, &val);
	sc->rssi_5ghz[2] = val & 0xff;	/* Ant C */

	sc->lna[3] = val >> 8;		/* channel group 3 */

	mtw_efuse_read_2(sc, MTW_EEPROM_LNA, &val);
	sc->lna[0] = val & 0xff;	/* channel group 0 */
	sc->lna[1] = val >> 8;		/* channel group 1 */
	DPRINTF(("LNA0 0x%x\n", sc->lna[0]));

	/* fix broken 5GHz LNA entries */
	if (sc->lna[2] == 0 || sc->lna[2] == 0xff) {
		DPRINTF(("invalid LNA for channel group %d\n", 2));
		sc->lna[2] = sc->lna[1];
	}
	if (sc->lna[3] == 0 || sc->lna[3] == 0xff) {
		DPRINTF(("invalid LNA for channel group %d\n", 3));
		sc->lna[3] = sc->lna[1];
	}

	/* fix broken RSSI offset entries */
	for (ant = 0; ant < 3; ant++) {
		if (sc->rssi_2ghz[ant] < -10 || sc->rssi_2ghz[ant] > 10) {
			DPRINTF(("invalid RSSI%d offset: %d (2GHz)\n",
			    ant + 1, sc->rssi_2ghz[ant]));
			sc->rssi_2ghz[ant] = 0;
		}
		if (sc->rssi_5ghz[ant] < -10 || sc->rssi_5ghz[ant] > 10) {
			DPRINTF(("invalid RSSI%d offset: %d (5GHz)\n",
			    ant + 1, sc->rssi_5ghz[ant]));
			sc->rssi_5ghz[ant] = 0;
		}
	}
	return 0;
}

static void
mtw_select_chan_group(struct mtw_softc *sc, int group)
{
	uint32_t tmp;
	uint8_t bbp;

	/* Tx band 20MHz 2G */
	mtw_read(sc, MTW_TX_BAND_CFG, &tmp);
	tmp &= ~(MTW_TX_BAND_SEL_2G | MTW_TX_BAND_SEL_5G |
	    MTW_TX_BAND_UPPER_40M);
	tmp |= (group == 0) ? MTW_TX_BAND_SEL_2G : MTW_TX_BAND_SEL_5G;
	mtw_write(sc, MTW_TX_BAND_CFG, tmp);

	/* select 20 MHz bandwidth */
	mtw_bbp_read(sc, 4, &bbp);
	bbp &= ~0x18;
	bbp |= 0x40;
	mtw_bbp_write(sc, 4, bbp);

	/* calibrate BBP */
	mtw_bbp_write(sc, 69, 0x12);
	mtw_bbp_write(sc, 91, 0x07);
	mtw_bbp_write(sc, 195, 0x23);
	mtw_bbp_write(sc, 196, 0x17);
	mtw_bbp_write(sc, 195, 0x24);
	mtw_bbp_write(sc, 196, 0x06);
	mtw_bbp_write(sc, 195, 0x81);
	mtw_bbp_write(sc, 196, 0x12);
	mtw_bbp_write(sc, 195, 0x83);
	mtw_bbp_write(sc, 196, 0x17);
	mtw_rf_write(sc, 5, 8, 0x00);
	mtw_mcu_calibrate(sc, 0x6, 0x10001);

	/* set initial AGC value */
	mt7601_set_agc(sc, 0x14);
}

static void
mt7601_set_agc(struct mtw_softc *sc, uint8_t agc)
{
	uint8_t bbp;

	mtw_bbp_write(sc, 66, agc);
	mtw_bbp_write(sc, 195, 0x87);
	bbp = (agc & 0xf0) | 0x08;
	mtw_bbp_write(sc, 196, bbp);
}

static void
mt7601_set_chan(struct mtw_softc *sc, unsigned chan)
{
	uint32_t tmp;
	uint8_t bbp, rf, txpow1;
	int i;

	/* find the settings for this channel */
	for (i = 0; i < (int)nitems(mt7601_rf_chan) && mt7601_rf_chan[i].chan != chan; i++)
		;

	if (i == (int)nitems(mt7601_rf_chan)) { sc->ok=0; return; }
	mtw_rf_write(sc, 0, 17, mt7601_rf_chan[i].r17);
	mtw_rf_write(sc, 0, 18, mt7601_rf_chan[i].r18);
	mtw_rf_write(sc, 0, 19, mt7601_rf_chan[i].r19);
	mtw_rf_write(sc, 0, 20, mt7601_rf_chan[i].r20);

	/* use Tx power values from EEPROM */
	txpow1 = sc->txpow1[i];

	/* Tx automatic level control */
	mtw_read(sc, MTW_TX_ALC_CFG0, &tmp);
	tmp &= ~0x3f3f;
	tmp |= (txpow1 & 0x3f);
	mtw_write(sc, MTW_TX_ALC_CFG0, tmp);

	/* LNA */
	mtw_bbp_write(sc, 62, 0x37 - sc->lna[0]);
	mtw_bbp_write(sc, 63, 0x37 - sc->lna[0]);
	mtw_bbp_write(sc, 64, 0x37 - sc->lna[0]);

	/* VCO calibration */
	mtw_rf_write(sc, 0, 4, 0x0a);
	mtw_rf_write(sc, 0, 5, 0x20);
	mtw_rf_read(sc, 0, 4, &rf);
	mtw_rf_write(sc, 0, 4, rf | 0x80);

	/* select 20 MHz bandwidth */
	mtw_bbp_read(sc, 4, &bbp);
	bbp &= ~0x18;
	bbp |= 0x40;
	mtw_bbp_write(sc, 4, bbp);
	mtw_bbp_write(sc, 178, 0xff);
}

static void
mtw_enable_mrr(struct mtw_softc *sc)
{
#define CCK(mcs)	(mcs)
#define OFDM(mcs)	(1u << 3 | (mcs))
	mtw_write(sc, MTW_LG_FBK_CFG0,
	    OFDM(6) << 28 |	/* 54->48 */
	    OFDM(5) << 24 |	/* 48->36 */
	    OFDM(4) << 20 |	/* 36->24 */
	    OFDM(3) << 16 |	/* 24->18 */
	    OFDM(2) << 12 |	/* 18->12 */
	    OFDM(1) <<  8 |	/* 12-> 9 */
	    OFDM(0) <<  4 |	/*  9-> 6 */
	    OFDM(0));		/*  6-> 6 */

	mtw_write(sc, MTW_LG_FBK_CFG1,
	    CCK(2) << 12 |	/* 11->5.5 */
	    CCK(1) <<  8 |	/* 5.5-> 2 */
	    CCK(0) <<  4 |	/*   2-> 1 */
	    CCK(0));		/*   1-> 1 */
#undef OFDM
#undef CCK
}

static void
mtw_set_txrts(struct mtw_softc *sc)
{
	uint32_t tmp;

	/* set RTS threshold */
	mtw_read(sc, MTW_TX_RTS_CFG, &tmp);
	tmp &= ~0xffff00;
	tmp |= 0x1000 << MTW_RTS_THRES_SHIFT;
	mtw_write(sc, MTW_TX_RTS_CFG, tmp);
}

static void
mtw_abort_tsf_sync(struct mtw_softc *sc)
{
	uint32_t tmp;

	mtw_read(sc, MTW_BCN_TIME_CFG, &tmp);
	tmp &= ~(MTW_BCN_TX_EN | MTW_TSF_TIMER_EN | MTW_TBTT_TIMER_EN);
	mtw_write(sc, MTW_BCN_TIME_CFG, tmp);
}

static void
mtw_set_leds(struct mtw_softc *sc, uint16_t which)
{
	struct mtw_mcu_cmd_8 cmd;

	cmd.func = 1;
	cmd.val = which;
	mtw_mcu_cmd(sc, 16, &cmd, sizeof(struct mtw_mcu_cmd_8));
}

static void
mtw_set_bssid(struct mtw_softc *sc, const uint8_t *bssid)
{
	mtw_write(sc, MTW_MAC_BSSID_DW0,
	    bssid[0] | bssid[1] << 8 | bssid[2] << 16 | (uint32_t)bssid[3] << 24);
	mtw_write(sc, MTW_MAC_BSSID_DW1,
	    bssid[4] | bssid[5] << 8);
}

static void
mtw_set_macaddr(struct mtw_softc *sc, const uint8_t *addr)
{
	mtw_write(sc, MTW_MAC_ADDR_DW0,
	    addr[0] | addr[1] << 8 | addr[2] << 16 | (uint32_t)addr[3] << 24);
	mtw_write(sc, MTW_MAC_ADDR_DW1,
	    addr[4] | addr[5] << 8 | 0xff << 16);
}

static int
mt7601_bbp_init(struct mtw_softc *sc)
{
	uint8_t bbp;
	int i, error, ntries;

	/* wait for BBP to wake up */
	for (ntries = 0; ntries < 20; ntries++) {
		if ((error = mtw_bbp_read(sc, 0, &bbp)) != 0)
			return error;
		if (bbp != 0 && bbp != 0xff)
			break;
	}

	if (ntries == 20)
		return -1;

	mtw_bbp_read(sc, 3, &bbp);
	mtw_bbp_write(sc, 3, 0);
	mtw_bbp_read(sc, 105, &bbp);
	mtw_bbp_write(sc, 105, 0);

	/* initialize BBP registers to default values */
	for (i = 0; i < nitems(mt7601_def_bbp); i++) {
		if ((error = mtw_bbp_write(sc, mt7601_def_bbp[i].reg,
		    mt7601_def_bbp[i].val)) != 0)
			return error;
	}

	sc->sc_bw_calibrated = 0;

	return 0;
}

static int
mt7601_rf_init(struct mtw_softc *sc)
{
	int i, error;

	/* RF bank 0 */
	for (i = 0; i < nitems(mt7601_rf_bank0); i++) {
		error = mtw_rf_write(sc, 0, mt7601_rf_bank0[i].reg,
		    mt7601_rf_bank0[i].val);
		if (error != 0)
			return error;
	}
	/* RF bank 4 */
	for (i = 0; i < nitems(mt7601_rf_bank4); i++) {
		error = mtw_rf_write(sc, 4, mt7601_rf_bank4[i].reg,
		    mt7601_rf_bank4[i].val);
		if (error != 0)
			return error;
	}
	/* RF bank 5 */
	for (i = 0; i < nitems(mt7601_rf_bank5); i++) {
		error = mtw_rf_write(sc, 5, mt7601_rf_bank5[i].reg,
		    mt7601_rf_bank5[i].val);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
mt7601_rf_setup(struct mtw_softc *sc)
{
	uint32_t tmp;
	uint8_t rf;
	int error;

	if (sc->sc_rf_calibrated)
		return 0;

	/* init RF registers */
	if ((error = mt7601_rf_init(sc)) != 0)
		return error;

	/* init frequency offset */
	mtw_rf_write(sc, 0, 12, sc->rf_freq_offset);
	mtw_rf_read(sc, 0, 12, &rf);

	/* read temperature */
	mt7601_rf_temperature(sc, (int8_t*)&rf);
	sc->bbp_temp = rf;
	DPRINTF(("BBP temp 0x%x ", rf));

	mtw_rf_read(sc, 0, 7, &rf);
	if ((error = mtw_mcu_calibrate(sc, 0x1, 0)) != 0)
		return error;
	usb_delay_ms(100);
	mtw_rf_read(sc, 0, 7, &rf);

	/* Calibrate VCO RF 0/4 */
	mtw_rf_write(sc, 0, 4, 0x0a);
	mtw_rf_write(sc, 0, 4, 0x20);
	mtw_rf_read(sc, 0, 4, &rf);
	mtw_rf_write(sc, 0, 4, rf | 0x80);

	if ((error = mtw_mcu_calibrate(sc, 0x9, 0)) != 0)
		return error;
	if ((error = mt7601_rxdc_cal(sc)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x6, 1)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x6, 0)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x4, 0)) != 0)
		return error;
	if ((error = mtw_mcu_calibrate(sc, 0x5, 0)) != 0)
		return error;

	mtw_read(sc, MTW_LDO_CFG0, &tmp);
	tmp &= ~(1 << 4);
	tmp |= (1 << 2);
	mtw_write(sc, MTW_LDO_CFG0, tmp);

	if ((error = mtw_mcu_calibrate(sc, 0x8, 0)) != 0)
		return error;
	if ((error = mt7601_rxdc_cal(sc)) != 0)
		return error;

	sc->sc_rf_calibrated = 1;
	return 0;
}

static int
mt7601_rf_temperature(struct mtw_softc *sc, int8_t *val)
{
	uint32_t rfb, rfs;
	uint8_t bbp;
	int ntries;

	mtw_read(sc, MTW_RF_BYPASS0, &rfb);
	mtw_read(sc, MTW_RF_SETTING0, &rfs);
	mtw_write(sc, MTW_RF_BYPASS0, 0);
	mtw_write(sc, MTW_RF_SETTING0, 0x10);
	mtw_write(sc, MTW_RF_BYPASS0, 0x10);

	mtw_bbp_read(sc, 47, &bbp);
	bbp &= ~0x7f;
	bbp |= 0x10;
	mtw_bbp_write(sc, 47, bbp);

	mtw_bbp_write(sc, 22, 0x40);

	for (ntries = 0; ntries < 10; ntries++) {
		mtw_bbp_read(sc, 47, &bbp);
		if ((bbp & 0x10) == 0)
			break;
	}
	if (ntries == 10)
		return -1;

	mt7601_r49_read(sc, MT7601_R47_TEMP, val);

	mtw_bbp_write(sc, 22, 0);

	mtw_bbp_read(sc, 21, &bbp);
	bbp |= 0x02;
	mtw_bbp_write(sc, 21, bbp);
	bbp &= ~0x02;
	mtw_bbp_write(sc, 21, bbp);

	mtw_write(sc, MTW_RF_BYPASS0, 0);
	mtw_write(sc, MTW_RF_SETTING0, rfs);
	mtw_write(sc, MTW_RF_BYPASS0, rfb);
	return 0;
}

static int
mt7601_r49_read(struct mtw_softc *sc, uint8_t flag, int8_t *val)
{
	uint8_t bbp;

	mtw_bbp_read(sc, 47, &bbp);
	bbp = 0x90;
	mtw_bbp_write(sc, 47, bbp);
	bbp &= ~0x0f;
	bbp |= flag;
	mtw_bbp_write(sc, 47, bbp);
	return mtw_bbp_read(sc, 49, (uint8_t*)val);
}

static int
mt7601_rxdc_cal(struct mtw_softc *sc)
{
	uint32_t tmp;
	uint8_t bbp;
	int ntries;

	mtw_read(sc, MTW_MAC_SYS_CTRL, &tmp);
	mtw_write(sc, MTW_MAC_SYS_CTRL, MTW_MAC_RX_EN);
	mtw_bbp_write(sc, 158, 0x8d);
	mtw_bbp_write(sc, 159, 0xfc);
	mtw_bbp_write(sc, 158, 0x8c);
	mtw_bbp_write(sc, 159, 0x4c);

	for (ntries = 0; ntries < 20; ntries++) {
		usb_delay_ms(1);
		mtw_bbp_write(sc, 158, 0x8c);
		mtw_bbp_read(sc, 159, &bbp);
		if (bbp == 0x0c)
			break;
	}

	if (ntries == 20)
		return -1;

	mtw_write(sc, MTW_MAC_SYS_CTRL, 0);
	mtw_bbp_write(sc, 158, 0x8d);
	mtw_bbp_write(sc, 159, 0xe0);
	mtw_write(sc, MTW_MAC_SYS_CTRL, tmp);
	return 0;
}

static int
mtw_wlan_enable(struct mtw_softc *sc, int enable)
{
	uint32_t tmp;
	int error = 0;

	if (enable) {
		mtw_read(sc, MTW_WLAN_CTRL, &tmp);
		if (sc->asic_ver == 0x7612)
			tmp &= ~0xfffff000;

		tmp &= ~MTW_WLAN_CLK_EN;
		tmp |= MTW_WLAN_EN;
		mtw_write(sc, MTW_WLAN_CTRL, tmp);
		usb_delay_ms(2);

		tmp |= MTW_WLAN_CLK_EN;
		if (sc->asic_ver == 0x7612) {
			tmp |= (MTW_WLAN_RESET | MTW_WLAN_RESET_RF);
		}
		mtw_write(sc, MTW_WLAN_CTRL, tmp);
		usb_delay_ms(2);

		mtw_read(sc, MTW_OSC_CTRL, &tmp);
		tmp |= MTW_OSC_EN;
		mtw_write(sc, MTW_OSC_CTRL, tmp);
		tmp |= MTW_OSC_CAL_REQ;
		mtw_write(sc, MTW_OSC_CTRL, tmp);
	} else {
		mtw_read(sc, MTW_WLAN_CTRL, &tmp);
		tmp &= ~(MTW_WLAN_CLK_EN | MTW_WLAN_EN);
		mtw_write(sc, MTW_WLAN_CTRL, tmp);

		mtw_read(sc, MTW_OSC_CTRL, &tmp);
		tmp &= ~MTW_OSC_EN;
		mtw_write(sc, MTW_OSC_CTRL, tmp);
	}
	return error;
}

static int
mtw_txrx_enable(struct mtw_softc *sc)
{
	uint32_t tmp;
	int error, ntries;

	mtw_write(sc, MTW_MAC_SYS_CTRL, MTW_MAC_TX_EN);
	for (ntries = 0; ntries < 200; ntries++) {
		if ((error = mtw_read(sc, MTW_WPDMA_GLO_CFG, &tmp)) != 0)
			return error;
		if ((tmp & (MTW_TX_DMA_BUSY | MTW_RX_DMA_BUSY)) == 0)
			break;
		usb_delay_ms(1);
	}
	if (ntries == 200)
		return -1;

	usb_delay_ms(1);

	tmp |= MTW_RX_DMA_EN | MTW_TX_DMA_EN | MTW_TX_WB_DDONE;
	mtw_write(sc, MTW_WPDMA_GLO_CFG, tmp);

	/* enable Rx bulk aggregation (set timeout and limit) */
	tmp = MTW_USB_TX_EN | MTW_USB_RX_EN | MTW_USB_RX_AGG_EN |
	   MTW_USB_RX_AGG_TO(128) | MTW_USB_RX_AGG_LMT(2);
	mtw_write(sc, MTW_USB_DMA_CFG, tmp);

	/* set Rx filter */
	tmp = MTW_DROP_CRC_ERR | MTW_DROP_PHY_ERR;
	if (1) {
		tmp |= MTW_DROP_UC_NOME | MTW_DROP_DUPL |
		    MTW_DROP_CTS | MTW_DROP_BA | MTW_DROP_ACK |
		    MTW_DROP_VER_ERR | MTW_DROP_CTRL_RSV |
		    MTW_DROP_CFACK | MTW_DROP_CFEND;
		if (1)
			tmp |= MTW_DROP_RTS | MTW_DROP_PSPOLL;
	}
	mtw_write(sc, MTW_RX_FILTR_CFG, tmp);

	mtw_write(sc, MTW_MAC_SYS_CTRL,
	    MTW_MAC_RX_EN | MTW_MAC_TX_EN);
	return 0;
}

static int mt_load_firmware(struct mtw_softc* sc) {
    uint32_t ready;
    if(mtw_read_cfg(sc,MTW_MCU_DMA_ADDR,&ready)) return -1;
    if(ready==MTW_MCU_READY) return 0;
    uint32_t size=(uint32_t)(mt7601_fw_end-mt7601_fw_start);
    const uint8_t* fw=mt7601_fw_start;
    if(size<96) return -1;
    uint32_t ilm=mt_le32(fw),dlm=mt_le32(fw+4);
    if(ilm<64 || ilm>65536 || ilm>size-32 || dlm) return -1;
    mtw_write(sc,MTW_FCE_PDMA,0);mtw_write(sc,MTW_FCE_PSE_CTRL,0);
    mtw_usb_dma_write(sc,MTW_USB_TX_EN|MTW_USB_RX_EN);
    mtw_write(sc,MTW_FCE_PSE_CTRL,1);
    mtw_write(sc,MTW_TX_CPU_FCE_BASE,0x400230);
    mtw_write(sc,MTW_TX_CPU_FCE_MAX_COUNT,1);
    mtw_write(sc,MTW_MCU_FW_IDX,1);
    mtw_write(sc,MTW_FCE_PDMA,0x44);mtw_write(sc,MTW_FCE_SKIP_FS,3);
    for(uint32_t at=0;at<ilm-64;) {
        uint32_t n=ilm-64-at;if(n>8192) n=8192;
        if(sc->cmd.status==USB_XFER_PENDING) return -1;
        uint8_t* b=sc->cmd.buf;memset(b,0,n+8);
        mt_put16(b,(uint16_t)n);mt_put16(b+2,MTW_TXD_DATA|MTW_TXD_MCU);
        memcpy(b+4,fw+96+at,n);
        if(mtw_write_cfg(sc,MTW_MCU_DMA_ADDR,0x40+at) ||
           mtw_write_cfg(sc,MTW_MCU_DMA_LEN,n<<16) || mt_bulk(sc,n+8)) return -1;
        uint32_t index;
        if(mtw_read(sc,MTW_MCU_FW_IDX,&index) || mtw_write(sc,MTW_MCU_FW_IDX,index+1)) return -1;
        at+=n;
    }
    if(mt_ctl(sc,0,MTW_RESET,0x12,0,(void*)(fw+32),64)) return -1;
    usb_delay_ms(10);
    for(int i=0;i<100;i++) {
        if(mtw_read_cfg(sc,MTW_MCU_DMA_ADDR,&ready)) return -1;
        if(ready&MTW_MCU_READY) return 0;
        usb_delay_ms(100);
    }
    sc->ok=0;return -1;
}
static int mt_init(struct mtw_softc* sc) {
    uint32_t tmp=0;
    for(int i=0;i<100;i++) {
        if(mtw_read(sc,MTW_WPDMA_GLO_CFG,&tmp)) return -1;
        if(!(tmp&(MTW_TX_DMA_BUSY|MTW_RX_DMA_BUSY))) break;
        if(i==99) return -1;
        usb_delay_ms(1);
    }
    mtw_write(sc,MTW_WPDMA_GLO_CFG,(tmp&0xff0)|MTW_TX_WB_DDONE);
    mtw_write(sc,MTW_MAC_SYS_CTRL,MTW_BBP_HRST|MTW_MAC_SRST);
    mtw_usb_dma_write(sc,0);mtw_write(sc,MTW_MAC_SYS_CTRL,0);
    for(int i=0;i<nitems(mt7601_def_mac);i++)
        mtw_write(sc,mt7601_def_mac[i].reg,mt7601_def_mac[i].val);
    for(int i=0;i<100;i++) {
        if(mtw_read(sc,MTW_MAC_STATUS_REG,&tmp)) return -1;
        if(!(tmp&(MTW_RX_STATUS_BUSY|MTW_TX_STATUS_BUSY))) break;
        if(i==99) return -1;
        usb_delay_ms(1);
    }
    mtw_set_macaddr(sc,sc->wifi.nd.mac);
    /* No hardware cipher/key table: CCMP runs in the station core. */
    mtw_set_region_4(sc,MTW_WCID_ATTR(0),0,8*32);
    mtw_write(sc,0x1648,0x00830083);
    mtw_read(sc,MTW_FCE_L2_STUFF,&tmp);
    mtw_write(sc,MTW_FCE_L2_STUFF,tmp&~MTW_L2S_WR_MPDU_LEN_EN);
    mtw_set_txrts(sc);mtw_write(sc,MTW_BBP_CSR,0);mtw_write(sc,MTW_H2M_MAILBOX,0);
    mtw_set_region_4(sc,MTW_WCID_ENTRY(0),0xffffffffu,512);
    mtw_abort_tsf_sync(sc);
    mtw_read(sc,MTW_US_CYC_CNT,&tmp);mtw_write(sc,MTW_US_CYC_CNT,(tmp&~0xffu)|0x1e);
    mtw_set_region_4(sc,MTW_SKEY(0,0),0,8*32);
    mtw_set_region_4(sc,MTW_IVEIV(0),0,8*32);
    mtw_write(sc,MTW_SKEY_MODE_0_7,0);mtw_write(sc,MTW_SKEY_MODE_8_15,0);
    mtw_write(sc,MTW_TXOP_CTRL_CFG,0x583f);
    for(int i=0;i<5;i++) if(sc->txpow20mhz[i]!=0xffffffffu)
        mtw_write(sc,MTW_TX_PWR_CFG(i),sc->txpow20mhz[i]);
    mtw_write(sc,MTW_TX_PWR_CFG7,0);mtw_write(sc,MTW_TX_PWR_CFG9,0);
    mtw_read(sc,MTW_CMB_CTRL,&tmp);mtw_write(sc,MTW_CMB_CTRL,tmp&~((1<<18)|(1<<14)));
    mtw_usb_dma_write(sc,MTW_USB_TX_EN|MTW_USB_RX_EN|MTW_USB_RX_AGG_EN|
                     MTW_USB_TX_CLEAR|MTW_USB_TXOP_HALT|MTW_USB_RX_WL_DROP);
    usb_delay_ms(50);mtw_read(sc,MTW_USB_DMA_CFG,&tmp);
    mtw_usb_dma_write(sc,tmp&~(MTW_USB_TX_CLEAR|MTW_USB_TXOP_HALT|MTW_USB_RX_WL_DROP));
    if(mtw_mcu_radio(sc,0x31,0) || mt7601_rf_init(sc) ||
       mt7601_bbp_init(sc) || mt7601_rf_setup(sc)) return -1;
    mtw_select_chan_group(sc,0);mt7601_set_chan(sc,1);sc->sc_chan_group=0;
    mtw_enable_mrr(sc);
    mtw_write(sc,MTW_LEGACY_BASIC_RATE,0x17f);
    if(mtw_txrx_enable(sc)) return -1;
    mtw_set_leds(sc,MTW_LED_MODE_ON);return sc->ok?0:-1;
}
static struct mtw_softc* mt_hw(wifi_radio_t* r) { return r->hw; }
static int mt_channel(wifi_radio_t* r,unsigned channel) {
    struct mtw_softc* sc=mt_hw(r);
    /* Initial regulatory domain: passive discovery on world-overlap
     * 2.4GHz channels 1..11, board EEPROM power limits, no HT/40MHz/DFS. */
    if(!sc->ok || channel<1 || channel>11) return -1;
    mt7601_set_chan(sc,channel);usb_delay_ms(1);
    return sc->ok?0:-1;
}
static int mt_station(wifi_radio_t* r,const uint8_t* bssid,unsigned aid) {
    struct mtw_softc* sc=mt_hw(r);uint8_t zero[6]={0};
    if(!sc->ok) return -1;
    mtw_set_bssid(sc,bssid?bssid:zero);
    uint32_t a=bssid?mt_le32(bssid):0xffffffffu;
    uint32_t b=bssid?mt_le16(bssid+4):0xffff;
    mtw_write(sc,MTW_WCID_ENTRY(1),a);mtw_write(sc,MTW_WCID_ENTRY(1)+4,b);
    mtw_write(sc,MTW_WCID_ATTR(1),0);
    if(!bssid) { mtw_abort_tsf_sync(sc);return sc->ok?0:-1; }
    sc->rate=0;
    uint32_t basics=0;int have_ofdm=0;
    for(unsigned i=0;i<r->ap.nrates;i++) for(int j=0;j<nitems(rt2860_rates);j++)
        if((r->ap.rates[i]&0x7f)==rt2860_rates[j].rate) {
            if(j==4) have_ofdm=1;
            if(r->ap.rates[i]&0x80) basics|=1u<<j;
        }
    if(have_ofdm) sc->rate=4;
    if(basics) mtw_write(sc,MTW_LEGACY_BASIC_RATE,basics);
    uint32_t slot;
    mtw_read(sc,MTW_BKOFF_SLOT_CFG,&slot);
    mtw_write(sc,MTW_BKOFF_SLOT_CFG,(slot&~0xffu)|((r->ap.capability&0x400)?9:20));
    if(aid) {
        uint32_t beacon;
        mtw_read(sc,MTW_BCN_TIME_CFG,&beacon);
        beacon=(beacon&~0x1fffffu)|((uint32_t)r->ap.beacon_ms*16)|
               MTW_TSF_TIMER_EN|MTW_TBTT_TIMER_EN|(1<<MTW_TSF_SYNC_MODE_SHIFT);
        mtw_write(sc,MTW_BCN_TIME_CFG,beacon);
    }
    return sc->ok?0:-1;
}
static int mt_rate_supported(wifi_radio_t* r,unsigned idx) {
    for(unsigned i=0;i<r->ap.nrates;i++)
        if((r->ap.rates[i]&0x7f)==rt2860_rates[idx].rate) return 1;
    return 0;
}
static void mt_rate_update(struct mtw_softc* sc) {
    uint8_t b[12];if(mtw_read_region_1(sc,MTW_TX_STA_CNT0,b,12)) return;
    unsigned fail=mt_le32(b)&0xffff,success=mt_le32(b+4)&0xffff,retries=mt_le32(b+4)>>16;
    sc->wifi.nd.tx_errors+=fail;
    unsigned total=fail+success+retries;
    if(total<10) return;
    static const uint8_t order[]={0,1,2,4,5,3,6,7,8,9,10,11};
    int pos=0;while(pos<nitems(order) && order[pos]!=sc->rate) pos++;
    if(pos==nitems(order)) return;
    if((fail+retries)*3>total) {
        while(--pos>=0) if(mt_rate_supported(&sc->wifi,order[pos])) { sc->rate=order[pos];break; }
        sc->good_windows=0;
    } else if((fail+retries)*10<total && ++sc->good_windows>=2) {
        while(++pos<nitems(order)) if(mt_rate_supported(&sc->wifi,order[pos])) { sc->rate=order[pos];break; }
        sc->good_windows=0;
    }
}
static int mt_send(wifi_radio_t* r,const uint8_t* frame,uint32_t n) {
    struct mtw_softc* sc=mt_hw(r);
    if(!sc->ok || !r->nd.present || n<24 || n>WIFI_FRAME_MAX) return -1;
    int slot;
    for(slot=0;slot<MTW_TX_SLOTS;slot++)
        if(sc->tx[slot].status!=USB_XFER_PENDING && sc->zlp[slot].status!=USB_XFER_PENDING) break;
    if(slot==MTW_TX_SLOTS) return -1;
    usb_xfer_t* x=&sc->tx[slot];
    int management=(frame[0]&0x0c)==0;
    x->ep=management?sc->out[0]:sc->out[1];
    sc->zlp[slot].ep=x->ep;
    uint8_t* b=x->buf;
    unsigned block=(sizeof(struct mtw_txwi)+n+3)&~3u;
    unsigned length=block+8;
    memset(b,0,length);
    mt_put16(b,(uint16_t)block);
    /* WIV is required for software-encrypted frames too: hardware must
     * neither generate a CCMP IV nor replace the software cipher payload. */
    mt_put16(b+2,MTW_TXD_DATA|MTW_TXD_80211|MTW_TXD_WLAN|MTW_TXD_QSEL_EDCA|MTW_TXD_WIV);
    unsigned rate=r->state==WIFI_UP&&!management?sc->rate:0;
    if(!mt_rate_supported(r,rate)) {
        for(rate=0;rate<(unsigned)nitems(rt2860_rates);rate++) if(mt_rate_supported(r,rate)) break;
        if(rate==(unsigned)nitems(rt2860_rates)) rate=0;
    }
    struct mtw_txwi* wi=(struct mtw_txwi*)(b+4);
    wi->txop=MTW_TX_TXOP_BACKOFF;
    wi->phy=(uint16_t)(rt2860_rates[rate].mcs|
        ((rt2860_rates[rate].phy==IEEE80211_T_OFDM?MTW_PHY_OFDM:MTW_PHY_CCK)<<MT7601_PHY_SHIFT));
    wi->xflags=MTW_TX_NSEQ|MTW_TX_ACK;wi->wcid=management?0xff:1;wi->len=(uint16_t)n;
    uint8_t* payload=b+4+sizeof(*wi);memcpy(payload,frame,n);
    mt_put16(payload+2,rt2860_rates[rt2860_rates[rate].ctl_ridx].lp_ack_dur);
    x->len=length;
    if(usb_submit(x)<0) return -1;
    unsigned mps=management?sc->mps[0]:sc->mps[1];
    if(mps && !(length%mps)) { sc->zlp[slot].len=0;if(usb_submit(&sc->zlp[slot])<0) return -1; }
    return 0;
}
static int mt_receive(struct mtw_softc* sc,const uint8_t* b,uint32_t n) {
    /* USB aggregation: [4 DMA length][28 RXWI][frame + possible L2pad][4 FCE].
     * Validate all spans before invoking protocol callbacks. */
    uint32_t at=0;
    while(at<n) {
        if(n-at<8) goto bad;
        uint32_t size=mt_le32(b+at)&MTW_RXD_LEN;
        if(!size || (size&3) || size>n-at-8 || size<sizeof(struct mtw_rxwi)) goto bad;
        const uint8_t* wi=b+at+4;unsigned len=mt_le16(wi+6)&0xfff;
        unsigned pad=(mt_le32(wi)&MTW_RX_L2PAD)?2:0;
        if(len<24 || len>WIFI_FRAME_MAX || len+pad>size-sizeof(struct mtw_rxwi)) goto bad;
        at+=size+8;
    }
    at=0;int received=0;
    while(at<n) {
        uint32_t size=mt_le32(b+at)&MTW_RXD_LEN;
        const uint8_t* wi=b+at+4;unsigned len=mt_le16(wi+6)&0xfff;
        const uint8_t* frame=wi+sizeof(struct mtw_rxwi);uint32_t flags=mt_le32(wi);
        if(!(flags&MTW_RX_CRCERR) && !((frame[1]&0x40) && (flags&MTW_RX_DEC))) {
            if(flags&MTW_RX_L2PAD) {
                unsigned hlen=(frame[0]&0x0c)==8 && (frame[0]&0x80)?26:24;
                if(hlen>len) goto next;
                memcpy(sc->raw_frame,frame,hlen);
                memcpy(sc->raw_frame+hlen,frame+hlen+2,len-hlen);
                frame=sc->raw_frame;
            }
            /* Hardware cipher error bits are not authentication results
             * with no hardware key installed: the station verifies CCMP. */
            int rssi=-12-sc->rssi_2ghz[0]+sc->lna[0]-(int)wi[12];
            wifi_rx(&sc->wifi,frame,len,rssi);received++;
        } else sc->wifi.nd.rx_errors++;
next:  at+=size+8;
    }
    return received;
bad:
    sc->wifi.nd.rx_errors++;return -1;
}
static int mt_poll(wifi_radio_t* r) {
    struct mtw_softc* sc=mt_hw(r);int count=0;
    if(sc->detached || !sc->sc_udev->present) return 0;
    usb_poll();
    if(!sc->ok) { if(r->state!=WIFI_DOWN) wifi_disconnect(r);return 0; }
    for(int i=0;i<3;i++) {
        usb_xfer_t* x=&sc->rx[i];
        if(x->status==USB_XFER_PENDING || x->status==USB_XFER_GONE) continue;
        if(x->status==USB_XFER_OK && x->actual && i<2) {
            int n=mt_receive(sc,x->buf,x->actual);if(n>0) count+=n;
        } else if(x->status<0) r->nd.rx_errors++;
        x->len=i<2?MTW_RX_SIZE:1024;
        if(usb_submit(x)<0) x->status=USB_XFER_ERROR;
    }
    if(r->state==WIFI_UP && timer_ms()-sc->stats_ms>=1000) {
        sc->stats_ms=timer_ms();mt_rate_update(sc);
    }
    return count;
}
static const wifi_ops_t mt_ops={mt_poll,mt_send,mt_channel,mt_station};
typedef struct { usb_ep_desc_t in[2],out[6];unsigned nrx,ntx;uint32_t seen;int vendor,done; } mt_eps_t;
static int mt_ep(const uint8_t* b,void* ctx) {
    mt_eps_t* e=ctx;
    if(e->done) return 1;
    if(b[1]==USB_DT_INTERFACE && b[0]>=sizeof(usb_iface_desc_t)) {
        const usb_iface_desc_t* i=(const usb_iface_desc_t*)b;
        e->vendor=i->bInterfaceClass==0xff && !i->bAlternateSetting && i->bNumEndpoints==8;
        e->nrx=e->ntx=0;e->seen=0;
    } else if(b[1]==USB_DT_ENDPOINT && b[0]>=sizeof(usb_ep_desc_t) && e->vendor) {
        const usb_ep_desc_t* ep=(const usb_ep_desc_t*)b;
        if((ep->bmAttributes&3)!=USB_EP_BULK || !(ep->bEndpointAddress&15) || !(ep->wMaxPacketSize&0x7ff)) return 0;
        unsigned bit=(ep->bEndpointAddress&15)+((ep->bEndpointAddress&0x80)?16:0);
        if(e->seen&(1u<<bit)) return 1; /* malformed duplicate: done stays false */
        e->seen|=1u<<bit;
        if(ep->bEndpointAddress&0x80) { if(e->nrx<2) e->in[e->nrx++]=*ep; }
        else if(e->ntx<6) e->out[e->ntx++]=*ep;
        if(e->nrx==2 && e->ntx==6) { e->done=1;return 1; }
    }
    return 0;
}
static int mt_config(usb_device_t* d,int cfg,mt_eps_t* e) {
    memset(e,0,sizeof(*e));return usb_walk_config(d,cfg,mt_ep,e)>=0 && e->done?0:-1;
}
static int mt_probe(usb_device_t* d) {
    /* Start with the reference MT7601U ID; no broad Realtek/vendor-class match. */
    if(d->desc.idVendor!=0x148f || d->desc.idProduct!=0x7601) return -1;
    for(int i=0;i<USB_MAX_CONFIGS;i++) { mt_eps_t e;if(d->configs[i] && !mt_config(d,i,&e)) return i; }
    return -1;
}
static int mt_attach(usb_device_t* d) {
    mt_eps_t e;if(mt_config(d,d->active_config,&e)) return -1;
    struct mtw_softc* sc=kzalloc(sizeof(*sc));if(!sc) return -1;
    sc->sc_udev=d;sc->ok=1;sc->wifi.ops=&mt_ops;sc->wifi.hw=sc;
    sc->wifi.nd.name="mt7601u";sc->wifi.nd.model="MediaTek MT7601U (experimental 802.11b/g)";
    sc->wifi.nd.is_usb=1;sc->wifi.nd.irq=0xff;
    for(int i=0;i<2;i++) {
        sc->in[i]=e.in[i].bEndpointAddress;
        if(usb_open_endpoint(d,&e.in[i])) goto fail;
    }
    for(int i=0;i<6;i++) {
        sc->out[i]=e.out[i].bEndpointAddress;sc->mps[i]=e.out[i].wMaxPacketSize&0x7ff;
        if(usb_open_endpoint(d,&e.out[i])) goto fail;
    }
    uint32_t version;
    if(mtw_read(sc,MTW_ASIC_VER,&version) || version>>16!=0x7601) goto fail;
    sc->asic_ver=(uint16_t)(version>>16);sc->asic_rev=(uint16_t)version;
    sc->cmd.dev=d;sc->cmd.ep=sc->out[0];sc->cmd.status=USB_XFER_OK;
    sc->cmd.buf=usb_dma_alloc(8200);if(!sc->cmd.buf) goto fail;
    if(mtw_wlan_enable(sc,1) || mt_load_firmware(sc)) goto fail;
    if(mtw_read(sc,MTW_MAC_VER_ID,&version) || version>>16!=0x7601) goto fail;
    sc->mac_ver=(uint16_t)(version>>16);sc->mac_rev=(uint16_t)version;
    if(mtw_read_eeprom(sc) || !sc->ok) goto fail;
    uint8_t any=0;for(int i=0;i<6;i++) any|=sc->wifi.nd.mac[i];
    if(!any || (sc->wifi.nd.mac[0]&1)) goto fail;
    if(mt_init(sc)) goto fail;
    for(int i=0;i<MTW_TX_SLOTS;i++) {
        sc->tx[i].dev=sc->zlp[i].dev=d;
        sc->tx[i].buf=usb_dma_alloc(MTW_TX_SIZE);sc->zlp[i].buf=sc->tx[i].buf;
        sc->tx[i].status=sc->zlp[i].status=USB_XFER_OK;
        if(!sc->tx[i].buf) goto fail;
    }
    for(int i=0;i<3;i++) {
        sc->rx[i].dev=d;sc->rx[i].ep=sc->in[i<2?0:1];
        sc->rx[i].len=i<2?MTW_RX_SIZE:1024;
        sc->rx[i].buf=usb_dma_alloc(sc->rx[i].len);
        if(!sc->rx[i].buf) goto fail;
    }
    /* Publish no interface before all hardware initialization succeeds. */
    if(wifi_register(&sc->wifi)) goto fail;
    d->drvdata=sc;
    for(int i=0;i<3;i++) {
        if(usb_submit(&sc->rx[i])<0) sc->rx[i].status=USB_XFER_ERROR;
        else sc->queued=1;
    }
    klog("mt7601u: firmware loaded, radio ready for passive scan; hardware validation pending\n");
    return 0;
fail:
    if(sc->ok) { mtw_write(sc,MTW_MAC_SYS_CTRL,0);mtw_usb_dma_write(sc,0); }
    /* USB has no cancel/DMA-free API yet. A queued transfer can still
     * reference its context after a timeout, so retain that context. */
    if(!sc->queued) kfree(sc);
    klog("mt7601u: initialization failed; no Wi-Fi interface registered\n");return -1;
}
static void mt_detach(usb_device_t* d) {
    struct mtw_softc* sc=d->drvdata;if(!sc) return;
    sc->detached=1;wifi_unregister(&sc->wifi);d->drvdata=NULL;
}
const usb_driver_t mt7601u_driver={"mt7601u",mt_probe,mt_attach,NULL,mt_detach};
