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
/* Adapted hardware-only routines; see README.md for changes and scope. */
static void
ar5008_apply_noisefloor(struct athn_softc *sc);
static int
ar5008_attach(struct athn_softc *sc);
static void
ar5008_bb_load_noisefloor(struct athn_softc *sc);
static void
ar5008_calib_adc_dc_off(struct athn_softc *sc);
static void
ar5008_calib_adc_gain(struct athn_softc *sc);
static void
ar5008_calib_iq(struct athn_softc *sc);
static void
ar5008_disable_ofdm_weak_signal(struct athn_softc *sc);
static void
ar5008_disable_phy(struct athn_softc *sc);
static void
ar5008_do_calib(struct athn_softc *sc);
static void
ar5008_do_noisefloor_calib(struct athn_softc *sc);
static void
ar5008_enable_antenna_diversity(struct athn_softc *sc);
static void
ar5008_enable_ofdm_weak_signal(struct athn_softc *sc);
static void
ar5008_get_ht_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const struct ar_cal_target_power_ht *tgt, int nchans,
    uint8_t tpow[8]);
static void
ar5008_get_lg_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const struct ar_cal_target_power_leg *tgt, int nchans,
    uint8_t tpow[4]);
static int
ar5008_get_noisefloor(struct athn_softc *sc);
static void
ar5008_get_pdadcs(struct athn_softc *sc, uint8_t fbin,
    struct athn_pier *lopier, struct athn_pier *hipier, int nxpdgains,
    int nicepts, uint8_t overlap, uint8_t *boundaries, uint8_t *pdadcs);
static uint8_t
ar5008_get_vpd(uint8_t pwr, const uint8_t *pwrPdg, const uint8_t *vpdPdg,
    int nicepts);
static void
ar5008_gpio_config_input(struct athn_softc *sc, int pin);
static void
ar5008_gpio_config_output(struct athn_softc *sc, int pin, int type);
static int
ar5008_gpio_read(struct athn_softc *sc, int pin);
static void
ar5008_gpio_write(struct athn_softc *sc, int pin, int set);
static void
ar5008_hw_init(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar5008_init_baseband(struct athn_softc *sc);
static void
ar5008_init_chains(struct athn_softc *sc);
static void
ar5008_init_noisefloor_calib(struct athn_softc *sc);
static void
ar5008_next_calib(struct athn_softc *sc);
static int
ar5008_read_eep_word(struct athn_softc *sc, uint32_t addr, uint16_t *val);
static void
ar5008_read_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext);
static int
ar5008_read_rom(struct athn_softc *sc);
static void
ar5008_rf_bus_release(struct athn_softc *sc);
static int
ar5008_rf_bus_request(struct athn_softc *sc);
static void
ar5008_rfsilent_init(struct athn_softc *sc);
static void
ar5008_set_cck_weak_signal(struct athn_softc *sc, int high);
static void
ar5008_set_delta_slope(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar5008_set_firstep_level(struct athn_softc *sc, int level);
static void
ar5008_set_noise_immunity_level(struct athn_softc *sc, int level);
static void
ar5008_set_phy(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar5008_set_rf_mode(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar5008_set_rxchains(struct athn_softc *sc);
static void
ar5008_set_spur_immunity_level(struct athn_softc *sc, int level);
static void
ar5008_set_viterbi_mask(struct athn_softc *sc, int bin);
static void
ar5008_swap_rom(struct athn_softc *sc);
static __inline uint32_t
ar5008_synth_delay(struct athn_softc *sc);
static void
ar5008_write_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext);
static void
ar5008_write_txpower(struct athn_softc *sc, int16_t power[ATHN_POWER_COUNT]);
static void
ar5416_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int nxpdgains, uint8_t overlap, uint8_t *boundaries,
    uint8_t *pdadcs);
static uint8_t
ar5416_get_rf_rev(struct athn_softc *sc);
static const struct ar_spur_chan *
ar5416_get_spur_chans(struct athn_softc *sc, int is2ghz);
static int
ar5416_init_calib(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar5416_reset_addac(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar5416_reset_bb_gain(struct athn_softc *sc, struct ieee80211_channel *c);
static uint8_t
ar5416_reverse_bits(uint8_t v, int nbits);
static void
ar5416_rf_reset(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar5416_rw_bank6tpc(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *rwbank6tpc);
static void
ar5416_rw_rfbits(uint32_t *buf, int col, int off, uint32_t val, int nbits);
static void
ar5416_set_power_calib(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar5416_set_txpower(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar5416_swap_rom(struct athn_softc *sc);
static void
ar9160_rw_addac(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *addac);
static int
ar9280_attach(struct athn_softc *sc);
static void
ar9280_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9280_olpc_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, uint8_t *boundaries, uint8_t *pdadcs, uint8_t *txgain);
static void
ar9280_olpc_init(struct athn_softc *sc);
static void
ar9280_olpc_temp_compensation(struct athn_softc *sc);
static void
ar9280_reset_rx_gain(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar9280_reset_tx_gain(struct athn_softc *sc, struct ieee80211_channel *c);
static int
ar9280_set_synth(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9280_setup(struct athn_softc *sc);
static void
ar9280_spur_mitigate(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static int
athn_acktimeout(struct ieee80211_channel *c, int slot);
static uint8_t
athn_chan2fbin(struct ieee80211_channel *c);
static int
athn_chan_sifs(struct ieee80211_channel *c);
static int
athn_clock_rate(struct athn_softc *sc);
static void
athn_config_nonpcie(struct athn_softc *sc);
static void
athn_config_pcie(struct athn_softc *sc);
static void
athn_disable_interrupts(struct athn_softc *sc);
static void
athn_get_chipid(struct athn_softc *sc);
static void
athn_get_delta_slope(uint32_t coeff, uint32_t *exponent, uint32_t *mantissa);
static void
athn_get_pier_ival(uint8_t fbin, const uint8_t *pierfreq, int npiers,
    int *lo, int *hi);
static int
athn_hw_reset(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc, int init);
static void
athn_init_dma(struct athn_softc *sc);
static void
athn_init_pll(struct athn_softc *sc, const struct ieee80211_channel *c);
static void
athn_init_qos(struct athn_softc *sc);
static void
athn_init_tx_queues(struct athn_softc *sc);
static int
athn_interpolate(int x, int x1, int y1, int x2, int y2);
static int
athn_reset(struct athn_softc *sc, int cold);
static void
athn_reset_key(struct athn_softc *sc, int entry);
static int
athn_reset_power_on(struct athn_softc *sc);
static void
athn_set_opmode(struct athn_softc *sc);
static int
athn_set_power_awake(struct athn_softc *sc);
static void
athn_set_rxfilter(struct athn_softc *sc, uint32_t rfilt);
static void
athn_setacktimeout(struct athn_softc *sc, struct ieee80211_channel *c, int slot);
static void
athn_setclockrate(struct athn_softc *sc);
static void
athn_setctstimeout(struct athn_softc *sc, struct ieee80211_channel *c, int slot);
static void
athn_setsifs(struct athn_softc *sc);
static void
athn_start_noisefloor_calib(struct athn_softc *sc, int reset_history);
static void
athn_updateslot(struct ieee80211com *ic);
static void
athn_write_serdes(struct athn_softc *sc, const struct athn_serdes *serdes);

/* ar5008.c: ar5008_apply_noisefloor */
static void
ar5008_apply_noisefloor(struct athn_softc *sc)
{
	uint32_t agc_nfcal;

	agc_nfcal = AR_READ(sc, AR_PHY_AGC_CONTROL) &
	    (AR_PHY_AGC_CONTROL_NF | AR_PHY_AGC_CONTROL_ENABLE_NF |
	    AR_PHY_AGC_CONTROL_NO_UPDATE_NF);

	if (agc_nfcal & AR_PHY_AGC_CONTROL_NF) {
		/* Pause running NF calibration while values are updated. */
		AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
		AR_WRITE_BARRIER(sc);
	}

	ar5008_bb_load_noisefloor(sc);

	if (agc_nfcal & AR_PHY_AGC_CONTROL_NF) {
		/* Restart interrupted NF calibration. */
		AR_SETBITS(sc, AR_PHY_AGC_CONTROL, agc_nfcal);
		AR_WRITE_BARRIER(sc);
	}
}

/* ar5008.c: ar5008_attach */
static int
ar5008_attach(struct athn_softc *sc)
{
	struct athn_ops *ops = &sc->ops;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ar_base_eep_header *base;
	uint8_t eep_ver, kc_entries_log;
	int error;

	/* Set callbacks for AR5008, AR9001 and AR9002 families. */
	ops->gpio_read = ar5008_gpio_read;
	ops->gpio_write = ar5008_gpio_write;
	ops->gpio_config_input = ar5008_gpio_config_input;
	ops->gpio_config_output = ar5008_gpio_config_output;
	ops->rfsilent_init = ar5008_rfsilent_init;


	ops->set_rf_mode = ar5008_set_rf_mode;
	ops->rf_bus_request = ar5008_rf_bus_request;
	ops->rf_bus_release = ar5008_rf_bus_release;
	ops->set_phy = ar5008_set_phy;
	ops->set_delta_slope = ar5008_set_delta_slope;
	ops->enable_antenna_diversity = ar5008_enable_antenna_diversity;
	ops->init_baseband = ar5008_init_baseband;
	ops->disable_phy = ar5008_disable_phy;
	ops->set_rxchains = ar5008_set_rxchains;
	ops->noisefloor_calib = ar5008_do_noisefloor_calib;
	ops->init_noisefloor_calib = ar5008_init_noisefloor_calib;
	ops->get_noisefloor = ar5008_get_noisefloor;
	ops->apply_noisefloor = ar5008_apply_noisefloor;
	ops->do_calib = ar5008_do_calib;
	ops->next_calib = ar5008_next_calib;
	ops->hw_init = ar5008_hw_init;

	ops->set_noise_immunity_level = ar5008_set_noise_immunity_level;
	ops->enable_ofdm_weak_signal = ar5008_enable_ofdm_weak_signal;
	ops->disable_ofdm_weak_signal = ar5008_disable_ofdm_weak_signal;
	ops->set_cck_weak_signal = ar5008_set_cck_weak_signal;
	ops->set_firstep_level = ar5008_set_firstep_level;
	ops->set_spur_immunity_level = ar5008_set_spur_immunity_level;

	/* Set MAC registers offsets. */
	sc->obs_off = AR_OBS;
	sc->gpio_input_en_off = AR_GPIO_INPUT_EN_VAL;

	if (!(sc->flags & ATHN_FLAG_PCIE))
		athn_config_nonpcie(sc);
	else
		athn_config_pcie(sc);

	/* Read entire ROM content in memory. */
	if ((error = ar5008_read_rom(sc)) != 0) {
		printf("%s: could not read ROM\n", sc->sc_dev.dv_xname);
		return (error);
	}

	/* Get RF revision. */
	sc->rf_rev = ar5416_get_rf_rev(sc);

	base = sc->eep;
	eep_ver = (base->version >> 12) & 0xf;
	sc->eep_rev = (base->version & 0xfff);
	if (eep_ver != AR_EEP_VER || sc->eep_rev == 0) {
		printf("%s: unsupported ROM version %d.%d\n",
		    sc->sc_dev.dv_xname, eep_ver, sc->eep_rev);
		return (EINVAL);
	}

	if (base->opCapFlags & AR_OPFLAGS_11A) {
		sc->flags |= ATHN_FLAG_11A;
		if ((base->opCapFlags & AR_OPFLAGS_11N_5G20) == 0)
			sc->flags |= ATHN_FLAG_11N;
#ifdef notyet
		if ((base->opCapFlags & AR_OPFLAGS_11N_5G40) == 0)
			sc->flags |= ATHN_FLAG_11N;
#endif
	}
	if (base->opCapFlags & AR_OPFLAGS_11G) {
		sc->flags |= ATHN_FLAG_11G;
		if ((base->opCapFlags & AR_OPFLAGS_11N_2G20) == 0)
			sc->flags |= ATHN_FLAG_11N;
#ifdef notyet
		if ((base->opCapFlags & AR_OPFLAGS_11N_2G40) == 0)
			sc->flags |= ATHN_FLAG_11N;
#endif
	}

	IEEE80211_ADDR_COPY(ic->ic_myaddr, base->macAddr);

	/* Check if we have a hardware radio switch. */
	if (base->rfSilent & AR_EEP_RFSILENT_ENABLED) {
		sc->flags |= ATHN_FLAG_RFSILENT;
		/* Get GPIO pin used by hardware radio switch. */
		sc->rfsilent_pin = MS(base->rfSilent,
		    AR_EEP_RFSILENT_GPIO_SEL);
		/* Get polarity of hardware radio switch. */
		if (base->rfSilent & AR_EEP_RFSILENT_POLARITY)
			sc->flags |= ATHN_FLAG_RFSILENT_REVERSED;
	}

	/* Get the number of HW key cache entries. */
	kc_entries_log = MS(base->deviceCap, AR_EEP_DEVCAP_KC_ENTRIES);
	sc->kc_entries = (kc_entries_log != 0) ?
	    1u << kc_entries_log : AR_KEYTABLE_SIZE;
	if (sc->kc_entries > AR_KEYTABLE_SIZE)
		sc->kc_entries = AR_KEYTABLE_SIZE;

	sc->txchainmask = base->txMask;
	if (sc->mac_ver == AR_SREV_VERSION_5416_PCI &&
	    !(base->opCapFlags & AR_OPFLAGS_11A)) {
		/* For single-band AR5416 PCI, use GPIO pin 0. */
		sc->rxchainmask = ar5008_gpio_read(sc, 0) ? 0x5 : 0x7;
	} else
		sc->rxchainmask = base->rxMask;

	ops->setup(sc);
	return (0);
}

/* ar5008.c: ar5008_bb_load_noisefloor */
static void
ar5008_bb_load_noisefloor(struct athn_softc *sc)
{
	int16_t nf[AR_MAX_CHAINS], nf_ext[AR_MAX_CHAINS];
	int i, ntries;

	/* Write filtered noisefloor values. */
	for (i = 0; i < sc->nrxchains; i++) {
		nf[i] = sc->nf_priv[i] * 2;
		nf_ext[i] = sc->nf_ext_priv[i] * 2;
	}
	ar5008_write_noisefloor(sc, nf, nf_ext);

	/* Load filtered noisefloor values into baseband. */
	AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(AR_READ(sc, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF))
			break;
		DELAY(50);
	}
	if (ntries == 1000) {
		DPRINTF(("failed to load noisefloor values\n"));
		return;
	}

	/*
	 * Restore noisefloor values to initial (max) values. These will
	 * be used as initial values during the next NF calibration.
	 */
	for (i = 0; i < AR_MAX_CHAINS; i++)
		nf[i] = nf_ext[i] = AR_DEFAULT_NOISE_FLOOR;
	ar5008_write_noisefloor(sc, nf, nf_ext);
}

/* ar5008.c: ar5008_calib_adc_dc_off */
static void
ar5008_calib_adc_dc_off(struct athn_softc *sc)
{
	struct athn_adc_cal *cal;
	int32_t dc_offset_mismatch_i, dc_offset_mismatch_q;
	uint32_t reg;
	int count, i;

	for (i = 0; i < AR_MAX_CHAINS; i++) {
		cal = &sc->calib.adc_dc_offset[i];

		/* Accumulate ADC DC offset measures (clear on read). */
		cal->pwr_meas_odd_i  += AR_READ(sc, AR_PHY_CAL_MEAS_0(i));
		cal->pwr_meas_even_i += AR_READ(sc, AR_PHY_CAL_MEAS_1(i));
		cal->pwr_meas_odd_q  += AR_READ(sc, AR_PHY_CAL_MEAS_2(i));
		cal->pwr_meas_even_q += AR_READ(sc, AR_PHY_CAL_MEAS_3(i));
	}
	if (!AR_SREV_9280_10_OR_LATER(sc) &&
	    ++sc->calib.nsamples < AR_CAL_SAMPLES) {
		/* Not enough samples accumulated, continue. */
		ar5008_do_calib(sc);
		return;
	}

	if (AR_SREV_9280_10_OR_LATER(sc))
		count = (1u << (10 + 5));
	else
		count = (1u << ( 2 + 5)) * AR_CAL_SAMPLES;
	for (i = 0; i < sc->nrxchains; i++) {
		cal = &sc->calib.adc_dc_offset[i];

		dc_offset_mismatch_i =
		    (cal->pwr_meas_even_i - cal->pwr_meas_odd_i * 2) / count;
		dc_offset_mismatch_q =
		    (cal->pwr_meas_odd_q - cal->pwr_meas_even_q * 2) / count;

		DPRINTFN(2, ("ADC DC offset calibration for chain %d\n", i));
		reg = AR_READ(sc, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
		reg = RW(reg, AR_PHY_NEW_ADC_DC_GAIN_QDC,
		    dc_offset_mismatch_q);
		reg = RW(reg, AR_PHY_NEW_ADC_DC_GAIN_IDC,
		    dc_offset_mismatch_i);
		AR_WRITE(sc, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), reg);
	}

	/* Apply new settings. */
	AR_SETBITS(sc, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
	    AR_PHY_NEW_ADC_DC_OFFSET_CORR_ENABLE);
	AR_WRITE_BARRIER(sc);

	/* ADC DC offset calibration done. */
	sc->cur_calib_mask &= ~ATHN_CAL_ADC_DC;
	memset(&sc->calib, 0, sizeof(sc->calib));
}

/* ar5008.c: ar5008_calib_adc_gain */
static void
ar5008_calib_adc_gain(struct athn_softc *sc)
{
	struct athn_adc_cal *cal;
	uint32_t reg, gain_mismatch_i, gain_mismatch_q;
	int i;

	for (i = 0; i < AR_MAX_CHAINS; i++) {
		cal = &sc->calib.adc_gain[i];

		/* Accumulate ADC gain measures (clear on read). */
		cal->pwr_meas_odd_i  += AR_READ(sc, AR_PHY_CAL_MEAS_0(i));
		cal->pwr_meas_even_i += AR_READ(sc, AR_PHY_CAL_MEAS_1(i));
		cal->pwr_meas_odd_q  += AR_READ(sc, AR_PHY_CAL_MEAS_2(i));
		cal->pwr_meas_even_q += AR_READ(sc, AR_PHY_CAL_MEAS_3(i));
	}
	if (!AR_SREV_9280_10_OR_LATER(sc) &&
	    ++sc->calib.nsamples < AR_CAL_SAMPLES) {
		/* Not enough samples accumulated, continue. */
		ar5008_do_calib(sc);
		return;
	}

	for (i = 0; i < sc->nrxchains; i++) {
		cal = &sc->calib.adc_gain[i];

		if (cal->pwr_meas_odd_i == 0 || cal->pwr_meas_even_q == 0)
			continue;	/* Prevents division by zero. */

		gain_mismatch_i =
		    (cal->pwr_meas_even_i * 32) / cal->pwr_meas_odd_i;
		gain_mismatch_q =
		    (cal->pwr_meas_odd_q * 32) / cal->pwr_meas_even_q;

		DPRINTFN(2, ("ADC gain calibration for chain %d\n", i));
		reg = AR_READ(sc, AR_PHY_NEW_ADC_DC_GAIN_CORR(i));
		reg = RW(reg, AR_PHY_NEW_ADC_DC_GAIN_IGAIN, gain_mismatch_i);
		reg = RW(reg, AR_PHY_NEW_ADC_DC_GAIN_QGAIN, gain_mismatch_q);
		AR_WRITE(sc, AR_PHY_NEW_ADC_DC_GAIN_CORR(i), reg);
	}

	/* Apply new settings. */
	AR_SETBITS(sc, AR_PHY_NEW_ADC_DC_GAIN_CORR(0),
	    AR_PHY_NEW_ADC_GAIN_CORR_ENABLE);
	AR_WRITE_BARRIER(sc);

	/* ADC gain calibration done. */
	sc->cur_calib_mask &= ~ATHN_CAL_ADC_GAIN;
	memset(&sc->calib, 0, sizeof(sc->calib));
}

/* ar5008.c: ar5008_calib_iq */
static void
ar5008_calib_iq(struct athn_softc *sc)
{
	struct athn_iq_cal *cal;
	uint32_t reg, i_coff_denom, q_coff_denom;
	int32_t i_coff, q_coff;
	int i, iq_corr_neg;

	for (i = 0; i < AR_MAX_CHAINS; i++) {
		cal = &sc->calib.iq[i];

		/* Accumulate IQ calibration measures (clear on read). */
		cal->pwr_meas_i += AR_READ(sc, AR_PHY_CAL_MEAS_0(i));
		cal->pwr_meas_q += AR_READ(sc, AR_PHY_CAL_MEAS_1(i));
		cal->iq_corr_meas +=
		    (int32_t)AR_READ(sc, AR_PHY_CAL_MEAS_2(i));
	}
	if (!AR_SREV_9280_10_OR_LATER(sc) &&
	    ++sc->calib.nsamples < AR_CAL_SAMPLES) {
		/* Not enough samples accumulated, continue. */
		ar5008_do_calib(sc);
		return;
	}

	for (i = 0; i < sc->nrxchains; i++) {
		cal = &sc->calib.iq[i];

		if (cal->pwr_meas_q == 0)
			continue;

		if ((iq_corr_neg = cal->iq_corr_meas < 0))
			cal->iq_corr_meas = -cal->iq_corr_meas;

		i_coff_denom =
		    (cal->pwr_meas_i / 2 + cal->pwr_meas_q / 2) / 128;
		q_coff_denom = cal->pwr_meas_q / 64;

		if (i_coff_denom == 0 || q_coff_denom == 0)
			continue;	/* Prevents division by zero. */

		i_coff = cal->iq_corr_meas / i_coff_denom;
		q_coff = (cal->pwr_meas_i / q_coff_denom) - 64;

		/* Negate i_coff if iq_corr_meas is positive. */
		if (!iq_corr_neg)
			i_coff = 0x40 - (i_coff & 0x3f);
		if (q_coff > 15)
			q_coff = 15;
		else if (q_coff <= -16)
			q_coff = -16;	/* XXX Linux has a bug here? */

		DPRINTFN(2, ("IQ calibration for chain %d\n", i));
		reg = AR_READ(sc, AR_PHY_TIMING_CTRL4(i));
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF, i_coff);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF, q_coff);
		AR_WRITE(sc, AR_PHY_TIMING_CTRL4(i), reg);
	}

	/* Apply new settings. */
	AR_SETBITS(sc, AR_PHY_TIMING_CTRL4_0,
	    AR_PHY_TIMING_CTRL4_IQCORR_ENABLE);
	AR_WRITE_BARRIER(sc);

	/* IQ calibration done. */
	sc->cur_calib_mask &= ~ATHN_CAL_IQ;
	memset(&sc->calib, 0, sizeof(sc->calib));
}

/* ar5008.c: ar5008_disable_ofdm_weak_signal */
static void
ar5008_disable_ofdm_weak_signal(struct athn_softc *sc)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_SFCORR_LOW);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M1_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, 63);
	AR_WRITE(sc, AR_PHY_SFCORR_LOW, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR);
	reg = RW(reg, AR_PHY_SFCORR_M1_THRESH, 127);
	reg = RW(reg, AR_PHY_SFCORR_M2_THRESH, 127);
	reg = RW(reg, AR_PHY_SFCORR_M2COUNT_THR, 31);
	AR_WRITE(sc, AR_PHY_SFCORR, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR_EXT);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH_LOW, 127);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH, 127);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH, 127);
	AR_WRITE(sc, AR_PHY_SFCORR_EXT, reg);

	AR_CLRBITS(sc, AR_PHY_SFCORR_LOW,
	    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_disable_phy */
static void
ar5008_disable_phy(struct athn_softc *sc)
{
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_do_calib */
static void
ar5008_do_calib(struct athn_softc *sc)
{
	uint32_t mode, reg;
	int log;

	reg = AR_READ(sc, AR_PHY_TIMING_CTRL4_0);
	log = AR_SREV_9280_10_OR_LATER(sc) ? 10 : 2;
	reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX, log);
	AR_WRITE(sc, AR_PHY_TIMING_CTRL4_0, reg);

	if (sc->cur_calib_mask & ATHN_CAL_ADC_GAIN)
		mode = AR_PHY_CALMODE_ADC_GAIN;
	else if (sc->cur_calib_mask & ATHN_CAL_ADC_DC)
		mode = AR_PHY_CALMODE_ADC_DC_PER;
	else	/* ATHN_CAL_IQ */
		mode = AR_PHY_CALMODE_IQ;
	AR_WRITE(sc, AR_PHY_CALMODE, mode);

	DPRINTF(("starting calibration mode=0x%x\n", mode));
	AR_SETBITS(sc, AR_PHY_TIMING_CTRL4_0, AR_PHY_TIMING_CTRL4_DO_CAL);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_do_noisefloor_calib */
static void
ar5008_do_noisefloor_calib(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_enable_antenna_diversity */
static void
ar5008_enable_antenna_diversity(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_CCK_DETECT,
	    AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_enable_ofdm_weak_signal */
static void
ar5008_enable_ofdm_weak_signal(struct athn_softc *sc)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_SFCORR_LOW);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M1_THRESH_LOW, 50);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2_THRESH_LOW, 40);
	reg = RW(reg, AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW, 48);
	AR_WRITE(sc, AR_PHY_SFCORR_LOW, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR);
	reg = RW(reg, AR_PHY_SFCORR_M1_THRESH, 77);
	reg = RW(reg, AR_PHY_SFCORR_M2_THRESH, 64);
	reg = RW(reg, AR_PHY_SFCORR_M2COUNT_THR, 16);
	AR_WRITE(sc, AR_PHY_SFCORR, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR_EXT);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH_LOW, 50);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH_LOW, 40);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M1_THRESH, 77);
	reg = RW(reg, AR_PHY_SFCORR_EXT_M2_THRESH, 64);
	AR_WRITE(sc, AR_PHY_SFCORR_EXT, reg);

	AR_SETBITS(sc, AR_PHY_SFCORR_LOW,
	    AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_get_ht_tpow */
static void
ar5008_get_ht_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const struct ar_cal_target_power_ht *tgt, int nchans,
    uint8_t tpow[8])
{
	uint8_t fbin;
	int i, lo, hi;

	/* Find interval (lower and upper indices). */
	fbin = athn_chan2fbin(c);
	for (i = 0; i < nchans; i++) {
		if (tgt[i].bChannel == AR_BCHAN_UNUSED ||
		    tgt[i].bChannel > fbin)
			break;
	}
	hi = i;
	lo = hi - 1;
	if (lo == -1)
		lo = hi;
	else if (hi == nchans || tgt[hi].bChannel == AR_BCHAN_UNUSED)
		hi = lo;

	/* Interpolate values. */
	for (i = 0; i < 8; i++) {
		tpow[i] = athn_interpolate(fbin,
		    tgt[lo].bChannel, tgt[lo].tPow2x[i],
		    tgt[hi].bChannel, tgt[hi].tPow2x[i]);
	}
	/* XXX Apply conformance testing limit. */
}

