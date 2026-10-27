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



static void
rtwn_rf_write(struct rtwn_softc *sc, int chain, uint16_t addr, uint32_t val);
static uint32_t
rtwn_rf_read(struct rtwn_softc *sc, int chain, uint8_t addr);
static uint8_t
rtwn_efuse_read_1(struct rtwn_softc *sc, uint16_t addr);
static void
rtwn_efuse_read(struct rtwn_softc *sc, uint8_t *rom, size_t size);
static void
rtwn_efuse_switch_power(struct rtwn_softc *sc);
static void
rtwn_r88e_read_rom(struct rtwn_softc *sc);
static void
rtwn_r88e_fw_reset(struct rtwn_softc *sc);
static void
rtwn_rf_init(struct rtwn_softc *sc);
static void
rtwn_cam_init(struct rtwn_softc *sc);
static void
rtwn_rxfilter_init(struct rtwn_softc *sc);
static void
rtwn_write_txpower(struct rtwn_softc *sc, int chain,
    uint16_t power[RTWN_POWER_COUNT]);
static void
rtwn_r88e_get_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, struct ieee80211_channel *extc,
    uint16_t power[RTWN_POWER_COUNT]);
static void
rtwn_set_txpower(struct rtwn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static void
rtwn_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc);
static int
rtwn_chan2group(int chan);
static int
rtwn_iq_calib_chain(struct rtwn_softc *sc, int chain, uint16_t tx[2],
    uint16_t rx[2]);
static void
rtwn_iq_calib_run(struct rtwn_softc *sc, int n, uint16_t tx[2][2],
    uint16_t rx[2][2], struct rtwn_iq_cal_regs *iq_cal_regs);
static int
rtwn_iq_calib_compare_results(uint16_t tx1[2][2], uint16_t rx1[2][2],
    uint16_t tx2[2][2], uint16_t rx2[2][2], int ntxchains);
static void
rtwn_iq_calib_write_results(struct rtwn_softc *sc, uint16_t tx[2],
    uint16_t rx[2], int chain);
static void
rtwn_iq_calib(struct rtwn_softc *sc);
static void
rtwn_lc_calib(struct rtwn_softc *sc);
static int
urtwn_llt_write(struct rtwn_softc *sc, uint32_t addr, uint32_t data);
static int
urtwn_llt_init(struct rtwn_softc *sc, int page_count);
static int
urtwn_r88e_power_on(struct rtwn_softc *sc);
static int
urtwn_fw_loadpage(void *cookie, int page, uint8_t *buf, int len);
static int
urtwn_dma_init(void *cookie);
static void
urtwn_mac_init(void *cookie);
static void
urtwn_bb_init(void *cookie);

static void
rtwn_rf_write(struct rtwn_softc *sc, int chain, uint16_t addr, uint32_t val)
{
	uint32_t param_addr;

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		rtwn_write_4(sc, R92C_FPGA0_POWER_SAVE,
		    rtwn_read_4(sc, R92C_FPGA0_POWER_SAVE) & ~0x20000);
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E))
		param_addr = SM(R88E_LSSI_PARAM_ADDR, addr);
	else
		param_addr = SM(R92C_LSSI_PARAM_ADDR, addr);

	rtwn_bb_write(sc, R92C_LSSI_PARAM(chain),
	    param_addr | SM(R92C_LSSI_PARAM_DATA, val));

	DELAY(1);

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		rtwn_write_4(sc, R92C_FPGA0_POWER_SAVE,
		    rtwn_read_4(sc, R92C_FPGA0_POWER_SAVE) | 0x20000);
	}
}

static uint32_t
rtwn_rf_read(struct rtwn_softc *sc, int chain, uint8_t addr)
{
	uint32_t reg[R92C_MAX_CHAINS], val;

	reg[0] = rtwn_bb_read(sc, R92C_HSSI_PARAM2(0));
	if (chain != 0)
		reg[chain] = rtwn_bb_read(sc, R92C_HSSI_PARAM2(chain));

	rtwn_bb_write(sc, R92C_HSSI_PARAM2(0),
	    reg[0] & ~R92C_HSSI_PARAM2_READ_EDGE);
	DELAY(1000);

	rtwn_bb_write(sc, R92C_HSSI_PARAM2(chain),
	    RW(reg[chain], R92C_HSSI_PARAM2_READ_ADDR, addr) |
	    R92C_HSSI_PARAM2_READ_EDGE);
	DELAY(1000);

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E)) {
		rtwn_bb_write(sc, R92C_HSSI_PARAM2(0),
		    reg[0] | R92C_HSSI_PARAM2_READ_EDGE);
		DELAY(1000);
	}

	if (rtwn_bb_read(sc, R92C_HSSI_PARAM1(chain)) & R92C_HSSI_PARAM1_PI)
		val = rtwn_bb_read(sc, R92C_HSPI_READBACK(chain));
	else
		val = rtwn_bb_read(sc, R92C_LSSI_READBACK(chain));
	return (MS(val, R92C_LSSI_READBACK_DATA));
}

static uint8_t
rtwn_efuse_read_1(struct rtwn_softc *sc, uint16_t addr)
{
	uint32_t reg;
	int ntries;

	reg = rtwn_read_4(sc, R92C_EFUSE_CTRL);
	reg = RW(reg, R92C_EFUSE_CTRL_ADDR, addr);
	reg &= ~R92C_EFUSE_CTRL_VALID;
	rtwn_write_4(sc, R92C_EFUSE_CTRL, reg);
	/* Wait for read operation to complete. */
	for (ntries = 0; ntries < 100; ntries++) {
		reg = rtwn_read_4(sc, R92C_EFUSE_CTRL);
		if (reg & R92C_EFUSE_CTRL_VALID)
			return (MS(reg, R92C_EFUSE_CTRL_DATA));
		DELAY(5);
	}
	printf("%s: could not read efuse byte at address 0x%x\n",
	    "rtl8188eu", addr);
	sc->ok = 0;
	return (0xff);
}

static void
rtwn_efuse_read(struct rtwn_softc *sc, uint8_t *rom, size_t size)
{
	uint8_t off, msk, tmp;
	uint16_t addr = 0;
	uint32_t reg;
	int i, len;

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_92C | RTWN_CHIP_88C)))
		rtwn_write_1(sc, R92C_EFUSE_ACCESS, R92C_EFUSE_ACCESS_ON);

	rtwn_efuse_switch_power(sc);

	/* Switch bank to 0 for wifi/bt later use. */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
		reg = rtwn_read_4(sc, R92C_EFUSE_TEST);
		reg = RW(reg, R92C_EFUSE_TEST_SEL, 0);
		rtwn_write_4(sc, R92C_EFUSE_TEST, reg);
	}

	memset(rom, 0xff, size);
	len = ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E) ? 256 : 512;
	while (sc->ok && addr < len) {
		reg = rtwn_efuse_read_1(sc, addr);
		if (reg == 0xff)
			break;
		addr++;

		/* Check for extended header. */
		if ((sc->sc_flags & RTWN_FLAG_EXT_HDR) &&
		    (reg & 0x1f) == 0x0f) {
			if (addr >= len) { sc->ok=0; break; }
			tmp = (reg & 0xe0) >> 5;
			reg = rtwn_efuse_read_1(sc, addr);
			addr++;
			if ((reg & 0x0f) != 0x0f)
				off = ((reg & 0xf0) >> 1) | tmp;
			else
				continue;
		} else
			off = reg >> 4;
		msk = reg & 0xf;
		for (i = 0; i < 4; i++) {
			if (msk & (1 << i))
				continue;
			if (addr + 1 >= len || off * 8u + i * 2u + 1 >= size) { sc->ok = 0; break; }
			rom[off * 8 + i * 2 + 0] = rtwn_efuse_read_1(sc, addr);
			addr++;
			rom[off * 8 + i * 2 + 1] = rtwn_efuse_read_1(sc, addr);
			addr++;
		}
	}
#ifdef RTWN_DEBUG
	if (rtwn_debug >= 2) {
		/* Dump ROM content. */
		printf("\n");
		for (i = 0; i < size; i++)
			printf("%02x:", rom[i]);
		printf("\n");
	}
#endif
	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_92C | RTWN_CHIP_88C)))
		rtwn_write_1(sc, R92C_EFUSE_ACCESS, R92C_EFUSE_ACCESS_OFF);
}

static void
rtwn_efuse_switch_power(struct rtwn_softc *sc)
{
	uint16_t reg;

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88F | RTWN_CHIP_92E))) {
		reg = rtwn_read_2(sc, R92C_SYS_ISO_CTRL);
		if (!(reg & R92C_SYS_ISO_CTRL_PWC_EV12V)) {
			rtwn_write_2(sc, R92C_SYS_ISO_CTRL,
			    reg | R92C_SYS_ISO_CTRL_PWC_EV12V);
		}
	}
	reg = rtwn_read_2(sc, R92C_SYS_FUNC_EN);
	if (!(reg & R92C_SYS_FUNC_EN_ELDR)) {
		rtwn_write_2(sc, R92C_SYS_FUNC_EN,
		    reg | R92C_SYS_FUNC_EN_ELDR);
	}
	reg = rtwn_read_2(sc, R92C_SYS_CLKR);
	if ((reg & (R92C_SYS_CLKR_LOADER_EN | R92C_SYS_CLKR_ANA8M)) !=
	    (R92C_SYS_CLKR_LOADER_EN | R92C_SYS_CLKR_ANA8M)) {
		rtwn_write_2(sc, R92C_SYS_CLKR,
		    reg | R92C_SYS_CLKR_LOADER_EN | R92C_SYS_CLKR_ANA8M);
	}
}

