/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2011 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2010 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 * Copyright (c) 2009 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#ifndef BANANA_ATH_NATIVE9380_H
#define BANANA_ATH_NATIVE9380_H
/* Hardware-only compatibility layer for native AR9380. */
#include "types.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"
#include "timer.h"
#define __packed __attribute__((packed))
typedef unsigned int u_int;
#define IEEE80211_STA_ONLY 1
#define NATHN_USB 0
#define BYTE_ORDER 1234
#define BIG_ENDIAN 4321
#define LITTLE_ENDIAN 1234
#define nitems(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define howmany(a,b) (((a)+(b)-1)/(b))
#define roundup(a,b) (howmany(a,b)*(b))
#define DPRINTF(a) do{}while(0)
#define DPRINTFN(n,a) do{}while(0)
#define KASSERT(a) do{}while(0)
#define printf klog
static int abs(int a){return a<0?-a:a;}
#define swap16 __builtin_bswap16
#define swap32 __builtin_bswap32
#define htole32(a) (a)
#define htole16(a) (a)
#define letoh16(a) (a)
#define letoh32(a) (a)
#define ENOMEM 12
#define EINVAL 22
#define EIO 5
#define EPERM 1
#define ETIMEDOUT 110
#define malloc(n,t,f) kmalloc(n)
#define free(p,t,n) kfree(p)
#define IEEE80211_ADDR_COPY(a,b) memcpy((a),(b),6)
#define IEEE80211_IS_CHAN_2GHZ(c) ((c)->ic_freq>=2400&&(c)->ic_freq<2500)
#define IEEE80211_IS_CHAN_5GHZ(c) ((c)->ic_freq>=4900)
#define IEEE80211_CHAN_ANYC ((struct ieee80211_channel*)0)
#define IEEE80211_MODE_11B 1
#define IEEE80211_MODE_11G 2
#define IEEE80211_T_OFDM 1
#define IEEE80211_T_CCK 2
#define IEEE80211_M_STA 1
#define IEEE80211_M_MONITOR 2
#define IEEE80211_F_SHSLOT 1
#define IEEE80211_FC1_RETRY 8
#define IEEE80211_FC1_PWR_MGT 16
#define IEEE80211_FC1_MORE_DATA 32
#define IEEE80211_DUR_DS_SIFS 10
#define IEEE80211_DUR_DS_SLOT 20
#define IEEE80211_DUR_DS_SHSLOT 9
#define LE_READ_4(p) ((uint32_t)(p)[0]|(uint32_t)(p)[1]<<8|(uint32_t)(p)[2]<<16|(uint32_t)(p)[3]<<24)
#define LE_READ_2(p) ((p)[0]|(uint16_t)(p)[1]<<8)
struct ieee80211_channel {unsigned ic_freq;};
struct ieee80211_node {struct ieee80211_channel*ni_chan;};
struct ieee80211com {void*ic_softc;struct ieee80211_node*ic_bss;unsigned ic_flags,ic_curmode,ic_opmode;uint8_t ic_myaddr[6];};
struct mbuf;
struct athn_softc;
static void ath_wait_us(unsigned);
#define DELAY(us) ath_wait_us(us)
#include "athnreg.h"
#define AR9280_TX_GAIN_TABLE_SIZE 22
#undef SM
#define SM(field,val) (((uint32_t)(val)<<field##_S)&field##_M)
#undef RW
#define RW(var,field,val) (((uint32_t)(var)&~field##_M)|SM(field,val))
struct athn_series {
	uint16_t	dur;
	uint8_t		hwrate;
};

struct athn_pier {
	uint8_t		fbin;
	const uint8_t	*pwr[AR_PD_GAINS_IN_MASK];
	const uint8_t	*vpd[AR_PD_GAINS_IN_MASK];
};

/*
 * Structures used to store initialization values.
 */