/* ar5008.c: ar5008_get_lg_tpow */
static void
ar5008_get_lg_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const struct ar_cal_target_power_leg *tgt, int nchans,
    uint8_t tpow[4])
{
	uint8_t fbin;
	int i, lo, hi;

	/* Find interval (lower and upper indices). */
	fbin = athn_chan2fbin(c);
	for (i = 0; i < nchans; i++) {
		if (tgt[i].bChannel == AR_BCHAN_UNUSED ||
		    tgt[i].bChannel > fbin)
			break;
	}
	hi = i;
	lo = hi - 1;
	if (lo == -1)
		lo = hi;
	else if (hi == nchans || tgt[hi].bChannel == AR_BCHAN_UNUSED)
		hi = lo;

	/* Interpolate values. */
	for (i = 0; i < 4; i++) {
		tpow[i] = athn_interpolate(fbin,
		    tgt[lo].bChannel, tgt[lo].tPow2x[i],
		    tgt[hi].bChannel, tgt[hi].tPow2x[i]);
	}
	/* XXX Apply conformance testing limit. */
}

/* ar5008.c: ar5008_get_noisefloor */
static int
ar5008_get_noisefloor(struct athn_softc *sc)
{
	int16_t nf[AR_MAX_CHAINS], nf_ext[AR_MAX_CHAINS];
	int i;

	if (AR_READ(sc, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		/* Noisefloor calibration not finished. */
		return 0;
	}
	/* Noisefloor calibration is finished. */
	ar5008_read_noisefloor(sc, nf, nf_ext);

	/* Update noisefloor history. */
	for (i = 0; i < sc->nrxchains; i++) {
		sc->nf_hist[sc->nf_hist_cur].nf[i] = nf[i];
		sc->nf_hist[sc->nf_hist_cur].nf_ext[i] = nf_ext[i];
	}
	if (++sc->nf_hist_cur >= ATHN_NF_CAL_HIST_MAX)
		sc->nf_hist_cur = 0;
	return 1;
}

/* ar5008.c: ar5008_get_pdadcs */
static void
ar5008_get_pdadcs(struct athn_softc *sc, uint8_t fbin,
    struct athn_pier *lopier, struct athn_pier *hipier, int nxpdgains,
    int nicepts, uint8_t overlap, uint8_t *boundaries, uint8_t *pdadcs)
{
#define DB(x)	((x) / 2)	/* Convert half dB to dB. */
	uint8_t minpwr[AR_PD_GAINS_IN_MASK], maxpwr[AR_PD_GAINS_IN_MASK];
	uint8_t vpd[AR_MAX_PWR_RANGE_IN_HALF_DB], pwr;
	uint8_t lovpd, hivpd, boundary;
	int16_t ss, delta, vpdstep, val;
	int i, j, npdadcs, nvpds, maxidx, tgtidx;

	/* Compute min and max power in half dB for each pdGain. */
	for (i = 0; i < nxpdgains; i++) {
		minpwr[i] = MAX(lopier->pwr[i][0], hipier->pwr[i][0]);
		maxpwr[i] = MIN(lopier->pwr[i][nicepts - 1],
		    hipier->pwr[i][nicepts - 1]);
	}

	/* Fill phase domain analog-to-digital converter (PDADC) table. */
	npdadcs = 0;
	for (i = 0; i < nxpdgains; i++) {
		if (i != nxpdgains - 1)
			boundaries[i] = DB(maxpwr[i] + minpwr[i + 1]) / 2;
		else
			boundaries[i] = DB(maxpwr[i]);
		if (boundaries[i] > AR_MAX_RATE_POWER)
			boundaries[i] = AR_MAX_RATE_POWER;

		if (i == 0 && !AR_SREV_5416_20_OR_LATER(sc)) {
			/* Fix the gain delta (AR5416 1.0 only). */
			delta = boundaries[0] - 23;
			boundaries[0] = 23;
		} else
			delta = 0;

		/* Find starting index for this pdGain. */
		if (i != 0) {
			ss = boundaries[i - 1] - DB(minpwr[i]) -
			    overlap + 1 + delta;
		} else if (AR_SREV_9280_10_OR_LATER(sc)) {
			ss = -DB(minpwr[i]);
		} else
			ss = 0;

		/* Compute Vpd table for this pdGain. */
		nvpds = DB(maxpwr[i] - minpwr[i]) + 1;
		memset(vpd, 0, sizeof(vpd));
		pwr = minpwr[i];
		for (j = 0; j < nvpds; j++) {
			/* Get lower and higher Vpd. */
			lovpd = ar5008_get_vpd(pwr, lopier->pwr[i],
			    lopier->vpd[i], nicepts);
			hivpd = ar5008_get_vpd(pwr, hipier->pwr[i],
			    hipier->vpd[i], nicepts);

			/* Interpolate the final Vpd. */
			vpd[j] = athn_interpolate(fbin,
			    lopier->fbin, lovpd, hipier->fbin, hivpd);

			pwr += 2;	/* In half dB. */
		}

		/* Extrapolate data for ss < 0. */
		if (vpd[1] > vpd[0])
			vpdstep = vpd[1] - vpd[0];
		else
			vpdstep = 1;
		while (ss < 0 && npdadcs < AR_NUM_PDADC_VALUES - 1) {
			val = vpd[0] + ss * vpdstep;
			pdadcs[npdadcs++] = MAX(val, 0);
			ss++;
		}

		tgtidx = boundaries[i] + overlap - DB(minpwr[i]);
		maxidx = MIN(tgtidx, nvpds);
		while (ss < maxidx && npdadcs < AR_NUM_PDADC_VALUES - 1)
			pdadcs[npdadcs++] = vpd[ss++];

		if (tgtidx < maxidx)
			continue;

		/* Extrapolate data for maxidx <= ss <= tgtidx. */
		if (vpd[nvpds - 1] > vpd[nvpds - 2])
			vpdstep = vpd[nvpds - 1] - vpd[nvpds - 2];
		else
			vpdstep = 1;
		while (ss <= tgtidx && npdadcs < AR_NUM_PDADC_VALUES - 1) {
			val = vpd[nvpds - 1] + (ss - maxidx + 1) * vpdstep;
			pdadcs[npdadcs++] = MIN(val, 255);
			ss++;
		}
	}

	/* Fill remaining PDADC and boundaries entries. */
	if (AR_SREV_9285(sc))
		boundary = AR9285_PD_GAIN_BOUNDARY_DEFAULT;
	else	/* Fill with latest. */
		boundary = boundaries[nxpdgains - 1];

	for (; nxpdgains < AR_PD_GAINS_IN_MASK; nxpdgains++)
		boundaries[nxpdgains] = boundary;

	for (; npdadcs < AR_NUM_PDADC_VALUES; npdadcs++)
		pdadcs[npdadcs] = pdadcs[npdadcs - 1];
#undef DB
}

/* ar5008.c: ar5008_get_vpd */
static uint8_t
ar5008_get_vpd(uint8_t pwr, const uint8_t *pwrPdg, const uint8_t *vpdPdg,
    int nicepts)
{
	uint8_t vpd;
	int i, lo, hi;

	for (i = 0; i < nicepts; i++)
		if (pwrPdg[i] > pwr)
			break;
	hi = i;
	lo = hi - 1;
	if (lo == -1)
		lo = hi;
	else if (hi == nicepts)
		hi = lo;

	vpd = athn_interpolate(pwr, pwrPdg[lo], vpdPdg[lo],
	    pwrPdg[hi], vpdPdg[hi]);
	return (vpd);
}

/* ar5008.c: ar5008_gpio_config_input */
static void
ar5008_gpio_config_input(struct athn_softc *sc, int pin)
{
	uint32_t reg;

	if ((sc->flags & ATHN_FLAG_USB) && !AR_SREV_9271(sc)) {
		/* Special case for AR7010. */
		AR_SETBITS(sc, AR7010_GPIO_OE, 1u << pin);
	} else {
		reg = AR_READ(sc, AR_GPIO_OE_OUT);
		reg &= ~(AR_GPIO_OE_OUT_DRV_M << (pin * 2));
		reg |= AR_GPIO_OE_OUT_DRV_NO << (pin * 2);
		AR_WRITE(sc, AR_GPIO_OE_OUT, reg);
	}
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_gpio_config_output */
static void
ar5008_gpio_config_output(struct athn_softc *sc, int pin, int type)
{
	uint32_t reg;
	int mux, off;

	if ((sc->flags & ATHN_FLAG_USB) && !AR_SREV_9271(sc)) {
		/* Special case for AR7010. */
		AR_CLRBITS(sc, AR7010_GPIO_OE, 1u << pin);
		AR_WRITE_BARRIER(sc);
		return;
	}
	mux = pin / 6;
	off = pin % 6;

	reg = AR_READ(sc, AR_GPIO_OUTPUT_MUX(mux));
	if (!AR_SREV_9280_20_OR_LATER(sc) && mux == 0)
		reg = (reg & ~0x1f0) | (reg & 0x1f0) << 1;
	reg &= ~(0x1f << (off * 5));
	reg |= (type & 0x1f) << (off * 5);
	AR_WRITE(sc, AR_GPIO_OUTPUT_MUX(mux), reg);

	reg = AR_READ(sc, AR_GPIO_OE_OUT);
	reg &= ~(AR_GPIO_OE_OUT_DRV_M << (pin * 2));
	reg |= AR_GPIO_OE_OUT_DRV_ALL << (pin * 2);
	AR_WRITE(sc, AR_GPIO_OE_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_gpio_read */
static int
ar5008_gpio_read(struct athn_softc *sc, int pin)
{
	KASSERT(pin < sc->ngpiopins);
	if ((sc->flags & ATHN_FLAG_USB) && !AR_SREV_9271(sc))
		return (!((AR_READ(sc, AR7010_GPIO_IN) >> pin) & 1));
	return ((AR_READ(sc, AR_GPIO_IN_OUT) >> (sc->ngpiopins + pin)) & 1);
}

/* ar5008.c: ar5008_gpio_write */
static void
ar5008_gpio_write(struct athn_softc *sc, int pin, int set)
{
	uint32_t reg;

	KASSERT(pin < sc->ngpiopins);

	if (sc->flags & ATHN_FLAG_USB)
		set = !set;	/* AR9271/AR7010 is reversed. */

	if ((sc->flags & ATHN_FLAG_USB) && !AR_SREV_9271(sc)) {
		/* Special case for AR7010. */
		reg = AR_READ(sc, AR7010_GPIO_OUT);
		if (set)
			reg |= 1u << pin;
		else
			reg &= ~(1u << pin);
		AR_WRITE(sc, AR7010_GPIO_OUT, reg);
	} else {
		reg = AR_READ(sc, AR_GPIO_IN_OUT);
		if (set)
			reg |= 1u << pin;
		else
			reg &= ~(1u << pin);
		AR_WRITE(sc, AR_GPIO_IN_OUT, reg);
	}
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_hw_init */
static void
ar5008_hw_init(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	struct athn_ops *ops = &sc->ops;
	const struct athn_ini *ini = sc->ini;
	const uint32_t *pvals;
	uint32_t reg;
	int i;

	AR_WRITE(sc, AR_PHY(0), 0x00000007);
	AR_WRITE(sc, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_EXTERNAL_RADIO);

	if (!AR_SINGLE_CHIP(sc))
		ar5416_reset_addac(sc, c);

	AR_WRITE(sc, AR_PHY_ADC_SERIAL_CTL, AR_PHY_SEL_INTERNAL_ADDAC);

	/* First initialization step (depends on channel band/bandwidth). */
	if (extc != NULL) {
		if (IEEE80211_IS_CHAN_2GHZ(c))
			pvals = ini->vals_2g40;
		else
			pvals = ini->vals_5g40;
	} else {
		if (IEEE80211_IS_CHAN_2GHZ(c))
			pvals = ini->vals_2g20;
		else
			pvals = ini->vals_5g20;
	}
	DPRINTFN(4, ("writing modal init vals\n"));
	for (i = 0; i < ini->nregs; i++) {
		uint32_t val = pvals[i];

		/* Fix AR_AN_TOP2 initialization value if required. */
		if (ini->regs[i] == AR_AN_TOP2 &&
		    (sc->flags & ATHN_FLAG_AN_TOP2_FIXUP))
			val &= ~AR_AN_TOP2_PWDCLKIND;
		AR_WRITE(sc, ini->regs[i], val);
		if (AR_IS_ANALOG_REG(ini->regs[i])) {
			AR_WRITE_BARRIER(sc);
			DELAY(100);
		}
		if ((i & 0x1f) == 0)
			DELAY(1);
	}
	AR_WRITE_BARRIER(sc);

	if (sc->rx_gain != NULL)
		ar9280_reset_rx_gain(sc, c);
	if (sc->tx_gain != NULL)
		ar9280_reset_tx_gain(sc, c);

	if (AR_SREV_9271_10(sc)) {
		AR_WRITE(sc, AR_PHY(68), 0x30002311);
		AR_WRITE(sc, AR_PHY_RF_CTL3, 0x0a020001);
	}
	AR_WRITE_BARRIER(sc);

	/* Second initialization step (common to all channels). */
	DPRINTFN(4, ("writing common init vals\n"));
	for (i = 0; i < ini->ncmregs; i++) {
		AR_WRITE(sc, ini->cmregs[i], ini->cmvals[i]);
		if (AR_IS_ANALOG_REG(ini->cmregs[i])) {
			AR_WRITE_BARRIER(sc);
			DELAY(100);
		}
		if ((i & 0x1f) == 0)
			DELAY(1);
	}
	AR_WRITE_BARRIER(sc);

	if (!AR_SINGLE_CHIP(sc))
		ar5416_reset_bb_gain(sc, c);

	if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)) {
		/* Update modal values for fast PLL clock. */
		if (extc != NULL)
			pvals = ini->fastvals_5g40;
		else
			pvals = ini->fastvals_5g20;
		DPRINTFN(4, ("writing fast pll clock init vals\n"));
		for (i = 0; i < ini->nfastregs; i++) {
			AR_WRITE(sc, ini->fastregs[i], pvals[i]);
			if (AR_IS_ANALOG_REG(ini->fastregs[i])) {
				AR_WRITE_BARRIER(sc);
				DELAY(100);
			}
			if ((i & 0x1f) == 0)
				DELAY(1);
		}
	}

	/*
	 * Set the RX_ABORT and RX_DIS bits to prevent frames with corrupted
	 * descriptor status.
	 */
	AR_SETBITS(sc, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);

	/* Hardware workarounds for occasional Rx data corruption. */
	if (AR_SREV_9280_10_OR_LATER(sc)) {
		reg = AR_READ(sc, AR_PCU_MISC_MODE2);
		if (!AR_SREV_9271(sc))
			reg &= ~AR_PCU_MISC_MODE2_HWWAR1;
		if (AR_SREV_9287_10_OR_LATER(sc))
			reg &= ~AR_PCU_MISC_MODE2_HWWAR2;
		AR_WRITE(sc, AR_PCU_MISC_MODE2, reg);

	} else if (AR_SREV_5416_20_OR_LATER(sc)) {
		/* Disable baseband clock gating. */
		AR_WRITE(sc, AR_PHY(651), 0x11);

		if (AR_SREV_9160(sc)) {
			/* Disable RIFS search to fix baseband hang. */
			AR_CLRBITS(sc, AR_PHY_HEAVY_CLIP_FACTOR_RIFS,
			    AR_PHY_RIFS_INIT_DELAY_M);
		}
	}
	AR_WRITE_BARRIER(sc);

	ar5008_set_phy(sc, c, extc);
	ar5008_init_chains(sc);

	if (sc->flags & ATHN_FLAG_OLPC) {
		
		sc->olpc_ticks = (int)timer_ms();
		ops->olpc_init(sc);
	}

	ops->set_txpower(sc, c, extc);

	if (!AR_SINGLE_CHIP(sc))
		ar5416_rf_reset(sc, c);
}

/* ar5008.c: ar5008_init_baseband */
static void
ar5008_init_baseband(struct athn_softc *sc)
{
	uint32_t synth_delay;

	synth_delay = ar5008_synth_delay(sc);
	/* Activate the PHY (includes baseband activate and synthesizer on). */
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
	AR_WRITE_BARRIER(sc);
	DELAY(AR_BASE_PHY_ACTIVE_DELAY + synth_delay);
}

/* ar5008.c: ar5008_init_chains */
static void
ar5008_init_chains(struct athn_softc *sc)
{
	if (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5)
		AR_SETBITS(sc, AR_PHY_ANALOG_SWAP, AR_PHY_SWAP_ALT_CHAIN);

	/* Setup chain masks. */
	if (sc->mac_ver <= AR_SREV_VERSION_9160 &&
	    (sc->rxchainmask == 0x3 || sc->rxchainmask == 0x5)) {
		AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  0x7);
		AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, 0x7);
	} else {
		AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  sc->rxchainmask);
		AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, sc->rxchainmask);
	}
	AR_WRITE(sc, AR_SELFGEN_MASK, sc->txchainmask);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_init_noisefloor_calib */
