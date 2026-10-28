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
static void
ar9003_apply_noisefloor(struct athn_softc *sc);
static int
ar9003_attach(struct athn_softc *sc);
static void
ar9003_bb_load_noisefloor(struct athn_softc *sc);
static void
ar9003_calib_iq(struct athn_softc *sc);
static int
ar9003_calib_tx_iq(struct athn_softc *sc);
static void
ar9003_disable_ofdm_weak_signal(struct athn_softc *sc);
static void
ar9003_disable_phy(struct athn_softc *sc);
static void
ar9003_do_calib(struct athn_softc *sc);
static void
ar9003_do_noisefloor_calib(struct athn_softc *sc);
static void
ar9003_enable_antenna_diversity(struct athn_softc *sc);
static void
ar9003_enable_ofdm_weak_signal(struct athn_softc *sc);
static int
ar9003_find_rom(struct athn_softc *sc);
static void
ar9003_get_ht_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const uint8_t *fbins,
    const struct ar_cal_target_power_ht *tgt, int nchans, uint8_t tpow[14]);
static int
ar9003_get_iq_corr(struct athn_softc *sc, int32_t res[6], int32_t coeff[2]);
static void
ar9003_get_lg_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const uint8_t *fbins,
    const struct ar_cal_target_power_leg *tgt, int nchans, uint8_t tpow[4]);