struct athn_ini {
	int		nregs;
	const uint16_t	*regs;
	const uint32_t	*vals_5g20;
	const uint32_t	*vals_5g40;
	const uint32_t	*vals_2g40;
	const uint32_t	*vals_2g20;
	int		ncmregs;
	const uint16_t	*cmregs;
	const uint32_t	*cmvals;
	int		nfastregs;
	const uint16_t	*fastregs;
	const uint32_t	*fastvals_5g20;
	const uint32_t	*fastvals_5g40;
};

struct athn_gain {
	int		nregs;
	const uint16_t	*regs;
	const uint32_t	*vals_5g;
	const uint32_t	*vals_2g;
};

struct athn_addac {
	int		nvals;
	const uint32_t	*vals;
};

struct athn_serdes {
	int		nvals;
	const uint32_t	*regs;
	const uint32_t	*vals;
};

/* Rx queue software indexes. */
#define ATHN_QID_LP		0
#define ATHN_QID_HP		1

/* Tx queue software indexes. */
#define ATHN_QID_AC_BE		0
#define ATHN_QID_PSPOLL		1
#define ATHN_QID_AC_BK		2
#define ATHN_QID_AC_VI		3
#define ATHN_QID_AC_VO		4
#define ATHN_QID_UAPSD		5
#define ATHN_QID_CAB		6
#define ATHN_QID_BEACON		7
#define ATHN_QID_COUNT		8

#define ATHN_POWER_OFDM6	0
#define ATHN_POWER_OFDM9	1
#define ATHN_POWER_OFDM12	2
#define ATHN_POWER_OFDM18	3
#define ATHN_POWER_OFDM24	4
#define ATHN_POWER_OFDM36	5
#define ATHN_POWER_OFDM48	6
#define ATHN_POWER_OFDM54	7
#define ATHN_POWER_CCK1_LP	8
#define ATHN_POWER_CCK2_LP	9
#define ATHN_POWER_CCK2_SP	10
#define ATHN_POWER_CCK55_LP	11
#define ATHN_POWER_CCK55_SP	12
#define ATHN_POWER_CCK11_LP	13
#define ATHN_POWER_CCK11_SP	14
#define ATHN_POWER_XR		15
#define ATHN_POWER_HT20(mcs)	(16 + (mcs))
#define ATHN_POWER_HT40(mcs)	(40 + (mcs))
#define ATHN_POWER_CCK_DUP	64
#define ATHN_POWER_OFDM_DUP	65
#define ATHN_POWER_CCK_EXT	66
#define ATHN_POWER_OFDM_EXT	67
#define ATHN_POWER_COUNT	68

#define ATHN_ANI_PERIOD		100
#define ATHN_ANI_RSSI_THR_HIGH	40
#define ATHN_ANI_RSSI_THR_LOW	7
struct athn_ani {
	uint8_t		noise_immunity_level;
	uint8_t		spur_immunity_level;
	uint8_t		firstep_level;
	uint8_t		ofdm_weak_signal;
	uint8_t		cck_weak_signal;

	uint32_t	listen_time;

	uint32_t	ofdm_trig_high;
	uint32_t	ofdm_trig_low;

	int32_t		cck_trig_high;
	int32_t		cck_trig_low;

	uint32_t	ofdm_phy_err_base;
	uint32_t	cck_phy_err_base;
	uint32_t	ofdm_phy_err_count;
	uint32_t	cck_phy_err_count;

	uint32_t	cyccnt;
	uint32_t	txfcnt;
	uint32_t	rxfcnt;
};

struct athn_iq_cal {
	uint32_t	pwr_meas_i;
	uint32_t	pwr_meas_q;
	int32_t		iq_corr_meas;
};

struct athn_adc_cal {
	uint32_t	pwr_meas_odd_i;
	uint32_t	pwr_meas_even_i;
	uint32_t	pwr_meas_odd_q;
	uint32_t	pwr_meas_even_q;
};

struct athn_calib {
	int			nsamples;
	struct athn_iq_cal	iq[AR_MAX_CHAINS];
	struct athn_adc_cal	adc_gain[AR_MAX_CHAINS];
	struct athn_adc_cal	adc_dc_offset[AR_MAX_CHAINS];
};

#define ATHN_NF_CAL_HIST_MAX	5

struct athn_softc;