static void
ar5008_init_noisefloor_calib(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_next_calib */
static void
ar5008_next_calib(struct athn_softc *sc)
{
	/* Check if we have any calibration in progress. */
	if (sc->cur_calib_mask != 0) {
		if (!(AR_READ(sc, AR_PHY_TIMING_CTRL4_0) &
		    AR_PHY_TIMING_CTRL4_DO_CAL)) {
			/* Calibration completed for current sample. */
			if (sc->cur_calib_mask & ATHN_CAL_ADC_GAIN)
				ar5008_calib_adc_gain(sc);
			else if (sc->cur_calib_mask & ATHN_CAL_ADC_DC)
				ar5008_calib_adc_dc_off(sc);
			else	/* ATHN_CAL_IQ */
				ar5008_calib_iq(sc);
		}
	}
}

/* ar5008.c: ar5008_read_eep_word */
static int
ar5008_read_eep_word(struct athn_softc *sc, uint32_t addr, uint16_t *val)
{
	uint32_t reg;
	int ntries;

	reg = AR_READ(sc, AR_EEPROM_OFFSET(addr));
	for (ntries = 0; ntries < 1000; ntries++) {
		reg = AR_READ(sc, AR_EEPROM_STATUS_DATA);
		if (!(reg & (AR_EEPROM_STATUS_DATA_BUSY | AR_EEPROM_STATUS_DATA_BUSY_ACCESS |
		    AR_EEPROM_STATUS_DATA_PROT_ACCESS | AR_EEPROM_STATUS_DATA_ABSENT_ACCESS))) {
			*val = MS(reg, AR_EEPROM_STATUS_DATA_VAL);
			return (0);
		}
		DELAY(10);
	}
	*val = 0xffff;
	return (ETIMEDOUT);
}

/* ar5008.c: ar5008_read_noisefloor */
static void
ar5008_read_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext)
{
/* Sign-extends 9-bit value (assumes upper bits are zeroes). */
#define SIGN_EXT(v)	(((v) ^ 0x100) - 0x100)
	uint32_t reg;
	int i;

	for (i = 0; i < sc->nrxchains; i++) {
		reg = AR_READ(sc, AR_PHY_CCA(i));
		if (AR_SREV_9280_10_OR_LATER(sc))
			nf[i] = MS(reg, AR9280_PHY_MINCCA_PWR);
		else
			nf[i] = MS(reg, AR_PHY_MINCCA_PWR);
		nf[i] = SIGN_EXT(nf[i]);

		reg = AR_READ(sc, AR_PHY_EXT_CCA(i));
		if (AR_SREV_9280_10_OR_LATER(sc))
			nf_ext[i] = MS(reg, AR9280_PHY_EXT_MINCCA_PWR);
		else
			nf_ext[i] = MS(reg, AR_PHY_EXT_MINCCA_PWR);
		nf_ext[i] = SIGN_EXT(nf_ext[i]);
	}
#undef SIGN_EXT
}

/* ar5008.c: ar5008_read_rom */
static int
ar5008_read_rom(struct athn_softc *sc)
{
	uint32_t addr, end;
	uint16_t magic, sum, *eep;
	int need_swap = 0;
	int error;

	/* Determine ROM endianness. */
	error = ar5008_read_eep_word(sc, AR_EEPROM_MAGIC_OFFSET, &magic);
	if (error != 0)
		return (error);
	if (magic != AR_EEPROM_MAGIC) {
		if (magic != swap16(AR_EEPROM_MAGIC)) {
			DPRINTF(("invalid ROM magic 0x%x != 0x%x\n",
			    magic, AR_EEPROM_MAGIC));
			return (EIO);
		}
		DPRINTF(("non-native ROM endianness\n"));
		need_swap = 1;
	}

	/* Allocate space to store ROM in host memory. */
	sc->eep = malloc(sc->eep_size, M_DEVBUF, M_NOWAIT);
	if (sc->eep == NULL)
		return (ENOMEM);

	/* Read entire ROM and compute checksum. */
	sum = 0;
	eep = sc->eep;
	end = sc->eep_base + sc->eep_size / sizeof(uint16_t);
	for (addr = sc->eep_base; addr < end; addr++, eep++) {
		if ((error = ar5008_read_eep_word(sc, addr, eep)) != 0) {
			DPRINTF(("could not read ROM at 0x%x\n", addr));
			return (error);
		}
		if (need_swap)
			*eep = swap16(*eep);
		sum ^= *eep;
	}
	if (sum != 0xffff) {
		printf("%s: bad ROM checksum 0x%04x\n",
		    sc->sc_dev.dv_xname, sum);
		return (EIO);
	}
	if (need_swap)
		ar5008_swap_rom(sc);

	return (0);
}

/* ar5008.c: ar5008_rf_bus_release */
static void
ar5008_rf_bus_release(struct athn_softc *sc)
{
	/* Wait for the synthesizer to settle. */
	DELAY(AR_BASE_PHY_ACTIVE_DELAY + ar5008_synth_delay(sc));

	/* Release the RF Bus grant. */
	AR_WRITE(sc, AR_PHY_RFBUS_REQ, 0);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_rf_bus_request */
static int
ar5008_rf_bus_request(struct athn_softc *sc)
{
	int ntries;

	/* Request RF Bus grant. */
	AR_WRITE(sc, AR_PHY_RFBUS_REQ, AR_PHY_RFBUS_REQ_EN);
	for (ntries = 0; ntries < 10000; ntries++) {
		if (AR_READ(sc, AR_PHY_RFBUS_GRANT) & AR_PHY_RFBUS_GRANT_EN)
			return (0);
		DELAY(10);
	}
	DPRINTF(("could not kill baseband Rx"));
	return (ETIMEDOUT);
}

/* ar5008.c: ar5008_rfsilent_init */
static void
ar5008_rfsilent_init(struct athn_softc *sc)
{
	uint32_t reg;

	/* Configure hardware radio switch. */
	AR_SETBITS(sc, AR_GPIO_INPUT_EN_VAL, AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);
	reg = AR_READ(sc, AR_GPIO_INPUT_MUX2);
	reg = RW(reg, AR_GPIO_INPUT_MUX2_RFSILENT, 0);
	AR_WRITE(sc, AR_GPIO_INPUT_MUX2, reg);
	ar5008_gpio_config_input(sc, sc->rfsilent_pin);
	AR_SETBITS(sc, AR_PHY_TEST, AR_PHY_TEST_RFSILENT_BB);
	if (!(sc->flags & ATHN_FLAG_RFSILENT_REVERSED)) {
		AR_SETBITS(sc, AR_GPIO_INTR_POL,
		    AR_GPIO_INTR_POL_PIN(sc->rfsilent_pin));
	}
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_cck_weak_signal */
static void
ar5008_set_cck_weak_signal(struct athn_softc *sc, int high)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_CCK_DETECT);
	reg = RW(reg, AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK, high ? 6 : 8);
	AR_WRITE(sc, AR_PHY_CCK_DETECT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_delta_slope */
static void
ar5008_set_delta_slope(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t coeff, exp, man, reg;

	/* Set Delta Slope (exponent and mantissa). */
	coeff = (100 << 24) / c->ic_freq;
	athn_get_delta_slope(coeff, &exp, &man);
	DPRINTFN(5, ("delta slope coeff exp=%u man=%u\n", exp, man));

	reg = AR_READ(sc, AR_PHY_TIMING3);
	reg = RW(reg, AR_PHY_TIMING3_DSC_EXP, exp);
	reg = RW(reg, AR_PHY_TIMING3_DSC_MAN, man);
	AR_WRITE(sc, AR_PHY_TIMING3, reg);

	/* For Short GI, coeff is 9/10 that of normal coeff. */
	coeff = (9 * coeff) / 10;
	athn_get_delta_slope(coeff, &exp, &man);
	DPRINTFN(5, ("delta slope coeff exp=%u man=%u\n", exp, man));

	reg = AR_READ(sc, AR_PHY_HALFGI);
	reg = RW(reg, AR_PHY_HALFGI_DSC_EXP, exp);
	reg = RW(reg, AR_PHY_HALFGI_DSC_MAN, man);
	AR_WRITE(sc, AR_PHY_HALFGI, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_firstep_level */
static void
ar5008_set_firstep_level(struct athn_softc *sc, int level)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_FIND_SIG);
	reg = RW(reg, AR_PHY_FIND_SIG_FIRSTEP, level * 4);
	AR_WRITE(sc, AR_PHY_FIND_SIG, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_noise_immunity_level */
static void
ar5008_set_noise_immunity_level(struct athn_softc *sc, int level)
{
	int high = level == 4;
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_TOT_DES, high ? -62 : -55);
	AR_WRITE(sc, AR_PHY_DESIRED_SZ, reg);

	reg = AR_READ(sc, AR_PHY_AGC_CTL1);
	reg = RW(reg, AR_PHY_AGC_CTL1_COARSE_LOW, high ? -70 : -64);
	reg = RW(reg, AR_PHY_AGC_CTL1_COARSE_HIGH, high ? -12 : -14);
	AR_WRITE(sc, AR_PHY_AGC_CTL1, reg);

	reg = AR_READ(sc, AR_PHY_FIND_SIG);
	reg = RW(reg, AR_PHY_FIND_SIG_FIRPWR, high ? -80 : -78);
	AR_WRITE(sc, AR_PHY_FIND_SIG, reg);

	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_phy */
static void
ar5008_set_phy(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t phy;

	if (AR_SREV_9285_10_OR_LATER(sc))
		phy = AR_READ(sc, AR_PHY_TURBO) & AR_PHY_FC_ENABLE_DAC_FIFO;
	else
		phy = 0;
	phy |= AR_PHY_FC_HT_EN | AR_PHY_FC_SHORT_GI_40 |
	    AR_PHY_FC_SINGLE_HT_LTF1 | AR_PHY_FC_WALSH;
	if (extc != NULL) {
		phy |= AR_PHY_FC_DYN2040_EN;
		if (extc > c)	/* XXX */
			phy |= AR_PHY_FC_DYN2040_PRI_CH;
	}
	AR_WRITE(sc, AR_PHY_TURBO, phy);

	AR_WRITE(sc, AR_2040_MODE,
	    (extc != NULL) ? AR_2040_JOINED_RX_CLEAR : 0);

	/* Set global transmit timeout. */
	AR_WRITE(sc, AR_GTXTO, SM(AR_GTXTO_TIMEOUT_LIMIT, 25));
	/* Set carrier sense timeout. */
	AR_WRITE(sc, AR_CST, SM(AR_CST_TIMEOUT_LIMIT, 15));
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_rf_mode */
static void
ar5008_set_rf_mode(struct athn_softc *sc, struct ieee80211_channel *c)
{
	uint32_t reg;

	reg = IEEE80211_IS_CHAN_2GHZ(c) ?
	    AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;
	if (!AR_SREV_9280_10_OR_LATER(sc)) {
		reg |= IEEE80211_IS_CHAN_2GHZ(c) ?
		    AR_PHY_MODE_RF2GHZ : AR_PHY_MODE_RF5GHZ;
	} else if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)) {
		reg |= AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE;
	}
	AR_WRITE(sc, AR_PHY_MODE, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_rxchains */
static void
ar5008_set_rxchains(struct athn_softc *sc)
{
	if (sc->rxchainmask == 0x3 || sc->rxchainmask == 0x5) {
		AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  sc->rxchainmask);
		AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, sc->rxchainmask);
		AR_WRITE_BARRIER(sc);
	}
}

/* ar5008.c: ar5008_set_spur_immunity_level */
static void
ar5008_set_spur_immunity_level(struct athn_softc *sc, int level)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_TIMING5);
	reg = RW(reg, AR_PHY_TIMING5_CYCPWR_THR1, (level + 1) * 2);
	AR_WRITE(sc, AR_PHY_TIMING5, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_set_viterbi_mask */
static void
ar5008_set_viterbi_mask(struct athn_softc *sc, int bin)
{
	uint32_t mask[4], reg;
	uint8_t m[62], p[62];	/* XXX use bit arrays? */
	int i, bit, cur;

	/* Compute pilot mask. */
	cur = -6000;
	for (i = 0; i < 4; i++) {
		mask[i] = 0;
		for (bit = 0; bit < 30; bit++) {
			if (abs(cur - bin) < 100)
				mask[i] |= 1u << bit;
			cur += 100;
		}
		if (cur == 0)	/* Skip entry "0". */
			cur = 100;
	}
	/* Write entries from -6000 to -3100. */
	AR_WRITE(sc, AR_PHY_TIMING7, mask[0]);
	AR_WRITE(sc, AR_PHY_TIMING9, mask[0]);
	/* Write entries from -3000 to -100. */
	AR_WRITE(sc, AR_PHY_TIMING8, mask[1]);
	AR_WRITE(sc, AR_PHY_TIMING10, mask[1]);
	/* Write entries from 100 to 3000. */
	AR_WRITE(sc, AR_PHY_PILOT_MASK_01_30, mask[2]);
	AR_WRITE(sc, AR_PHY_CHANNEL_MASK_01_30, mask[2]);
	/* Write entries from 3100 to 6000. */
	AR_WRITE(sc, AR_PHY_PILOT_MASK_31_60, mask[3]);
	AR_WRITE(sc, AR_PHY_CHANNEL_MASK_31_60, mask[3]);

	/* Compute viterbi mask. */
	for (cur = 6100; cur >= 0; cur -= 100)
		p[+cur / 100] = abs(cur - bin) < 75;
	for (cur = -100; cur >= -6100; cur -= 100)
		m[-cur / 100] = abs(cur - bin) < 75;

	/* Write viterbi mask (XXX needs to be reworked). */
	reg =
	    m[46] << 30 | m[47] << 28 | m[48] << 26 | m[49] << 24 |
	    m[50] << 22 | m[51] << 20 | m[52] << 18 | m[53] << 16 |
	    m[54] << 14 | m[55] << 12 | m[56] << 10 | m[57] <<  8 |
	    m[58] <<  6 | m[59] <<  4 | m[60] <<  2 | m[61] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK_1, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_M_46_61, reg);

	/* XXX m[48] should be m[38] ? */
	reg =             m[31] << 28 | m[32] << 26 | m[33] << 24 |
	    m[34] << 22 | m[35] << 20 | m[36] << 18 | m[37] << 16 |
	    m[48] << 14 | m[39] << 12 | m[40] << 10 | m[41] <<  8 |
	    m[42] <<  6 | m[43] <<  4 | m[44] <<  2 | m[45] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK_2, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_M_31_45, reg);

	/* XXX This one is weird too. */
	reg =
	    m[16] << 30 | m[16] << 28 | m[18] << 26 | m[18] << 24 |
	    m[20] << 22 | m[20] << 20 | m[22] << 18 | m[22] << 16 |
	    m[24] << 14 | m[24] << 12 | m[25] << 10 | m[26] <<  8 |
	    m[27] <<  6 | m[28] <<  4 | m[29] <<  2 | m[30] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK_3, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_M_16_30, reg);

	reg =
	    m[ 0] << 30 | m[ 1] << 28 | m[ 2] << 26 | m[ 3] << 24 |
	    m[ 4] << 22 | m[ 5] << 20 | m[ 6] << 18 | m[ 7] << 16 |
	    m[ 8] << 14 | m[ 9] << 12 | m[10] << 10 | m[11] <<  8 |
	    m[12] <<  6 | m[13] <<  4 | m[14] <<  2 | m[15] <<  0;
	AR_WRITE(sc, AR_PHY_MASK_CTL, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_M_00_15, reg);

	reg =             p[15] << 28 | p[14] << 26 | p[13] << 24 |
	    p[12] << 22 | p[11] << 20 | p[10] << 18 | p[ 9] << 16 |
	    p[ 8] << 14 | p[ 7] << 12 | p[ 6] << 10 | p[ 5] <<  8 |
	    p[ 4] <<  6 | p[ 3] <<  4 | p[ 2] <<  2 | p[ 1] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK2_1, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_P_15_01, reg);

	reg =             p[30] << 28 | p[29] << 26 | p[28] << 24 |
	    p[27] << 22 | p[26] << 20 | p[25] << 18 | p[24] << 16 |
	    p[23] << 14 | p[22] << 12 | p[21] << 10 | p[20] <<  8 |
	    p[19] <<  6 | p[18] <<  4 | p[17] <<  2 | p[16] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK2_2, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_P_30_16, reg);

	reg =             p[45] << 28 | p[44] << 26 | p[43] << 24 |
	    p[42] << 22 | p[41] << 20 | p[40] << 18 | p[39] << 16 |
	    p[38] << 14 | p[37] << 12 | p[36] << 10 | p[35] <<  8 |
	    p[34] <<  6 | p[33] <<  4 | p[32] <<  2 | p[31] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK2_3, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_P_45_31, reg);

	reg =
	    p[61] << 30 | p[60] << 28 | p[59] << 26 | p[58] << 24 |
	    p[57] << 22 | p[56] << 20 | p[55] << 18 | p[54] << 16 |
	    p[53] << 14 | p[52] << 12 | p[51] << 10 | p[50] <<  8 |
	    p[49] <<  6 | p[48] <<  4 | p[47] <<  2 | p[46] <<  0;
	AR_WRITE(sc, AR_PHY_BIN_MASK2_4, reg);
	AR_WRITE(sc, AR_PHY_VIT_MASK2_P_61_46, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_swap_rom */
static void
ar5008_swap_rom(struct athn_softc *sc)
{
	struct ar_base_eep_header *base = sc->eep;

	/* Swap common fields first. */
	base->length = swap16(base->length);
	base->version = swap16(base->version);
	base->regDmn[0] = swap16(base->regDmn[0]);
	base->regDmn[1] = swap16(base->regDmn[1]);
	base->rfSilent = swap16(base->rfSilent);
	base->blueToothOptions = swap16(base->blueToothOptions);
	base->deviceCap = swap16(base->deviceCap);

	/* Swap device-dependent fields. */
	sc->ops.swap_rom(sc);
}

/* ar5008.c: ar5008_synth_delay */
static __inline uint32_t
ar5008_synth_delay(struct athn_softc *sc)
{
	uint32_t delay;

	delay = MS(AR_READ(sc, AR_PHY_RX_DELAY), AR_PHY_RX_DELAY_DELAY);
	if (sc->sc_ic.ic_curmode == IEEE80211_MODE_11B)
		delay = (delay * 4) / 22;
	else
		delay = delay / 10;	/* in 100ns steps */
	return (delay);
}

/* ar5008.c: ar5008_write_noisefloor */
static void
ar5008_write_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext)
{
	uint32_t reg;
	int i;

	for (i = 0; i < sc->nrxchains; i++) {
		reg = AR_READ(sc, AR_PHY_CCA(i));
		reg = RW(reg, AR_PHY_MAXCCA_PWR, nf[i]);
		AR_WRITE(sc, AR_PHY_CCA(i), reg);

		reg = AR_READ(sc, AR_PHY_EXT_CCA(i));
		reg = RW(reg, AR_PHY_EXT_MAXCCA_PWR, nf_ext[i]);
		AR_WRITE(sc, AR_PHY_EXT_CCA(i), reg);
	}
	AR_WRITE_BARRIER(sc);
}