static void
rtwn_r88e_read_rom(struct rtwn_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct r88e_rom *rom = &sc->sc_r88e_rom;

	/* Read full ROM image. */
	rtwn_efuse_read(sc, (uint8_t *)&sc->sc_r88e_rom,
	    sizeof(sc->sc_r88e_rom));

	sc->crystal_cap = ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_PCI) ? 0x20 : rom->xtal;
	DPRINTF(("Crystal cap=0x%x\n", sc->crystal_cap));

	sc->regulatory = MS(rom->rf_board_opt, R92C_ROM_RF1_REGULATORY);
	DPRINTF(("regulatory type=%d\n", sc->regulatory));

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_PCI)
		IEEE80211_ADDR_COPY(ic->ic_myaddr, rom->r88ee_rom.macaddr);
	else
		IEEE80211_ADDR_COPY(ic->ic_myaddr, rom->r88eu_rom.macaddr);
}

static void
rtwn_r88e_fw_reset(struct rtwn_softc *sc)
{
	/* Reset MCU IO wrapper. */
	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)) {
		rtwn_write_1(sc, R92C_RSV_CTRL,
		    rtwn_read_1(sc, R92C_RSV_CTRL) & ~R92C_RSV_CTRL_WLOCK_00);
	}
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E) {
		rtwn_write_2(sc, R92C_RSV_CTRL,
		    rtwn_read_2(sc, R92C_RSV_CTRL) & ~R88E_RSV_CTRL_MCU_RST);
	} else {
		rtwn_write_2(sc, R92C_RSV_CTRL,
		    rtwn_read_2(sc, R92C_RSV_CTRL) & ~R88E_RSV_CTRL_MIO_EN);
	}
	rtwn_write_2(sc, R92C_SYS_FUNC_EN,
	    rtwn_read_2(sc, R92C_SYS_FUNC_EN) & ~R92C_SYS_FUNC_EN_CPUEN);

	/* Enable MCU IO wrapper. */
	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)) {
		rtwn_write_1(sc, R92C_RSV_CTRL,
		    rtwn_read_1(sc, R92C_RSV_CTRL) & ~R92C_RSV_CTRL_WLOCK_00);
	}
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E) {
		rtwn_write_2(sc, R92C_RSV_CTRL,
		    rtwn_read_2(sc, R92C_RSV_CTRL) | R88E_RSV_CTRL_MCU_RST);
	} else {
		rtwn_write_2(sc, R92C_RSV_CTRL,
		    rtwn_read_2(sc, R92C_RSV_CTRL) | R88E_RSV_CTRL_MIO_EN);
	}
	rtwn_write_2(sc, R92C_SYS_FUNC_EN,
	    rtwn_read_2(sc, R92C_SYS_FUNC_EN) | R92C_SYS_FUNC_EN_CPUEN);
}

static void
rtwn_rf_init(struct rtwn_softc *sc)
{
	const struct r92c_rf_prog *prog;
	uint32_t reg, type;
	int i, j, idx, off;

	/* Select RF programming based on board type. */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E)
		prog = rtl8188eu_rf_prog;
	else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)
		prog = rtl8188ftv_rf_prog;
	else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)
		prog = rtl8192e_rf_prog;
	else if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92C)) {
		if (sc->board_type == R92C_BOARD_TYPE_MINICARD)
			prog = rtl8188ce_rf_prog;
		else if (sc->board_type == R92C_BOARD_TYPE_HIGHPA)
			prog = rtl8188ru_rf_prog;
		else
			prog = rtl8188cu_rf_prog;
	} else
		prog = rtl8192ce_rf_prog;

	for (i = 0; i < sc->nrxchains; i++) {
		/* Save RF_ENV control type. */
		idx = i / 2;
		off = (i % 2) * 16;
		reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(idx));
		type = (reg >> off) & 0x10;

		/* Set RF_ENV enable. */
		reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(i));
		reg |= 0x100000;
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(i), reg);
		DELAY(50);
		/* Set RF_ENV output high. */
		reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(i));
		reg |= 0x10;
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(i), reg);
		DELAY(50);
		/* Set address and data lengths of RF registers. */
		reg = rtwn_bb_read(sc, R92C_HSSI_PARAM2(i));
		reg &= ~R92C_HSSI_PARAM2_ADDR_LENGTH;
		rtwn_bb_write(sc, R92C_HSSI_PARAM2(i), reg);
		DELAY(50);
		reg = rtwn_bb_read(sc, R92C_HSSI_PARAM2(i));
		reg &= ~R92C_HSSI_PARAM2_DATA_LENGTH;
		rtwn_bb_write(sc, R92C_HSSI_PARAM2(i), reg);
		DELAY(50);

		/* Write RF initialization values for this chain. */
		for (j = 0; j < prog[i].count; j++) {
			switch (prog[i].regs[j]) {
			case 0xfe:
			case 0xffe:
				DELAY(50000);
				continue;
			case 0xfd:
				DELAY(5000);
				continue;
			case 0xfc:
				DELAY(1000);
				continue;
			case 0xfb:
				DELAY(50);
				continue;
			case 0xfa:
				DELAY(5);
				continue;
			case 0xf9:
				DELAY(1);
				continue;
			}
			rtwn_rf_write(sc, i, prog[i].regs[j],
			    prog[i].vals[j]);
			DELAY(5);
		}

		/* Restore RF_ENV control type. */
		reg = rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(idx));
		reg &= ~(0x10 << off) | (type << off);
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(idx), reg);

		/* Cache RF register CHNLBW. */
		sc->rf_chnlbw[i] = rtwn_rf_read(sc, i, R92C_RF_CHNLBW);
	}

	/* magic value for HP 8188EEs */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) == (RTWN_CHIP_88E | RTWN_CHIP_PCI)) {
		struct r88e_rom *rom = &sc->sc_r88e_rom;
		if (rom->r88ee_rom.svid == 0x103c &&
		    rom->r88ee_rom.smid == 0x197d)
			rtwn_rf_write(sc, 0, 0x52, 0x7e4bd);
	}

	if (((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_UMC_A_CUT | RTWN_CHIP_92C)) ==
	    RTWN_CHIP_UMC_A_CUT) {
		rtwn_rf_write(sc, 0, R92C_RF_RX_G1, 0x30255);
		rtwn_rf_write(sc, 0, R92C_RF_RX_G2, 0x50a00);
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_23A) {
		rtwn_rf_write(sc, 0, 0x0C, 0x894ae);
		rtwn_rf_write(sc, 0, 0x0A, 0x1af31);
		rtwn_rf_write(sc, 0, R92C_RF_IPA, 0x8f425);
		rtwn_rf_write(sc, 0, R92C_RF_SYN_G(1), 0x4f200);
		rtwn_rf_write(sc, 0, R92C_RF_RCK1, 0x44053);
		rtwn_rf_write(sc, 0, R92C_RF_RCK2, 0x80201);
	}
}

static void
rtwn_cam_init(struct rtwn_softc *sc)
{
	/* Invalidate all CAM entries. */
	rtwn_write_4(sc, R92C_CAMCMD,
	    R92C_CAMCMD_POLLING | R92C_CAMCMD_CLR);
}

static void
rtwn_rxfilter_init(struct rtwn_softc *sc)
{
	/* Initialize Rx filter. */
	rtwn_write_4(sc, R92C_RCR,
	    R92C_RCR_AAP | R92C_RCR_APM | R92C_RCR_AM | R92C_RCR_AB |
	    R92C_RCR_APP_ICV | R92C_RCR_AMF | R92C_RCR_HTC_LOC_CTRL |
	    R92C_RCR_APP_MIC | R92C_RCR_APP_PHYSTS);
	/* Accept all multicast frames. */
	rtwn_write_4(sc, R92C_MAR + 0, 0xffffffff);
	rtwn_write_4(sc, R92C_MAR + 4, 0xffffffff);
	/* Accept all management frames. */
	rtwn_write_2(sc, R92C_RXFLTMAP0, 0xffff);
	/* Reject all control frames. */
	rtwn_write_2(sc, R92C_RXFLTMAP1, 0x0000);
	/* Accept all data frames. */
	rtwn_write_2(sc, R92C_RXFLTMAP2, 0xffff);
}