struct athn_ops {
	/* Bus callbacks. */
	uint32_t	(*read)(struct athn_softc *, uint32_t);
	void		(*write)(struct athn_softc *, uint32_t, uint32_t);
	void		(*write_barrier)(struct athn_softc *);

	void	(*setup)(struct athn_softc *);
	void	(*set_txpower)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	void	(*spur_mitigate)(struct athn_softc *,
		    struct ieee80211_channel *, struct ieee80211_channel *);
	const struct ar_spur_chan *
		(*get_spur_chans)(struct athn_softc *, int);
	void	(*init_from_rom)(struct athn_softc *,
		    struct ieee80211_channel *, struct ieee80211_channel *);
	int	(*set_synth)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	int	(*read_rom_data)(struct athn_softc *, uint32_t, void *, int);
	const uint8_t *
		(*get_rom_template)(struct athn_softc *, uint8_t);
	void	(*swap_rom)(struct athn_softc *);
	void	(*olpc_init)(struct athn_softc *);
	void	(*olpc_temp_compensation)(struct athn_softc *);
	/* GPIO callbacks. */
	int	(*gpio_read)(struct athn_softc *, int);
	void	(*gpio_write)(struct athn_softc *, int, int);
	void	(*gpio_config_input)(struct athn_softc *, int);
	void	(*gpio_config_output)(struct athn_softc *, int, int);
	void	(*rfsilent_init)(struct athn_softc *);
	/* DMA callbacks. */
	int	(*dma_alloc)(struct athn_softc *);
	void	(*dma_free)(struct athn_softc *);
	void	(*rx_enable)(struct athn_softc *);
	int	(*intr)(struct athn_softc *);
	int	(*tx)(struct athn_softc *, struct mbuf *,
		    struct ieee80211_node *, int);
	/* PHY callbacks. */
	void	(*set_rf_mode)(struct athn_softc *,
		    struct ieee80211_channel *);
	int	(*rf_bus_request)(struct athn_softc *);
	void	(*rf_bus_release)(struct athn_softc *);
	void	(*set_phy)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	void	(*set_delta_slope)(struct athn_softc *,
		    struct ieee80211_channel *, struct ieee80211_channel *);
	void	(*enable_antenna_diversity)(struct athn_softc *);
	void	(*init_baseband)(struct athn_softc *);
	void	(*disable_phy)(struct athn_softc *);
	void	(*set_rxchains)(struct athn_softc *);
	void	(*noisefloor_calib)(struct athn_softc *);
	void	(*init_noisefloor_calib)(struct athn_softc *);
	int	(*get_noisefloor)(struct athn_softc *);
	void	(*apply_noisefloor)(struct athn_softc *);
	void	(*do_calib)(struct athn_softc *);
	void	(*next_calib)(struct athn_softc *);
	void	(*hw_init)(struct athn_softc *, struct ieee80211_channel *,
		    struct ieee80211_channel *);
	void	(*get_paprd_masks)(struct athn_softc *sc,
		    struct ieee80211_channel *, uint32_t *, uint32_t *);
	/* ANI callbacks. */
	void	(*set_noise_immunity_level)(struct athn_softc *, int);
	void	(*enable_ofdm_weak_signal)(struct athn_softc *);
	void	(*disable_ofdm_weak_signal)(struct athn_softc *);
	void	(*set_cck_weak_signal)(struct athn_softc *, int);
	void	(*set_firstep_level)(struct athn_softc *, int);
	void	(*set_spur_immunity_level)(struct athn_softc *, int);
};