/* ar5008.c: ar5008_write_txpower */
static void
ar5008_write_txpower(struct athn_softc *sc, int16_t power[ATHN_POWER_COUNT])
{
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE1,
	    (power[ATHN_POWER_OFDM18  ] & 0x3f) << 24 |
	    (power[ATHN_POWER_OFDM12  ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM9   ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_OFDM6   ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE2,
	    (power[ATHN_POWER_OFDM54  ] & 0x3f) << 24 |
	    (power[ATHN_POWER_OFDM48  ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM36  ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_OFDM24  ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE3,
	    (power[ATHN_POWER_CCK2_SP ] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK2_LP ] & 0x3f) << 16 |
	    (power[ATHN_POWER_XR      ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_CCK1_LP ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE4,
	    (power[ATHN_POWER_CCK11_SP] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK11_LP] & 0x3f) << 16 |
	    (power[ATHN_POWER_CCK55_SP] & 0x3f) <<  8 |
	    (power[ATHN_POWER_CCK55_LP] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE5,
	    (power[ATHN_POWER_HT20(3) ] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20(2) ] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20(1) ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20(0) ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE6,
	    (power[ATHN_POWER_HT20(7) ] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20(6) ] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20(5) ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20(4) ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE7,
	    (power[ATHN_POWER_HT40(3) ] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(2) ] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40(1) ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40(0) ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE8,
	    (power[ATHN_POWER_HT40(7) ] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(6) ] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40(5) ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40(4) ] & 0x3f));
	AR_WRITE(sc, AR_PHY_POWER_TX_RATE9,
	    (power[ATHN_POWER_OFDM_EXT] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK_EXT ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM_DUP] & 0x3f) <<  8 |
	    (power[ATHN_POWER_CCK_DUP ] & 0x3f));
	AR_WRITE_BARRIER(sc);
}

/* ar5416.c: ar5416_get_pdadcs */
static void
ar5416_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int nxpdgains, uint8_t overlap, uint8_t *boundaries,
    uint8_t *pdadcs)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_cal_data_per_freq *pierdata;
	const uint8_t *pierfreq;
	struct athn_pier lopier, hipier;
	int16_t delta;
	uint8_t fbin;
	int pwroff;
	int i, lo, hi, npiers;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		pierfreq = eep->calFreqPier2G;
		pierdata = eep->calPierData2G[chain];
		npiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pierfreq = eep->calFreqPier5G;
		pierdata = eep->calPierData5G[chain];
		npiers = AR5416_NUM_5G_CAL_PIERS;
	}
	/* Find channel in ROM pier table. */
	fbin = athn_chan2fbin(c);
	athn_get_pier_ival(fbin, pierfreq, npiers, &lo, &hi);

	lopier.fbin = pierfreq[lo];
	hipier.fbin = pierfreq[hi];
	for (i = 0; i < nxpdgains; i++) {
		lopier.pwr[i] = pierdata[lo].pwrPdg[i];
		lopier.vpd[i] = pierdata[lo].vpdPdg[i];
		hipier.pwr[i] = pierdata[hi].pwrPdg[i];
		hipier.vpd[i] = pierdata[hi].vpdPdg[i];
	}
	ar5008_get_pdadcs(sc, fbin, &lopier, &hipier, nxpdgains,
	    AR5416_PD_GAIN_ICEPTS, overlap, boundaries, pdadcs);

	if (!AR_SREV_9280_20_OR_LATER(sc))
		return;

	if (sc->eep_rev >= AR_EEP_MINOR_VER_21)
		pwroff = (int8_t)eep->baseEepHeader.pwrTableOffset;
	else
		pwroff = AR_PWR_TABLE_OFFSET_DB;
	delta = (pwroff - AR_PWR_TABLE_OFFSET_DB) * 2;	/* In half dB. */

	/* Change the original gain boundaries setting. */
	for (i = 0; i < nxpdgains; i++) {
		int value = (int)boundaries[i] - delta;
		boundaries[i] = MAX(0, MIN(value, AR_MAX_RATE_POWER - overlap));
	}
	if (delta != 0) {
		/* Preserve input when shifting backwards; clamp both ends. */
		uint8_t original[AR_NUM_PDADC_VALUES];
		memcpy(original, pdadcs, sizeof(original));
		for (i = 0; i < AR_NUM_PDADC_VALUES; i++)
			pdadcs[i] = original[MAX(0, MIN(i + delta,
			    AR_NUM_PDADC_VALUES - 1))];
	}
}

/* ar5416.c: ar5416_get_rf_rev */
static uint8_t
ar5416_get_rf_rev(struct athn_softc *sc)
{
	uint8_t rev, reg;
	int i;

	/* Allow access to analog chips. */
	AR_WRITE(sc, AR_PHY(0), 0x00000007);

	AR_WRITE(sc, AR_PHY(0x36), 0x00007058);
	for (i = 0; i < 8; i++)
		AR_WRITE(sc, AR_PHY(0x20), 0x00010000);
	reg = (AR_READ(sc, AR_PHY(256)) >> 24) & 0xff;
	reg = (reg & 0xf0) >> 4 | (reg & 0x0f) << 4;

	rev = ar5416_reverse_bits(reg, 8);
	if ((rev & AR_RADIO_SREV_MAJOR) == 0)
		rev = AR_RAD5133_SREV_MAJOR;
	return (rev);
}

/* ar5416.c: ar5416_get_spur_chans */
static const struct ar_spur_chan *
ar5416_get_spur_chans(struct athn_softc *sc, int is2ghz)
{
	const struct ar5416_eeprom *eep = sc->eep;

	return (eep->modalHeader[is2ghz].spurChans);
}

/* ar5416.c: ar5416_init_calib */
static int
ar5416_init_calib(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	int ntries;

	if (AR_SREV_9280_10_OR_LATER(sc)) {
		/* XXX Linux tests AR9287?! */
		AR_CLRBITS(sc, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
		AR_SETBITS(sc, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	}
	/* Calibrate the AGC. */
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);
	/* Poll for offset calibration completion. */
	for (ntries = 0; ntries < 10000; ntries++) {
		if (!(AR_READ(sc, AR_PHY_AGC_CONTROL) &
		    AR_PHY_AGC_CONTROL_CAL))
			break;
		DELAY(10);
	}
	if (ntries == 10000)
		return (ETIMEDOUT);
	if (AR_SREV_9280_10_OR_LATER(sc)) {
		AR_SETBITS(sc, AR_PHY_ADC_CTL, AR_PHY_ADC_CTL_OFF_PWDADC);
		AR_CLRBITS(sc, AR_PHY_AGC_CONTROL,
		    AR_PHY_AGC_CONTROL_FLTR_CAL);
	}
	return (0);
}

/* ar5416.c: ar5416_reset_addac */
static void
ar5416_reset_addac(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct athn_addac *addac = sc->addac;
	const uint32_t *pvals;
	int i;

	if (AR_SREV_9160(sc) && sc->eep_rev >= AR_EEP_MINOR_VER_7) {
		uint32_t *rwaddac = sc->rwbuf;

		/* Copy values from .rodata to writable buffer. */
		memcpy(rwaddac, addac->vals, addac->nvals * sizeof(uint32_t));
		ar9160_rw_addac(sc, c, rwaddac);
		pvals = rwaddac;
	} else
		pvals = addac->vals;
	for (i = 0; i < addac->nvals; i++)
		AR_WRITE(sc, 0x989c, pvals[i]);
	AR_WRITE(sc, 0x98cc, 0);	/* Finalize. */
}

/* ar5416.c: ar5416_reset_bb_gain */
static void
ar5416_reset_bb_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = ar5416_bb_rfgain_vals_2g;
	else
		pvals = ar5416_bb_rfgain_vals_5g;
	for (i = 0; i < 64; i++)
		AR_WRITE(sc, AR_PHY_BB_RFGAIN(i), pvals[i]);
}

/* ar5416.c: ar5416_reverse_bits */
static uint8_t
ar5416_reverse_bits(uint8_t v, int nbits)
{
	KASSERT(nbits <= 8);
	v = ((v >> 1) & 0x55) | ((v & 0x55) << 1);
	v = ((v >> 2) & 0x33) | ((v & 0x33) << 2);
	v = ((v >> 4) & 0x0f) | ((v & 0x0f) << 4);
	return (v >> (8 - nbits));
}

/* ar5416.c: ar5416_rf_reset */
static void
ar5416_rf_reset(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const uint32_t *bank6tpc;
	int i;

	/* Bank 0. */
	AR_WRITE(sc, 0x98b0, 0x1e5795e5);
	AR_WRITE(sc, 0x98e0, 0x02008020);

	/* Bank 1. */
	AR_WRITE(sc, 0x98b0, 0x02108421);
	AR_WRITE(sc, 0x98ec, 0x00000008);

	/* Bank 2. */
	AR_WRITE(sc, 0x98b0, 0x0e73ff17);
	AR_WRITE(sc, 0x98e0, 0x00000420);

	/* Bank 3. */
	if (IEEE80211_IS_CHAN_5GHZ(c))
		AR_WRITE(sc, 0x98f0, 0x01400018);
	else
		AR_WRITE(sc, 0x98f0, 0x01c00018);

	/* Select the Bank 6 TPC values to use. */
	if (AR_SREV_9160_10_OR_LATER(sc))
		bank6tpc = ar9160_bank6tpc_vals;
	else
		bank6tpc = ar5416_bank6tpc_vals;
	if (sc->eep_rev >= AR_EEP_MINOR_VER_2) {
		uint32_t *rwbank6tpc = sc->rwbuf;

		/* Copy values from .rodata to writable buffer. */
		memcpy(rwbank6tpc, bank6tpc, 32 * sizeof(uint32_t));
		ar5416_rw_bank6tpc(sc, c, rwbank6tpc);
		bank6tpc = rwbank6tpc;
	}
	/* Bank 6 TPC. */
	for (i = 0; i < 32; i++)
		AR_WRITE(sc, 0x989c, bank6tpc[i]);
	if (IEEE80211_IS_CHAN_5GHZ(c))
		AR_WRITE(sc, 0x98d0, 0x0000000f);
	else
		AR_WRITE(sc, 0x98d0, 0x0010000f);

	/* Bank 7. */
	AR_WRITE(sc, 0x989c, 0x00000500);
	AR_WRITE(sc, 0x989c, 0x00000800);
	AR_WRITE(sc, 0x98cc, 0x0000000e);
}

/* ar5416.c: ar5416_rw_bank6tpc */
static void
ar5416_rw_bank6tpc(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *rwbank6tpc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;

	if (IEEE80211_IS_CHAN_5GHZ(c)) {
		modal = &eep->modalHeader[0];
		/* 5GHz db in column 0, bits [200-202]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 200, modal->db, 3);
		/* 5GHz ob in column 0, bits [203-205]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 203, modal->ob, 3);
	} else {
		modal = &eep->modalHeader[1];
		/* 2GHz db in column 0, bits [194-196]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 194, modal->db, 3);
		/* 2GHz ob in column 0, bits [197-199]. */
		ar5416_rw_rfbits(rwbank6tpc, 0, 197, modal->ob, 3);
	}
}

/* ar5416.c: ar5416_rw_rfbits */
static void
ar5416_rw_rfbits(uint32_t *buf, int col, int off, uint32_t val, int nbits)
{
	int idx, bit;

	KASSERT(off >= 1 && col < 4 && nbits <= 32);

	off--;	/* Starts at 1. */
	while (nbits-- > 0) {
		idx = off / 8;
		bit = off % 8;
		buf[idx] &= ~(1u << (bit + col * 8));
		buf[idx] |= ((val >> nbits) & 1) << (bit + col * 8);
		off++;
	}
}