static void
rtwn_write_txpower(struct rtwn_softc *sc, int chain,
    uint16_t power[RTWN_POWER_COUNT])
{
	uint32_t reg;

	/* Write per-CCK rate Tx power. */
	if (chain == 0) {
		reg = rtwn_bb_read(sc, R92C_TXAGC_A_CCK1_MCS32);
		reg = RW(reg, R92C_TXAGC_A_CCK1,  power[RTWN_POWER_CCK1]);
		rtwn_bb_write(sc, R92C_TXAGC_A_CCK1_MCS32, reg);
		reg = rtwn_bb_read(sc, R92C_TXAGC_B_CCK11_A_CCK2_11);
		reg = RW(reg, R92C_TXAGC_A_CCK2,  power[RTWN_POWER_CCK2]);
		reg = RW(reg, R92C_TXAGC_A_CCK55, power[RTWN_POWER_CCK55]);
		reg = RW(reg, R92C_TXAGC_A_CCK11, power[RTWN_POWER_CCK11]);
		rtwn_bb_write(sc, R92C_TXAGC_B_CCK11_A_CCK2_11, reg);
	} else {
		reg = rtwn_bb_read(sc, R92C_TXAGC_B_CCK1_55_MCS32);
		reg = RW(reg, R92C_TXAGC_B_CCK1,  power[RTWN_POWER_CCK1]);
		reg = RW(reg, R92C_TXAGC_B_CCK2,  power[RTWN_POWER_CCK2]);
		reg = RW(reg, R92C_TXAGC_B_CCK55, power[RTWN_POWER_CCK55]);
		rtwn_bb_write(sc, R92C_TXAGC_B_CCK1_55_MCS32, reg);
		reg = rtwn_bb_read(sc, R92C_TXAGC_B_CCK11_A_CCK2_11);
		reg = RW(reg, R92C_TXAGC_B_CCK11, power[RTWN_POWER_CCK11]);
		rtwn_bb_write(sc, R92C_TXAGC_B_CCK11_A_CCK2_11, reg);
	}
	/* Write per-OFDM rate Tx power. */
	rtwn_bb_write(sc, R92C_TXAGC_RATE18_06(chain),
	    SM(R92C_TXAGC_RATE06, power[RTWN_POWER_OFDM6]) |
	    SM(R92C_TXAGC_RATE09, power[RTWN_POWER_OFDM9]) |
	    SM(R92C_TXAGC_RATE12, power[RTWN_POWER_OFDM12]) |
	    SM(R92C_TXAGC_RATE18, power[RTWN_POWER_OFDM18]));
	rtwn_bb_write(sc, R92C_TXAGC_RATE54_24(chain),
	    SM(R92C_TXAGC_RATE24, power[RTWN_POWER_OFDM24]) |
	    SM(R92C_TXAGC_RATE36, power[RTWN_POWER_OFDM36]) |
	    SM(R92C_TXAGC_RATE48, power[RTWN_POWER_OFDM48]) |
	    SM(R92C_TXAGC_RATE54, power[RTWN_POWER_OFDM54]));
	/* Write per-MCS Tx power. */
	rtwn_bb_write(sc, R92C_TXAGC_MCS03_MCS00(chain),
	    SM(R92C_TXAGC_MCS00,  power[RTWN_POWER_MCS( 0)]) |
	    SM(R92C_TXAGC_MCS01,  power[RTWN_POWER_MCS( 1)]) |
	    SM(R92C_TXAGC_MCS02,  power[RTWN_POWER_MCS( 2)]) |
	    SM(R92C_TXAGC_MCS03,  power[RTWN_POWER_MCS( 3)]));
	rtwn_bb_write(sc, R92C_TXAGC_MCS07_MCS04(chain),
	    SM(R92C_TXAGC_MCS04,  power[RTWN_POWER_MCS( 4)]) |
	    SM(R92C_TXAGC_MCS05,  power[RTWN_POWER_MCS( 5)]) |
	    SM(R92C_TXAGC_MCS06,  power[RTWN_POWER_MCS( 6)]) |
	    SM(R92C_TXAGC_MCS07,  power[RTWN_POWER_MCS( 7)]));
	if (sc->ntxchains > 1) {
		rtwn_bb_write(sc, R92C_TXAGC_MCS11_MCS08(chain),
		    SM(R92C_TXAGC_MCS08,  power[RTWN_POWER_MCS( 8)]) |
		    SM(R92C_TXAGC_MCS09,  power[RTWN_POWER_MCS( 9)]) |
		    SM(R92C_TXAGC_MCS10,  power[RTWN_POWER_MCS(10)]) |
		    SM(R92C_TXAGC_MCS11,  power[RTWN_POWER_MCS(11)]));
		rtwn_bb_write(sc, R92C_TXAGC_MCS15_MCS12(chain),
		    SM(R92C_TXAGC_MCS12,  power[RTWN_POWER_MCS(12)]) |
		    SM(R92C_TXAGC_MCS13,  power[RTWN_POWER_MCS(13)]) |
		    SM(R92C_TXAGC_MCS14,  power[RTWN_POWER_MCS(14)]) |
		    SM(R92C_TXAGC_MCS15,  power[RTWN_POWER_MCS(15)]));
	}
}

static void
rtwn_r88e_get_txpower(struct rtwn_softc *sc, int chain,
    struct ieee80211_channel *c, struct ieee80211_channel *extc,
    uint16_t power[RTWN_POWER_COUNT])
{
    (void)chain;
	struct ieee80211com *ic = &sc->sc_ic;
	struct r88e_rom *rom = &sc->sc_r88e_rom;
	int cckpow, htpow, ofdmpow;
	int8_t diff;
	int ridx, chan, group;

	/* Determine channel group. */
	chan = ieee80211_chan2ieee(ic, c);	/* XXX center freq! */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)
		group = rtwn_chan2group(chan);
	else {
		if (chan <= 2)
			group = 0;
		else if (chan <= 5)
			group = 1;
		else if (chan <= 8)
			group = 2;
		else if (chan <= 11)
			group = 3;
		else if (chan <= 13)
			group = 4;
		else
			group = 5;
	}

	memset(power, 0, RTWN_POWER_COUNT * sizeof(power[0]));

	/* Compute per-CCK rate Tx power. */
	cckpow = rom->txpwr.cck_tx_pwr[group];
	for (ridx = RTWN_RIDX_CCK1; ridx <= RTWN_RIDX_CCK11; ridx++) {
		if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)
			power[ridx] = cckpow;
		else
			power[ridx] = (uint16_t)MAX(0, (ridx == RTWN_RIDX_CCK2) ? cckpow - 9 : cckpow);
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)
		htpow = rom->txpwr.ht40_tx_pwr[group];
	else
		htpow = (group == 5) ? rom->txpwr.ht40_tx_pwr[group - 1] :
		     rom->txpwr.ht40_tx_pwr[group];

	/* Compute per-OFDM rate Tx power. */
	diff = RTWN_SIGN4TO8(MS(rom->txpwr.ht20_ofdm_tx_pwr_diff,
	    R88E_ROM_TXPWR_OFDM_DIFF));
	ofdmpow = MAX(0, htpow + diff);
	for (ridx = RTWN_RIDX_OFDM6; ridx <= RTWN_RIDX_OFDM54; ridx++) {
		power[ridx] = ofdmpow;
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}

	/* Compute per-MCS Tx power. */
	if (extc == NULL) {
		diff = RTWN_SIGN4TO8(MS(rom->txpwr.ht20_ofdm_tx_pwr_diff,
		    R88E_ROM_TXPWR_HT20_DIFF));
		htpow = MAX(0, htpow + diff);
	}
	for (ridx = RTWN_RIDX_MCS0; ridx < RTWN_RIDX_MCS8; ridx++) {
		power[ridx] = htpow;
		if (power[ridx] > R92C_MAX_TX_PWR)
			power[ridx] = R92C_MAX_TX_PWR;
	}
}

static void
rtwn_set_txpower(struct rtwn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	uint16_t power[RTWN_POWER_COUNT];
	int i;

	for (i = 0; i < sc->ntxchains; i++) {
		/* Compute per-rate Tx power values. */
		rtwn_r88e_get_txpower(sc, i, c, extc, power);
		/* Write per-rate Tx power values to hardware. */
		rtwn_write_txpower(sc, i, power);
	}
}