static int
ar9003_get_noisefloor(struct athn_softc *sc);
static void
ar9003_gpio_config_input(struct athn_softc *sc, int pin);
static void
ar9003_gpio_config_output(struct athn_softc *sc, int pin, int type);
static int
ar9003_gpio_read(struct athn_softc *sc, int pin);
static void
ar9003_gpio_write(struct athn_softc *sc, int pin, int set);
static void
ar9003_hw_init(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9003_init_baseband(struct athn_softc *sc);
static int
ar9003_init_calib(struct athn_softc *sc);
static void
ar9003_init_chains(struct athn_softc *sc);
static void
ar9003_init_noisefloor_calib(struct athn_softc *sc);
static void
ar9003_next_calib(struct athn_softc *sc);
static int
ar9003_read_eep_data(struct athn_softc *sc, uint32_t addr, void *buf, int len);
static int
ar9003_read_eep_word(struct athn_softc *sc, uint32_t addr, uint16_t *val);
static void
ar9003_read_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext);
static int
ar9003_read_otp_data(struct athn_softc *sc, uint32_t addr, void *buf, int len);
static int
ar9003_read_otp_word(struct athn_softc *sc, uint32_t addr, uint32_t *val);
static int
ar9003_read_rom(struct athn_softc *sc);
static void
ar9003_reset_rx_gain(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar9003_reset_tx_gain(struct athn_softc *sc, struct ieee80211_channel *c);
static int
ar9003_restore_rom_block(struct athn_softc *sc, uint8_t alg, uint8_t ref,
    const uint8_t *buf, int len);
static void
ar9003_rf_bus_release(struct athn_softc *sc);
static int
ar9003_rf_bus_request(struct athn_softc *sc);
static void
ar9003_rfsilent_init(struct athn_softc *sc);
static void
ar9003_set_cck_weak_signal(struct athn_softc *sc, int high);
static void
ar9003_set_delta_slope(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9003_set_firstep_level(struct athn_softc *sc, int level);
static void
ar9003_set_noise_immunity_level(struct athn_softc *sc, int level);
static void
ar9003_set_phy(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9003_set_rf_mode(struct athn_softc *sc, struct ieee80211_channel *c);
static void
ar9003_set_rxchains(struct athn_softc *sc);
static void
ar9003_set_spur_immunity_level(struct athn_softc *sc, int level);
static __inline uint32_t
ar9003_synth_delay(struct athn_softc *sc);
static void
ar9003_write_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext);
static void
ar9003_write_txpower(struct athn_softc *sc, int16_t power[ATHN_POWER_COUNT]);
static int
ar9380_attach(struct athn_softc *sc);
static void
ar9380_get_correction(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int *corr, int *temp);
static void
ar9380_get_paprd_masks(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *ht20mask, uint32_t *ht40mask);
static const uint8_t *
ar9380_get_rom_template(struct athn_softc *sc, uint8_t ref);
static void
ar9380_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9380_init_swreg(struct athn_softc *sc);
static void
ar9380_set_correction(struct athn_softc *sc, struct ieee80211_channel *c);
static int
ar9380_set_synth(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9380_set_txpower(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9380_setup(struct athn_softc *sc);
static void
ar9380_spur_mitigate(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9380_spur_mitigate_cck(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9380_spur_mitigate_ofdm(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
ar9380_swap_rom(struct athn_softc *sc);
static void
ar9485_init_swreg(struct athn_softc *sc);
static int
ar9485_pmu_write(struct athn_softc *sc, uint32_t addr, uint32_t val);
static int
athn_acktimeout(struct ieee80211_channel *c, int slot);
static int
athn_cap_noisefloor(struct athn_softc *sc, int nf);
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
athn_filter_noisefloor(struct athn_softc *sc);
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
athn_nf_hist_mid(int *nf_vals, int nvalid);
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

/* ar9003.c: ar9003_apply_noisefloor */
static void
ar9003_apply_noisefloor(struct athn_softc *sc)
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

	ar9003_bb_load_noisefloor(sc);

	if (agc_nfcal & AR_PHY_AGC_CONTROL_NF) {
		/* Restart interrupted NF calibration. */
		AR_SETBITS(sc, AR_PHY_AGC_CONTROL, agc_nfcal);
		AR_WRITE_BARRIER(sc);
	}
}

/* ar9003.c: ar9003_attach */
static int
ar9003_attach(struct athn_softc *sc)
{
	struct athn_ops *ops = &sc->ops;
	int error;

	/* Set callbacks for AR9003 family. */
	ops->gpio_read = ar9003_gpio_read;
	ops->gpio_write = ar9003_gpio_write;
	ops->gpio_config_input = ar9003_gpio_config_input;
	ops->gpio_config_output = ar9003_gpio_config_output;
	ops->rfsilent_init = ar9003_rfsilent_init;


	ops->set_rf_mode = ar9003_set_rf_mode;
	ops->rf_bus_request = ar9003_rf_bus_request;
	ops->rf_bus_release = ar9003_rf_bus_release;
	ops->set_phy = ar9003_set_phy;
	ops->set_delta_slope = ar9003_set_delta_slope;
	ops->enable_antenna_diversity = ar9003_enable_antenna_diversity;
	ops->init_baseband = ar9003_init_baseband;
	ops->disable_phy = ar9003_disable_phy;
	ops->set_rxchains = ar9003_set_rxchains;
	ops->noisefloor_calib = ar9003_do_noisefloor_calib;
	ops->init_noisefloor_calib = ar9003_init_noisefloor_calib;
	ops->get_noisefloor = ar9003_get_noisefloor;
	ops->apply_noisefloor = ar9003_apply_noisefloor;
	ops->do_calib = ar9003_do_calib;
	ops->next_calib = ar9003_next_calib;
	ops->hw_init = ar9003_hw_init;

	ops->set_noise_immunity_level = ar9003_set_noise_immunity_level;
	ops->enable_ofdm_weak_signal = ar9003_enable_ofdm_weak_signal;
	ops->disable_ofdm_weak_signal = ar9003_disable_ofdm_weak_signal;
	ops->set_cck_weak_signal = ar9003_set_cck_weak_signal;
	ops->set_firstep_level = ar9003_set_firstep_level;
	ops->set_spur_immunity_level = ar9003_set_spur_immunity_level;

	/* Set MAC registers offsets. */
	sc->obs_off = AR_OBS;
	sc->gpio_input_en_off = AR_GPIO_INPUT_EN_VAL;

	if (!(sc->flags & ATHN_FLAG_PCIE))
		athn_config_nonpcie(sc);
	else
		athn_config_pcie(sc);

	/* Determine ROM type and location. */
	if ((error = ar9003_find_rom(sc)) != 0) {
		printf("%s: could not find ROM\n", sc->sc_dev.dv_xname);
		return (error);
	}
	/* Read entire ROM content in memory. */
	if ((error = ar9003_read_rom(sc)) != 0) {
		printf("%s: could not read ROM\n", sc->sc_dev.dv_xname);
		return (error);
	}

	/* Determine if it is a non-enterprise AR9003 card. */
	if (AR_READ(sc, AR_ENT_OTP) & AR_ENT_OTP_MPSD)
		sc->flags |= ATHN_FLAG_NON_ENTERPRISE;

	ops->setup(sc);
	return (0);
}

/* ar9003.c: ar9003_bb_load_noisefloor */
static void
ar9003_bb_load_noisefloor(struct athn_softc *sc)
{
	int16_t nf[AR_MAX_CHAINS], nf_ext[AR_MAX_CHAINS];
	int i, ntries;

	/* Write filtered noisefloor values. */
	for (i = 0; i < sc->nrxchains; i++) {
		nf[i] = sc->nf_priv[i] * 2;
		nf_ext[i] = sc->nf_ext_priv[i] * 2;
	}
	ar9003_write_noisefloor(sc, nf, nf_ext);

	/* Load filtered noisefloor values into baseband. */
	AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	AR_CLRBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	/* Wait for load to complete. */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (!(AR_READ(sc, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF))
			break;
		DELAY(10);
	}
	if (ntries == 1000) {
		DPRINTF(("failed to load noisefloor values\n"));
		return;
	}

	/* Restore noisefloor values to initial (max) values. */
	for (i = 0; i < AR_MAX_CHAINS; i++)
		nf[i] = nf_ext[i] = -50 * 2;
	ar9003_write_noisefloor(sc, nf, nf_ext);
}

/* ar9003.c: ar9003_calib_iq */
static void
ar9003_calib_iq(struct athn_softc *sc)
{
	struct athn_iq_cal *cal;
	uint32_t reg, i_coff_denom, q_coff_denom;
	int64_t i_coff, q_coff;
	int i, iq_corr_neg;

	for (i = 0; i < AR_MAX_CHAINS; i++) {
		cal = &sc->calib.iq[i];

		/* Read IQ calibration measures (clear on read). */
		cal->pwr_meas_i = AR_READ(sc, AR_PHY_IQ_ADC_MEAS_0_B(i));
		cal->pwr_meas_q = AR_READ(sc, AR_PHY_IQ_ADC_MEAS_1_B(i));
		cal->iq_corr_meas =
		    (int32_t)AR_READ(sc, AR_PHY_IQ_ADC_MEAS_2_B(i));
	}

	for (i = 0; i < sc->nrxchains; i++) {
		cal = &sc->calib.iq[i];

		if (cal->pwr_meas_q == 0)
			continue;

		iq_corr_neg = cal->iq_corr_meas < 0;
        uint32_t magnitude=iq_corr_neg?(uint32_t)(-(int64_t)cal->iq_corr_meas):(uint32_t)cal->iq_corr_meas;

		i_coff_denom =
		    (cal->pwr_meas_i / 2 + cal->pwr_meas_q / 2) / 256;
		q_coff_denom = cal->pwr_meas_q / 64;

		if (i_coff_denom == 0 || q_coff_denom == 0)
			continue;	/* Prevents division by zero. */

		i_coff = magnitude / i_coff_denom;
		q_coff = (int64_t)(cal->pwr_meas_i / q_coff_denom) - 64;

		if (i_coff > 63)
			i_coff = 63;
		else if (i_coff < -63)
			i_coff = -63;
		/* Negate i_coff if iq_corr_meas is positive. */
		if (!iq_corr_neg)
			i_coff = -i_coff;
		if (q_coff > 63)
			q_coff = 63;
		else if (q_coff < -63)
			q_coff = -63;

		DPRINTFN(2, ("IQ calibration for chain %d\n", i));
		reg = AR_READ(sc, AR_PHY_RX_IQCAL_CORR_B(i));
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_IQCORR_Q_I_COFF, i_coff);
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_IQCORR_Q_Q_COFF, q_coff);
		AR_WRITE(sc, AR_PHY_RX_IQCAL_CORR_B(i), reg);
	}

	/* Apply new settings. */
	AR_SETBITS(sc, AR_PHY_RX_IQCAL_CORR_B(0),
	    AR_PHY_RX_IQCAL_CORR_IQCORR_ENABLE);
	AR_WRITE_BARRIER(sc);

	/* IQ calibration done. */
	sc->cur_calib_mask &= ~ATHN_CAL_IQ;
	memset(&sc->calib, 0, sizeof(sc->calib));
}

/* ar9003.c: ar9003_calib_tx_iq */
static int
ar9003_calib_tx_iq(struct athn_softc *sc)
{
	uint32_t reg;
	int32_t res[6], coeff[2];
	int i, j, ntries;

	reg = AR_READ(sc, AR_PHY_TX_IQCAL_CONTROL_1);
	reg = RW(reg, AR_PHY_TX_IQCAQL_CONTROL_1_IQCORR_I_Q_COFF_DELPT, DELPT);
	AR_WRITE(sc, AR_PHY_TX_IQCAL_CONTROL_1, reg);

	/* Start Tx IQ calibration. */
	AR_SETBITS(sc, AR_PHY_TX_IQCAL_START, AR_PHY_TX_IQCAL_START_DO_CAL);
	/* Wait for completion. */
	for (ntries = 0; ntries < 10000; ntries++) {
		reg = AR_READ(sc, AR_PHY_TX_IQCAL_START);
		if (!(reg & AR_PHY_TX_IQCAL_START_DO_CAL))
			break;
		DELAY(10);
	}
	if (ntries == 10000)
		return (ETIMEDOUT);

	for (i = 0; i < sc->ntxchains; i++) {
		/* Read Tx IQ calibration status for this chain. */
		reg = AR_READ(sc, AR_PHY_TX_IQCAL_STATUS_B(i));
		if (reg & AR_PHY_TX_IQCAL_STATUS_FAILED)
			return (EIO);
		/*
		 * Read Tx IQ calibration results for this chain.
		 * This consists in twelve signed 12-bit values.
		 */
		for (j = 0; j < 3; j++) {
			AR_CLRBITS(sc, AR_PHY_CHAN_INFO_MEMORY,
			    AR_PHY_CHAN_INFO_TAB_S2_READ);
			reg = AR_READ(sc, AR_PHY_CHAN_INFO_TAB(i, j));
			res[j * 2 + 0] = reg;

			AR_SETBITS(sc, AR_PHY_CHAN_INFO_MEMORY,
			    AR_PHY_CHAN_INFO_TAB_S2_READ);
			reg = AR_READ(sc, AR_PHY_CHAN_INFO_TAB(i, j));
			res[j * 2 + 1] = reg & 0xffff;
		}

		/* Compute Tx IQ correction. */
		if (ar9003_get_iq_corr(sc, res, coeff) != 0)
			return (EIO);

		/* Write Tx IQ correction coefficients. */
		reg = AR_READ(sc, AR_PHY_TX_IQCAL_CORR_COEFF_01_B(i));
		reg = RW(reg, AR_PHY_TX_IQCAL_CORR_COEFF_01_COEFF_TABLE,
		    coeff[0]);
		AR_WRITE(sc, AR_PHY_TX_IQCAL_CORR_COEFF_01_B(i), reg);

		reg = AR_READ(sc, AR_PHY_RX_IQCAL_CORR_B(i));
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_Q_COFF,
		    coeff[1] >> 7);
		reg = RW(reg, AR_PHY_RX_IQCAL_CORR_LOOPBACK_IQCORR_Q_I_COFF,
		    coeff[1]);
		AR_WRITE(sc, AR_PHY_RX_IQCAL_CORR_B(i), reg);
		AR_WRITE_BARRIER(sc);
	}

	/* Enable Tx IQ correction. */
	AR_SETBITS(sc, AR_PHY_TX_IQCAL_CONTROL_3,
	    AR_PHY_TX_IQCAL_CONTROL_3_IQCORR_EN);
	AR_SETBITS(sc, AR_PHY_RX_IQCAL_CORR_B(0),
	    AR_PHY_RX_IQCAL_CORR_B0_LOOPBACK_IQCORR_EN);
	AR_WRITE_BARRIER(sc);
	return (0);
}

/* ar9003.c: ar9003_disable_ofdm_weak_signal */
static void
ar9003_disable_ofdm_weak_signal(struct athn_softc *sc)
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

/* ar9003.c: ar9003_disable_phy */
static void
ar9003_disable_phy(struct athn_softc *sc)
{
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_do_calib */
static void
ar9003_do_calib(struct athn_softc *sc)
{
	uint32_t reg;

	if (sc->cur_calib_mask & ATHN_CAL_IQ) {
		reg = AR_READ(sc, AR_PHY_TIMING4);
		reg = RW(reg, AR_PHY_TIMING4_IQCAL_LOG_COUNT_MAX, 10);
		AR_WRITE(sc, AR_PHY_TIMING4, reg);
		AR_WRITE(sc, AR_PHY_CALMODE, AR_PHY_CALMODE_IQ);
		AR_SETBITS(sc, AR_PHY_TIMING4, AR_PHY_TIMING4_DO_CAL);
		AR_WRITE_BARRIER(sc);
	} else if (sc->cur_calib_mask & ATHN_CAL_TEMP) {
		AR_SETBITS(sc, AR_PHY_65NM_CH0_THERM,
		    AR_PHY_65NM_CH0_THERM_LOCAL);
		AR_SETBITS(sc, AR_PHY_65NM_CH0_THERM,
		    AR_PHY_65NM_CH0_THERM_START);
		AR_WRITE_BARRIER(sc);
	}
}

/* ar9003.c: ar9003_do_noisefloor_calib */
static void
ar9003_do_noisefloor_calib(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_ENABLE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NO_UPDATE_NF);
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_enable_antenna_diversity */
static void
ar9003_enable_antenna_diversity(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_CCK_DETECT,
	    AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_enable_ofdm_weak_signal */
static void
ar9003_enable_ofdm_weak_signal(struct athn_softc *sc)
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

/* ar9003.c: ar9003_find_rom */
static int
ar9003_find_rom(struct athn_softc *sc)
{
 static const unsigned capacities[]={1024,4096,512,1024,512};
 for(unsigned i=0;i<nitems(capacities);i++){
  sc->ops.read_rom_data=i<3?ar9003_read_eep_data:ar9003_read_otp_data;
  sc->rom_bytes=capacities[i];sc->eep_base=sc->rom_bytes-1;
  uint8_t h[4];
  if(sc->ops.read_rom_data(sc,sc->eep_base,h,4))continue;
  uint32_t header=LE_READ_4(h);
  if(header==0||header==0xffffffffu)continue;
  unsigned alg=h[0]>>5,len=((h[1]&127)<<4)|(h[2]>>4);
  if((alg!=AR_EEP_COMPRESS_NONE&&alg!=AR_EEP_COMPRESS_BLOCK)||len>2047||len+6>sc->rom_bytes)continue;
  sc->eep_size=sizeof(struct ar9380_eeprom);return 0;
 }
 return EIO;
}


/* Native legacy profile: take the most restrictive matching board CTL edge. */
static int ar9380_ctl_limit(const struct ar9380_eeprom*e,unsigned fbin,unsigned mode){
 int limit=63,found=0;
 for(unsigned row=0;row<AR9380_NUM_CTLS_2G;row++){
  if(!e->ctlIndex_2G[row]||(e->ctlIndex_2G[row]&15)!=mode)continue;
  for(unsigned edge=0;edge<AR9380_NUM_BAND_EDGES_2G;edge++){
   unsigned freq=e->ctl_freqbin_2G[row][edge];
   if(!freq||freq==255)break;
   unsigned data=e->ctlPowerData_2G[row].ctlEdges[edge];
   if(fbin==freq){
    limit=MIN(limit,(int)(data&63));found=1;break;
   }
   if(fbin<freq){
    if(edge&& (e->ctlPowerData_2G[row].ctlEdges[edge-1]&0xc0)){
     limit=MIN(limit,(int)(e->ctlPowerData_2G[row].ctlEdges[edge-1]&63));found=1;
    }
    break;
   }
  }
 }
 return found?limit:-1;
}
/* ar9003.c: ar9003_get_ht_tpow */
static void
ar9003_get_ht_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const uint8_t *fbins,
    const struct ar_cal_target_power_ht *tgt, int nchans, uint8_t tpow[14])
{
	uint8_t fbin;
	int i, delta, lo, hi;

	lo = hi = -1;
	fbin = athn_chan2fbin(c);
	for (i = 0; i < nchans && fbins[i] != AR_BCHAN_UNUSED; i++) {
		delta = fbin - fbins[i];
		/* Find the largest sample that is <= our frequency. */
		if (delta >= 0 && (lo == -1 || delta < fbin - fbins[lo]))
			lo = i;
		/* Find the smallest sample that is >= our frequency. */
		if (delta <= 0 && (hi == -1 || delta > fbin - fbins[hi]))
			hi = i;
	}
	if (lo == -1)
		lo = hi;
	else if (hi == -1)
		hi = lo;
	/* Interpolate values. */
	for (i = 0; i < 14; i++) {
		tpow[i] = athn_interpolate(fbin,
		    fbins[lo], tgt[lo].tPow2x[i],
		    fbins[hi], tgt[hi].tPow2x[i]);
	}
	/* XXX Apply conformance test limit. */
}

/* ar9003.c: ar9003_get_iq_corr */
static int
ar9003_get_iq_corr(struct athn_softc *sc, int32_t res[6], int32_t coeff[2])
{
/* Sign-extends 12-bit value (assumes upper bits are zeroes). */
#define SIGN_EXT(v)	(((v) ^ 0x800) - 0x800)
#define SCALE		(1u << 15)
#define SHIFT		(1u <<  8)
	struct {
		int32_t	m, p, c;
	} val[2][2];
	int64_t mag[2][2], phs[2][2], cos[2], sin[2];
	int64_t min, max, div, f1, f2, f3, m, p, c;
	int64_t txmag, txphs, rxmag, rxphs;
	int64_t q_coff, i_coff;
	int i, j;

	/* Extract our twelve signed 12-bit values from res[] array. */
	val[0][0].m = res[0] & 0xfff;
	val[0][0].p = (res[0] >> 12) & 0xfff;
	val[0][0].c = ((res[0] >> 24) & 0xff) | (res[1] & 0xf) << 8;

	val[0][1].m = (res[1] >> 4) & 0xfff;
	val[0][1].p = res[2] & 0xfff;
	val[0][1].c = (res[2] >> 12) & 0xfff;

	val[1][0].m = ((res[2] >> 24) & 0xff) | (res[3] & 0xf) << 8;
	val[1][0].p = (res[3] >> 4) & 0xfff;
	val[1][0].c = res[4] & 0xfff;

	val[1][1].m = (res[4] >> 12) & 0xfff;
	val[1][1].p = ((res[4] >> 24) & 0xff) | (res[5] & 0xf) << 8;
	val[1][1].c = (res[5] >> 4) & 0xfff;

	for (i = 0; i < 2; i++) {
		for (j = 0; j < 2; j++) {
			m = SIGN_EXT(val[i][j].m);
			p = SIGN_EXT(val[i][j].p);
			c = SIGN_EXT(val[i][j].c);

			if (p == 0)
				return (1);	/* Prevent division by 0. */

			mag[i][j] = (m * SCALE) / p;
			phs[i][j] = (c * SCALE) / p;
		}
		sin[i] = ((mag[i][0] - mag[i][1]) * SHIFT) / DELPT;
		cos[i] = ((phs[i][0] - phs[i][1]) * SHIFT) / DELPT;
		/* Find magnitude by approximation. */
		min = MIN((sin[i]<0?-sin[i]:sin[i]), (cos[i]<0?-cos[i]:cos[i]));
		max = MAX((sin[i]<0?-sin[i]:sin[i]), (cos[i]<0?-cos[i]:cos[i]));
		div = max - (max / 32) + (min / 8) + (min / 4);
		if (div == 0)
			return (1);	/* Prevent division by 0. */
		/* Normalize sin and cos by magnitude. */
		sin[i] = (sin[i] * SCALE) / div;
		cos[i] = (cos[i] * SCALE) / div;
	}

	/* Compute IQ mismatch (solve 4x4 linear equation). */
	f1 = cos[0] - cos[1];
	f3 = sin[0] - sin[1];
	f2 = (f1 * f1 + f3 * f3) / SCALE;
	if (f2 == 0)
		return (1);	/* Prevent division by 0. */

	/* Compute Tx magnitude mismatch. */
	txmag = (f1 * ( mag[0][0] - mag[1][0]) +
		 f3 * ( phs[0][0] - phs[1][0])) / f2;
	/* Compute Tx phase mismatch. */
	txphs = (f3 * (-mag[0][0] + mag[1][0]) +
		 f1 * ( phs[0][0] - phs[1][0])) / f2;

	if (txmag == SCALE)
		return (1);	/* Prevent division by 0. */

	/* Compute Rx magnitude mismatch. */
	rxmag = mag[0][0] - (cos[0] * txmag + sin[0] * txphs) / SCALE;
	/* Compute Rx phase mismatch. */
	rxphs = phs[0][0] + (sin[0] * txmag - cos[0] * txphs) / SCALE;

	if (-rxmag == SCALE)
		return (1);	/* Prevent division by 0. */

	txmag = (txmag * SCALE) / (SCALE - txmag);
	txphs = -txphs;

	q_coff = (txmag * 128) / SCALE;
	if (q_coff < -63)
		q_coff = -63;
	else if (q_coff > 63)
		q_coff = 63;
	i_coff = (txphs * 256) / SCALE;
	if (i_coff < -63)
		i_coff = -63;
	else if (i_coff > 63)
		i_coff = 63;
	coeff[0] = ((q_coff & 127) << 7) | (i_coff & 127);

	rxmag = (-rxmag * SCALE) / (SCALE + rxmag);
	rxphs = -rxphs;

	q_coff = (rxmag * 128) / SCALE;
	if (q_coff < -63)
		q_coff = -63;
	else if (q_coff > 63)
		q_coff = 63;
	i_coff = (rxphs * 256) / SCALE;
	if (i_coff < -63)
		i_coff = -63;
	else if (i_coff > 63)
		i_coff = 63;
	coeff[1] = ((q_coff & 127) << 7) | (i_coff & 127);

	return (0);
#undef SHIFT
#undef SCALE
#undef SIGN_EXT
}

/* ar9003.c: ar9003_get_lg_tpow */
static void
ar9003_get_lg_tpow(struct athn_softc *sc, struct ieee80211_channel *c,
    uint8_t ctl, const uint8_t *fbins,
    const struct ar_cal_target_power_leg *tgt, int nchans, uint8_t tpow[4])
{
	uint8_t fbin;
	int i, delta, lo, hi;

	lo = hi = -1;
	fbin = athn_chan2fbin(c);
	for (i = 0; i < nchans && fbins[i] != AR_BCHAN_UNUSED; i++) {
		delta = fbin - fbins[i];
		/* Find the largest sample that is <= our frequency. */
		if (delta >= 0 && (lo == -1 || delta < fbin - fbins[lo]))
			lo = i;
		/* Find the smallest sample that is >= our frequency. */
		if (delta <= 0 && (hi == -1 || delta > fbin - fbins[hi]))
			hi = i;
	}
	if (lo == -1)
		lo = hi;
	else if (hi == -1)
		hi = lo;
	/* Interpolate values. */
	for (i = 0; i < 4; i++) {
		tpow[i] = athn_interpolate(fbin,
		    fbins[lo], tgt[lo].tPow2x[i],
		    fbins[hi], tgt[hi].tPow2x[i]);
	}
	/* XXX Apply conformance test limit. */
}

/* ar9003.c: ar9003_get_noisefloor */
static int
ar9003_get_noisefloor(struct athn_softc *sc)
{
	int16_t nf[AR_MAX_CHAINS], nf_ext[AR_MAX_CHAINS];
	int i;

	if (AR_READ(sc, AR_PHY_AGC_CONTROL) & AR_PHY_AGC_CONTROL_NF) {
		/* Noisefloor calibration not finished. */
		return 0;
	}
	/* Noisefloor calibration is finished. */
	ar9003_read_noisefloor(sc, nf, nf_ext);

	/* Update noisefloor history. */
	for (i = 0; i < sc->nrxchains; i++) {
		sc->nf_hist[sc->nf_hist_cur].nf[i] = nf[i];
		sc->nf_hist[sc->nf_hist_cur].nf_ext[i] = nf_ext[i];
	}
	if (++sc->nf_hist_cur >= ATHN_NF_CAL_HIST_MAX)
		sc->nf_hist_cur = 0;
	return 1;
}

/* ar9003.c: ar9003_gpio_config_input */
static void
ar9003_gpio_config_input(struct athn_softc *sc, int pin)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_GPIO_OE_OUT);
	reg &= ~(AR_GPIO_OE_OUT_DRV_M << (pin * 2));
	reg |= AR_GPIO_OE_OUT_DRV_NO << (pin * 2);
	AR_WRITE(sc, AR_GPIO_OE_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_gpio_config_output */
static void
ar9003_gpio_config_output(struct athn_softc *sc, int pin, int type)
{
	uint32_t reg;
	int mux, off;

	mux = pin / 6;
	off = pin % 6;

	reg = AR_READ(sc, AR_GPIO_OUTPUT_MUX(mux));
	reg &= ~(0x1f << (off * 5));
	reg |= (type & 0x1f) << (off * 5);
	AR_WRITE(sc, AR_GPIO_OUTPUT_MUX(mux), reg);

	reg = AR_READ(sc, AR_GPIO_OE_OUT);
	reg &= ~(AR_GPIO_OE_OUT_DRV_M << (pin * 2));
	reg |= AR_GPIO_OE_OUT_DRV_ALL << (pin * 2);
	AR_WRITE(sc, AR_GPIO_OE_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_gpio_read */
static int
ar9003_gpio_read(struct athn_softc *sc, int pin)
{
	KASSERT(pin < sc->ngpiopins);
	return (((AR_READ(sc, AR_GPIO_IN) & AR9300_GPIO_IN_VAL) &
	    (1u << pin)) != 0);
}

/* ar9003.c: ar9003_gpio_write */
static void
ar9003_gpio_write(struct athn_softc *sc, int pin, int set)
{
	uint32_t reg;

	KASSERT(pin < sc->ngpiopins);
	reg = AR_READ(sc, AR_GPIO_IN_OUT);
	if (set)
		reg |= 1u << pin;
	else
		reg &= ~(1u << pin);
	AR_WRITE(sc, AR_GPIO_IN_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_hw_init */
static void
ar9003_hw_init(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
#define X(x)	((uint32_t)(x) << 2)
	struct athn_ops *ops = &sc->ops;
	const struct athn_ini *ini = sc->ini;
	const uint32_t *pvals;
	uint32_t reg;
	int i;

	/*
	 * The common init values include the pre and core phases for the
	 * SoC, MAC, BB and Radio subsystems.
	 */
	DPRINTFN(4, ("writing pre and core init vals\n"));
	for (i = 0; i < ini->ncmregs; i++) {
		AR_WRITE(sc, X(ini->cmregs[i]), ini->cmvals[i]);
		if (AR_IS_ANALOG_REG(X(ini->cmregs[i])))
			DELAY(100);
		if ((i & 0x1f) == 0)
			DELAY(1);
	}

	/*
	 * The modal init values include the post phase for the SoC, MAC,
	 * BB and Radio subsystems.
	 */
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
	DPRINTFN(4, ("writing post init vals\n"));
	for (i = 0; i < ini->nregs; i++) {
		AR_WRITE(sc, X(ini->regs[i]), pvals[i]);
		if (AR_IS_ANALOG_REG(X(ini->regs[i])))
			DELAY(100);
		if ((i & 0x1f) == 0)
			DELAY(1);
	}

	if (sc->rx_gain != NULL)
		ar9003_reset_rx_gain(sc, c);
	if (sc->tx_gain != NULL)
		ar9003_reset_tx_gain(sc, c);

	if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)) {
		/* Update modal values for fast PLL clock. */
		if (extc != NULL)
			pvals = ini->fastvals_5g40;
		else
			pvals = ini->fastvals_5g20;
		DPRINTFN(4, ("writing fast pll clock init vals\n"));
		for (i = 0; i < ini->nfastregs; i++) {
			AR_WRITE(sc, X(ini->fastregs[i]), pvals[i]);
			if (AR_IS_ANALOG_REG(X(ini->fastregs[i])))
				DELAY(100);
			if ((i & 0x1f) == 0)
				DELAY(1);
		}
	}

	/*
	 * Set the RX_ABORT and RX_DIS bits to prevent frames with corrupted
	 * descriptor status.
	 */
	AR_SETBITS(sc, AR_DIAG_SW, AR_DIAG_RX_DIS | AR_DIAG_RX_ABORT);

	reg = AR_READ(sc, AR_PCU_MISC_MODE2);
	reg &= ~AR_PCU_MISC_MODE2_ADHOC_MCAST_KEYID_ENABLE;
	reg |= AR_PCU_MISC_MODE2_AGG_WEP_ENABLE_FIX;
	reg |= AR_PCU_MISC_MODE2_ENABLE_AGGWEP;
	AR_WRITE(sc, AR_PCU_MISC_MODE2, reg);
	AR_WRITE_BARRIER(sc);

	ar9003_set_phy(sc, c, extc);
	ar9003_init_chains(sc);

	ops->set_txpower(sc, c, extc);
#undef X
}

/* ar9003.c: ar9003_init_baseband */
static void
ar9003_init_baseband(struct athn_softc *sc)
{
	uint32_t synth_delay;

	synth_delay = ar9003_synth_delay(sc);
	/* Activate the PHY (includes baseband activate and synthesizer on). */
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);
	AR_WRITE_BARRIER(sc);
	DELAY(AR_BASE_PHY_ACTIVE_DELAY + synth_delay);
}

/* ar9003.c: ar9003_init_calib */
static int
ar9003_init_calib(struct athn_softc *sc)
{
	uint8_t txchainmask, rxchainmask;
	uint32_t reg;
	int ntries;

	/* Save chains masks. */
	txchainmask = sc->txchainmask;
	rxchainmask = sc->rxchainmask;
	/* Configure hardware before calibration. */
	if (AR_READ(sc, AR_ENT_OTP) & AR_ENT_OTP_CHAIN2_DISABLE)
		sc->txchainmask = sc->rxchainmask = 0x3;
	else
		sc->txchainmask = sc->rxchainmask = 0x7;
	ar9003_init_chains(sc);

	/* Perform Tx IQ calibration. */
	int error=ar9003_calib_tx_iq(sc);
	if(error)goto restore_chains;
	/* Disable and re-enable the PHY chips. */
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_DIS);
	AR_WRITE_BARRIER(sc);
	DELAY(5);
	AR_WRITE(sc, AR_PHY_ACTIVE, AR_PHY_ACTIVE_EN);

	/* Calibrate the AGC. */
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_CAL);
	/* Poll for offset calibration completion. */
	for (ntries = 0; ntries < 10000; ntries++) {
		reg = AR_READ(sc, AR_PHY_AGC_CONTROL);
		if (!(reg & AR_PHY_AGC_CONTROL_CAL))
			break;
		DELAY(10);
	}
	if (ntries == 10000)error=ETIMEDOUT;

restore_chains:
	/* Restore chains masks on both successful and failed calibration. */
	sc->txchainmask = txchainmask;
	sc->rxchainmask = rxchainmask;
	ar9003_init_chains(sc);

	return error;
}

/* ar9003.c: ar9003_init_chains */
static void
ar9003_init_chains(struct athn_softc *sc)
{
	if (sc->rxchainmask == 0x5 || sc->txchainmask == 0x5)
		AR_SETBITS(sc, AR_PHY_ANALOG_SWAP, AR_PHY_SWAP_ALT_CHAIN);

	/* Setup chain masks. */
	AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  sc->rxchainmask);
	AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, sc->rxchainmask);

	if (sc->flags & ATHN_FLAG_3TREDUCE_CHAIN) {
		/*
		 * All self-generated frames are sent using two Tx chains
		 * on these chips to not exceed PCIe power requirements.
		 */
		AR_WRITE(sc, AR_SELFGEN_MASK, 0x3);
	} else
		AR_WRITE(sc, AR_SELFGEN_MASK, sc->txchainmask);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_init_noisefloor_calib */
static void
ar9003_init_noisefloor_calib(struct athn_softc *sc)
{
	AR_SETBITS(sc, AR_PHY_AGC_CONTROL, AR_PHY_AGC_CONTROL_NF);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_next_calib */
static void
ar9003_next_calib(struct athn_softc *sc)
{
	/* Check if we have any calibration in progress. */
	if (sc->cur_calib_mask != 0) {
		if (!(AR_READ(sc, AR_PHY_TIMING4) & AR_PHY_TIMING4_DO_CAL)) {
			/* Calibration completed for current sample. */
			ar9003_calib_iq(sc);
		}
	}
}

/* ar9003.c: ar9003_read_eep_data */
static int
ar9003_read_eep_data(struct athn_softc *sc, uint32_t addr, void *buf, int len)
{
 if(!buf||len<0||addr>=sc->rom_bytes||(uint32_t)len>addr+1)return EINVAL;
 uint8_t*dst=buf;uint32_t cached=0xffffffffu;uint16_t word=0;
 for(int i=0;i<len;i++){
  uint32_t at=addr-(unsigned)i;
  if((at>>1)!=cached){
   int rc=ar9003_read_eep_word(sc,at>>1,&word);if(rc)return rc;cached=at>>1;
  }
  dst[i]=(word>>((at&1)*8))&255;
 }
 return 0;
}

/* ar9003.c: ar9003_read_eep_word */
static int
ar9003_read_eep_word(struct athn_softc *sc, uint32_t addr, uint16_t *val)
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

/* ar9003.c: ar9003_read_noisefloor */
static void
ar9003_read_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext)
{
/* Sign-extends 9-bit value (assumes upper bits are zeroes). */
#define SIGN_EXT(v)	(((v) ^ 0x100) - 0x100)
	uint32_t reg;
	int i;

	for (i = 0; i < sc->nrxchains; i++) {
		reg = AR_READ(sc, AR_PHY_CCA(i));
		nf[i] = MS(reg, AR_PHY_MINCCA_PWR);
		nf[i] = SIGN_EXT(nf[i]);

		reg = AR_READ(sc, AR_PHY_EXT_CCA(i));
		nf_ext[i] = MS(reg, AR_PHY_EXT_MINCCA_PWR);
		nf_ext[i] = SIGN_EXT(nf_ext[i]);
	}
#undef SIGN_EXT
}

/* ar9003.c: ar9003_read_otp_data */
static int
ar9003_read_otp_data(struct athn_softc *sc, uint32_t addr, void *buf, int len)
{
 if(!buf||len<0||addr>=sc->rom_bytes||(uint32_t)len>addr+1)return EINVAL;
 uint8_t*dst=buf;uint32_t cached=0xffffffffu,word=0;
 for(int i=0;i<len;i++){
  uint32_t at=addr-(unsigned)i;
  if((at>>2)!=cached){
   int rc=ar9003_read_otp_word(sc,at>>2,&word);if(rc)return rc;cached=at>>2;
  }
  dst[i]=(word>>((at&3)*8))&255;
 }
 return 0;
}

/* ar9003.c: ar9003_read_otp_word */
static int
ar9003_read_otp_word(struct athn_softc *sc, uint32_t addr, uint32_t *val)
{
	uint32_t reg;
	int ntries;

	reg = AR_READ(sc, AR_OTP_BASE(addr));
	for (ntries = 0; ntries < 1000; ntries++) {
		reg = AR_READ(sc, AR_OTP_STATUS);
		if (MS(reg, AR_OTP_STATUS_TYPE) == AR_OTP_STATUS_VALID) {
			*val = AR_READ(sc, AR_OTP_READ_DATA);
			return (0);
		}
		DELAY(10);
	}
	return (ETIMEDOUT);
}

/* ar9003.c: ar9003_read_rom */
static int
ar9003_read_rom(struct athn_softc *sc)
{
 if(sc->eep_size!=sizeof(struct ar9380_eeprom))return EINVAL;
 sc->eep=malloc(sc->eep_size,M_DEVBUF,M_NOWAIT);if(!sc->eep)return ENOMEM;
 memset(sc->eep,0,sc->eep_size);sc->rom_valid=0;
 uint8_t data[2048];unsigned address=sc->eep_base;
 for(unsigned block=0;block<100;block++){
  if(address<3)return sc->rom_valid?0:EIO;
  uint8_t h[4];int rc=sc->ops.read_rom_data(sc,address,h,4);if(rc)return rc;
  uint32_t header=LE_READ_4(h);
  if(header==0||header==0xffffffffu)return sc->rom_valid?0:EIO;
  unsigned alg=h[0]>>5,ref=((h[1]&128)>>2)|(h[0]&31);
  unsigned len=((h[1]&127)<<4)|(h[2]>>4);
  if(len>sizeof(data)||len+6>address+1)return EINVAL;
  address-=4;
  if(len){rc=sc->ops.read_rom_data(sc,address,data,len);if(rc)return rc;}
  address-=len;
  uint8_t checksum[2];rc=sc->ops.read_rom_data(sc,address,checksum,2);if(rc)return rc;
  uint16_t sum=0;for(unsigned i=0;i<len;i++)sum+=data[i];
  if(sum!=LE_READ_2(checksum))return EINVAL;
  rc=ar9003_restore_rom_block(sc,alg,ref,data,len);if(rc)return rc;
  if(address<2)return sc->rom_valid?0:EIO;
  address-=2;
 }
 return EINVAL;
}

/* ar9003.c: ar9003_reset_rx_gain */
static void
ar9003_reset_rx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
#define X(x)	((uint32_t)(x) << 2)
	const struct athn_gain *prog = sc->rx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, X(prog->regs[i]), pvals[i]);
	AR_WRITE_BARRIER(sc);
#undef X
}