/* ar5416.c: ar5416_set_power_calib */
static void
ar5416_set_power_calib(struct athn_softc *sc, struct ieee80211_channel *c)
{
	static const uint32_t chainoffset[] = { 0x0000, 0x2000, 0x1000 };
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;
	uint8_t boundaries[AR_PD_GAINS_IN_MASK];
	uint8_t pdadcs[AR_NUM_PDADC_VALUES];
	uint8_t xpdgains[AR5416_NUM_PD_GAINS];
	uint8_t overlap, txgain;
	uint32_t reg, offset;
	int i, j, nxpdgains;

	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];

	if (sc->eep_rev < AR_EEP_MINOR_VER_2) {
		overlap = MS(AR_READ(sc, AR_PHY_TPCRG5),
		    AR_PHY_TPCRG5_PD_GAIN_OVERLAP);
	} else
		overlap = modal->pdGainOverlap;

	if ((sc->flags & ATHN_FLAG_OLPC) && IEEE80211_IS_CHAN_2GHZ(c)) {
		/* XXX not here. */
		sc->pdadc =
		    ((const struct ar_cal_data_per_freq_olpc *)
		     eep->calPierData2G[0])->vpdPdg[0][0];
	}

	nxpdgains = 0;
	memset(xpdgains, 0, sizeof(xpdgains));
	for (i = AR5416_PD_GAINS_IN_MASK - 1; i >= 0; i--) {
		if (nxpdgains >= AR5416_NUM_PD_GAINS)
			break;	/* Can't happen. */
		if (modal->xpdGain & (1u << i))
			xpdgains[nxpdgains++] = i;
	}
	reg = AR_READ(sc, AR_PHY_TPCRG1);
	reg = RW(reg, AR_PHY_TPCRG1_NUM_PD_GAIN, nxpdgains - 1);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_1, xpdgains[0]);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_2, xpdgains[1]);
	reg = RW(reg, AR_PHY_TPCRG1_PD_GAIN_3, xpdgains[2]);
	AR_WRITE(sc, AR_PHY_TPCRG1, reg);

	for (i = 0; i < AR5416_MAX_CHAINS; i++) {
		if (!(sc->txchainmask & (1u << i)))
			continue;

		if (AR_SREV_5416_20_OR_LATER(sc) &&
		    (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5))
			offset = chainoffset[i];
		else
			offset = i * 0x1000;

		if (sc->flags & ATHN_FLAG_OLPC) {
			ar9280_olpc_get_pdadcs(sc, c, i, boundaries,
			    pdadcs, &txgain);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL6_0);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL6_0, reg);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL6_1);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_ERR_EST_MODE, 3);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL6_1, reg);

			reg = AR_READ(sc, AR_PHY_TX_PWRCTRL7);
			reg = RW(reg, AR_PHY_TX_PWRCTRL_INIT_TX_GAIN, txgain);
			AR_WRITE(sc, AR_PHY_TX_PWRCTRL7, reg);

			overlap = 6;
		} else {
			ar5416_get_pdadcs(sc, c, i, nxpdgains, overlap,
			    boundaries, pdadcs);
		}
		/* Write boundaries. */
		if (i == 0 || AR_SREV_5416_20_OR_LATER(sc)) {
			reg  = SM(AR_PHY_TPCRG5_PD_GAIN_OVERLAP,
			    overlap);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1,
			    boundaries[0]);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2,
			    boundaries[1]);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3,
			    boundaries[2]);
			reg |= SM(AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4,
			    boundaries[3]);
			AR_WRITE(sc, AR_PHY_TPCRG5 + offset, reg);
		}
		/* Write PDADC values. */
		for (j = 0; j < AR_NUM_PDADC_VALUES; j += 4) {
			AR_WRITE(sc, AR_PHY_PDADC_TBL_BASE + offset + j,
			    (uint32_t)pdadcs[j + 0] <<  0 |
			    (uint32_t)pdadcs[j + 1] <<  8 |
			    (uint32_t)pdadcs[j + 2] << 16 |
			    (uint32_t)pdadcs[j + 3] << 24);
		}
	}
}

/* ar5416.c: ar5416_set_txpower */
static void
ar5416_set_txpower(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;
	uint8_t tpow_cck[4], tpow_ofdm[4];
	uint8_t tpow_cck_ext[4], tpow_ofdm_ext[4];
	uint8_t tpow_ht20[8], tpow_ht40[8];
	uint8_t ht40inc;
	int16_t pwr = 0, pwroff, max_ant_gain, power[ATHN_POWER_COUNT];
	int8_t cckinc;
	int i;

	ar5416_set_power_calib(sc, c);

	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];

	/* Compute transmit power reduction due to antenna gain. */
	max_ant_gain = MAX(modal->antennaGainCh[0], modal->antennaGainCh[1]);
	max_ant_gain = MAX(modal->antennaGainCh[2], max_ant_gain);
	/* XXX */

	/*
	 * Reduce scaled power by number of active chains to get per-chain
	 * transmit power level.
	 */
	if (sc->ntxchains == 2)
		pwr -= AR_PWR_DECREASE_FOR_2_CHAIN;
	else if (sc->ntxchains == 3)
		pwr -= AR_PWR_DECREASE_FOR_3_CHAIN;
	if (pwr < 0)
		pwr = 0;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		/* Get CCK target powers. */
		ar5008_get_lg_tpow(sc, c, AR_CTL_11B, eep->calTargetPowerCck,
		    AR5416_NUM_2G_CCK_TARGET_POWERS, tpow_cck);

		/* Get OFDM target powers. */
		ar5008_get_lg_tpow(sc, c, AR_CTL_11G, eep->calTargetPower2G,
		    AR5416_NUM_2G_20_TARGET_POWERS, tpow_ofdm);

		/* Get HT-20 target powers. */
		ar5008_get_ht_tpow(sc, c, AR_CTL_2GHT20,
		    eep->calTargetPower2GHT20, AR5416_NUM_2G_20_TARGET_POWERS,
		    tpow_ht20);

		if (extc != NULL) {
			/* Get HT-40 target powers. */
			ar5008_get_ht_tpow(sc, c, AR_CTL_2GHT40,
			    eep->calTargetPower2GHT40,
			    AR5416_NUM_2G_40_TARGET_POWERS, tpow_ht40);

			/* Get secondary channel CCK target powers. */
			ar5008_get_lg_tpow(sc, extc, AR_CTL_11B,
			    eep->calTargetPowerCck,
			    AR5416_NUM_2G_CCK_TARGET_POWERS, tpow_cck_ext);

			/* Get secondary channel OFDM target powers. */
			ar5008_get_lg_tpow(sc, extc, AR_CTL_11G,
			    eep->calTargetPower2G,
			    AR5416_NUM_2G_20_TARGET_POWERS, tpow_ofdm_ext);
		}
	} else {
		/* Get OFDM target powers. */
		ar5008_get_lg_tpow(sc, c, AR_CTL_11A, eep->calTargetPower5G,
		    AR5416_NUM_5G_20_TARGET_POWERS, tpow_ofdm);

		/* Get HT-20 target powers. */
		ar5008_get_ht_tpow(sc, c, AR_CTL_5GHT20,
		    eep->calTargetPower5GHT20, AR5416_NUM_5G_20_TARGET_POWERS,
		    tpow_ht20);

		if (extc != NULL) {
			/* Get HT-40 target powers. */
			ar5008_get_ht_tpow(sc, c, AR_CTL_5GHT40,
			    eep->calTargetPower5GHT40,
			    AR5416_NUM_5G_40_TARGET_POWERS, tpow_ht40);

			/* Get secondary channel OFDM target powers. */
			ar5008_get_lg_tpow(sc, extc, AR_CTL_11A,
			    eep->calTargetPower5G,
			    AR5416_NUM_5G_20_TARGET_POWERS, tpow_ofdm_ext);
		}
	}

	/* Compute CCK/OFDM delta. */
	cckinc = (sc->flags & ATHN_FLAG_OLPC) ? -2 : 0;

	memset(power, 0, sizeof(power));
	/* Shuffle target powers accross transmit rates. */
	power[ATHN_POWER_OFDM6 ] =
	power[ATHN_POWER_OFDM9 ] =
	power[ATHN_POWER_OFDM12] =
	power[ATHN_POWER_OFDM18] =
	power[ATHN_POWER_OFDM24] = tpow_ofdm[0];
	power[ATHN_POWER_OFDM36] = tpow_ofdm[1];
	power[ATHN_POWER_OFDM48] = tpow_ofdm[2];
	power[ATHN_POWER_OFDM54] = tpow_ofdm[3];
	power[ATHN_POWER_XR    ] = tpow_ofdm[0];
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		power[ATHN_POWER_CCK1_LP ] = tpow_cck[0] + cckinc;
		power[ATHN_POWER_CCK2_LP ] =
		power[ATHN_POWER_CCK2_SP ] = tpow_cck[1] + cckinc;
		power[ATHN_POWER_CCK55_LP] =
		power[ATHN_POWER_CCK55_SP] = tpow_cck[2] + cckinc;
		power[ATHN_POWER_CCK11_LP] =
		power[ATHN_POWER_CCK11_SP] = tpow_cck[3] + cckinc;
	}
	for (i = 0; i < nitems(tpow_ht20); i++)
		power[ATHN_POWER_HT20(i)] = tpow_ht20[i];
	if (extc != NULL) {
		/* Correct PAR difference between HT40 and HT20/Legacy. */
		if (sc->eep_rev >= AR_EEP_MINOR_VER_2)
			ht40inc = modal->ht40PowerIncForPdadc;
		else
			ht40inc = AR_HT40_POWER_INC_FOR_PDADC;
		for (i = 0; i < nitems(tpow_ht40); i++)
			power[ATHN_POWER_HT40(i)] = tpow_ht40[i] + ht40inc;
		power[ATHN_POWER_OFDM_DUP] = tpow_ht40[0];
		power[ATHN_POWER_CCK_DUP ] = tpow_ht40[0] + cckinc;
		power[ATHN_POWER_OFDM_EXT] = tpow_ofdm_ext[0];
		if (IEEE80211_IS_CHAN_2GHZ(c))
			power[ATHN_POWER_CCK_EXT] = tpow_cck_ext[0] + cckinc;
	}

	/* Initial profile: no more than 10 dBm before antenna/chain reduction.
	 * Per-board targets can reduce this further. Offset is applied afterwards. */
	int gain = 0;
	for (i = 0; i < AR9280_MAX_CHAINS; i++)
		if (sc->txchainmask & (1u << i))
			gain = MAX(gain, (int8_t)modal->antennaGainCh[i]);
	int limit = MAX(0, 20 - 2 * gain - (sc->ntxchains > 1 ? 6 : 0));
	for (i = 0; i < ATHN_POWER_COUNT; i++)
		power[i] = MAX(0, MIN(power[i], limit));

	if (AR_SREV_9280_10_OR_LATER(sc)) {
		if (sc->eep_rev >= AR_EEP_MINOR_VER_21)
			pwroff = (int8_t)eep->baseEepHeader.pwrTableOffset;
		else
			pwroff = AR_PWR_TABLE_OFFSET_DB;
		for (i = 0; i < ATHN_POWER_COUNT; i++)
			power[i] -= pwroff * 2;	/* In half dB. */
	}
	for (i = 0; i < ATHN_POWER_COUNT; i++) {
		power[i] = MAX(0, MIN(power[i], AR_MAX_RATE_POWER));
	}

	/* Write transmit power values to hardware. */
	ar5008_write_txpower(sc, power);

	/*
	 * Write transmit power substraction for dynamic chain changing
	 * and per-packet transmit power.
	 */
	AR_WRITE(sc, AR_PHY_POWER_TX_SUB,
	    (modal->pwrDecreaseFor3Chain & 0x3f) << 6 |
	    (modal->pwrDecreaseFor2Chain & 0x3f));
}

/* ar5416.c: ar5416_swap_rom */
static void
ar5416_swap_rom(struct athn_softc *sc)
{
	struct ar5416_eeprom *eep = sc->eep;
	struct ar5416_modal_eep_header *modal;
	int i, j;

	for (i = 0; i < 2; i++) {	/* Dual-band. */
		modal = &eep->modalHeader[i];

		modal->antCtrlCommon = swap32(modal->antCtrlCommon);
		for (j = 0; j < AR5416_MAX_CHAINS; j++) {
			modal->antCtrlChain[j] =
			    swap32(modal->antCtrlChain[j]);
		}
		for (j = 0; j < AR_EEPROM_MODAL_SPURS; j++) {
			modal->spurChans[j].spurChan =
			    swap16(modal->spurChans[j].spurChan);
		}
	}
}

/* ar5416.c: ar9160_rw_addac */
static void
ar9160_rw_addac(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *addac)
{
	struct ar5416_eeprom *eep = sc->eep;
	struct ar5416_modal_eep_header *modal;
	uint8_t fbin, bias;
	int i;

	/* XXX xpaBiasLvlFreq values have not been endian-swapped? */

	/* Get the XPA bias level to use for the specified channel. */
	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];
	if (modal->xpaBiasLvl == 0xff) {
		bias = modal->xpaBiasLvlFreq[0] >> 14;
		fbin = athn_chan2fbin(c);
		for (i = 1; i < 3; i++) {
			if (modal->xpaBiasLvlFreq[i] == 0)
				break;
			if ((modal->xpaBiasLvlFreq[i] & 0xff) < fbin)
				break;
			bias = modal->xpaBiasLvlFreq[i] >> 14;
		}
	} else
		bias = modal->xpaBiasLvl & 0x3;

	bias = ar5416_reverse_bits(bias, 2);	/* Put in host bit-order. */
	DPRINTFN(4, ("bias level=%d\n", bias));
	if (IEEE80211_IS_CHAN_2GHZ(c))
		ar5416_rw_rfbits(addac, 0, 60, bias, 2);
	else
		ar5416_rw_rfbits(addac, 0, 55, bias, 2);
}

/* ar9280.c: ar9280_attach */
static int
ar9280_attach(struct athn_softc *sc)
{
	sc->eep_base = AR5416_EEP_START_LOC;
	sc->eep_size = sizeof(struct ar5416_eeprom);
	sc->ngpiopins = (sc->flags & ATHN_FLAG_USB) ? 16 : 10;
	sc->led_pin = 1;
	sc->workaround = AR9280_WA_DEFAULT;
	sc->ops.setup = ar9280_setup;
	sc->ops.swap_rom = ar5416_swap_rom;
	sc->ops.init_from_rom = ar9280_init_from_rom;
	sc->ops.set_txpower = ar5416_set_txpower;
	sc->ops.set_synth = ar9280_set_synth;
	sc->ops.spur_mitigate = ar9280_spur_mitigate;
	sc->ops.get_spur_chans = ar5416_get_spur_chans;
	sc->ops.olpc_init = ar9280_olpc_init;
	sc->ops.olpc_temp_compensation = ar9280_olpc_temp_compensation;
	sc->cca_min_2g = AR9280_PHY_CCA_MIN_GOOD_VAL_2GHZ;
	sc->cca_max_2g = AR9280_PHY_CCA_MAX_GOOD_VAL_2GHZ;
	sc->cca_min_5g = AR9280_PHY_CCA_MIN_GOOD_VAL_5GHZ;
	sc->cca_max_5g = AR9280_PHY_CCA_MAX_GOOD_VAL_5GHZ;
	sc->ini = &ar9280_2_0_ini;
	sc->serdes = &ar9280_2_0_serdes;

	return (ar5008_attach(sc));
}

/* ar9280.c: ar9280_init_from_rom */
static void
ar9280_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	static const uint32_t chainoffset[] = { 0x0000, 0x2000, 0x1000 };
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar5416_modal_eep_header *modal;
	uint32_t reg, offset;
	uint8_t txRxAtten;
	int i;

	modal = &eep->modalHeader[IEEE80211_IS_CHAN_2GHZ(c)];

	AR_WRITE(sc, AR_PHY_SWITCH_COM, modal->antCtrlCommon);

	for (i = 0; i < AR9280_MAX_CHAINS; i++) {
		if (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5)
			offset = chainoffset[i];
		else
			offset = i * 0x1000;

		AR_WRITE(sc, AR_PHY_SWITCH_CHAIN_0 + offset,
		    modal->antCtrlChain[i]);

		reg = AR_READ(sc, AR_PHY_TIMING_CTRL4_0 + offset);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF,
		    modal->iqCalICh[i]);
		reg = RW(reg, AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF,
		    modal->iqCalQCh[i]);
		AR_WRITE(sc, AR_PHY_TIMING_CTRL4_0 + offset, reg);

		if (sc->eep_rev >= AR_EEP_MINOR_VER_3) {
			reg = AR_READ(sc, AR_PHY_GAIN_2GHZ + offset);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN1_MARGIN,
			    modal->bswMargin[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN1_DB,
			    modal->bswAtten[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN2_MARGIN,
			    modal->xatten2Margin[i]);
			reg = RW(reg, AR_PHY_GAIN_2GHZ_XATTEN2_DB,
			    modal->xatten2Db[i]);
			AR_WRITE(sc, AR_PHY_GAIN_2GHZ + offset, reg);
		}
		if (sc->eep_rev >= AR_EEP_MINOR_VER_3)
			txRxAtten = modal->txRxAttenCh[i];
		else	/* Workaround for ROM versions < 14.3. */
			txRxAtten = IEEE80211_IS_CHAN_2GHZ(c) ? 23 : 44;
		reg = AR_READ(sc, AR_PHY_RXGAIN + offset);
		reg = RW(reg, AR9280_PHY_RXGAIN_TXRX_ATTEN,
		    txRxAtten);
		reg = RW(reg, AR9280_PHY_RXGAIN_TXRX_MARGIN,
		    modal->rxTxMarginCh[i]);
		AR_WRITE(sc, AR_PHY_RXGAIN + offset, reg);
	}
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		reg = AR_READ(sc, AR_AN_RF2G1_CH0);
		reg = RW(reg, AR_AN_RF2G1_CH0_OB, modal->ob);
		reg = RW(reg, AR_AN_RF2G1_CH0_DB, modal->db);
		AR_WRITE(sc, AR_AN_RF2G1_CH0, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);

		reg = AR_READ(sc, AR_AN_RF2G1_CH1);
		reg = RW(reg, AR_AN_RF2G1_CH1_OB, modal->ob_ch1);
		reg = RW(reg, AR_AN_RF2G1_CH1_DB, modal->db_ch1);
		AR_WRITE(sc, AR_AN_RF2G1_CH1, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);
	} else {
		reg = AR_READ(sc, AR_AN_RF5G1_CH0);
		reg = RW(reg, AR_AN_RF5G1_CH0_OB5, modal->ob);
		reg = RW(reg, AR_AN_RF5G1_CH0_DB5, modal->db);
		AR_WRITE(sc, AR_AN_RF5G1_CH0, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);

		reg = AR_READ(sc, AR_AN_RF5G1_CH1);
		reg = RW(reg, AR_AN_RF5G1_CH1_OB5, modal->ob_ch1);
		reg = RW(reg, AR_AN_RF5G1_CH1_DB5, modal->db_ch1);
		AR_WRITE(sc, AR_AN_RF5G1_CH1, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);
	}
	reg = AR_READ(sc, AR_AN_TOP2);
	if ((sc->flags & ATHN_FLAG_USB) && IEEE80211_IS_CHAN_5GHZ(c)) {
		/*
		 * Hardcode the output voltage of x-PA bias LDO to the
		 * lowest value for UB94 such that the card doesn't get
		 * too hot.
		 */
		reg = RW(reg, AR_AN_TOP2_XPABIAS_LVL, 0);
	} else
		reg = RW(reg, AR_AN_TOP2_XPABIAS_LVL, modal->xpaBiasLvl);
	if (modal->flagBits & AR5416_EEP_FLAG_LOCALBIAS)
		reg |= AR_AN_TOP2_LOCALBIAS;
	else
		reg &= ~AR_AN_TOP2_LOCALBIAS;
	AR_WRITE(sc, AR_AN_TOP2, reg);
	AR_WRITE_BARRIER(sc);
	DELAY(100);

	reg = AR_READ(sc, AR_PHY_XPA_CFG);
	if (modal->flagBits & AR5416_EEP_FLAG_FORCEXPAON)
		reg |= AR_PHY_FORCE_XPA_CFG;
	else
		reg &= ~AR_PHY_FORCE_XPA_CFG;
	AR_WRITE(sc, AR_PHY_XPA_CFG, reg);

	reg = AR_READ(sc, AR_PHY_SETTLING);
	reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->switchSettling);
	AR_WRITE(sc, AR_PHY_SETTLING, reg);

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_ADC, modal->adcDesiredSize);
	AR_WRITE(sc, AR_PHY_DESIRED_SZ, reg);

	reg =  SM(AR_PHY_RF_CTL4_TX_END_XPAA_OFF, modal->txEndToXpaOff);
	reg |= SM(AR_PHY_RF_CTL4_TX_END_XPAB_OFF, modal->txEndToXpaOff);
	reg |= SM(AR_PHY_RF_CTL4_FRAME_XPAA_ON, modal->txFrameToXpaOn);
	reg |= SM(AR_PHY_RF_CTL4_FRAME_XPAB_ON, modal->txFrameToXpaOn);
	AR_WRITE(sc, AR_PHY_RF_CTL4, reg);

	reg = AR_READ(sc, AR_PHY_RF_CTL3);
	reg = RW(reg, AR_PHY_TX_END_TO_A2_RX_ON, modal->txEndToRxOn);
	AR_WRITE(sc, AR_PHY_RF_CTL3, reg);

	reg = AR_READ(sc, AR_PHY_CCA(0));
	reg = RW(reg, AR9280_PHY_CCA_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_CCA(0), reg);

	reg = AR_READ(sc, AR_PHY_EXT_CCA0);
	reg = RW(reg, AR_PHY_EXT_CCA0_THRESH62, modal->thresh62);
	AR_WRITE(sc, AR_PHY_EXT_CCA0, reg);

	if (sc->eep_rev >= AR_EEP_MINOR_VER_2) {
		reg = AR_READ(sc, AR_PHY_RF_CTL2);
		reg = RW(reg, AR_PHY_TX_END_DATA_START,
		    modal->txFrameToDataStart);
		reg = RW(reg, AR_PHY_TX_END_PA_ON, modal->txFrameToPaOn);
		AR_WRITE(sc, AR_PHY_RF_CTL2, reg);
	}
	if (sc->eep_rev >= AR_EEP_MINOR_VER_3 && extc != NULL) {
		/* Overwrite switch settling with HT-40 value. */
		reg = AR_READ(sc, AR_PHY_SETTLING);
		reg = RW(reg, AR_PHY_SETTLING_SWITCH, modal->swSettleHt40);
		AR_WRITE(sc, AR_PHY_SETTLING, reg);
	}
	if (sc->eep_rev >= AR_EEP_MINOR_VER_19) {
		reg = AR_READ(sc, AR_PHY_CCK_TX_CTRL);
		reg = RW(reg, AR_PHY_CCK_TX_CTRL_TX_DAC_SCALE_CCK,
		    MS(modal->miscBits, AR5416_EEP_MISC_TX_DAC_SCALE_CCK));
		AR_WRITE(sc, AR_PHY_CCK_TX_CTRL, reg);
	}
	if (AR_SREV_9280_20(sc) &&
	    sc->eep_rev >= AR_EEP_MINOR_VER_20) {
		reg = AR_READ(sc, AR_AN_TOP1);
		if (eep->baseEepHeader.dacLpMode &&
		    (IEEE80211_IS_CHAN_2GHZ(c) ||
		     !eep->baseEepHeader.dacHiPwrMode_5G))
			reg |= AR_AN_TOP1_DACLPMODE;
		else
			reg &= ~AR_AN_TOP1_DACLPMODE;
		AR_WRITE(sc, AR_AN_TOP1, reg);
		AR_WRITE_BARRIER(sc);
		DELAY(100);

		reg = AR_READ(sc, AR_PHY_FRAME_CTL);
		reg = RW(reg, AR_PHY_FRAME_CTL_TX_CLIP,
		    MS(modal->miscBits, AR5416_EEP_MISC_TX_CLIP));
		AR_WRITE(sc, AR_PHY_FRAME_CTL, reg);

		reg = AR_READ(sc, AR_PHY_TX_PWRCTRL9);
		reg = RW(reg, AR_PHY_TX_DESIRED_SCALE_CCK,
		    eep->baseEepHeader.desiredScaleCCK);
		AR_WRITE(sc, AR_PHY_TX_PWRCTRL9, reg);
	}
	AR_WRITE_BARRIER(sc);
}