struct athn_softc {
 struct {char dv_xname[16];} sc_dev;
 struct ieee80211com sc_ic;
 unsigned flags;
 uint8_t paprd_curchain;
 void *priv;
 int ok;
 void *eep;
 const void *eep_def;
 unsigned eep_base,eep_size,rom_bytes;
 int rom_valid;
 uint16_t mac_ver,eep_rev;
 uint8_t mac_rev,rf_rev,txchainmask,rxchainmask,ntxchains,nrxchains,ngpiopins,cur_calib_mask;
 int led_pin,rfsilent_pin,kc_entries;
 uint32_t gpio_input_en_off,imask,isync,obs_off,workaround,rwbuf[64];
 struct athn_calib calib;
 uint16_t gain1[AR_MAX_CHAINS];
 uint32_t txgain[32];
 int16_t pa_in[AR_MAX_CHAINS][24],angle[AR_MAX_CHAINS][24];
 int32_t trainpow;
 struct ieee80211_channel *curchan,*curchanext;
 const struct athn_ini *ini;
 const struct athn_serdes *serdes;
 const struct athn_gain *rx_gain,*tx_gain;
 const struct athn_addac *addac;
 struct athn_ops ops;
 uint8_t tx_gain_tbl[AR9280_TX_GAIN_TABLE_SIZE],pdadc;
 int8_t tcomp;
 int olpc_ticks,cca_max_2g,cca_max_5g,cca_min_2g,cca_min_5g;
 struct {int16_t nf[AR_MAX_CHAINS],nf_ext[AR_MAX_CHAINS];} nf_hist[ATHN_NF_CAL_HIST_MAX];
 int nf_hist_cur,nf_hist_nvalid,nf_calib_pending,nf_calib_ticks;
 int16_t nf_priv[AR_MAX_CHAINS],nf_ext_priv[AR_MAX_CHAINS];
};
#define ATHN_FLAG_PCIE (1 << 0)
#define ATHN_FLAG_USB (1 << 1)
#define ATHN_FLAG_OLPC (1 << 2)
#define ATHN_FLAG_PAPRD (1 << 3)
#define ATHN_FLAG_FAST_PLL_CLOCK (1 << 4)
#define ATHN_FLAG_RFSILENT (1 << 5)
#define ATHN_FLAG_RFSILENT_REVERSED (1 << 6)
#define ATHN_FLAG_11A (1 << 9)
#define ATHN_FLAG_11G (1 << 10)
#define ATHN_FLAG_11N (1 << 11)
#define ATHN_FLAG_AN_TOP2_FIXUP (1 << 12)
#define ATHN_FLAG_NON_ENTERPRISE (1u<<13)
#define ATHN_FLAG_3TREDUCE_CHAIN (1u<<14)
#define ATHN_CAL_IQ (1 << 0)
#define ATHN_CAL_ADC_GAIN (1 << 1)
#define ATHN_CAL_ADC_DC (1 << 2)
#define ATHN_CAL_TEMP (1 << 3)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-const-variable"
#include "ar9003reg.h"
#include "ar9380reg.h"
#pragma GCC diagnostic pop
static int athn_init_calib(struct athn_softc*,struct ieee80211_channel*,struct ieee80211_channel*);
static void ar9003_reset_txsring(struct athn_softc*);
static void ar9287_1_3_enable_async_fifo(struct athn_softc*s){s->ok=0;}
static void ar9287_1_3_setup_async_fifo(struct athn_softc*s){s->ok=0;}
static const uint32_t ar_nonpcie_serdes_regs[] = {
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES,
	AR_PCIE_SERDES2,
};

static const uint32_t ar_nonpcie_serdes_vals[] = {
	0x9248fc00,
	0x24924924,
	0x28000029,
	0x57160824,
	0x25980579,
	0x00000000,
	0x1aaabe40,
	0xbe105554,
	0x000e1007,
	0x00000000
};

static const struct athn_serdes ar_nonpcie_serdes = {
	nitems(ar_nonpcie_serdes_vals),
	ar_nonpcie_serdes_regs,
	ar_nonpcie_serdes_vals
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#define DELPT 32
#include "ar9380_hal.inc"
#undef DELPT
#pragma GCC diagnostic pop
static int athn_init_calib(struct athn_softc*s,struct ieee80211_channel*c,struct ieee80211_channel*ext){
 if(!AR_SREV_9380(s)||ext||!IEEE80211_IS_CHAN_2GHZ(c))return EINVAL;
 int rc=ar9003_init_calib(s);if(rc)return rc;
 s->ops.init_noisefloor_calib(s);return s->ok?0:EIO;
}
#endif