/* ar9003.c: ar9003_reset_tx_gain */
static void
ar9003_reset_tx_gain(struct athn_softc *sc, struct ieee80211_channel *c)
{
#define X(x)	((uint32_t)(x) << 2)
	const struct athn_gain *prog = sc->tx_gain;
	const uint32_t *pvals;
	int i;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		pvals = prog->vals_2g;
	else
		pvals = prog->vals_5g;
	for (i = 0; i < prog->nregs; i++)
		AR_WRITE(sc, X(prog->regs[i]), pvals[i]);
	AR_WRITE_BARRIER(sc);
#undef X
}

/* ar9003.c: ar9003_restore_rom_block */
static int
ar9003_restore_rom_block(struct athn_softc *sc, uint8_t alg, uint8_t ref,
    const uint8_t *buf, int len)
{
 if(!buf||len<0||sc->eep_size!=sizeof(struct ar9380_eeprom))return EINVAL;
 uint8_t candidate[sizeof(struct ar9380_eeprom)];
 if(alg==AR_EEP_COMPRESS_NONE){
  if((unsigned)len!=sizeof(candidate))return EINVAL;
  memcpy(candidate,buf,sizeof(candidate));
 }else if(alg==AR_EEP_COMPRESS_BLOCK){
  if(ref){
   const uint8_t*template=sc->ops.get_rom_template(sc,ref);
   if(!template)return EINVAL;
   memcpy(candidate,template,sizeof(candidate));
  }else{
   if(!sc->rom_valid)return EINVAL;
   memcpy(candidate,sc->eep,sizeof(candidate));
  }
  unsigned source=0,target=0;
  while(source<(unsigned)len){
   if((unsigned)len-source<2)return EINVAL;
   unsigned gap=buf[source++],count=buf[source++];
   if(gap>sizeof(candidate)-target)return EINVAL;
   target+=gap;
   if(count>sizeof(candidate)-target||count>(unsigned)len-source)return EINVAL;
   memcpy(candidate+target,buf+source,count);source+=count;target+=count;
  }
 }else return EINVAL;
 memcpy(sc->eep,candidate,sizeof(candidate));sc->rom_valid=1;return 0;
}