/* ar9280.c: ar9280_olpc_get_pdadcs */
static void
ar9280_olpc_get_pdadcs(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, uint8_t *boundaries, uint8_t *pdadcs, uint8_t *txgain)
{
	const struct ar5416_eeprom *eep = sc->eep;
	const struct ar_cal_data_per_freq_olpc *pierdata;
	const uint8_t *pierfreq;
	uint8_t fbin, pcdac, pwr, idx;
	int i, lo, hi, npiers;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		pierfreq = eep->calFreqPier2G;
		pierdata = (const struct ar_cal_data_per_freq_olpc *)
		    eep->calPierData2G[chain];
		npiers = AR5416_NUM_2G_CAL_PIERS;
	} else {
		pierfreq = eep->calFreqPier5G;
		pierdata = (const struct ar_cal_data_per_freq_olpc *)
		    eep->calPierData5G[chain];
		npiers = AR5416_NUM_5G_CAL_PIERS;
	}
	/* Find channel in ROM pier table. */
	fbin = athn_chan2fbin(c);
	athn_get_pier_ival(fbin, pierfreq, npiers, &lo, &hi);

	/* Get average. */
	pwr = (pierdata[lo].pwrPdg[0][0] + pierdata[hi].pwrPdg[0][0]) / 2;
	pwr /= 2;	/* Convert to dB. */

	/* Find power control digital-to-analog converter (PCDAC) value. */
	pcdac = pierdata[hi].pcdac[0][0];
	for (idx = 0; idx < AR9280_TX_GAIN_TABLE_SIZE - 1; idx++)
		if (pcdac <= sc->tx_gain_tbl[idx])
			break;
	*txgain = idx;

	DPRINTFN(3, ("fbin=%d lo=%d hi=%d pwr=%d pcdac=%d txgain=%d\n",
	    fbin, lo, hi, pwr, pcdac, idx));

	/* Fill phase domain analog-to-digital converter (PDADC) table. */
	for (i = 0; i < AR_NUM_PDADC_VALUES; i++)
		pdadcs[i] = (i < pwr) ? 0x00 : 0xff;

	for (i = 0; i < AR_PD_GAINS_IN_MASK; i++)
		boundaries[i] = AR9280_PD_GAIN_BOUNDARY_DEFAULT;
}

/* ar9280.c: ar9280_olpc_init */
static void
ar9280_olpc_init(struct athn_softc *sc)
{
	uint32_t reg;
	int i;

	/* Save original Tx gain values. */
	for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++) {
		reg = AR_READ(sc, AR_PHY_TX_GAIN_TBL(i));
		sc->tx_gain_tbl[i] = MS(reg, AR_PHY_TX_GAIN);
	}
	/* Initial Tx gain temperature compensation. */
	sc->tcomp = 0;
}

/* ar9280.c: ar9280_olpc_temp_compensation */
static void
ar9280_olpc_temp_compensation(struct athn_softc *sc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	int pdadc, txgain, tcomp;
	uint32_t reg;
	int i;

	reg = AR_READ(sc, AR_PHY_TX_PWRCTRL4);
	pdadc = MS(reg, AR_PHY_TX_PWRCTRL_PD_AVG_OUT);
	DPRINTFN(3, ("PD Avg Out=%d\n", pdadc));

	if (sc->pdadc == 0 || pdadc == 0)
		return;	/* No frames transmitted yet. */

	/* Compute Tx gain temperature compensation. */
	if (sc->eep_rev >= AR_EEP_MINOR_VER_20 &&
	    eep->baseEepHeader.dacHiPwrMode_5G)
		tcomp = (pdadc - sc->pdadc + 4) / 8;
	else
		tcomp = (pdadc - sc->pdadc + 5) / 10;
	DPRINTFN(3, ("OLPC temp compensation=%d\n", tcomp));

	if (tcomp == sc->tcomp)
		return;	/* Don't rewrite the same values. */
	sc->tcomp = tcomp;

	/* Adjust Tx gain values. */
	for (i = 0; i < AR9280_TX_GAIN_TABLE_SIZE; i++) {
		txgain = sc->tx_gain_tbl[i] - tcomp;
		txgain = MAX(0, MIN(txgain, 127));
		reg = AR_READ(sc, AR_PHY_TX_GAIN_TBL(i));
		reg = RW(reg, AR_PHY_TX_GAIN, txgain);
		AR_WRITE(sc, AR_PHY_TX_GAIN_TBL(i), reg);
	}
	AR_WRITE_BARRIER(sc);
}

/* ar9280.c: ar9280_reset_rx_gain */
static void
ar9280_reset_rx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct athn_gain *prog = sc->rx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, prog->regs[i], pvals[i]);
}

/* ar9280.c: ar9280_reset_tx_gain */
static void
ar9280_reset_tx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct athn_gain *prog = sc->tx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, prog->regs[i], pvals[i]);
}

/* ar9280.c: ar9280_set_synth */
static int
ar9280_set_synth(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t phy, reg, ndiv = 0;
	uint32_t freq = c->ic_freq;

	phy = AR_READ(sc, AR9280_PHY_SYNTH_CONTROL) & ~0x3fffffff;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		phy |= (freq << 16) / 15;
		phy |= AR9280_BMODE | AR9280_FRACMODE;

		if (AR_SREV_9287_11_OR_LATER(sc)) {
			/* NB: Magic values from the Linux driver. */
			if (freq == 2484) {	/* Channel 14. */
				/* Japanese regulatory requirements. */
				AR_WRITE(sc, AR_PHY(637), 0x00000000);
				AR_WRITE(sc, AR_PHY(638), 0xefff0301);
				AR_WRITE(sc, AR_PHY(639), 0xca9228ee);
			} else {
				AR_WRITE(sc, AR_PHY(637), 0x00fffeff);
				AR_WRITE(sc, AR_PHY(638), 0x00f5f9ff);
				AR_WRITE(sc, AR_PHY(639), 0xb79f6427);
			}
		} else {
			reg = AR_READ(sc, AR_PHY_CCK_TX_CTRL);
			if (freq == 2484)	/* Channel 14. */
				reg |= AR_PHY_CCK_TX_CTRL_JAPAN;
			else
				reg &= ~AR_PHY_CCK_TX_CTRL_JAPAN;
			AR_WRITE(sc, AR_PHY_CCK_TX_CTRL, reg);
		}
	} else {
		if (AR_SREV_9285_10_OR_LATER(sc) ||
		    sc->eep_rev < AR_EEP_MINOR_VER_22 ||
		    !((struct ar5416_base_eep_header *)sc->eep)->frac_n_5g) {
			if ((freq % 20) == 0) {
				ndiv = (freq * 3) / 60;
				phy |= SM(AR9280_AMODE_REFSEL, 3);
			} else if ((freq % 10) == 0) {
				ndiv = (freq * 6) / 60;
				phy |= SM(AR9280_AMODE_REFSEL, 2);
			}
		}
		if (ndiv != 0) {
			phy |= (ndiv & 0x1ff) << 17;
			phy |= (ndiv & ~0x1ff) * 2;
		} else {
			phy |= (freq << 15) / 15;
			phy |= AR9280_FRACMODE;

			reg = AR_READ(sc, AR_AN_SYNTH9);
			reg = RW(reg, AR_AN_SYNTH9_REFDIVA, 1);
			AR_WRITE(sc, AR_AN_SYNTH9, reg);
		}
	}
	AR_WRITE_BARRIER(sc);
	DPRINTFN(4, ("AR9280_PHY_SYNTH_CONTROL=0x%08x\n", phy));
	AR_WRITE(sc, AR9280_PHY_SYNTH_CONTROL, phy);
	AR_WRITE_BARRIER(sc);
	return (0);
}

/* ar9280.c: ar9280_setup */
static void
ar9280_setup(struct athn_softc *sc)
{
	const struct ar5416_eeprom *eep = sc->eep;
	uint8_t type;

	/* Determine if open loop power control should be used. */
	if (sc->eep_rev >= AR_EEP_MINOR_VER_19 &&
	    eep->baseEepHeader.openLoopPwrCntl)
		sc->flags |= ATHN_FLAG_OLPC;

	/* Determine if fast PLL clock is supported. */
	if (AR_SREV_9280_20(sc) &&
	    (sc->eep_rev <= AR_EEP_MINOR_VER_16 ||
	     eep->baseEepHeader.fastClk5g))
		sc->flags |= ATHN_FLAG_FAST_PLL_CLOCK;

	/*
	 * Determine if initialization value for AR_AN_TOP2 must be fixed.
	 * This is required for some AR9220 devices such as Ubiquiti SR71-12.
	 */
	if (AR_SREV_9280_20(sc) &&
	    sc->eep_rev > AR_EEP_MINOR_VER_10 &&
	    !eep->baseEepHeader.pwdclkind) {
		DPRINTF(("AR_AN_TOP2 fixup required\n"));
		sc->flags |= ATHN_FLAG_AN_TOP2_FIXUP;
	}

	if (AR_SREV_9280_20(sc)) {
		/* Check if we have a valid rxGainType field in ROM. */
		if (sc->eep_rev >= AR_EEP_MINOR_VER_17) {
			/* Select initialization values based on ROM. */
			type = eep->baseEepHeader.rxGainType;
			DPRINTF(("Rx gain type=0x%x\n", type));
			if (type == AR5416_EEP_RXGAIN_23DB_BACKOFF)
				sc->rx_gain = &ar9280_2_0_rx_gain_23db_backoff;
			else if (type == AR5416_EEP_RXGAIN_13DB_BACKOFF)
				sc->rx_gain = &ar9280_2_0_rx_gain_13db_backoff;
			else
				sc->rx_gain = &ar9280_2_0_rx_gain;
		} else
			sc->rx_gain = &ar9280_2_0_rx_gain;

		/* Check if we have a valid txGainType field in ROM. */
		if (sc->eep_rev >= AR_EEP_MINOR_VER_19) {
			/* Select initialization values based on ROM. */
			type = eep->baseEepHeader.txGainType;
			DPRINTF(("Tx gain type=0x%x\n", type));
			if (type == AR_EEP_TXGAIN_HIGH_POWER)
				sc->tx_gain = &ar9280_2_0_tx_gain_high_power;
			else
				sc->tx_gain = &ar9280_2_0_tx_gain;
		} else
			sc->tx_gain = &ar9280_2_0_tx_gain;
	}
}

/* ar9280.c: ar9280_spur_mitigate */
static void
ar9280_spur_mitigate(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar_spur_chan *spurchans;
	int spur, bin, spur_delta_phase, spur_freq_sd, spur_subchannel_sd;
	int spur_off, range, i;

	/* NB: Always clear. */
	AR_CLRBITS(sc, AR_PHY_FORCE_CLKEN_CCK, AR_PHY_FORCE_CLKEN_CCK_MRC_MUX);

	range = (extc != NULL) ? 19 : 10;

	spurchans = sc->ops.get_spur_chans(sc, IEEE80211_IS_CHAN_2GHZ(c));
	for (i = 0; i < AR_EEPROM_MODAL_SPURS; i++) {
		spur = spurchans[i].spurChan;
		if (spur == AR_NO_SPUR)
			return;	/* XXX disable if it was enabled! */
		spur /= 10;
		if (IEEE80211_IS_CHAN_2GHZ(c))
			spur += AR_BASE_FREQ_2GHZ;
		else
			spur += AR_BASE_FREQ_5GHZ;
		spur -= c->ic_freq;
		if (abs(spur) < range)
			break;
	}
	if (i == AR_EEPROM_MODAL_SPURS)
		return;	/* XXX disable if it was enabled! */
	DPRINTFN(2, ("enabling spur mitigation\n"));

	AR_SETBITS(sc, AR_PHY_TIMING_CTRL4_0,
	    AR_PHY_TIMING_CTRL4_ENABLE_SPUR_RSSI |
	    AR_PHY_TIMING_CTRL4_ENABLE_SPUR_FILTER |
	    AR_PHY_TIMING_CTRL4_ENABLE_CHAN_MASK |
	    AR_PHY_TIMING_CTRL4_ENABLE_PILOT_MASK);

	AR_WRITE(sc, AR_PHY_SPUR_REG,
	    AR_PHY_SPUR_REG_MASK_RATE_CNTL |
	    AR_PHY_SPUR_REG_ENABLE_MASK_PPM |
	    AR_PHY_SPUR_REG_MASK_RATE_SELECT |
	    AR_PHY_SPUR_REG_ENABLE_VIT_SPUR_RSSI |
	    SM(AR_PHY_SPUR_REG_SPUR_RSSI_THRESH, AR_SPUR_RSSI_THRESH));

	if (extc != NULL) {
		spur_delta_phase = (spur * 262144) / 10;
		if (spur < 0) {
			spur_subchannel_sd = 1;
			spur_off = spur + 10;
		} else {
			spur_subchannel_sd = 0;
			spur_off = spur - 10;
		}
	} else {
		spur_delta_phase = (spur * 524288) / 10;
		spur_subchannel_sd = 0;
		spur_off = spur;
	}
	if (IEEE80211_IS_CHAN_2GHZ(c))
		spur_freq_sd = (spur_off * 2048) / 44;
	else
		spur_freq_sd = (spur_off * 2048) / 40;

	AR_WRITE(sc, AR_PHY_TIMING11,
	    AR_PHY_TIMING11_USE_SPUR_IN_AGC |
	    SM(AR_PHY_TIMING11_SPUR_FREQ_SD, spur_freq_sd) |
	    SM(AR_PHY_TIMING11_SPUR_DELTA_PHASE, spur_delta_phase));

	AR_WRITE(sc, AR_PHY_SFCORR_EXT,
	    SM(AR_PHY_SFCORR_SPUR_SUBCHNL_SD, spur_subchannel_sd));
	AR_WRITE_BARRIER(sc);

	bin = spur * 320;
	ar5008_set_viterbi_mask(sc, bin);
}

/* athn.c: athn_acktimeout */
static int
athn_acktimeout(struct ieee80211_channel *c, int slot)
{
	int sifs = athn_chan_sifs(c);
	int ackto = sifs + slot;

	/* Workaround for early ACK timeouts. */
	if (IEEE80211_IS_CHAN_2GHZ(c))
		ackto += 64 - sifs - slot;

	return ackto;
}

/* athn.c: athn_chan2fbin */
static uint8_t
athn_chan2fbin(struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_2GHZ(c))
		return (c->ic_freq - 2300);
	else
		return ((c->ic_freq - 4800) / 5);
}

/* athn.c: athn_chan_sifs */
static int
athn_chan_sifs(struct ieee80211_channel *c)
{
	return IEEE80211_IS_CHAN_2GHZ(c) ? IEEE80211_DUR_DS_SIFS : 16;
}