static void
rtwn_set_chan(struct rtwn_softc *sc, struct ieee80211_channel *c,
    struct ieee80211_channel *extc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	u_int chan;
	uint32_t reg;
	int i;

	chan = ieee80211_chan2ieee(ic, c);	/* XXX center freq! */

	/* Set Tx power for this new channel. */
	rtwn_set_txpower(sc, c, extc);

	if (extc != NULL) {
		/* Is secondary channel below or above primary? */
		int prichlo = c->ic_freq < extc->ic_freq;

		if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
			reg = rtwn_read_2(sc, R92C_WMAC_TRXPTCL_CTL);
			reg &= ~R92C_WMAC_TRXPTCL_CTL_BW_MASK;
			reg |= R92C_WMAC_TRXPTCL_CTL_BW_40;
			rtwn_write_2(sc, R92C_WMAC_TRXPTCL_CTL, reg);
			rtwn_write_1(sc, R92E_DATA_SC, 0);
		} else {
			rtwn_write_1(sc, R92C_BWOPMODE,
			    rtwn_read_1(sc, R92C_BWOPMODE) &
			    ~R92C_BWOPMODE_20MHZ);
		}

		reg = rtwn_read_1(sc, R92C_RRSR + 2);
		reg = (reg & ~0x6f) | (prichlo ? 1 : 2) << 5;
		rtwn_write_1(sc, R92C_RRSR + 2, reg);

		rtwn_bb_write(sc, R92C_FPGA0_RFMOD,
		    rtwn_bb_read(sc, R92C_FPGA0_RFMOD) | R92C_RFMOD_40MHZ);
		rtwn_bb_write(sc, R92C_FPGA1_RFMOD,
		    rtwn_bb_read(sc, R92C_FPGA1_RFMOD) | R92C_RFMOD_40MHZ);

		/* Set CCK side band. */
		reg = rtwn_bb_read(sc, R92C_CCK0_SYSTEM);
		reg = (reg & ~0x00000010) | (prichlo ? 0 : 1) << 4;
		rtwn_bb_write(sc, R92C_CCK0_SYSTEM, reg);

		reg = rtwn_bb_read(sc, R92C_OFDM1_LSTF);
		reg = (reg & ~0x00000c00) | (prichlo ? 1 : 2) << 10;
		rtwn_bb_write(sc, R92C_OFDM1_LSTF, reg);

		if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88E | RTWN_CHIP_92E))) {
			rtwn_bb_write(sc, R92C_FPGA0_ANAPARAM2,
			    rtwn_bb_read(sc, R92C_FPGA0_ANAPARAM2) &
			    ~R92C_FPGA0_ANAPARAM2_CBW20);
		}

		reg = rtwn_bb_read(sc, 0x818);
		reg = (reg & ~0x0c000000) | (prichlo ? 2 : 1) << 26;
		rtwn_bb_write(sc, 0x818, reg);

		/* Select 40MHz bandwidth. */
		for (i = 0; i < sc->nrxchains; i++) {
			rtwn_rf_write(sc, i, R92C_RF_CHNLBW,
			    (sc->rf_chnlbw[i] & ~0xfff) | chan);
		}
	} else {
		if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
			reg = rtwn_read_2(sc, R92C_WMAC_TRXPTCL_CTL);
			reg &= ~R92C_WMAC_TRXPTCL_CTL_BW_MASK;
			rtwn_write_2(sc, R92C_WMAC_TRXPTCL_CTL, reg);
			rtwn_write_1(sc, R92E_DATA_SC, 0);
		} else if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)) {
			rtwn_write_1(sc, R92C_BWOPMODE,
			    rtwn_read_1(sc, R92C_BWOPMODE) |
			    R92C_BWOPMODE_20MHZ);
		}

		rtwn_bb_write(sc, R92C_FPGA0_RFMOD,
		    rtwn_bb_read(sc, R92C_FPGA0_RFMOD) & ~R92C_RFMOD_40MHZ);
		rtwn_bb_write(sc, R92C_FPGA1_RFMOD,
		    rtwn_bb_read(sc, R92C_FPGA1_RFMOD) & ~R92C_RFMOD_40MHZ);

		if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) &
		    (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E))) {
			rtwn_bb_write(sc, R92C_FPGA0_ANAPARAM2,
			    rtwn_bb_read(sc, R92C_FPGA0_ANAPARAM2) |
			    R92C_FPGA0_ANAPARAM2_CBW20);
		} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88F | RTWN_CHIP_92E)) {
			if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
				reg = rtwn_bb_read(sc, R92C_FPGA0_RFMOD);
				reg = (reg & ~0x00000700) | 0x7 << 8;
				rtwn_bb_write(sc, R92C_FPGA0_RFMOD, reg);
				reg = rtwn_bb_read(sc, R92C_FPGA0_RFMOD);
				reg = (reg & ~0x00007000) | 0x5 << 12;
				rtwn_bb_write(sc, R92C_FPGA0_RFMOD, reg);
			}

			reg = rtwn_bb_read(sc, R92C_OFDM0_TX_PSDO_NOISE_WEIGHT);
			reg &= ~0xc0000000;
			rtwn_bb_write(sc, R92C_OFDM0_TX_PSDO_NOISE_WEIGHT, reg);

			if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
				/* Small bandwidth */
				reg = rtwn_bb_read(sc,
				    R92C_OFDM0_TX_PSDO_NOISE_WEIGHT);
				reg |= 0x30000000;
				rtwn_bb_write(sc,
				    R92C_OFDM0_TX_PSDO_NOISE_WEIGHT, reg);
				/* ADC buffer clk */
				rtwn_bb_write(sc, R92C_OFDM0_RXAFE,
				    rtwn_bb_read(sc, R92C_OFDM0_RXAFE) |
				    0x30000000);
				/* OFDM Rx DFIR */
				rtwn_bb_write(sc, R88F_RX_DFIR,
				    rtwn_bb_read(sc, R88F_RX_DFIR) &
				    ~0x00080000);
				reg = rtwn_bb_read(sc, R88F_RX_DFIR);
				reg = (reg & ~0x00f00000) | 0x3 << 15;
				rtwn_bb_write(sc, R88F_RX_DFIR, reg);
			}
		}

		/* Select 20MHz bandwidth. */
		for (i = 0; i < sc->nrxchains; i++) {
			rtwn_rf_write(sc, i, R92C_RF_CHNLBW,
			    (sc->rf_chnlbw[i] & ~0xfff) | chan |
			    (((RTWN_CHIP_88E | RTWN_CHIP_USB) &
			    (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E)) ?
			    R88E_RF_CHNLBW_BW20 : R92C_RF_CHNLBW_BW20));

			if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
				rtwn_rf_write(sc, i, 0x87, 0x65);
				rtwn_rf_write(sc, i, 0x1c, 0);
				rtwn_rf_write(sc, i, 0xdf, 0x0140);
				rtwn_rf_write(sc, i, 0x1b, 0x1c6c);
			}
		}
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) == (RTWN_CHIP_88E | RTWN_CHIP_PCI))
		DELAY(25000);
}

static int
rtwn_chan2group(int chan)
{
	int group;

	if (chan <= 2)
		group = 0;
	else if (chan <= 5)
		group = 1;
	else if (chan <= 8)
		group = 2;
	else if (chan <= 11)
		group = 3;
	else
		group = 4;

	return (group);
}

static int
rtwn_iq_calib_chain(struct rtwn_softc *sc, int chain, uint16_t tx[2],
    uint16_t rx[2])
{
	uint32_t status;
	int offset = chain * 0x20;
	uint32_t iqk_tone_92c[] = {
		0x10008c1f, 0x10008c1f, 0x82140102, 0x28160202, 0x10008c22
	};
	uint32_t iqk_tone_92e[] = {
		0x18008c1c, 0x38008c1c, 0x82140303, 0x68160000, 0x38008c1c
	};
	uint32_t *iqk_tone;

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)
		iqk_tone = iqk_tone_92e;
	else
		iqk_tone = iqk_tone_92c;

	if (chain == 0) {	/* IQ calibration for chain 0. */
		/* IQ calibration settings for chain 0. */
		rtwn_bb_write(sc, R92C_TX_IQK_TONE_A, iqk_tone[0]);
		rtwn_bb_write(sc, R92C_RX_IQK_TONE_B, iqk_tone[1]);
		rtwn_bb_write(sc, R92C_TX_IQK_PI_A, iqk_tone[2]);

		if (sc->ntxchains > 1) {
			rtwn_bb_write(sc, R92C_RX_IQK_PI_A, iqk_tone[3]);
			/* IQ calibration settings for chain 1. */
			rtwn_bb_write(sc, R92C_TX_IQK_TONE_B, iqk_tone[4]);
			rtwn_bb_write(sc, R92C_RX_IQK_TONE_B, iqk_tone[4]);
			rtwn_bb_write(sc, R92C_TX_IQK_PI_B, 0x82140102);
			rtwn_bb_write(sc, R92C_RX_IQK_PI_B, 0x28160202);
		} else
			rtwn_bb_write(sc, R92C_RX_IQK_PI_A, 0x28160502);

		/* LO calibration settings. */
		if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88E | RTWN_CHIP_92E))
			rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x00462911);
		else
			rtwn_bb_write(sc, R92C_IQK_AGC_RSP, 0x001028d1);
		/* We're doing LO and IQ calibration in one shot. */
		rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf9000000);
		rtwn_bb_write(sc, R92C_IQK_AGC_PTS, 0xf8000000);

	} else {		/* IQ calibration for chain 1. */
		/* We're doing LO and IQ calibration in one shot. */
		rtwn_bb_write(sc, R92C_IQK_AGC_CONT, 0x00000002);
		rtwn_bb_write(sc, R92C_IQK_AGC_CONT, 0x00000000);
	}

	/* Give LO and IQ calibrations the time to complete. */
	DELAY(1000);

	/* Read IQ calibration status. */
	status = rtwn_bb_read(sc, 0xeac);

	if (status & (1 << (28 + chain * 3)))
		return (0);	/* Tx failed. */
	/* Read Tx IQ calibration results. */
	tx[0] = (rtwn_bb_read(sc, R92C_TX_POWER_BEFORE_IQK_A + offset) >> 16)
	    & 0x3ff;
	tx[1] = (rtwn_bb_read(sc, R92C_TX_POWER_AFTER_IQK_A + offset) >> 16)
	    & 0x3ff;
	if (tx[0] == 0x142 || tx[1] == 0x042)
		return (0);	/* Tx failed. */

	if (status & (1 << (27 + chain * 3)))
		return (1);	/* Rx failed. */
	/* Read Rx IQ calibration results. */
	rx[0] = (rtwn_bb_read(sc, R92C_RX_POWER_BEFORE_IQK_A_2 + offset) >> 16)
	    & 0x3ff;
	rx[1] = (rtwn_bb_read(sc, R92C_RX_POWER_AFTER_IQK_A_2 + offset) >> 16)
	    & 0x3ff;
	if (rx[0] == 0x132 || rx[1] == 0x036)
		return (1);	/* Rx failed. */

	return (3);	/* Both Tx and Rx succeeded. */
}