/* ar9003.c: ar9003_rf_bus_release */
static void
ar9003_rf_bus_release(struct athn_softc *sc)
{
	/* Wait for the synthesizer to settle. */
	DELAY(AR_BASE_PHY_ACTIVE_DELAY + ar9003_synth_delay(sc));

	/* Release the RF Bus grant. */
	AR_WRITE(sc, AR_PHY_RFBUS_REQ, 0);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_rf_bus_request */
static int
ar9003_rf_bus_request(struct athn_softc *sc)
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

/* ar9003.c: ar9003_rfsilent_init */
static void
ar9003_rfsilent_init(struct athn_softc *sc)
{
	uint32_t reg;

	/* Configure hardware radio switch. */
	AR_SETBITS(sc, AR_GPIO_INPUT_EN_VAL, AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);
	reg = AR_READ(sc, AR_GPIO_INPUT_MUX2);
	reg = RW(reg, AR_GPIO_INPUT_MUX2_RFSILENT, 0);
	AR_WRITE(sc, AR_GPIO_INPUT_MUX2, reg);
	ar9003_gpio_config_input(sc, sc->rfsilent_pin);
	AR_SETBITS(sc, AR_PHY_TEST, AR_PHY_TEST_RFSILENT_BB);
	if (!(sc->flags & ATHN_FLAG_RFSILENT_REVERSED)) {
		AR_SETBITS(sc, AR_GPIO_INTR_POL,
		    AR_GPIO_INTR_POL_PIN(sc->rfsilent_pin));
	}
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_cck_weak_signal */
static void
ar9003_set_cck_weak_signal(struct athn_softc *sc, int high)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_CCK_DETECT);
	reg = RW(reg, AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK, high ? 6 : 8);
	AR_WRITE(sc, AR_PHY_CCK_DETECT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_delta_slope */
static void
ar9003_set_delta_slope(struct athn_softc *sc, struct ieee80211_channel *c,
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

	reg = AR_READ(sc, AR_PHY_SGI_DELTA);
	reg = RW(reg, AR_PHY_SGI_DSC_EXP, exp);
	reg = RW(reg, AR_PHY_SGI_DSC_MAN, man);
	AR_WRITE(sc, AR_PHY_SGI_DELTA, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_firstep_level */
static void
ar9003_set_firstep_level(struct athn_softc *sc, int level)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_FIND_SIG);
	reg = RW(reg, AR_PHY_FIND_SIG_FIRSTEP, level * 4);
	AR_WRITE(sc, AR_PHY_FIND_SIG, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_noise_immunity_level */
static void
ar9003_set_noise_immunity_level(struct athn_softc *sc, int level)
{
	int high = level == 4;
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_DESIRED_SZ);
	reg = RW(reg, AR_PHY_DESIRED_SZ_TOT_DES, high ? -62 : -55);
	AR_WRITE(sc, AR_PHY_DESIRED_SZ, reg);

	reg = AR_READ(sc, AR_PHY_AGC);
	reg = RW(reg, AR_PHY_AGC_COARSE_LOW, high ? -70 : -64);
	reg = RW(reg, AR_PHY_AGC_COARSE_HIGH, high ? -12 : -14);
	AR_WRITE(sc, AR_PHY_AGC, reg);

	reg = AR_READ(sc, AR_PHY_FIND_SIG);
	reg = RW(reg, AR_PHY_FIND_SIG_FIRPWR, high ? -80 : -78);
	AR_WRITE(sc, AR_PHY_FIND_SIG, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_phy */
static void
ar9003_set_phy(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t phy;

	phy = AR_READ(sc, AR_PHY_GEN_CTRL);
	phy |= AR_PHY_GC_HT_EN | AR_PHY_GC_SHORT_GI_40 |
	    AR_PHY_GC_SINGLE_HT_LTF1 | AR_PHY_GC_WALSH;
	if (extc != NULL) {
		phy |= AR_PHY_GC_DYN2040_EN;
		if (extc > c)	/* XXX */
			phy |= AR_PHY_GC_DYN2040_PRI_CH;
	}
	/* Turn off Green Field detection for now. */
	phy &= ~AR_PHY_GC_GF_DETECT_EN;
	AR_WRITE(sc, AR_PHY_GEN_CTRL, phy);

	AR_WRITE(sc, AR_2040_MODE,
	    (extc != NULL) ? AR_2040_JOINED_RX_CLEAR : 0);

	/* Set global transmit timeout. */
	AR_WRITE(sc, AR_GTXTO, SM(AR_GTXTO_TIMEOUT_LIMIT, 25));
	/* Set carrier sense timeout. */
	AR_WRITE(sc, AR_CST, SM(AR_CST_TIMEOUT_LIMIT, 15));
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_rf_mode */
static void
ar9003_set_rf_mode(struct athn_softc *sc, struct ieee80211_channel *c)
{
	uint32_t reg;

	reg = IEEE80211_IS_CHAN_2GHZ(c) ?
	    AR_PHY_MODE_DYNAMIC : AR_PHY_MODE_OFDM;
	if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    (sc->flags & ATHN_FLAG_FAST_PLL_CLOCK)) {
		reg |= AR_PHY_MODE_DYNAMIC | AR_PHY_MODE_DYN_CCK_DISABLE;
	}
	AR_WRITE(sc, AR_PHY_MODE, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_set_rxchains */
static void
ar9003_set_rxchains(struct athn_softc *sc)
{
	if (sc->rxchainmask == 0x3 || sc->rxchainmask == 0x5) {
		AR_WRITE(sc, AR_PHY_RX_CHAINMASK,  sc->rxchainmask);
		AR_WRITE(sc, AR_PHY_CAL_CHAINMASK, sc->rxchainmask);
		AR_WRITE_BARRIER(sc);
	}
}

/* ar9003.c: ar9003_set_spur_immunity_level */
static void
ar9003_set_spur_immunity_level(struct athn_softc *sc, int level)
{
	uint32_t reg;

	reg = AR_READ(sc, AR_PHY_TIMING5);
	reg = RW(reg, AR_PHY_TIMING5_CYCPWR_THR1, (level + 1) * 2);
	AR_WRITE(sc, AR_PHY_TIMING5, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9003.c: ar9003_synth_delay */
static __inline uint32_t
ar9003_synth_delay(struct athn_softc *sc)
{
	uint32_t delay;

	delay = MS(AR_READ(sc, AR_PHY_RX_DELAY), AR_PHY_RX_DELAY_DELAY);
	if (sc->sc_ic.ic_curmode == IEEE80211_MODE_11B)
		delay = (delay * 4) / 22;
	else
		delay = delay / 10;	/* in 100ns steps */
	return (delay);
}

/* ar9003.c: ar9003_write_noisefloor */
static void
ar9003_write_noisefloor(struct athn_softc *sc, int16_t *nf, int16_t *nf_ext)
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

/* ar9003.c: ar9003_write_txpower */
static void
ar9003_write_txpower(struct athn_softc *sc, int16_t power[ATHN_POWER_COUNT])
{
	/* Make sure forced gain is disabled. */
	AR_WRITE(sc, AR_PHY_TX_FORCED_GAIN, 0);

	AR_WRITE(sc, AR_PHY_PWRTX_RATE1,
	    (power[ATHN_POWER_OFDM18  ] & 0x3f) << 24 |
	    (power[ATHN_POWER_OFDM12  ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM9   ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_OFDM6   ] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE2,
	    (power[ATHN_POWER_OFDM54  ] & 0x3f) << 24 |
	    (power[ATHN_POWER_OFDM48  ] & 0x3f) << 16 |
	    (power[ATHN_POWER_OFDM36  ] & 0x3f) <<  8 |
	    (power[ATHN_POWER_OFDM24  ] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE3,
	    (power[ATHN_POWER_CCK2_SP ] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK2_LP ] & 0x3f) << 16 |
	    /* NB: No eXtended Range for AR9003. */
	    (power[ATHN_POWER_CCK1_LP ] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE4,
	    (power[ATHN_POWER_CCK11_SP] & 0x3f) << 24 |
	    (power[ATHN_POWER_CCK11_LP] & 0x3f) << 16 |
	    (power[ATHN_POWER_CCK55_SP] & 0x3f) <<  8 |
	    (power[ATHN_POWER_CCK55_LP] & 0x3f));
	/*
	 * NB: AR_PHY_PWRTX_RATE5 needs to be written even if HT is disabled
	 * because it is read by PA predistortion functions.
	 */
	AR_WRITE(sc, AR_PHY_PWRTX_RATE5,
	    (power[ATHN_POWER_HT20( 5)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20( 4)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20( 1)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20( 0)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE6,
	    (power[ATHN_POWER_HT20(13)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20(12)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20( 7)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20( 6)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE7,
	    (power[ATHN_POWER_HT40( 5)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40( 4)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40( 1)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40( 0)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE8,
	    (power[ATHN_POWER_HT40(13)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(12)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40( 7)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40( 6)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE10,
	    (power[ATHN_POWER_HT20(21)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT20(20)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20(15)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20(14)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE11,
	    (power[ATHN_POWER_HT40(23)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(22)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT20(23)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT20(22)] & 0x3f));
	AR_WRITE(sc, AR_PHY_PWRTX_RATE12,
	    (power[ATHN_POWER_HT40(21)] & 0x3f) << 24 |
	    (power[ATHN_POWER_HT40(20)] & 0x3f) << 16 |
	    (power[ATHN_POWER_HT40(15)] & 0x3f) <<  8 |
	    (power[ATHN_POWER_HT40(14)] & 0x3f));
	AR_WRITE_BARRIER(sc);
}

/* ar9380.c: ar9380_attach */
static int
ar9380_attach(struct athn_softc *sc)
{
	sc->ngpiopins = 17;
	sc->ops.setup = ar9380_setup;
	sc->ops.get_rom_template = ar9380_get_rom_template;
	sc->ops.swap_rom = ar9380_swap_rom;
	sc->ops.init_from_rom = ar9380_init_from_rom;
	sc->ops.set_txpower = ar9380_set_txpower;
	sc->ops.set_synth = ar9380_set_synth;
	sc->ops.spur_mitigate = ar9380_spur_mitigate;
	sc->ops.get_paprd_masks = ar9380_get_paprd_masks;
	sc->cca_min_2g = AR9380_PHY_CCA_MIN_GOOD_VAL_2GHZ;
	sc->cca_max_2g = AR9380_PHY_CCA_MAX_GOOD_VAL_2GHZ;
	sc->cca_min_5g = AR9380_PHY_CCA_MIN_GOOD_VAL_5GHZ;
	sc->cca_max_5g = AR9380_PHY_CCA_MAX_GOOD_VAL_5GHZ;
	if (AR_SREV_9485(sc)) {
		sc->ini = &ar9485_1_1_ini;
		sc->serdes = &ar9485_1_1_serdes;
	} else {
		sc->ini = &ar9380_2_2_ini;
		sc->serdes = &ar9380_2_2_serdes;
	}

	return (ar9003_attach(sc));
}

/* ar9380.c: ar9380_get_correction */
static void
ar9380_get_correction(struct athn_softc *sc, struct ieee80211_channel *c,
    int chain, int *corr, int *temp)
{
	const struct ar9380_eeprom *eep = sc->eep;
	const struct ar9380_cal_data_per_freq_op_loop *pierdata;
	const uint8_t *pierfreq;
	uint8_t fbin;
	int lo, hi, npiers;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		pierfreq = eep->calFreqPier2G;
		pierdata = eep->calPierData2G[chain];
		npiers = AR9380_NUM_2G_CAL_PIERS;
	} else {
		pierfreq = eep->calFreqPier5G;
		pierdata = eep->calPierData5G[chain];
		npiers = AR9380_NUM_5G_CAL_PIERS;
	}
	/* Find channel in ROM pier table. */
	fbin = athn_chan2fbin(c);
	athn_get_pier_ival(fbin, pierfreq, npiers, &lo, &hi);

	*corr = athn_interpolate(fbin,
	    pierfreq[lo], pierdata[lo].refPower,
	    pierfreq[hi], pierdata[hi].refPower);
	*temp = athn_interpolate(fbin,
	    pierfreq[lo], pierdata[lo].tempMeas,
	    pierfreq[hi], pierdata[hi].tempMeas);
}

/* ar9380.c: ar9380_get_paprd_masks */
static void
ar9380_get_paprd_masks(struct athn_softc *sc, struct ieee80211_channel *c,
    uint32_t *ht20mask, uint32_t *ht40mask)
{
	const struct ar9380_eeprom *eep = sc->eep;
	const struct ar9380_modal_eep_header *modal;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		modal = &eep->modalHeader2G;
	else
		modal = &eep->modalHeader5G;
	*ht20mask = modal->papdRateMaskHt20;
	*ht40mask = modal->papdRateMaskHt40;
}

/* ar9380.c: ar9380_get_rom_template */
static const uint8_t *
ar9380_get_rom_template(struct athn_softc *sc, uint8_t ref)
{
	int i;

	/* Retrieve template ROM image for given reference. */
	for (i = 0; i < nitems(ar9380_rom_templates); i++)
		if (ar9380_rom_templates[i][1] == ref)
			return (ar9380_rom_templates[i]);
	return (NULL);
}

/* ar9380.c: ar9380_init_from_rom */
static void
ar9380_init_from_rom(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar9380_eeprom *eep = sc->eep;
	const struct ar9380_modal_eep_header *modal;
	uint8_t db, margin, ant_div_ctrl;
	uint32_t reg;
	int i, maxchains;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		modal = &eep->modalHeader2G;
	else
		modal = &eep->modalHeader5G;

	/* Apply XPA bias level. */
	if (AR_SREV_9485(sc)) {
		reg = AR_READ(sc, AR9485_PHY_65NM_CH0_TOP2);
		reg = RW(reg, AR9485_PHY_65NM_CH0_TOP2_XPABIASLVL,
		    modal->xpaBiasLvl);
		AR_WRITE(sc, AR9485_PHY_65NM_CH0_TOP2, reg);
	} else {
		reg = AR_READ(sc, AR_PHY_65NM_CH0_TOP);
		reg = RW(reg, AR_PHY_65NM_CH0_TOP_XPABIASLVL,
		    modal->xpaBiasLvl & 0x3);
		AR_WRITE(sc, AR_PHY_65NM_CH0_TOP, reg);
		reg = AR_READ(sc, AR_PHY_65NM_CH0_THERM);
		reg = RW(reg, AR_PHY_65NM_CH0_THERM_XPABIASLVL_MSB,
		    modal->xpaBiasLvl >> 2);
		reg |= AR_PHY_65NM_CH0_THERM_XPASHORT2GND;
		AR_WRITE(sc, AR_PHY_65NM_CH0_THERM, reg);
	}

	/* Apply antenna control. */
	reg = AR_READ(sc, AR_PHY_SWITCH_COM);
	reg = RW(reg, AR_SWITCH_TABLE_COM_ALL, modal->antCtrlCommon);
	AR_WRITE(sc, AR_PHY_SWITCH_COM, reg);
	reg = AR_READ(sc, AR_PHY_SWITCH_COM_2);
	reg = RW(reg, AR_SWITCH_TABLE_COM_2_ALL, modal->antCtrlCommon2);
	AR_WRITE(sc, AR_PHY_SWITCH_COM_2, reg);

	maxchains = AR_SREV_9485(sc) ? 1 : AR9380_MAX_CHAINS;
	for (i = 0; i < maxchains; i++) {
		reg = AR_READ(sc, AR_PHY_SWITCH_CHAIN(i));
		reg = RW(reg, AR_SWITCH_TABLE_ALL, modal->antCtrlChain[i]);
		AR_WRITE(sc, AR_PHY_SWITCH_CHAIN(i), reg);
	}

	if (AR_SREV_9485(sc)) {
		ant_div_ctrl = eep->base_ext1.ant_div_control;
		reg = AR_READ(sc, AR_PHY_MC_GAIN_CTRL);
		reg = RW(reg, AR_PHY_MC_GAIN_CTRL_ANT_DIV_CTRL_ALL,
		    MS(ant_div_ctrl, AR_EEP_ANT_DIV_CTRL_ALL));
		if (ant_div_ctrl & AR_EEP_ANT_DIV_CTRL_ANT_DIV)
			reg |= AR_PHY_MC_GAIN_CTRL_ENABLE_ANT_DIV;
		else
			reg &= ~AR_PHY_MC_GAIN_CTRL_ENABLE_ANT_DIV;
		AR_WRITE(sc, AR_PHY_MC_GAIN_CTRL, reg);
		reg = AR_READ(sc, AR_PHY_CCK_DETECT);
		if (ant_div_ctrl & AR_EEP_ANT_DIV_CTRL_FAST_DIV)
			reg |= AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
		else
			reg &= ~AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV;
		AR_WRITE(sc, AR_PHY_CCK_DETECT, reg);
	}

	if (eep->baseEepHeader.miscConfiguration & AR_EEP_DRIVE_STRENGTH) {
		/* Apply drive strength. */
		reg = AR_READ(sc, AR_PHY_65NM_CH0_BIAS1);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS1_0, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS1_1, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS1_2, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS1_3, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS1_4, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS1_5, 5);
		AR_WRITE(sc, AR_PHY_65NM_CH0_BIAS1, reg);

		reg = AR_READ(sc, AR_PHY_65NM_CH0_BIAS2);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_0, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_1, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_2, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_3, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_4, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_5, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_6, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_7, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS2_8, 5);
		AR_WRITE(sc, AR_PHY_65NM_CH0_BIAS2, reg);

		reg = AR_READ(sc, AR_PHY_65NM_CH0_BIAS4);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS4_0, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS4_1, 5);
		reg = RW(reg, AR_PHY_65NM_CH0_BIAS4_2, 5);
		AR_WRITE(sc, AR_PHY_65NM_CH0_BIAS4, reg);
	}

	/* Apply attenuation settings. */
	maxchains = AR_SREV_9485(sc) ? 1 : AR9380_MAX_CHAINS;
	for (i = 0; i < maxchains; i++) {
		if (IEEE80211_IS_CHAN_5GHZ(c) &&
		    eep->base_ext2.xatten1DBLow[i] != 0) {
			if (c->ic_freq <= 5500) {
				db = athn_interpolate(c->ic_freq,
				    5180, eep->base_ext2.xatten1DBLow[i],
				    5500, modal->xatten1DB[i]);
			} else {
				db = athn_interpolate(c->ic_freq,
				    5500, modal->xatten1DB[i],
				    5785, eep->base_ext2.xatten1DBHigh[i]);
			}
		} else
			db = modal->xatten1DB[i];
		if (IEEE80211_IS_CHAN_5GHZ(c) &&
		    eep->base_ext2.xatten1MarginLow[i] != 0) {
			if (c->ic_freq <= 5500) {
				margin = athn_interpolate(c->ic_freq,
				    5180, eep->base_ext2.xatten1MarginLow[i],
				    5500, modal->xatten1Margin[i]);
			} else {
				margin = athn_interpolate(c->ic_freq,
				    5500, modal->xatten1Margin[i],
				    5785, eep->base_ext2.xatten1MarginHigh[i]);
			}
		} else
			margin = modal->xatten1Margin[i];
		reg = AR_READ(sc, AR_PHY_EXT_ATTEN_CTL(i));
		reg = RW(reg, AR_PHY_EXT_ATTEN_CTL_XATTEN1_DB, db);
		reg = RW(reg, AR_PHY_EXT_ATTEN_CTL_XATTEN1_MARGIN, margin);
		AR_WRITE(sc, AR_PHY_EXT_ATTEN_CTL(i), reg);
	}

	/* Initialize switching regulator. */
	if (AR_SREV_9485(sc))
		ar9485_init_swreg(sc);
	else
		ar9380_init_swreg(sc);

	/* Apply tuning capabilities. */
	if (AR_SREV_9485(sc) &&
	    (eep->baseEepHeader.featureEnable & AR_EEP_TUNING_CAPS)) {
		reg = AR_READ(sc, AR9485_PHY_CH0_XTAL);
		reg = RW(reg, AR9485_PHY_CH0_XTAL_CAPINDAC,
		    eep->baseEepHeader.params_for_tuning_caps[0]);
		reg = RW(reg, AR9485_PHY_CH0_XTAL_CAPOUTDAC,
		    eep->baseEepHeader.params_for_tuning_caps[0]);
		AR_WRITE(sc, AR9485_PHY_CH0_XTAL, reg);
	}
	AR_WRITE_BARRIER(sc);
}

/* ar9380.c: ar9380_init_swreg */
static void
ar9380_init_swreg(struct athn_softc *sc)
{
	const struct ar9380_eeprom *eep = sc->eep;

	if (eep->baseEepHeader.featureEnable & AR_EEP_INTERNAL_REGULATOR) {
		/* Internal regulator is ON. */
		AR_CLRBITS(sc, AR_RTC_REG_CONTROL1,
		    AR_RTC_REG_CONTROL1_SWREG_PROGRAM);
		AR_WRITE(sc, AR_RTC_REG_CONTROL0, eep->baseEepHeader.swreg);
		AR_SETBITS(sc, AR_RTC_REG_CONTROL1,
		    AR_RTC_REG_CONTROL1_SWREG_PROGRAM);
	} else
		AR_SETBITS(sc, AR_RTC_SLEEP_CLK, AR_RTC_FORCE_SWREG_PRD);
	AR_WRITE_BARRIER(sc);
}

/* ar9380.c: ar9380_set_correction */
static void
ar9380_set_correction(struct athn_softc *sc, struct ieee80211_channel *c)
{
	const struct ar9380_eeprom *eep = sc->eep;
	const struct ar9380_modal_eep_header *modal;
	uint32_t reg;
	int8_t slope;
	int i, corr, temp, temp0;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		modal = &eep->modalHeader2G;
	else
		modal = &eep->modalHeader5G;

	for (i = 0; i < AR9380_MAX_CHAINS; i++) {
		ar9380_get_correction(sc, c, i, &corr, &temp);
		if (i == 0)
			temp0 = temp;

		reg = AR_READ(sc, AR_PHY_TPC_11_B(i));
		reg = RW(reg, AR_PHY_TPC_11_OLPC_GAIN_DELTA, corr);
		AR_WRITE(sc, AR_PHY_TPC_11_B(i), reg);

		/* Enable open loop power control. */
		reg = AR_READ(sc, AR_PHY_TPC_6_B(i));
		reg = RW(reg, AR_PHY_TPC_6_ERROR_EST_MODE, 3);
		AR_WRITE(sc, AR_PHY_TPC_6_B(i), reg);
	}

	/* Enable temperature compensation. */
	if (IEEE80211_IS_CHAN_5GHZ(c) &&
	    eep->base_ext2.tempSlopeLow != 0) {
		if (c->ic_freq <= 5500) {
			slope = athn_interpolate(c->ic_freq,
			    5180, eep->base_ext2.tempSlopeLow,
			    5500, modal->tempSlope);
		} else {
			slope = athn_interpolate(c->ic_freq,
			    5500, modal->tempSlope,
			    5785, eep->base_ext2.tempSlopeHigh);
		}
	} else
		slope = modal->tempSlope;

	reg = AR_READ(sc, AR_PHY_TPC_19);
	reg = RW(reg, AR_PHY_TPC_19_ALPHA_THERM, slope);
	AR_WRITE(sc, AR_PHY_TPC_19, reg);

	reg = AR_READ(sc, AR_PHY_TPC_18);
	reg = RW(reg, AR_PHY_TPC_18_THERM_CAL, temp0);
	AR_WRITE(sc, AR_PHY_TPC_18, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9380.c: ar9380_set_synth */
static int
ar9380_set_synth(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint32_t freq = c->ic_freq;
	uint32_t chansel, phy;

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		if (AR_SREV_9485(sc))
			chansel = ((freq << 16) - 215) / 15;
		else
			chansel = (freq << 16) / 15;
		AR_WRITE(sc, AR_PHY_SYNTH_CONTROL, AR9380_BMODE);
	} else {
		chansel = (freq << 15) / 15;
		chansel >>= 1;
		AR_WRITE(sc, AR_PHY_SYNTH_CONTROL, 0);
	}

	/* Enable Long Shift Select for synthesizer. */
	AR_SETBITS(sc, AR_PHY_65NM_CH0_SYNTH4,
	    AR_PHY_SYNTH4_LONG_SHIFT_SELECT);
	AR_WRITE_BARRIER(sc);

	/* Program synthesizer. */
	phy = (chansel << 2) | AR9380_FRACMODE;
	DPRINTFN(4, ("AR_PHY_65NM_CH0_SYNTH7=0x%08x\n", phy));
	AR_WRITE(sc, AR_PHY_65NM_CH0_SYNTH7, phy);
	AR_WRITE_BARRIER(sc);
	/* Toggle Load Synth Channel bit. */
	AR_WRITE(sc, AR_PHY_65NM_CH0_SYNTH7, phy | AR9380_LOAD_SYNTH);
	AR_WRITE_BARRIER(sc);
	return (0);
}

/* ar9380.c: ar9380_set_txpower */
static void
ar9380_set_txpower(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar9380_eeprom *eep = sc->eep;
	uint8_t tpow_cck[4], tpow_ofdm[4];
	uint8_t tpow_ht20[14], tpow_ht40[14];
	int16_t power[ATHN_POWER_COUNT];

	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		/* Get CCK target powers. */
		ar9003_get_lg_tpow(sc, c, AR_CTL_11B,
		    eep->calTargetFbinCck, eep->calTargetPowerCck,
		    AR9380_NUM_2G_CCK_TARGET_POWERS, tpow_cck);

		/* Get OFDM target powers. */
		ar9003_get_lg_tpow(sc, c, AR_CTL_11G,
		    eep->calTargetFbin2G, eep->calTargetPower2G,
		    AR9380_NUM_2G_20_TARGET_POWERS, tpow_ofdm);

		/* Get HT-20 target powers. */
		ar9003_get_ht_tpow(sc, c, AR_CTL_2GHT20,
		    eep->calTargetFbin2GHT20, eep->calTargetPower2GHT20,
		    AR9380_NUM_2G_20_TARGET_POWERS, tpow_ht20);

		if (extc != NULL) {
			/* Get HT-40 target powers. */
			ar9003_get_ht_tpow(sc, c, AR_CTL_2GHT40,
			    eep->calTargetFbin2GHT40,
			    eep->calTargetPower2GHT40,
			    AR9380_NUM_2G_40_TARGET_POWERS, tpow_ht40);
		}
	} else {
		/* Get OFDM target powers. */
		ar9003_get_lg_tpow(sc, c, AR_CTL_11A,
		    eep->calTargetFbin5G, eep->calTargetPower5G,
		    AR9380_NUM_5G_20_TARGET_POWERS, tpow_ofdm);

		/* Get HT-20 target powers. */
		ar9003_get_ht_tpow(sc, c, AR_CTL_5GHT20,
		    eep->calTargetFbin5GHT20, eep->calTargetPower5GHT20,
		    AR9380_NUM_5G_20_TARGET_POWERS, tpow_ht20);

		if (extc != NULL) {
			/* Get HT-40 target powers. */
			ar9003_get_ht_tpow(sc, c, AR_CTL_5GHT40,
			    eep->calTargetFbin5GHT40,
			    eep->calTargetPower5GHT40,
			    AR9380_NUM_5G_40_TARGET_POWERS, tpow_ht40);
		}
	}

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
	if (IEEE80211_IS_CHAN_2GHZ(c)) {
		power[ATHN_POWER_CCK1_LP ] =
		power[ATHN_POWER_CCK2_LP ] =
		power[ATHN_POWER_CCK2_SP ] =
		power[ATHN_POWER_CCK55_LP] = tpow_cck[0];
		power[ATHN_POWER_CCK55_SP] = tpow_cck[1];
		power[ATHN_POWER_CCK11_LP] = tpow_cck[2];
		power[ATHN_POWER_CCK11_SP] = tpow_cck[3];
	}
	/* Next entry covers MCS0, MCS8 and MCS16. */
	power[ATHN_POWER_HT20( 0)] = tpow_ht20[ 0];
	/* Next entry covers MCS1-3, MCS9-11 and MCS17-19. */
	power[ATHN_POWER_HT20( 1)] = tpow_ht20[ 1];
	power[ATHN_POWER_HT20( 4)] = tpow_ht20[ 2];
	power[ATHN_POWER_HT20( 5)] = tpow_ht20[ 3];
	power[ATHN_POWER_HT20( 6)] = tpow_ht20[ 4];
	power[ATHN_POWER_HT20( 7)] = tpow_ht20[ 5];
	power[ATHN_POWER_HT20(12)] = tpow_ht20[ 6];
	power[ATHN_POWER_HT20(13)] = tpow_ht20[ 7];
	power[ATHN_POWER_HT20(14)] = tpow_ht20[ 8];
	power[ATHN_POWER_HT20(15)] = tpow_ht20[ 9];
	power[ATHN_POWER_HT20(20)] = tpow_ht20[10];
	power[ATHN_POWER_HT20(21)] = tpow_ht20[11];
	power[ATHN_POWER_HT20(22)] = tpow_ht20[12];
	power[ATHN_POWER_HT20(23)] = tpow_ht20[13];
	if (extc != NULL) {
		/* Next entry covers MCS0, MCS8 and MCS16. */
		power[ATHN_POWER_HT40( 0)] = tpow_ht40[ 0];
		/* Next entry covers MCS1-3, MCS9-11 and MCS17-19. */
		power[ATHN_POWER_HT40( 1)] = tpow_ht40[ 1];
		power[ATHN_POWER_HT40( 4)] = tpow_ht40[ 2];
		power[ATHN_POWER_HT40( 5)] = tpow_ht40[ 3];
		power[ATHN_POWER_HT40( 6)] = tpow_ht40[ 4];
		power[ATHN_POWER_HT40( 7)] = tpow_ht40[ 5];
		power[ATHN_POWER_HT40(12)] = tpow_ht40[ 6];
		power[ATHN_POWER_HT40(13)] = tpow_ht40[ 7];
		power[ATHN_POWER_HT40(14)] = tpow_ht40[ 8];
		power[ATHN_POWER_HT40(15)] = tpow_ht40[ 9];
		power[ATHN_POWER_HT40(20)] = tpow_ht40[10];
		power[ATHN_POWER_HT40(21)] = tpow_ht40[11];
		power[ATHN_POWER_HT40(22)] = tpow_ht40[12];
		power[ATHN_POWER_HT40(23)] = tpow_ht40[13];
	}

    /* Board CTLs and the conservative initial profile can only lower power. */
    int cck_limit=ar9380_ctl_limit(eep,athn_chan2fbin(c),AR_CTL_11B);
    int ofdm_limit=ar9380_ctl_limit(eep,athn_chan2fbin(c),AR_CTL_11G);
    if(cck_limit<0||ofdm_limit<0){sc->ok=0;return;}
    for(int k=ATHN_POWER_OFDM6;k<=ATHN_POWER_OFDM54;k++)power[k]=MIN(power[k],ofdm_limit);
    for(int k=ATHN_POWER_CCK1_LP;k<=ATHN_POWER_CCK11_SP;k++)power[k]=MIN(power[k],cck_limit);
    /* Initial native profile: conservative legacy power, board limits lower it. */
    const struct ar9380_modal_eep_header*modal=&eep->modalHeader2G;
    int antenna=MAX(0,(int8_t)modal->antennaGain);
    int limit=MAX(0,20-2*antenna-(sc->ntxchains==3?10:sc->ntxchains==2?6:0));
    for(int i=0;i<ATHN_POWER_COUNT;i++)power[i]=MAX(0,MIN(power[i],limit));

	/* Write transmit power values to hardware. */
	ar9003_write_txpower(sc, power);

	/* Apply transmit power correction. */
	ar9380_set_correction(sc, c);
}

/* ar9380.c: ar9380_setup */
static void
ar9380_setup(struct athn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ar9380_eeprom *eep = sc->eep;
	struct ar9380_base_eep_hdr *base = &eep->baseEepHeader;
	uint8_t type;

	if (base->opFlags & AR_OPFLAGS_11A)
		sc->flags |= ATHN_FLAG_11A;
	if (base->opFlags & AR_OPFLAGS_11G)
		sc->flags |= ATHN_FLAG_11G;
	if (base->opFlags & AR_OPFLAGS_11N)
		sc->flags |= ATHN_FLAG_11N;

	IEEE80211_ADDR_COPY(ic->ic_myaddr, eep->macAddr);
	sc->led_pin = base->wlanLedGpio;

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

	/* Set the number of HW key cache entries. */
	sc->kc_entries = AR_KEYTABLE_SIZE;

	sc->txchainmask = MS(base->txrxMask, AR_EEP_TX_MASK);
	sc->rxchainmask = MS(base->txrxMask, AR_EEP_RX_MASK);

	/* Fast PLL clock is always supported. */
	sc->flags |= ATHN_FLAG_FAST_PLL_CLOCK;

	/* Enable PA predistortion if supported. */
	if (base->featureEnable & AR_EEP_PAPRD)
		sc->flags |= ATHN_FLAG_PAPRD;
	/*
	 * Some 3-stream chips may exceed the PCIe power requirements,
	 * requiring to reduce the number of Tx chains in some cases.
	 */
	if ((base->miscConfiguration & AR_EEP_CHAIN_MASK_REDUCE) &&
	    sc->txchainmask == 0x7)
		sc->flags |= ATHN_FLAG_3TREDUCE_CHAIN;

	/* Select initialization values based on ROM. */
	type = MS(eep->baseEepHeader.txrxgain, AR_EEP_RX_GAIN);
	if (!AR_SREV_9485(sc)) {
		if (type == AR_EEP_RX_GAIN_WO_XLNA)
			sc->rx_gain = &ar9380_2_2_rx_gain_wo_xlna;
		else
			sc->rx_gain = &ar9380_2_2_rx_gain;
	} else
		sc->rx_gain = &ar9485_1_1_rx_gain;

	/* Select initialization values based on ROM. */
	type = MS(eep->baseEepHeader.txrxgain, AR_EEP_TX_GAIN);
	if (!AR_SREV_9485(sc)) {
		if (type == AR_EEP_TX_GAIN_HIGH_OB_DB)
			sc->tx_gain = &ar9380_2_2_tx_gain_high_ob_db;
		else if (type == AR_EEP_TX_GAIN_LOW_OB_DB)
			sc->tx_gain = &ar9380_2_2_tx_gain_low_ob_db;
		else if (type == AR_EEP_TX_GAIN_HIGH_POWER)
			sc->tx_gain = &ar9380_2_2_tx_gain_high_power;
		else
			sc->tx_gain = &ar9380_2_2_tx_gain;
	} else
		sc->tx_gain = &ar9485_1_1_tx_gain;
}

/* ar9380.c: ar9380_spur_mitigate */
static void
ar9380_spur_mitigate(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	/* NB: We call spur_mitigate_cck for 5GHz too, just to disable it. */
	ar9380_spur_mitigate_cck(sc, c, extc);
	ar9380_spur_mitigate_ofdm(sc, c, extc);
}

/* ar9380.c: ar9380_spur_mitigate_cck */
static void
ar9380_spur_mitigate_cck(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	/* NB: It is safe to call this function for 5GHz channels. */
	static const int16_t freqs[] = { 2420, 2440, 2464, 2480 };
	int i, spur, freq;
	uint32_t reg;

	for (i = 0; i < nitems(freqs); i++) {
		spur = freqs[i] - c->ic_freq;
		if (abs(spur) < 10)	/* +/- 10MHz range. */
			break;
	}
	if (i == nitems(freqs)) {
		/* Disable CCK spur mitigation. */
		reg = AR_READ(sc, AR_PHY_AGC_CONTROL);
		reg = RW(reg, AR_PHY_AGC_CONTROL_YCOK_MAX, 0x5);
		AR_WRITE(sc, AR_PHY_AGC_CONTROL, reg);
		reg = AR_READ(sc, AR_PHY_CCK_SPUR_MIT);
		reg = RW(reg, AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ, 0);
		reg &= ~AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT;
		AR_WRITE(sc, AR_PHY_CCK_SPUR_MIT, reg);
		AR_WRITE_BARRIER(sc);
		return;
	}
	freq = (spur * 524288) / 11;

	reg = AR_READ(sc, AR_PHY_AGC_CONTROL);
	reg = RW(reg, AR_PHY_AGC_CONTROL_YCOK_MAX, 0x7);
	AR_WRITE(sc, AR_PHY_AGC_CONTROL, reg);

	reg = AR_READ(sc, AR_PHY_CCK_SPUR_MIT);
	reg = RW(reg, AR_PHY_CCK_SPUR_MIT_CCK_SPUR_FREQ, freq);
	reg = RW(reg, AR_PHY_CCK_SPUR_MIT_SPUR_RSSI_THR, 0x7f);
	reg = RW(reg, AR_PHY_CCK_SPUR_MIT_SPUR_FILTER_TYPE, 0x2);
	reg |= AR_PHY_CCK_SPUR_MIT_USE_CCK_SPUR_MIT;
	AR_WRITE(sc, AR_PHY_CCK_SPUR_MIT, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9380.c: ar9380_spur_mitigate_ofdm */
static void
ar9380_spur_mitigate_ofdm(struct athn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	const struct ar9380_eeprom *eep = sc->eep;
	const uint8_t *spurchans;
	uint32_t reg;
	int idx, spur_delta_phase, spur_off, range, i;
	int freq, spur, spur_freq_sd, spur_subchannel_sd;

	if (IEEE80211_IS_CHAN_2GHZ(c))
		spurchans = eep->modalHeader2G.spurChans;
	else
		spurchans = eep->modalHeader5G.spurChans;
	if (spurchans[0] == 0)
		return;

	/* Disable OFDM spur mitigation. */
	AR_CLRBITS(sc, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_FILTER);

	reg = AR_READ(sc, AR_PHY_TIMING11);
	reg = RW(reg, AR_PHY_TIMING11_SPUR_FREQ_SD, 0);
	reg = RW(reg, AR_PHY_TIMING11_SPUR_DELTA_PHASE, 0);
	reg &= ~AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC;
	reg &= ~AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR;
	AR_WRITE(sc, AR_PHY_TIMING11, reg);

	AR_CLRBITS(sc, AR_PHY_SFCORR_EXT,
	    AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD);

	AR_CLRBITS(sc, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_RSSI);

	reg = AR_READ(sc, AR_PHY_SPUR_REG);
	reg = RW(reg, AR_PHY_SPUR_REG_MASK_RATE_CNTL, 0);
	reg &= ~AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI;
	reg &= ~AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT;
	reg &= ~AR_PHY_SPUR_REG_ENABLE_MASK_PPM;
	AR_WRITE(sc, AR_PHY_SPUR_REG, reg);
	AR_WRITE_BARRIER(sc);

	freq = c->ic_freq;
	if (extc != NULL) {
		range = 19;	/* +/- 19MHz range. */
		if (AR_READ(sc, AR_PHY_GEN_CTRL) & AR_PHY_GC_DYN2040_PRI_CH)
			freq += 10;
		else
			freq -= 10;
	} else
		range = 10;	/* +/- 10MHz range. */
	for (i = 0; i < AR9380_EEPROM_MODAL_SPURS; i++) {
		spur = spurchans[i];
		if (spur == 0)
			return;
		/* Convert to frequency. */
		if (IEEE80211_IS_CHAN_2GHZ(c))
			spur = 2300 + spur;
		else
			spur = 4900 + (spur * 5);
		spur -= freq;
		if (abs(spur) < range)
			break;
	}
	if (i == AR9380_EEPROM_MODAL_SPURS)
		return;

	/* Enable OFDM spur mitigation. */
	if (extc != NULL) {
		spur_delta_phase = (spur * 131072) / 5;
		reg = AR_READ(sc, AR_PHY_GEN_CTRL);
		if (spur < 0) {
			spur_subchannel_sd =
			    (reg & AR_PHY_GC_DYN2040_PRI_CH) == 0;
			spur_off = spur + 10;
		} else {
			spur_subchannel_sd =
			    (reg & AR_PHY_GC_DYN2040_PRI_CH) != 0;
			spur_off = spur - 10;
		}
	} else {
		spur_delta_phase = (spur * 262144) / 5;
		spur_subchannel_sd = 0;
		spur_off = spur;
	}
	spur_freq_sd = (spur_off * 512) / 11;

	AR_SETBITS(sc, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_FILTER);

	reg = AR_READ(sc, AR_PHY_TIMING11);
	reg = RW(reg, AR_PHY_TIMING11_SPUR_FREQ_SD, spur_freq_sd);
	reg = RW(reg, AR_PHY_TIMING11_SPUR_DELTA_PHASE, spur_delta_phase);
	reg |= AR_PHY_TIMING11_USE_SPUR_FILTER_IN_AGC;
	reg |= AR_PHY_TIMING11_USE_SPUR_FILTER_IN_SELFCOR;
	AR_WRITE(sc, AR_PHY_TIMING11, reg);

	reg = AR_READ(sc, AR_PHY_SFCORR_EXT);
	if (spur_subchannel_sd)
		reg |= AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD;
	else
		reg &= ~AR_PHY_SFCORR_EXT_SPUR_SUBCHANNEL_SD;
	AR_WRITE(sc, AR_PHY_SFCORR_EXT, reg);

	AR_SETBITS(sc, AR_PHY_TIMING4, AR_PHY_TIMING4_ENABLE_SPUR_RSSI);

	reg = AR_READ(sc, AR_PHY_SPUR_REG);
	reg = RW(reg, AR_PHY_SPUR_REG_MASK_RATE_CNTL, 0xff);
	reg = RW(reg, AR_PHY_SPUR_REG_SPUR_RSSI_THRESH, 34);
	reg |= AR_PHY_SPUR_REG_EN_VIT_SPUR_RSSI;
	if (AR_READ(sc, AR_PHY_MODE) & AR_PHY_MODE_DYNAMIC)
		reg |= AR_PHY_SPUR_REG_ENABLE_NF_RSSI_SPUR_MIT;
	reg |= AR_PHY_SPUR_REG_ENABLE_MASK_PPM;
	AR_WRITE(sc, AR_PHY_SPUR_REG, reg);

	idx = (spur * 16) / 5;
	if (idx < 0)
		idx--;

	/* Write pilot mask. */
	AR_SETBITS(sc, AR_PHY_TIMING4,
	    AR_PHY_TIMING4_ENABLE_PILOT_MASK |
	    AR_PHY_TIMING4_ENABLE_CHAN_MASK);

	reg = AR_READ(sc, AR_PHY_PILOT_SPUR_MASK);
	reg = RW(reg, AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_IDX_A, idx);
	reg = RW(reg, AR_PHY_PILOT_SPUR_MASK_CF_PILOT_MASK_A, 0x0c);
	AR_WRITE(sc, AR_PHY_PILOT_SPUR_MASK, reg);

	reg = AR_READ(sc, AR_PHY_SPUR_MASK_A);
	reg = RW(reg, AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_IDX_A, idx);
	reg = RW(reg, AR_PHY_SPUR_MASK_A_CF_PUNC_MASK_A, 0xa0);
	AR_WRITE(sc, AR_PHY_SPUR_MASK_A, reg);

	reg = AR_READ(sc, AR_PHY_CHAN_SPUR_MASK);
	reg = RW(reg, AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_IDX_A, idx);
	reg = RW(reg, AR_PHY_CHAN_SPUR_MASK_CF_CHAN_MASK_A, 0x0c);
	AR_WRITE(sc, AR_PHY_CHAN_SPUR_MASK, reg);
	AR_WRITE_BARRIER(sc);
}

/* ar9380.c: ar9380_swap_rom */
static void
ar9380_swap_rom(struct athn_softc *sc)
{
#if BYTE_ORDER == BIG_ENDIAN
	struct ar9380_eeprom *eep = sc->eep;
	struct ar9380_base_eep_hdr *base = &eep->baseEepHeader;
	struct ar9380_modal_eep_header *modal;
	int i;

	base->regDmn[0] = swap16(base->regDmn[0]);
	base->regDmn[1] = swap16(base->regDmn[1]);
	base->swreg = swap32(base->swreg);

	modal = &eep->modalHeader2G;
	modal->antCtrlCommon = swap32(modal->antCtrlCommon);
	modal->antCtrlCommon2 = swap32(modal->antCtrlCommon2);
	modal->papdRateMaskHt20 = swap32(modal->papdRateMaskHt20);
	modal->papdRateMaskHt40 = swap32(modal->papdRateMaskHt40);
	for (i = 0; i < AR9380_MAX_CHAINS; i++)
		modal->antCtrlChain[i] = swap16(modal->antCtrlChain[i]);

	modal = &eep->modalHeader5G;
	modal->antCtrlCommon = swap32(modal->antCtrlCommon);
	modal->antCtrlCommon2 = swap32(modal->antCtrlCommon2);
	modal->papdRateMaskHt20 = swap32(modal->papdRateMaskHt20);
	modal->papdRateMaskHt40 = swap32(modal->papdRateMaskHt40);
	for (i = 0; i < AR9380_MAX_CHAINS; i++)
		modal->antCtrlChain[i] = swap16(modal->antCtrlChain[i]);
#endif
}

/* ar9380.c: ar9485_init_swreg */
static void
ar9485_init_swreg(struct athn_softc *sc)
{
	const struct ar9380_eeprom *eep = sc->eep;
	uint32_t reg;

	ar9485_pmu_write(sc, AR_PHY_PMU2,
	    AR_READ(sc, AR_PHY_PMU2) & ~AR_PHY_PMU2_PGM);

	if (eep->baseEepHeader.featureEnable & AR_EEP_INTERNAL_REGULATOR) {
		ar9485_pmu_write(sc, AR_PHY_PMU1, 0x131dc17a);

		reg = AR_READ(sc, AR_PHY_PMU2);
		reg = (reg & ~0xffc00000) | 0x10000000;
		ar9485_pmu_write(sc, AR_PHY_PMU2, reg);
	} else {
		ar9485_pmu_write(sc, AR_PHY_PMU1,
		    AR_READ(sc, AR_PHY_PMU1) | AR_PHY_PMU1_PWD);
	}

	ar9485_pmu_write(sc, AR_PHY_PMU2,
	    AR_READ(sc, AR_PHY_PMU2) | AR_PHY_PMU2_PGM);
}

/* ar9380.c: ar9485_pmu_write */
static int
ar9485_pmu_write(struct athn_softc *sc, uint32_t addr, uint32_t val)
{
	int ntries;

	AR_WRITE(sc, addr, val);
	/* Wait for write to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		if (AR_READ(sc, addr) == val)
			return (0);
		AR_WRITE(sc, addr, val);	/* Insist. */
		AR_WRITE_BARRIER(sc);
		DELAY(10);
	}
	return (ETIMEDOUT);
}

/* athn_core.inc: athn_acktimeout */
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

/* athn_core.inc: athn_cap_noisefloor */
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

/* athn_core.inc: athn_chan2fbin */
static uint8_t
athn_chan2fbin(struct ieee80211_channel *c)
{
	if (IEEE80211_IS_CHAN_2GHZ(c))
		return (c->ic_freq - 2300);
	else
		return ((c->ic_freq - 4800) / 5);
}

/* athn_core.inc: athn_chan_sifs */
static int
athn_chan_sifs(struct ieee80211_channel *c)
{
	return IEEE80211_IS_CHAN_2GHZ(c) ? IEEE80211_DUR_DS_SIFS : 16;
}

/* athn_core.inc: athn_clock_rate */
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

/* athn_core.inc: athn_config_nonpcie */
static void
athn_config_nonpcie(struct athn_softc *sc)
{
	athn_write_serdes(sc, &ar_nonpcie_serdes);
}

/* athn_core.inc: athn_config_pcie */
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

/* athn_core.inc: athn_disable_interrupts */
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

/* athn_core.inc: athn_filter_noisefloor */
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

/* athn_core.inc: athn_get_chipid */
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

/* athn_core.inc: athn_get_delta_slope */
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

/* athn_core.inc: athn_get_pier_ival */
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

/* athn_core.inc: athn_hw_reset */
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

/* athn_core.inc: athn_init_dma */
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

/* athn_core.inc: athn_init_pll */
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

/* athn_core.inc: athn_init_qos */
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

/* athn_core.inc: athn_init_tx_queues */
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

/* athn_core.inc: athn_interpolate */
static int
athn_interpolate(int x, int x1, int y1, int x2, int y2)
{
	if (x1 == x2)	/* Prevents division by zero. */
		return (y1);
	/* Linear interpolation. */
	return (y1 + ((x - x1) * (y2 - y1)) / (x2 - x1));
}

/* athn_core.inc: athn_nf_hist_mid */
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

/* athn_core.inc: athn_reset */
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

/* athn_core.inc: athn_reset_key */
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

/* athn_core.inc: athn_reset_power_on */
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

/* athn_core.inc: athn_set_opmode */
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

/* athn_core.inc: athn_set_power_awake */
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

/* athn_core.inc: athn_set_rxfilter */
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

/* athn_core.inc: athn_setacktimeout */
static void
athn_setacktimeout(struct athn_softc *sc, struct ieee80211_channel *c, int slot)
{
	int ackto = athn_acktimeout(c, slot);
	uint32_t reg = AR_READ(sc, AR_TIME_OUT);
	reg = RW(reg, AR_TIME_OUT_ACK, ackto * athn_clock_rate(sc));
	AR_WRITE(sc, AR_TIME_OUT, reg);
	AR_WRITE_BARRIER(sc);
}

/* athn_core.inc: athn_setclockrate */
static void
athn_setclockrate(struct athn_softc *sc)
{
	int clockrate = athn_clock_rate(sc);
	uint32_t reg = AR_READ(sc, AR_USEC);
	reg = RW(reg, AR_USEC_USEC, clockrate - 1);
	AR_WRITE(sc, AR_USEC, reg);
	AR_WRITE_BARRIER(sc);
}

/* athn_core.inc: athn_setctstimeout */
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

/* athn_core.inc: athn_setsifs */
static void
athn_setsifs(struct athn_softc *sc)
{
	int sifs = athn_chan_sifs(sc->sc_ic.ic_bss->ni_chan);
	AR_WRITE(sc, AR_D_GBL_IFS_SIFS, (sifs - 2) * athn_clock_rate(sc));
	AR_WRITE_BARRIER(sc);
}

/* athn_core.inc: athn_start_noisefloor_calib */
static void
athn_start_noisefloor_calib(struct athn_softc *sc, int reset_history)
{
	

	if (reset_history)
		sc->nf_hist_nvalid = 0;

	sc->nf_calib_pending = 1;
	sc->nf_calib_ticks = (int)timer_ms();

	sc->ops.noisefloor_calib(sc);
}

/* athn_core.inc: athn_updateslot */
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

/* athn_core.inc: athn_write_serdes */
static void
athn_write_serdes(struct athn_softc *sc, const struct athn_serdes *serdes)
{
	int i;

	/* Write sequence to Serializer/Deserializer. */
	for (i = 0; i < serdes->nvals; i++)
		AR_WRITE(sc, serdes->regs[i], serdes->vals[i]);
	AR_WRITE_BARRIER(sc);
}