/* athn.c: athn_clock_rate */
static int
athn_clock_rate(struct athn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	int clockrate;	/* MHz. */

	/*
	 * AR9287 v1.3+ MAC runs at 117MHz (instead of 88/44MHz) when
	 * ASYNC FIFO is enabled.
	 */
	if (AR_SREV_9287_13_OR_LATER(sc) && !AR_SREV_9380_10_OR_LATER(sc))
		clockrate = 117;
	else if (ic->ic_bss->ni_chan != IEEE80211_CHAN_ANYC &&
	    IEEE80211_IS_CHAN_5GHZ(ic->ic_bss->ni_chan)) {
		if (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)
			clockrate = AR_CLOCK_RATE_FAST_5GHZ_OFDM;
		else
			clockrate = AR_CLOCK_RATE_5GHZ_OFDM;
	} else if (ic->ic_curmode == IEEE80211_MODE_11B) {
		clockrate = AR_CLOCK_RATE_CCK;
	} else
		clockrate = AR_CLOCK_RATE_2GHZ_OFDM;
	if (sc->curchanext != NULL)
		clockrate *= 2;

	return (clockrate);
}

/* athn.c: athn_config_nonpcie */
static void
athn_config_nonpcie(struct athn_softc *sc)
{
	athn_write_serdes(sc, &ar_nonpcie_serdes);
}

/* athn.c: athn_config_pcie */
static void
athn_config_pcie(struct athn_softc *sc)
{
	/* Disable PLL when in L0s as well as receiver clock when in L1. */
	athn_write_serdes(sc, sc->serdes);

	DELAY(1000);
	/* Allow forcing of PCIe core into L1 state. */
	AR_SETBITS(sc, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);

#ifndef ATHN_PCIE_WAEN
	AR_WRITE(sc, AR_WA, sc->workaround);
#else
	AR_WRITE(sc, AR_WA, ATHN_PCIE_WAEN);
#endif
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_disable_interrupts */
static void
athn_disable_interrupts(struct athn_softc *sc)
{
	AR_WRITE(sc, AR_IER, 0);
	(void)AR_READ(sc, AR_IER);

	AR_WRITE(sc, AR_INTR_ASYNC_ENABLE, 0);
	(void)AR_READ(sc, AR_INTR_ASYNC_ENABLE);

	AR_WRITE(sc, AR_INTR_SYNC_ENABLE, 0);
	(void)AR_READ(sc, AR_INTR_SYNC_ENABLE);

	AR_WRITE(sc, AR_IMR, 0);

	AR_CLRBITS(sc, AR_IMR_S2, AR_IMR_S2_TIM | AR_IMR_S2_DTIM |
	    AR_IMR_S2_DTIMSYNC | AR_IMR_S2_CABEND | AR_IMR_S2_CABTO |
	    AR_IMR_S2_TSFOOR | AR_IMR_S2_GTT | AR_IMR_S2_CST);

	AR_CLRBITS(sc, AR_IMR_S5, AR_IMR_S5_TIM_TIMER);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_get_chipid */
static void
athn_get_chipid(struct athn_softc *sc)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_SREV);
	if (MS(reg, AR_SREV_ID) == 0xff) {
		sc->mac_ver = MS(reg, AR_SREV_VERSION2);
		sc->mac_rev = MS(reg, AR_SREV_REVISION2);
		if (!(reg & AR_SREV_TYPE2_HOST_MODE))
			sc->flags |= ATHN_FLAG_PCIE;
	} else {
		sc->mac_ver = MS(reg, AR_SREV_VERSION);
		sc->mac_rev = MS(reg, AR_SREV_REVISION);
		if (sc->mac_ver == AR_SREV_VERSION_5416_PCIE)
			sc->flags |= ATHN_FLAG_PCIE;
	}
}

/* athn.c: athn_get_delta_slope */
static void
athn_get_delta_slope(uint32_t coeff, uint32_t *exponent, uint32_t *mantissa)
{
#define COEFF_SCALE_SHIFT	24
	uint32_t exp, man;

	/* exponent = 14 - floor(log2(coeff)) */
	for (exp = 31; exp > 0; exp--)
		if (coeff & (1u << exp))
			break;
	exp = 14 - (exp - COEFF_SCALE_SHIFT);

	/* mantissa = floor(coeff * 2^exponent + 0.5) */
	man = coeff + (1u << (COEFF_SCALE_SHIFT - exp - 1));

	*mantissa = man >> (COEFF_SCALE_SHIFT - exp);
	*exponent = exp - 16;
#undef COEFF_SCALE_SHIFT
}

/* athn.c: athn_get_pier_ival */
static void
athn_get_pier_ival(uint8_t fbin, const uint8_t *pierfreq, int npiers,
    int *lo, int *hi)
{
	int i;

	for (i = 0; i < npiers; i++)
		if (pierfreq[i] == AR_BCHAN_UNUSED ||
		    pierfreq[i] > fbin)
			break;
	*hi = i;
	*lo = *hi - 1;
	if (*lo == -1)
		*lo = *hi;
	else if (*hi == npiers || pierfreq[*hi] == AR_BCHAN_UNUSED)
		*hi = *lo;
}

/* athn.c: athn_hw_reset */
static int
athn_hw_reset(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc, int init)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct athn_ops *ops = &sc->ops;
	uint32_t reg, def_ant, sta_id1, cfg_led, tsflo, tsfhi;
	int i, error, tsf_saved = 0;

	/* XXX not if already awake */
	if ((error = athn_set_power_awake(sc)) != 0) {
		printf("%s: could not wakeup chip\n", sc->sc_dev.dv_xname);
		return (error);
	}

	/* Preserve the antenna on a channel switch. */
	if ((def_ant = AR_READ(sc, AR_DEF_ANTENNA)) == 0)
		def_ant = 1;
	/* Preserve other registers. */
	sta_id1 = AR_READ(sc, AR_STA_ID1) & AR_STA_ID1_BASE_RATE_11B;
	cfg_led = AR_READ(sc, AR_CFG_LED) & (AR_CFG_LED_ASSOC_CTL_M |
	    AR_CFG_LED_MODE_SEL_M | AR_CFG_LED_BLINK_THRESH_SEL_M |
	    AR_CFG_LED_BLINK_SLOW);

	/* Mark PHY as inactive. */
	ops->disable_phy(sc);

	if (init && AR_SREV_9271(sc)) {
		AR_WRITE(sc, AR9271_RESET_POWER_DOWN_CONTROL,
		    AR9271_RADIO_RF_RST);
		DELAY(50);
	}
	if (AR_SREV_9280(sc) && (sc->flags & ATHN_FLAG_OLPC)) {
		/* Save TSF before it gets cleared. */
		tsfhi = AR_READ(sc, AR_TSF_U32);
		tsflo = AR_READ(sc, AR_TSF_L32);
		tsf_saved = 1;

		/* NB: RTC reset clears TSF. */
		error = athn_reset_power_on(sc);
	} else
		error = athn_reset(sc, 0);
	if (error != 0) {
		printf("%s: could not reset chip (error=%d)\n",
		    sc->sc_dev.dv_xname, error);
		return (error);
	}

	/* XXX not if already awake */
	if ((error = athn_set_power_awake(sc)) != 0) {
		printf("%s: could not wakeup chip\n", sc->sc_dev.dv_xname);
		return (error);
	}

	athn_init_pll(sc, c);
	ops->set_rf_mode(sc, c);

	if (sc->flags & ATHN_FLAG_RFSILENT) {
		/* Check that the radio is not disabled by hardware switch. */
		reg = ops->gpio_read(sc, sc->rfsilent_pin);
		if (sc->flags & ATHN_FLAG_RFSILENT_REVERSED)
			reg = !reg;
		if (!reg) {
			printf("%s: radio is disabled by hardware switch\n",
			    sc->sc_dev.dv_xname);
			return (EPERM);
		}
	}
	if (init && AR_SREV_9271(sc)) {
		AR_WRITE(sc, AR9271_RESET_POWER_DOWN_CONTROL,
		    AR9271_GATE_MAC_CTL);
		DELAY(50);
	}
	if (tsf_saved) {
		/* Restore TSF if it got cleared. */
		AR_WRITE(sc, AR_TSF_L32, tsflo);
		AR_WRITE(sc, AR_TSF_U32, tsfhi);
	}

	if (AR_SREV_9280_10_OR_LATER(sc))
		AR_SETBITS(sc, sc->gpio_input_en_off, AR_GPIO_JTAG_DISABLE);

	if (AR_SREV_9287_13_OR_LATER(sc) && !AR_SREV_9380_10_OR_LATER(sc))
		ar9287_1_3_enable_async_fifo(sc);

	/* Write init values to hardware. */
	ops->hw_init(sc, c, extc);

	/*
	 * Only >=AR9280 2.0 parts are capable of encrypting unicast
	 * management frames using CCMP.
	 */
	if (AR_SREV_9280_20_OR_LATER(sc)) {
		reg = AR_READ(sc, AR_AES_MUTE_MASK1);
		/* Do not mask the subtype field in management frames. */
		reg = RW(reg, AR_AES_MUTE_MASK1_FC0_MGMT, 0xff);
		reg = RW(reg, AR_AES_MUTE_MASK1_FC1_MGMT,
		    ~(IEEE80211_FC1_RETRY | IEEE80211_FC1_PWR_MGT |
		      IEEE80211_FC1_MORE_DATA));
		AR_WRITE(sc, AR_AES_MUTE_MASK1, reg);
	} else if (AR_SREV_9160_10_OR_LATER(sc)) {
		/* Disable hardware crypto for management frames. */
		AR_CLRBITS(sc, AR_PCU_MISC_MODE2,
		    AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE);
		AR_SETBITS(sc, AR_PCU_MISC_MODE2,
		    AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT);
	}

	if (ic->ic_curmode != IEEE80211_MODE_11B)
		ops->set_delta_slope(sc, c, extc);

	ops->spur_mitigate(sc, c, extc);
	ops->init_from_rom(sc, c, extc);

	/* XXX */
	AR_WRITE(sc, AR_STA_ID0, LE_READ_4(&ic->ic_myaddr[0]));
	AR_WRITE(sc, AR_STA_ID1, LE_READ_2(&ic->ic_myaddr[4]) |
	    sta_id1 | AR_STA_ID1_RTS_USE_DEF | AR_STA_ID1_CRPT_MIC_ENABLE);

	athn_set_opmode(sc);

	AR_WRITE(sc, AR_BSSMSKL, 0xffffffff);
	AR_WRITE(sc, AR_BSSMSKU, 0xffff);

	/* Restore previous antenna. */
	AR_WRITE(sc, AR_DEF_ANTENNA, def_ant);

	AR_WRITE(sc, AR_BSS_ID0, 0);
	AR_WRITE(sc, AR_BSS_ID1, 0);

	AR_WRITE(sc, AR_ISR, 0xffffffff);

	AR_WRITE(sc, AR_RSSI_THR, SM(AR_RSSI_THR_BM_THR, 7));

	if ((error = ops->set_synth(sc, c, extc)) != 0) {
		printf("%s: could not set channel\n", sc->sc_dev.dv_xname);
		return (error);
	}
	sc->curchan = c;
	sc->curchanext = extc;

	for (i = 0; i < AR_NUM_DCU; i++)
		AR_WRITE(sc, AR_DQCUMASK(i), 1u << i);

	athn_init_tx_queues(sc);

	/* Initialize interrupt mask. */
	sc->imask =
	    AR_IMR_TXDESC | AR_IMR_TXEOL |
	    AR_IMR_RXERR | AR_IMR_RXEOL | AR_IMR_RXORN |
	    AR_IMR_RXMINTR | AR_IMR_RXINTM |
	    AR_IMR_GENTMR | AR_IMR_BCNMISC;
	if (AR_SREV_9380_10_OR_LATER(sc))
		sc->imask |= AR_IMR_RXERR | AR_IMR_HP_RXOK;
#ifndef IEEE80211_STA_ONLY
	if (0 && ic->ic_opmode == IEEE80211_M_HOSTAP)
		sc->imask |= AR_IMR_MIB;
#endif
	AR_WRITE(sc, AR_IMR, sc->imask);
	AR_SETBITS(sc, AR_IMR_S2, AR_IMR_S2_GTT);
	AR_WRITE(sc, AR_INTR_SYNC_CAUSE, 0xffffffff);
	sc->isync = AR_INTR_SYNC_DEFAULT;
	if (sc->flags & ATHN_FLAG_RFSILENT)
		sc->isync |= AR_INTR_SYNC_GPIO_PIN(sc->rfsilent_pin);
	AR_WRITE(sc, AR_INTR_SYNC_ENABLE, sc->isync);
	AR_WRITE(sc, AR_INTR_SYNC_MASK, 0);
	if (AR_SREV_9380_10_OR_LATER(sc)) {
		AR_WRITE(sc, AR_INTR_PRIO_ASYNC_ENABLE, 0);
		AR_WRITE(sc, AR_INTR_PRIO_ASYNC_MASK, 0);
		AR_WRITE(sc, AR_INTR_PRIO_SYNC_ENABLE, 0);
		AR_WRITE(sc, AR_INTR_PRIO_SYNC_MASK, 0);
	}

	athn_init_qos(sc);

	AR_SETBITS(sc, AR_PCU_MISC, AR_PCU_MIC_NEW_LOC_ENA);

	athn_setsifs(sc);
	athn_updateslot(ic);
	athn_setclockrate(sc);
	if (AR_SREV_9287_13_OR_LATER(sc) && !AR_SREV_9380_10_OR_LATER(sc))
		ar9287_1_3_setup_async_fifo(sc);

	/* Disable sequence number generation in hardware. */
	AR_SETBITS(sc, AR_STA_ID1, AR_STA_ID1_PRESERVE_SEQNUM);

	athn_init_dma(sc);

	/* Program observation bus to see MAC interrupts. */
	AR_WRITE(sc, sc->obs_off, 8);

	/* Setup Rx interrupt mitigation. */
	AR_WRITE(sc, AR_RIMT, SM(AR_RIMT_FIRST, 2000) | SM(AR_RIMT_LAST, 500));

	/* Setup Tx interrupt mitigation. */
	AR_WRITE(sc, AR_TIMT, SM(AR_TIMT_FIRST, 2000) | SM(AR_TIMT_LAST, 500));

	/* Set maximum interrupt rate threshold (in micro seconds). */
	AR_WRITE(sc, AR_MIRT, SM(AR_MIRT_RATE_THRES, 2000));

	ops->init_baseband(sc);

	if ((error = athn_init_calib(sc, c, extc)) != 0) {
		printf("%s: could not initialize calibration\n",
		    sc->sc_dev.dv_xname);
		return (error);
	}

	ops->set_rxchains(sc);

	AR_WRITE(sc, AR_CFG_LED, cfg_led | AR_CFG_SCLK_32KHZ);

	if (sc->flags & ATHN_FLAG_USB) {
		if (AR_SREV_9271(sc))
			AR_WRITE(sc, AR_CFG, AR_CFG_SWRB | AR_CFG_SWTB);
		else
			AR_WRITE(sc, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
	}
#if BYTE_ORDER == BIG_ENDIAN
	else {
		/* Default is LE, turn on swapping for BE. */
		AR_WRITE(sc, AR_CFG, AR_CFG_SWTD | AR_CFG_SWRD);
	}
#endif
	AR_WRITE_BARRIER(sc);

	return (0);
}

/* athn.c: athn_init_dma */
static void
athn_init_dma(struct athn_softc *sc)
{
	uint32_t reg;

	if (!AR_SREV_9380_10_OR_LATER(sc)) {
		/* Set AHB not to do cacheline prefetches. */
		AR_SETBITS(sc, AR_AHB_MODE, AR_AHB_PREFETCH_RD_EN);
	}
	reg = AR_READ(sc, AR_TXCFG);
	/* Let MAC DMA reads be in 128-byte chunks. */
	reg = RW(reg, AR_TXCFG_DMASZ, AR_DMASZ_128B);

	/* Set initial Tx trigger level. */
	if (AR_SREV_9285(sc) || AR_SREV_9271(sc))
		reg = RW(reg, AR_TXCFG_FTRIG, AR_TXCFG_FTRIG_256B);
	else if (!AR_SREV_9380_10_OR_LATER(sc))
		reg = RW(reg, AR_TXCFG_FTRIG, AR_TXCFG_FTRIG_512B);
	AR_WRITE(sc, AR_TXCFG, reg);

	/* Let MAC DMA writes be in 128-byte chunks. */
	reg = AR_READ(sc, AR_RXCFG);
	reg = RW(reg, AR_RXCFG_DMASZ, AR_DMASZ_128B);
	AR_WRITE(sc, AR_RXCFG, reg);

	/* Setup Rx FIFO threshold to hold off Tx activities. */
	AR_WRITE(sc, AR_RXFIFO_CFG, 512);

	/* Reduce the number of entries in PCU TXBUF to avoid wrap around. */
	if (AR_SREV_9285(sc)) {
		AR_WRITE(sc, AR_PCU_TXBUF_CTRL,
		    AR9285_PCU_TXBUF_CTRL_USABLE_SIZE);
	} else if (!AR_SREV_9271(sc)) {
		AR_WRITE(sc, AR_PCU_TXBUF_CTRL,
		    AR_PCU_TXBUF_CTRL_USABLE_SIZE);
	}
	AR_WRITE_BARRIER(sc);

	/* Reset Tx status ring. */
	if (AR_SREV_9380_10_OR_LATER(sc))
		ar9003_reset_txsring(sc);
}