static void
rtwn_iq_calib_run(struct rtwn_softc *sc, int n, uint16_t tx[2][2],
    uint16_t rx[2][2], struct rtwn_iq_cal_regs *iq_cal_regs)
{
	static const uint16_t reg_adda[16] = {
		0x85c, 0xe6c, 0xe70, 0xe74,
		0xe78, 0xe7c, 0xe80, 0xe84,
		0xe88, 0xe8c, 0xed0, 0xed4,
		0xed8, 0xedc, 0xee0, 0xeec
	};
	static const uint32_t adda_92c[] = {
		0x0b1b25a0, 0x0bdb25a0, 0x04db25a4, 0x0b1b25a4
	};
	static const uint32_t adda_92e[] = {
		0x0fc01616, 0x0fc01616, 0x0fc01616, 0x0fc01616
	};
	const uint32_t *adda_vals;

	int i, chain;
	uint32_t hssi_param1, reg;
	uint8_t xa_agc, xb_agc;

	xa_agc = rtwn_bb_read(sc, R92C_OFDM0_AGCCORE1(0)) & 0xff;
	xb_agc = rtwn_bb_read(sc, R92C_OFDM0_AGCCORE1(1)) & 0xff;

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)
		adda_vals = adda_92e;
	else
		adda_vals = adda_92c;

	if (n == 0) {
		for (i = 0; i < nitems(reg_adda); i++)
			iq_cal_regs->adda[i] = rtwn_bb_read(sc, reg_adda[i]);

		iq_cal_regs->txpause = rtwn_read_1(sc, R92C_TXPAUSE);
		iq_cal_regs->bcn_ctrl = rtwn_read_1(sc, R92C_BCN_CTRL);
		iq_cal_regs->bcn_ctrl1 = rtwn_read_1(sc, R92C_BCN_CTRL1);
		iq_cal_regs->gpio_muxcfg = rtwn_read_4(sc, R92C_GPIO_MUXCFG);
	}

	if (sc->ntxchains == 1) {
		rtwn_bb_write(sc, reg_adda[0], adda_vals[0]);
		for (i = 1; i < nitems(reg_adda); i++)
			rtwn_bb_write(sc, reg_adda[i], adda_vals[1]);
	} else {
		for (i = 0; i < nitems(reg_adda); i++)
			rtwn_bb_write(sc, reg_adda[i], adda_vals[2]);
	}

	if (n == 0) {
		iq_cal_regs->ofdm0_trxpathena =
		    rtwn_bb_read(sc, R92C_OFDM0_TRXPATHENA);
		iq_cal_regs->ofdm0_trmuxpar =
		    rtwn_bb_read(sc, R92C_OFDM0_TRMUXPAR);
		iq_cal_regs->fpga0_rfifacesw0 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(0));
		iq_cal_regs->fpga0_rfifacesw1 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(1));
		iq_cal_regs->fpga0_rfifaceoe0 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(0));
		iq_cal_regs->fpga0_rfifaceoe1 =
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACEOE(1));
		iq_cal_regs->config_ant_a =
		    rtwn_bb_read(sc, R92C_CONFIG_ANT_A);
		iq_cal_regs->config_ant_b =
		    rtwn_bb_read(sc, R92C_CONFIG_ANT_B);
		iq_cal_regs->cck0_afesetting =
		    rtwn_bb_read(sc, R92C_CCK0_AFESETTING);
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		rtwn_write_4(sc, R92C_CCK0_AFESETTING, rtwn_read_4(sc,
		    R92C_CCK0_AFESETTING) | 0x0f000000);
	} else {
		hssi_param1 = rtwn_bb_read(sc, R92C_HSSI_PARAM1(0));
		if (!(hssi_param1 & R92C_HSSI_PARAM1_PI)) {
			rtwn_bb_write(sc, R92C_HSSI_PARAM1(0),
			    hssi_param1 | R92C_HSSI_PARAM1_PI);
			rtwn_bb_write(sc, R92C_HSSI_PARAM1(1),
			    hssi_param1 | R92C_HSSI_PARAM1_PI);
		}
	}

	rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, 0x03a05600);
	rtwn_bb_write(sc, R92C_OFDM0_TRMUXPAR, 0x000800e4);

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(1), 0x22208200);
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(0),
		    rtwn_bb_read(sc, R92C_FPGA0_RFIFACESW(0)) | (1 << 10) |
		    (1 << 26));

		rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(0), rtwn_bb_read(sc,
		    R92C_FPGA0_RFIFACEOE(0)) | (1 << 10));
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(1), rtwn_bb_read(sc,
		    R92C_FPGA0_RFIFACEOE(1)) | (1 << 10));
	} else {
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(1), 0x22204000);

		if (sc->ntxchains > 1) {
			rtwn_bb_write(sc, R92C_LSSI_PARAM(0), 0x00010000);
			rtwn_bb_write(sc, R92C_LSSI_PARAM(1), 0x00010000);
		}

		rtwn_write_1(sc, R92C_TXPAUSE, R92C_TXPAUSE_AC_VO |
		    R92C_TXPAUSE_AC_VI | R92C_TXPAUSE_AC_BE |
		    R92C_TXPAUSE_AC_BK | R92C_TXPAUSE_MGNT |
		    R92C_TXPAUSE_HIGH);
	}
	rtwn_write_1(sc, R92C_BCN_CTRL,
	    iq_cal_regs->bcn_ctrl & ~(R92C_BCN_CTRL_EN_BCN));
	rtwn_write_1(sc, R92C_BCN_CTRL1,
	    iq_cal_regs->bcn_ctrl1 & ~(R92C_BCN_CTRL_EN_BCN));
	rtwn_write_1(sc, R92C_GPIO_MUXCFG,
	    iq_cal_regs->gpio_muxcfg & ~(R92C_GPIO_MUXCFG_ENBT));

	rtwn_bb_write(sc, R92C_CONFIG_ANT_A, 0x00080000);
	if (sc->ntxchains > 1)
		rtwn_bb_write(sc, R92C_CONFIG_ANT_B, 0x00080000);

	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x80800000);
	rtwn_bb_write(sc, R92C_TX_IQK, 0x01007c00);
	rtwn_bb_write(sc, R92C_RX_IQK, 0x01004800);

	rtwn_bb_write(sc, R92C_CONFIG_ANT_A, 0x00080000);

	for (chain = 0; chain < sc->ntxchains; chain++) {
		if (chain > 0) {
			/* Put chain 0 on standby. */
			rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x00);
			rtwn_bb_write(sc, R92C_LSSI_PARAM(0), 0x00010000);
			rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x80800000);

			/* Enable chain 1. */
			for (i = 0; i < nitems(reg_adda); i++)
				rtwn_bb_write(sc, reg_adda[i], adda_vals[3]);
		}

		/* Run IQ calibration twice. */
		for (i = 0; i < 2; i++) {
			int ret;

			ret = rtwn_iq_calib_chain(sc, chain,
			    tx[chain], rx[chain]);
			if (ret == 0) {
				DPRINTF(("%s: chain %d: Tx failed.\n",
				    __func__, chain));
				tx[chain][0] = 0xff;
				tx[chain][1] = 0xff;
				rx[chain][0] = 0xff;
				rx[chain][1] = 0xff;
			} else if (ret == 1) {
				DPRINTF(("%s: chain %d: Rx failed.\n",
				    __func__, chain));
				rx[chain][0] = 0xff;
				rx[chain][1] = 0xff;
			} else if (ret == 3) {
				DPRINTF(("%s: chain %d: Both Tx and Rx "
				    "succeeded.\n", __func__, chain));
			}
		}

		DPRINTF(("%s: results for run %d chain %d: tx[0]=0x%x, "
		    "tx[1]=0x%x rx[0]=0x%x rx[1]=0x%x\n", __func__, n, chain,
		    tx[chain][0], tx[chain][1], rx[chain][0], rx[chain][1]));
	}

	rtwn_bb_write(sc, R92C_FPGA0_IQK, 0x00);

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)) {
		rtwn_bb_write(sc, R92C_LSSI_PARAM(0), 0x00032ed3);
		if (sc->ntxchains > 1)
			rtwn_bb_write(sc, R92C_LSSI_PARAM(1), 0x00032ed3);
	}

	if (n != 0) {
		if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)) {
			if (!(hssi_param1 & R92C_HSSI_PARAM1_PI)) {
				rtwn_bb_write(sc, R92C_HSSI_PARAM1(0),
				    hssi_param1);
				rtwn_bb_write(sc, R92C_HSSI_PARAM1(1),
				    hssi_param1);
			}
		}

		for (i = 0; i < nitems(reg_adda); i++)
			rtwn_bb_write(sc, reg_adda[i], iq_cal_regs->adda[i]);

		rtwn_write_1(sc, R92C_TXPAUSE, iq_cal_regs->txpause);
		rtwn_write_1(sc, R92C_BCN_CTRL, iq_cal_regs->bcn_ctrl);
		rtwn_write_1(sc, R92C_BCN_CTRL1, iq_cal_regs->bcn_ctrl1);
		rtwn_write_4(sc, R92C_GPIO_MUXCFG, iq_cal_regs->gpio_muxcfg);

		rtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA,
		    iq_cal_regs->ofdm0_trxpathena);
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(0),
		    iq_cal_regs->fpga0_rfifacesw0);
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACESW(1),
		    iq_cal_regs->fpga0_rfifacesw1);
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(0),
		    iq_cal_regs->fpga0_rfifaceoe0);
		rtwn_bb_write(sc, R92C_FPGA0_RFIFACEOE(1),
		    iq_cal_regs->fpga0_rfifaceoe1);
		rtwn_bb_write(sc, R92C_OFDM0_TRMUXPAR,
		    iq_cal_regs->ofdm0_trmuxpar);
		rtwn_bb_write(sc, R92C_CONFIG_ANT_A,
		    iq_cal_regs->config_ant_a);
		rtwn_bb_write(sc, R92C_CONFIG_ANT_B,
		    iq_cal_regs->config_ant_b);
		rtwn_bb_write(sc, R92C_CCK0_AFESETTING,
		    iq_cal_regs->cck0_afesetting);

		reg = rtwn_bb_read(sc, R92C_OFDM0_AGCCORE1(0));
		reg &= ~0xff;
		rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), reg | 0x50);
		rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), reg | xa_agc);

		reg = rtwn_bb_read(sc, R92C_OFDM0_AGCCORE1(1));
		reg &= ~0xff;
		rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(1), reg | 0x50);
		rtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(1), reg | xb_agc);

		rtwn_bb_write(sc, R92C_TX_IQK_TONE_A, 0x01008c00);
		rtwn_bb_write(sc, R92C_RX_IQK_TONE_A, 0x01008c00);
	}
}

static int
rtwn_iq_calib_compare_results(uint16_t tx1[2][2], uint16_t rx1[2][2],
    uint16_t tx2[2][2], uint16_t rx2[2][2], int ntxchains)
{
	int chain, i, tx_ok[2], rx_ok[2];

	tx_ok[0] = tx_ok[1] = rx_ok[0] = rx_ok[1] = 0;
	for (chain = 0; chain < ntxchains; chain++) {
		for (i = 0; i < 2; i++)	{
			if (tx1[chain][i] == 0xff || tx2[chain][i] == 0xff ||
			    rx1[chain][i] == 0xff || rx2[chain][i] == 0xff)
				continue;

			tx_ok[chain] = (abs(tx1[chain][i] - tx2[chain][i]) <=
			    RTWN_IQ_CAL_MAX_TOLERANCE);

			rx_ok[chain] = (abs(rx1[chain][i] - rx2[chain][i]) <=
			    RTWN_IQ_CAL_MAX_TOLERANCE);
		}
	}

	if (ntxchains > 1)
		return (tx_ok[0] && tx_ok[1] && rx_ok[0] && rx_ok[1]);
	else
		return (tx_ok[0] && rx_ok[0]);
}

static void
rtwn_iq_calib_write_results(struct rtwn_softc *sc, uint16_t tx[2],
    uint16_t rx[2], int chain)
{
	uint32_t reg, val, x;
	long y, tx_c;

	if (tx[0] == 0xff || tx[1] == 0xff)
		return;

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(chain));
	val = ((reg >> 22) & 0x3ff);
	x = tx[0];
	if (x & 0x00000200)
		x |= 0xfffffc00;
	reg &= ~0x3ff;
	reg |= (((x * val) >> 8) & 0x3ff);
	rtwn_bb_write(sc, R92C_OFDM0_TXIQIMBALANCE(chain), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_ECCATHRESHOLD);
	if (((x * val) >> 7) & 0x01)
		reg |= 0x80000000;
	else
		reg &= ~0x80000000;
	rtwn_bb_write(sc, R92C_OFDM0_ECCATHRESHOLD, reg);

	y = tx[1];
	if (y & 0x00000200)
		y |= 0xfffffc00;
	tx_c = (y * val) >> 8;
	reg = rtwn_bb_read(sc, R92C_OFDM0_TXAFE(chain));
	reg &= ~0xf0000000;
	reg |= ((tx_c & 0x3c0) << 22);
	rtwn_bb_write(sc, R92C_OFDM0_TXAFE(chain), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_TXIQIMBALANCE(chain));
	reg &= ~0x003f0000;
	reg |= ((tx_c & 0x3f) << 16);
	rtwn_bb_write(sc, R92C_OFDM0_TXIQIMBALANCE(chain), reg);

	reg = rtwn_bb_read(sc, R92C_OFDM0_ECCATHRESHOLD);
	if (((y * val) >> 7) & 0x01)
		reg |= 0x20000000;
	else
		reg &= ~0x20000000;
	rtwn_bb_write(sc, R92C_OFDM0_ECCATHRESHOLD, reg);

	if (rx[0] == 0xff || rx[1] == 0xff)
		return;

	reg = rtwn_bb_read(sc, R92C_OFDM0_RXIQIMBALANCE(chain));
	reg &= ~0x3ff;
	reg |= (rx[0] & 0x3ff);
	rtwn_bb_write(sc, R92C_OFDM0_RXIQIMBALANCE(chain), reg);

	reg &= ~0xfc00;
	reg |= ((rx[1] & 0x03f) << 10);
	rtwn_bb_write(sc, R92C_OFDM0_RXIQIMBALANCE(chain), reg);

	if (chain == 0) {
		reg = rtwn_bb_read(sc, R92C_OFDM0_RXIQEXTANTA);
		reg &= ~0xf0000000;
		reg |= ((rx[1] & 0x3c0) << 22);
		rtwn_bb_write(sc, R92C_OFDM0_RXIQEXTANTA, reg);
	} else {
		reg = rtwn_bb_read(sc, R92C_OFDM0_AGCRSSITABLE);
		reg &= ~0xf000;
		reg |= ((rx[1] & 0x3c0) << 6);
		rtwn_bb_write(sc, R92C_OFDM0_AGCRSSITABLE, reg);
	}
}

static void
rtwn_iq_calib(struct rtwn_softc *sc)
{
	uint16_t tx[RTWN_IQ_CAL_NRUN][2][2]={0}, rx[RTWN_IQ_CAL_NRUN][2][2]={0};
	int n, valid;
	struct rtwn_iq_cal_regs regs;

	valid = 0;
	memset(&regs, 0, sizeof(regs));
	for (n = 0; n < RTWN_IQ_CAL_NRUN; n++) {
		rtwn_iq_calib_run(sc, n, tx[n], rx[n], &regs);

		if (n == 0)
			continue;

		/* Valid results remain stable after consecutive runs. */
		valid = rtwn_iq_calib_compare_results(tx[n - 1], rx[n - 1],
		    tx[n], rx[n], sc->ntxchains);
		if (valid)
			break;
	}

	sc->iq_ok = valid && tx[n][0][0] != 0 && rx[n][0][0] != 0;
	if (valid) {
		rtwn_iq_calib_write_results(sc, tx[n][0], rx[n][0], 0);
		if (sc->ntxchains > 1)
			rtwn_iq_calib_write_results(sc, tx[n][1], rx[n][1], 1);
	}
}