/* athn.c: athn_init_pll */
static void
athn_init_pll(struct athn_softc *sc, const struct ieee80211_channel *c)
{
	uint32_t pll;

	if (AR_SREV_9380_10_OR_LATER(sc)) {
		if (AR_SREV_9485(sc))
			AR_WRITE(sc, AR_RTC_PLL_CONTROL2, 0x886666);
		pll = SM(AR_RTC_9160_PLL_REFDIV, 0x5);
		pll |= SM(AR_RTC_9160_PLL_DIV, 0x2c);
	} else if (AR_SREV_9280_10_OR_LATER(sc)) {
		pll = SM(AR_RTC_9160_PLL_REFDIV, 0x05);
		if (c != NULL && IEEE80211_IS_CHAN_5GHZ(c)) {
			if (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)
				pll = 0x142c;
			else if (AR_SREV_9280_20(sc))
		 		pll = 0x2850;
			else
				pll |= SM(AR_RTC_9160_PLL_DIV, 0x28);
		} else
			pll |= SM(AR_RTC_9160_PLL_DIV, 0x2c);
	} else if (AR_SREV_9160_10_OR_LATER(sc)) {
		pll = SM(AR_RTC_9160_PLL_REFDIV, 0x05);
		if (c != NULL && IEEE80211_IS_CHAN_5GHZ(c))
			pll |= SM(AR_RTC_9160_PLL_DIV, 0x50);
		else
			pll |= SM(AR_RTC_9160_PLL_DIV, 0x58);
	} else {
		pll = AR_RTC_PLL_REFDIV_5 | AR_RTC_PLL_DIV2;
		if (c != NULL && IEEE80211_IS_CHAN_5GHZ(c))
			pll |= SM(AR_RTC_PLL_DIV, 0x0a);
		else
			pll |= SM(AR_RTC_PLL_DIV, 0x0b);
	}
	DPRINTFN(5, ("AR_RTC_PLL_CONTROL=0x%08x\n", pll));
	AR_WRITE(sc, AR_RTC_PLL_CONTROL, pll);
	if (AR_SREV_9271(sc)) {
		/* Switch core clock to 117MHz. */
		AR_WRITE_BARRIER(sc);
		DELAY(500);
		AR_WRITE(sc, AR9271_CLOCK_CONTROL, 0x304);
	}
	AR_WRITE_BARRIER(sc);
	DELAY(100);
	AR_WRITE(sc, AR_RTC_SLEEP_CLK, AR_RTC_FORCE_DERIVED_CLK);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_init_qos */
static void
athn_init_qos(struct athn_softc *sc)
{
	/* Initialize QoS settings. */
	AR_WRITE(sc, AR_MIC_QOS_CONTROL, 0x100aa);
	AR_WRITE(sc, AR_MIC_QOS_SELECT, 0x3210);
	AR_WRITE(sc, AR_QOS_NO_ACK,
	    SM(AR_QOS_NO_ACK_TWO_BIT, 2) |
	    SM(AR_QOS_NO_ACK_BIT_OFF, 5) |
	    SM(AR_QOS_NO_ACK_BYTE_OFF, 0));
	AR_WRITE(sc, AR_TXOP_X, AR_TXOP_X_VAL);
	/* Initialize TXOP for all TIDs. */
	AR_WRITE(sc, AR_TXOP_0_3,   0xffffffff);
	AR_WRITE(sc, AR_TXOP_4_7,   0xffffffff);
	AR_WRITE(sc, AR_TXOP_8_11,  0xffffffff);
	AR_WRITE(sc, AR_TXOP_12_15, 0xffffffff);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_init_tx_queues */
static void
athn_init_tx_queues(struct athn_softc *sc)
{
	int qid;

	for (qid = 0; qid < ATHN_QID_COUNT; qid++) {

		AR_WRITE(sc, AR_DRETRY_LIMIT(qid),
		    SM(AR_D_RETRY_LIMIT_STA_SH, 32) |
		    SM(AR_D_RETRY_LIMIT_STA_LG, 32) |
		    SM(AR_D_RETRY_LIMIT_FR_SH, 10));
		AR_WRITE(sc, AR_QMISC(qid),
		    AR_Q_MISC_DCU_EARLY_TERM_REQ);
		AR_WRITE(sc, AR_DMISC(qid),
		    SM(AR_D_MISC_BKOFF_THRESH, 2) |
		    AR_D_MISC_CW_BKOFF_EN | AR_D_MISC_FRAG_WAIT_EN);
	}

	/* Init beacon queue. */
	AR_SETBITS(sc, AR_QMISC(ATHN_QID_BEACON),
	    AR_Q_MISC_FSP_DBA_GATED | AR_Q_MISC_BEACON_USE |
	    AR_Q_MISC_CBR_INCR_DIS1);
	AR_SETBITS(sc, AR_DMISC(ATHN_QID_BEACON),
	    SM(AR_D_MISC_ARB_LOCKOUT_CNTRL,
	       AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL) |
	    AR_D_MISC_BEACON_USE |
	    AR_D_MISC_POST_FR_BKOFF_DIS);
	AR_WRITE(sc, AR_DLCL_IFS(ATHN_QID_BEACON),
	    SM(AR_D_LCL_IFS_CWMIN, 0) |
	    SM(AR_D_LCL_IFS_CWMAX, 0) |
	    SM(AR_D_LCL_IFS_AIFS,  1));

	/* Init CAB (Content After Beacon) queue. */
	AR_SETBITS(sc, AR_QMISC(ATHN_QID_CAB),
	    AR_Q_MISC_FSP_DBA_GATED | AR_Q_MISC_CBR_INCR_DIS1 |
	    AR_Q_MISC_CBR_INCR_DIS0);
	AR_SETBITS(sc, AR_DMISC(ATHN_QID_CAB),
	    SM(AR_D_MISC_ARB_LOCKOUT_CNTRL,
	       AR_D_MISC_ARB_LOCKOUT_CNTRL_GLOBAL));

	/* Init PS-Poll queue. */
	AR_SETBITS(sc, AR_QMISC(ATHN_QID_PSPOLL),
	    AR_Q_MISC_CBR_INCR_DIS1);

	/* Init UAPSD queue. */
	AR_SETBITS(sc, AR_DMISC(ATHN_QID_UAPSD),
	    AR_D_MISC_POST_FR_BKOFF_DIS);

	if (AR_SREV_9380_10_OR_LATER(sc)) {
		/* Enable MAC descriptor CRC check. */
		AR_WRITE(sc, AR_Q_DESC_CRCCHK, AR_Q_DESC_CRCCHK_EN);
	}
	/* Enable DESC interrupts for all Tx queues. */
	AR_WRITE(sc, AR_IMR_S0, 0x00ff0000);
	/* Enable EOL interrupts for all Tx queues except UAPSD. */
	AR_WRITE(sc, AR_IMR_S1, 0x00df0000);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_interpolate */
static int
athn_interpolate(int x, int x1, int y1, int x2, int y2)
{
	if (x1 == x2)	/* Prevents division by zero. */
		return (y1);
	/* Linear interpolation. */
	return (y1 + ((x - x1) * (y2 - y1)) / (x2 - x1));
}

/* athn.c: athn_reset */
static int
athn_reset(struct athn_softc *sc, int cold)
{
	int ntries;

	/* Set force wake. */
	AR_WRITE(sc, AR_RTC_FORCE_WAKE,
	    AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

	if (AR_READ(sc, AR_INTR_SYNC_CAUSE) &
	    (AR_INTR_SYNC_LOCAL_TIMEOUT | AR_INTR_SYNC_RADM_CPL_TIMEOUT)) {
		AR_WRITE(sc, AR_INTR_SYNC_ENABLE, 0);
		AR_WRITE(sc, AR_RC, AR_RC_HOSTIF |
		    (!AR_SREV_9380_10_OR_LATER(sc) ? AR_RC_AHB : 0));
	} else if (!AR_SREV_9380_10_OR_LATER(sc))
		AR_WRITE(sc, AR_RC, AR_RC_AHB);

	AR_WRITE(sc, AR_RTC_RC, AR_RTC_RC_MAC_WARM |
	    (cold ? AR_RTC_RC_MAC_COLD : 0));
	AR_WRITE_BARRIER(sc);
	DELAY(50);
	AR_WRITE(sc, AR_RTC_RC, 0);
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(AR_READ(sc, AR_RTC_RC) &
		      (AR_RTC_RC_MAC_WARM | AR_RTC_RC_MAC_COLD)))
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		DPRINTF(("RTC stuck in MAC reset\n"));
		return (ETIMEDOUT);
	}
	AR_WRITE(sc, AR_RC, 0);
	AR_WRITE_BARRIER(sc);
	return (0);
}

/* athn.c: athn_reset_key */
static void
athn_reset_key(struct athn_softc *sc, int entry)
{
	/*
	 * NB: Key cache registers access special memory area that requires
	 * two 32-bit writes to actually update the values in the internal
	 * memory.  Consequently, writes must be grouped by pair.
	 *
	 * All writes to registers with an offset of 0x0 or 0x8 write to a
	 * temporary register. A write to a register with an offset of 0x4
	 * or 0xc writes concatenates the written value with the value in
	 * the temporary register and writes the result to key cache memory.
	 * The actual written memory area is 50 bits wide.
	 */
	AR_WRITE(sc, AR_KEYTABLE_KEY0(entry), 0);
	AR_WRITE(sc, AR_KEYTABLE_KEY1(entry), 0);

	AR_WRITE(sc, AR_KEYTABLE_KEY2(entry), 0);
	AR_WRITE(sc, AR_KEYTABLE_KEY3(entry), 0);

	AR_WRITE(sc, AR_KEYTABLE_KEY4(entry), 0);
	AR_WRITE(sc, AR_KEYTABLE_TYPE(entry), AR_KEYTABLE_TYPE_CLR);

	AR_WRITE(sc, AR_KEYTABLE_MAC0(entry), 0);
	AR_WRITE(sc, AR_KEYTABLE_MAC1(entry), 0);

	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_reset_power_on */
static int
athn_reset_power_on(struct athn_softc *sc)
{
	int ntries;

	/* Set force wake. */
	AR_WRITE(sc, AR_RTC_FORCE_WAKE,
	    AR_RTC_FORCE_WAKE_EN | AR_RTC_FORCE_WAKE_ON_INT);

	if (!AR_SREV_9380_10_OR_LATER(sc)) {
		/* Make sure no DMA is active by doing an AHB reset. */
		AR_WRITE(sc, AR_RC, AR_RC_AHB);
	}
	/* RTC reset and clear. */
	AR_WRITE(sc, AR_RTC_RESET, 0);
	AR_WRITE_BARRIER(sc);
	DELAY(2);
	if (!AR_SREV_9380_10_OR_LATER(sc))
		AR_WRITE(sc, AR_RC, 0);
	AR_WRITE(sc, AR_RTC_RESET, 1);

	/* Poll until RTC is ON. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if ((AR_READ(sc, AR_RTC_STATUS) & AR_RTC_STATUS_M) ==
		    AR_RTC_STATUS_ON)
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		DPRINTF(("RTC not waking up\n"));
		return (ETIMEDOUT);
	}
	return (athn_reset(sc, 0));
}

/* athn.c: athn_set_opmode */
static void
athn_set_opmode(struct athn_softc *sc)
{
	uint32_t reg;

	switch (sc->sc_ic.ic_opmode) {
#ifndef IEEE80211_STA_ONLY
	case IEEE80211_M_HOSTAP:
		reg = AR_READ(sc, AR_STA_ID1);
		reg &= ~AR_STA_ID1_ADHOC;
		reg |= AR_STA_ID1_STA_AP | AR_STA_ID1_KSRCH_MODE;
		AR_WRITE(sc, AR_STA_ID1, reg);

		AR_CLRBITS(sc, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
	case IEEE80211_M_IBSS:
	case IEEE80211_M_AHDEMO:
		reg = AR_READ(sc, AR_STA_ID1);
		reg &= ~AR_STA_ID1_STA_AP;
		reg |= AR_STA_ID1_ADHOC | AR_STA_ID1_KSRCH_MODE;
		AR_WRITE(sc, AR_STA_ID1, reg);

		AR_SETBITS(sc, AR_CFG, AR_CFG_AP_ADHOC_INDICATION);
		break;
#endif
	default:
		reg = AR_READ(sc, AR_STA_ID1);
		reg &= ~(AR_STA_ID1_ADHOC | AR_STA_ID1_STA_AP);
		reg |= AR_STA_ID1_KSRCH_MODE;
		AR_WRITE(sc, AR_STA_ID1, reg);
		break;
	}
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_set_power_awake */
static int
athn_set_power_awake(struct athn_softc *sc)
{
	int ntries, error;

	/* Do a Power-On-Reset if shutdown. */
	if ((AR_READ(sc, AR_RTC_STATUS) & AR_RTC_STATUS_M) ==
	    AR_RTC_STATUS_SHUTDOWN) {
		if ((error = athn_reset_power_on(sc)) != 0)
			return (error);
		if (!AR_SREV_9380_10_OR_LATER(sc))
			athn_init_pll(sc, NULL);
	}
	AR_SETBITS(sc, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
	AR_WRITE_BARRIER(sc);
	DELAY(50);	/* Give chip the chance to awake. */

	/* Poll until RTC is ON. */
	for (ntries = 0; ntries < 4000; ntries++) {
		if ((AR_READ(sc, AR_RTC_STATUS) & AR_RTC_STATUS_M) ==
		    AR_RTC_STATUS_ON)
			break;
		DELAY(50);
		AR_SETBITS(sc, AR_RTC_FORCE_WAKE, AR_RTC_FORCE_WAKE_EN);
	}
	if (ntries == 4000) {
		DPRINTF(("RTC not waking up\n"));
		return (ETIMEDOUT);
	}

	AR_CLRBITS(sc, AR_STA_ID1, AR_STA_ID1_PWR_SAV);
	AR_WRITE_BARRIER(sc);
	return (0);
}

/* athn.c: athn_set_rxfilter */
static void
athn_set_rxfilter(struct athn_softc *sc, uint32_t rfilt)
{
	AR_WRITE(sc, AR_RX_FILTER, rfilt);

#ifdef notyet
	reg = AR_READ(sc, AR_PHY_ERR);
	reg &= (AR_PHY_ERR_RADAR | AR_PHY_ERR_OFDM_TIMING |
	    AR_PHY_ERR_CCK_TIMING);
	AR_WRITE(sc, AR_PHY_ERR, reg);
	if (reg != 0)
		AR_SETBITS(sc, AR_RXCFG, AR_RXCFG_ZLFDMA);
	else
		AR_CLRBITS(sc, AR_RXCFG, AR_RXCFG_ZLFDMA);
#else
	AR_WRITE(sc, AR_PHY_ERR, 0);
	AR_CLRBITS(sc, AR_RXCFG, AR_RXCFG_ZLFDMA);
#endif
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_setacktimeout */
static void
athn_setacktimeout(struct athn_softc *sc, struct ieee80211_channel *c, int slot)
{
	int ackto = athn_acktimeout(c, slot);
	uint32_t reg = AR_READ(sc, AR_TIME_OUT);
	reg = RW(reg, AR_TIME_OUT_ACK, ackto * athn_clock_rate(sc));
	AR_WRITE(sc, AR_TIME_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_setclockrate */
static void
athn_setclockrate(struct athn_softc *sc)
{
	int clockrate = athn_clock_rate(sc);
	uint32_t reg = AR_READ(sc, AR_USEC);
	reg = RW(reg, AR_USEC_USEC, clockrate - 1);
	AR_WRITE(sc, AR_USEC, reg);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_setctstimeout */
static void
athn_setctstimeout(struct athn_softc *sc, struct ieee80211_channel *c, int slot)
{
	int ctsto = athn_acktimeout(c, slot);
	int sifs = athn_chan_sifs(c);
	uint32_t reg = AR_READ(sc, AR_TIME_OUT);

	/* Workaround for early CTS timeouts. */
	if (IEEE80211_IS_CHAN_2GHZ(c))
		ctsto += 48 - sifs - slot;

	reg = RW(reg, AR_TIME_OUT_CTS, ctsto * athn_clock_rate(sc));
	AR_WRITE(sc, AR_TIME_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_setsifs */
static void
athn_setsifs(struct athn_softc *sc)
{
	int sifs = athn_chan_sifs(sc->sc_ic.ic_bss->ni_chan);
	AR_WRITE(sc, AR_D_GBL_IFS_SIFS, (sifs - 2) * athn_clock_rate(sc));
	AR_WRITE_BARRIER(sc);
}

/* athn.c: athn_start_noisefloor_calib */
static void
athn_start_noisefloor_calib(struct athn_softc *sc, int reset_history)
{
	

	if (reset_history)
		sc->nf_hist_nvalid = 0;

	sc->nf_calib_pending = 1;
	sc->nf_calib_ticks = (int)timer_ms();

	sc->ops.noisefloor_calib(sc);
}

/* athn.c: athn_updateslot */
static void
athn_updateslot(struct ieee80211com *ic)
{
	struct athn_softc *sc = ic->ic_softc;
	int slot;

	slot = (ic->ic_flags & IEEE80211_F_SHSLOT) ?
	    IEEE80211_DUR_DS_SHSLOT : IEEE80211_DUR_DS_SLOT;
	AR_WRITE(sc, AR_D_GBL_IFS_SLOT, slot * athn_clock_rate(sc));
	AR_WRITE_BARRIER(sc);

	athn_setacktimeout(sc, ic->ic_bss->ni_chan, slot);
	athn_setctstimeout(sc, ic->ic_bss->ni_chan, slot);
}

/* athn.c: athn_write_serdes */
static void
athn_write_serdes(struct athn_softc *sc, const struct athn_serdes *serdes)
{
	int i;

	/* Write sequence to Serializer/Deserializer. */
	for (i = 0; i < serdes->nvals; i++)
		AR_WRITE(sc, serdes->regs[i], serdes->vals[i]);
	AR_WRITE_BARRIER(sc);
}

/* athn.c: bounded noise-floor history helpers */
static int
athn_cap_noisefloor(struct athn_softc *sc, int nf)
{
	int16_t min, max;

	if (nf == 0 || nf == -1) /* invalid measurement */
		return AR_DEFAULT_NOISE_FLOOR;

	if (IEEE80211_IS_CHAN_2GHZ(sc->sc_ic.ic_bss->ni_chan)) {
		min = sc->cca_min_2g;
		max = sc->cca_max_2g;
	} else {
		min = sc->cca_min_5g;
		max = sc->cca_max_5g;
	}

	if (nf < min)
		return min;
	if (nf > max)
		return max;

	return nf;
}

static int
athn_nf_hist_mid(int *nf_vals, int nvalid)
{
	int nf_sorted[ATHN_NF_CAL_HIST_MAX];
	int i, j, nf;

	if (nvalid <= 1)
		return nf_vals[0];

	for (i = 0; i < nvalid; i++)
		nf_sorted[i] = nf_vals[i];

	for (i = 0; i < nvalid; i++) {
		for (j = 1; j < nvalid - i; j++) {
			if (nf_sorted[j] > nf_sorted[j - 1]) {
				nf = nf_sorted[j];
				nf_sorted[j] = nf_sorted[j - 1];
				nf_sorted[j - 1] = nf;
			}
		}
	}

	return nf_sorted[nvalid / 2];
}

static void
athn_filter_noisefloor(struct athn_softc *sc)
{
	int nf_vals[ATHN_NF_CAL_HIST_MAX];
	int nf_ext_vals[ATHN_NF_CAL_HIST_MAX];
	int i, cur, n;

	for (i = 0; i < sc->nrxchains; i++) {
		if (sc->nf_hist_cur > 0)
			cur = sc->nf_hist_cur - 1;
		else
			cur = ATHN_NF_CAL_HIST_MAX - 1;
		for (n = 0; n < sc->nf_hist_nvalid; n++) {
			nf_vals[n] = sc->nf_hist[cur].nf[i];
			nf_ext_vals[n] = sc->nf_hist[cur].nf_ext[i];
			if (++cur >= ATHN_NF_CAL_HIST_MAX)
				cur = 0;
		}
		sc->nf_priv[i] = athn_cap_noisefloor(sc,
		    athn_nf_hist_mid(nf_vals, sc->nf_hist_nvalid));
		sc->nf_ext_priv[i] = athn_cap_noisefloor(sc,
		    athn_nf_hist_mid(nf_ext_vals, sc->nf_hist_nvalid));
	}
}