static void
rtwn_lc_calib(struct rtwn_softc *sc)
{
	uint32_t rf_ac[2];
	uint8_t txmode;
	int i;

	txmode = rtwn_read_1(sc, R92C_OFDM1_LSTF + 3);
	if ((txmode & 0x70) != 0) {
		/* Disable all continuous Tx. */
		rtwn_write_1(sc, R92C_OFDM1_LSTF + 3, txmode & ~0x70);

		/* Set RF mode to standby mode. */
		for (i = 0; i < sc->nrxchains; i++) {
			rf_ac[i] = rtwn_rf_read(sc, i, R92C_RF_AC);
			rtwn_rf_write(sc, i, R92C_RF_AC,
			    RW(rf_ac[i], R92C_RF_AC_MODE,
				R92C_RF_AC_MODE_STANDBY));
		}
	} else {
		/* Block all Tx queues. */
		rtwn_write_1(sc, R92C_TXPAUSE, R92C_TXPAUSE_ALL);
	}
	/* Start calibration. */
	rtwn_rf_write(sc, 0, R92C_RF_CHNLBW,
	    rtwn_rf_read(sc, 0, R92C_RF_CHNLBW) | R92C_RF_CHNLBW_LCSTART);

	/* Give calibration the time to complete. */
	DELAY(100);

	/* Restore configuration. */
	if ((txmode & 0x70) != 0) {
		/* Restore Tx mode. */
		rtwn_write_1(sc, R92C_OFDM1_LSTF + 3, txmode);
		/* Restore RF mode. */
		for (i = 0; i < sc->nrxchains; i++)
			rtwn_rf_write(sc, i, R92C_RF_AC, rf_ac[i]);
	} else {
		/* Unblock all Tx queues. */
		rtwn_write_1(sc, R92C_TXPAUSE, 0x00);
	}
}

static int
urtwn_llt_write(struct rtwn_softc *sc, uint32_t addr, uint32_t data)
{
	int ntries;

	urtwn_write_4(sc, R92C_LLT_INIT,
	    SM(R92C_LLT_INIT_OP, R92C_LLT_INIT_OP_WRITE) |
	    SM(R92C_LLT_INIT_ADDR, addr) |
	    SM(R92C_LLT_INIT_DATA, data));
	/* Wait for write operation to complete. */
	for (ntries = 0; ntries < 20; ntries++) {
		if (MS(urtwn_read_4(sc, R92C_LLT_INIT), R92C_LLT_INIT_OP) ==
		    R92C_LLT_INIT_OP_NO_ACTIVE)
			return (0);
		DELAY(5);
	}
	return (ETIMEDOUT);
}

static int
urtwn_llt_init(struct rtwn_softc *sc, int page_count)
{
	int i, error, pktbuf_count;

	pktbuf_count = ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E) ?
	    R88E_TXPKTBUF_COUNT : R92C_TXPKTBUF_COUNT;

	/* Reserve pages [0; page_count]. */
	for (i = 0; i < page_count; i++) {
		if ((error = urtwn_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* NB: 0xff indicates end-of-list. */
	if ((error = urtwn_llt_write(sc, i, 0xff)) != 0)
		return (error);
	/*
	 * Use pages [page_count + 1; pktbuf_count - 1]
	 * as ring buffer.
	 */
	for (++i; i < pktbuf_count - 1; i++) {
		if ((error = urtwn_llt_write(sc, i, i + 1)) != 0)
			return (error);
	}
	/* Make the last page point to the beginning of the ring buffer. */
	error = urtwn_llt_write(sc, i, page_count + 1);
	return (error);
}

static int
urtwn_r88e_power_on(struct rtwn_softc *sc)
{
	uint32_t reg;
	int ntries;

	/* Wait for power ready bit. */
	for (ntries = 0; ntries < 5000; ntries++) {
		if (urtwn_read_4(sc, R92C_APS_FSMCO) & R92C_APS_FSMCO_SUS_HOST)
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for chip power up\n",
		    "rtl8188eu");
		return (ETIMEDOUT);
	}

	/* Reset BB. */
	urtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    urtwn_read_1(sc, R92C_SYS_FUNC_EN) & ~(R92C_SYS_FUNC_EN_BBRSTB |
	    R92C_SYS_FUNC_EN_BB_GLB_RST));

	urtwn_write_1(sc, R92C_AFE_XTAL_CTRL + 2,
	    urtwn_read_1(sc, R92C_AFE_XTAL_CTRL + 2) | 0x80);

	/* Disable HWPDN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) & ~R92C_APS_FSMCO_APDM_HPDN);
	/* Disable WL suspend. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) &
	    ~(R92C_APS_FSMCO_AFSM_HSUS | R92C_APS_FSMCO_AFSM_PCIE));

	/* Auto enable WLAN. */
	urtwn_write_2(sc, R92C_APS_FSMCO,
	    urtwn_read_2(sc, R92C_APS_FSMCO) | R92C_APS_FSMCO_APFM_ONMAC);
	for (ntries = 0; ntries < 5000; ntries++) {
		if (!(urtwn_read_2(sc, R92C_APS_FSMCO) &
		    R92C_APS_FSMCO_APFM_ONMAC))
			break;
		DELAY(10);
	}
	if (ntries == 5000) {
		printf("%s: timeout waiting for MAC auto ON\n",
		    "rtl8188eu");
		return (ETIMEDOUT);
	}

	/* Enable LDO normal mode. */
	urtwn_write_1(sc, R92C_LPLDO_CTRL,
	    urtwn_read_1(sc, R92C_LPLDO_CTRL) & ~0x10);

	/* Enable MAC DMA/WMAC/SCHEDULE/SEC blocks. */
	urtwn_write_2(sc, R92C_CR, 0);
	reg = urtwn_read_2(sc, R92C_CR);
	reg |= R92C_CR_HCI_TXDMA_EN | R92C_CR_HCI_RXDMA_EN |
	    R92C_CR_TXDMA_EN | R92C_CR_RXDMA_EN | R92C_CR_PROTOCOL_EN |
	    R92C_CR_SCHEDULE_EN | R92C_CR_ENSEC | R92C_CR_CALTMR_EN;
	urtwn_write_2(sc, R92C_CR, reg);
	return (0);
}

static int
urtwn_fw_loadpage(void *cookie, int page, uint8_t *buf, int len)
{
	struct rtwn_softc *sc = cookie;
	uint32_t reg;
	int maxblksz, off, mlen, error = 0;

	reg = urtwn_read_4(sc, R92C_MCUFWDL);
	reg = RW(reg, R92C_MCUFWDL_PAGE, page);
	urtwn_write_4(sc, R92C_MCUFWDL, reg);

	maxblksz = ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) ? 254 : 196;

	off = R92C_FW_START_ADDR;
	while (len > 0) {
		if (len > maxblksz)
			mlen = maxblksz;
		else if (len > 4)
			mlen = 4;
		else
			mlen = 1;
		error = urtwn_write_region_1(sc, off, buf, mlen);
		if (error != 0)
			break;
		off += mlen;
		buf += mlen;
		len -= mlen;
	}
	return (error);
}

static int
urtwn_dma_init(void *cookie)
{
	struct rtwn_softc *sc = cookie;
	uint32_t reg;
	uint16_t dmasize;
	int hqpages, lqpages, nqpages, pagecnt, boundary;
	int error, hashq, haslq, hasnq;

	/* Default initialization of chipset values. */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E) {
		hqpages = R88E_HQ_NPAGES;
		lqpages = R88E_LQ_NPAGES;
		nqpages = R88E_NQ_NPAGES;
		pagecnt = R88E_TX_PAGE_COUNT;
		dmasize = R88E_MAX_RX_DMA_SIZE;
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
		hqpages = R88F_HQ_NPAGES;
		lqpages = R88F_LQ_NPAGES;
		nqpages = R88F_NQ_NPAGES;
		pagecnt = R88F_TX_PAGE_COUNT;
		dmasize = R88F_MAX_RX_DMA_SIZE;
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		hqpages = R92E_HQ_NPAGES;
		lqpages = R92E_LQ_NPAGES;
		nqpages = R92E_NQ_NPAGES;
		pagecnt = R92E_TX_PAGE_COUNT;
		dmasize = R92E_MAX_RX_DMA_SIZE;
	} else {
		hqpages = R92C_HQ_NPAGES;
		lqpages = R92C_LQ_NPAGES;
		nqpages = R92C_NQ_NPAGES;
		pagecnt = R92C_TX_PAGE_COUNT;
		dmasize = R92C_MAX_RX_DMA_SIZE;
	}
	boundary = pagecnt + 1;

	/* Initialize LLT table. */
	error = urtwn_llt_init(sc, pagecnt);
	if (error != 0)
		return (error);

	/* Get Tx queues to USB endpoints mapping. */
	hashq = hasnq = haslq = 0;
	switch (sc->ntx) {
	case 3:
		haslq = 1;
		pagecnt -= lqpages;
		/* FALLTHROUGH */
	case 2:
		hasnq = 1;
		pagecnt -= nqpages;
		/* FALLTHROUGH */
	case 1:
		hashq = 1;
		pagecnt -= hqpages;
		break;
	}

	/* Set number of pages for normal priority queue. */
	urtwn_write_1(sc, R92C_RQPN_NPQ, hasnq ? nqpages : 0);
	urtwn_write_4(sc, R92C_RQPN,
	    /* Set number of pages for public queue. */
	    SM(R92C_RQPN_PUBQ, pagecnt) |
	    /* Set number of pages for high priority queue. */
	    SM(R92C_RQPN_HPQ, hashq ? hqpages : 0) |
	    /* Set number of pages for low priority queue. */
	    SM(R92C_RQPN_LPQ, haslq ? lqpages : 0) |
	    /* Load values. */
	    R92C_RQPN_LD);

	urtwn_write_1(sc, R92C_TXPKTBUF_BCNQ_BDNY, boundary);
	urtwn_write_1(sc, R92C_TXPKTBUF_MGQ_BDNY, boundary);
	urtwn_write_1(sc, R92C_TXPKTBUF_WMAC_LBK_BF_HD, boundary);
	urtwn_write_1(sc, R92C_TRXFF_BNDY, boundary);
	urtwn_write_1(sc, R92C_TDECTRL + 1, boundary);

	/* Set queue to USB pipe mapping. */
	reg = urtwn_read_2(sc, R92C_TRXDMA_CTRL);
	reg &= ~R92C_TRXDMA_CTRL_QMAP_M;
	if (haslq)
		reg |= R92C_TRXDMA_CTRL_QMAP_3EP;
	else if (hashq) {
		if (!hasnq)
			reg |= R92C_TRXDMA_CTRL_QMAP_HQ;
		else
			reg |= R92C_TRXDMA_CTRL_QMAP_HQ_NQ;
	}
	urtwn_write_2(sc, R92C_TRXDMA_CTRL, reg);

	/* Set Tx/Rx transfer page boundary. */
	urtwn_write_2(sc, R92C_TRXFF_BNDY + 2, dmasize - 1);

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)) {
		/* Set Tx/Rx transfer page size. */
		if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
			urtwn_write_1(sc, R92C_PBP,
			    SM(R92C_PBP_PSRX, R92C_PBP_256) |
			    SM(R92C_PBP_PSTX, R92C_PBP_256));
		} else {
			urtwn_write_1(sc, R92C_PBP,
			    SM(R92C_PBP_PSRX, R92C_PBP_128) |
			    SM(R92C_PBP_PSTX, R92C_PBP_128));
		}
	}
	return (error);
}

static void
urtwn_mac_init(void *cookie)
{
	struct rtwn_softc *sc = cookie;
	int i;

	/* Write MAC initialization values. */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E) {
		for (i = 0; i < nitems(rtl8188eu_mac); i++) {
			urtwn_write_1(sc, rtl8188eu_mac[i].reg,
			    rtl8188eu_mac[i].val);
		}
		urtwn_write_1(sc, R92C_MAX_AGGR_NUM, 0x07);
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F) {
		for (i = 0; i < nitems(rtl8188ftv_mac); i++) {
			urtwn_write_1(sc, rtl8188ftv_mac[i].reg,
			    rtl8188ftv_mac[i].val);
		}
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		for (i = 0; i < nitems(rtl8192eu_mac); i++) {
			urtwn_write_1(sc, rtl8192eu_mac[i].reg,
			    rtl8192eu_mac[i].val);
		}
	} else {
		for (i = 0; i < nitems(rtl8192cu_mac); i++)
			urtwn_write_1(sc, rtl8192cu_mac[i].reg,
			    rtl8192cu_mac[i].val);
	}
}

static void
urtwn_bb_init(void *cookie)
{
	struct rtwn_softc *sc = cookie;
	const struct r92c_bb_prog *prog;
	uint32_t reg;
	uint8_t xtal;
	int i;

	/* Enable BB and RF. */
	urtwn_write_2(sc, R92C_SYS_FUNC_EN,
	    urtwn_read_2(sc, R92C_SYS_FUNC_EN) |
	    R92C_SYS_FUNC_EN_BBRSTB | R92C_SYS_FUNC_EN_BB_GLB_RST |
	    R92C_SYS_FUNC_EN_DIO_RF);

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E)))
		urtwn_write_2(sc, R92C_AFE_PLL_CTRL, 0xdb83);

	urtwn_write_1(sc, R92C_RF_CTRL,
	    R92C_RF_CTRL_EN | R92C_RF_CTRL_RSTB | R92C_RF_CTRL_SDMRSTB);
	urtwn_write_1(sc, R92C_SYS_FUNC_EN,
	    R92C_SYS_FUNC_EN_USBA | R92C_SYS_FUNC_EN_USBD |
	    R92C_SYS_FUNC_EN_BB_GLB_RST | R92C_SYS_FUNC_EN_BBRSTB);

	if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) &
	    (RTWN_CHIP_88E | RTWN_CHIP_88F | RTWN_CHIP_92E))) {
		urtwn_write_1(sc, R92C_LDOHCI12_CTRL, 0x0f);
		urtwn_write_1(sc, 0x15, 0xe9);
		urtwn_write_1(sc, R92C_AFE_XTAL_CTRL + 1, 0x80);
	}

	/* Select BB programming based on board type. */
	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88E)
		prog = &rtl8188eu_bb_prog;
	else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_88F)
		prog = &rtl8188ftv_bb_prog;
	else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E)
		prog = &rtl8192eu_bb_prog;
	else if (!((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92C)) {
		if (sc->board_type == R92C_BOARD_TYPE_MINICARD)
			prog = &rtl8188ce_bb_prog;
		else if (sc->board_type == R92C_BOARD_TYPE_HIGHPA)
			prog = &rtl8188ru_bb_prog;
		else
			prog = &rtl8188cu_bb_prog;
	} else {
		if (sc->board_type == R92C_BOARD_TYPE_MINICARD)
			prog = &rtl8192ce_bb_prog;
		else
			prog = &rtl8192cu_bb_prog;
	}
	/* Write BB initialization values. */
	for (i = 0; i < prog->count; i++) {
		urtwn_bb_write(sc, prog->regs[i], prog->vals[i]);
		DELAY(1);
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92C_1T2R) {
		/* 8192C 1T only configuration. */
		reg = urtwn_bb_read(sc, R92C_FPGA0_TXINFO);
		reg = (reg & ~0x00000003) | 0x2;
		urtwn_bb_write(sc, R92C_FPGA0_TXINFO, reg);

		reg = urtwn_bb_read(sc, R92C_FPGA1_TXINFO);
		reg = (reg & ~0x00300033) | 0x00200022;
		urtwn_bb_write(sc, R92C_FPGA1_TXINFO, reg);

		reg = urtwn_bb_read(sc, R92C_CCK0_AFESETTING);
		reg = (reg & ~0xff000000) | 0x45 << 24;
		urtwn_bb_write(sc, R92C_CCK0_AFESETTING, reg);

		reg = urtwn_bb_read(sc, R92C_OFDM0_TRXPATHENA);
		reg = (reg & ~0x000000ff) | 0x23;
		urtwn_bb_write(sc, R92C_OFDM0_TRXPATHENA, reg);

		reg = urtwn_bb_read(sc, R92C_OFDM0_AGCPARAM1);
		reg = (reg & ~0x00000030) | 1 << 4;
		urtwn_bb_write(sc, R92C_OFDM0_AGCPARAM1, reg);

		reg = urtwn_bb_read(sc, 0xe74);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe74, reg);
		reg = urtwn_bb_read(sc, 0xe78);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe78, reg);
		reg = urtwn_bb_read(sc, 0xe7c);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe7c, reg);
		reg = urtwn_bb_read(sc, 0xe80);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe80, reg);
		reg = urtwn_bb_read(sc, 0xe88);
		reg = (reg & ~0x0c000000) | 2 << 26;
		urtwn_bb_write(sc, 0xe88, reg);
	}

	/* Write AGC values. */
	for (i = 0; i < prog->agccount; i++) {
		urtwn_bb_write(sc, R92C_OFDM0_AGCRSSITABLE,
		    prog->agcvals[i]);
		DELAY(1);
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88E | RTWN_CHIP_88F)) {
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x69553422);
		DELAY(1);
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x69553420);
		DELAY(1);
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x00040022);
		DELAY(1);
		urtwn_bb_write(sc, R92C_OFDM0_AGCCORE1(0), 0x00040020);
		DELAY(1);
	}

	if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & (RTWN_CHIP_88E | RTWN_CHIP_88F)) {
		xtal = sc->crystal_cap & 0x3f;
		reg = urtwn_bb_read(sc, R92C_AFE_XTAL_CTRL);
		urtwn_bb_write(sc, R92C_AFE_XTAL_CTRL,
		    RW(reg, R92C_AFE_XTAL_CTRL_ADDR, xtal | xtal << 6));
	} else if ((RTWN_CHIP_88E | RTWN_CHIP_USB) & RTWN_CHIP_92E) {
		xtal = sc->crystal_cap & 0x3f;
		reg = urtwn_bb_read(sc, R92C_AFE_CTRL3);
		urtwn_bb_write(sc, R92C_AFE_CTRL3,
		    RW(reg, R92C_AFE_CTRL3_ADDR, xtal | xtal << 6));
		urtwn_write_4(sc, R92C_AFE_XTAL_CTRL, 0x000f81fb);
	}

	if (urtwn_bb_read(sc, R92C_HSSI_PARAM2(0)) & R92C_HSSI_PARAM2_CCK_HIPWR)
		sc->sc_flags |= RTWN_FLAG_CCK_HIPWR;
}
