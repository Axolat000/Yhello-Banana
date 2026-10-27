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

static int
aq1_fw_reboot(struct aq_softc *sc);
static int
aq1_mac_soft_reset_rbl(struct aq_softc *sc, enum aq_fw_bootloader_mode *mode);
static int
aq1_mac_soft_reset_flb(struct aq_softc *sc);
static int
aq1_mac_soft_reset(struct aq_softc *sc, enum aq_fw_bootloader_mode *mode);
static void
aq1_global_software_reset(struct aq_softc *sc);
static int
aq1_fw_read_version(struct aq_softc *sc);
static int
aq1_fw_version_init(struct aq_softc *sc);
static int
aq1_hw_init_ucp(struct aq_softc *sc);
static int
aq1_get_mac_addr(struct aq_softc *sc);
static int
aq1_fw_downld_dwords(struct aq_softc *sc, uint32_t addr, uint32_t *p,
    uint32_t cnt);
static int
aq_fw2x_reset(struct aq_softc *sc);
static int
aq_hw_reset(struct aq_softc *sc);
static int
aq_fw2x_get_mode(struct aq_softc *sc, enum aq_hw_fw_mpi_state *modep,
    enum aq_link_speed *speedp, enum aq_link_fc *fcp, enum aq_link_eee *eeep);
static int
aq_fw2x_set_mode(struct aq_softc *sc, enum aq_hw_fw_mpi_state mode,
    enum aq_link_speed speed, enum aq_link_fc fc, enum aq_link_eee eee);
static int
aq_hw_init(struct aq_softc *sc, int irqmode, int multivec);
static void
aq_hw_init_tx_path(struct aq_softc *sc);
static void
aq_hw_init_rx_path(struct aq_softc *sc);
static int
aq_set_mac_addr(struct aq_softc *sc, int index, uint8_t *enaddr);
static int
aq_get_linkmode(struct aq_softc *sc, enum aq_link_speed *speed,
    enum aq_link_fc *fc, enum aq_link_eee *eee);
static int
aq_set_linkmode(struct aq_softc *sc, enum aq_link_speed speed,
    enum aq_link_fc fc, enum aq_link_eee eee);
static void
aq_hw_qos_set(struct aq_softc *sc);

static int
aq1_fw_reboot(struct aq_softc *sc)
{
	uint32_t ver, v, boot_exit_code;
	int i, error;
	enum aq_fw_bootloader_mode mode;

	mode = FW_BOOT_MODE_UNKNOWN;

	ver = AQ_READ_REG(sc, AQ_FW_VERSION_REG);

	for (i = 1000; i > 0; i--) {
		v = AQ_READ_REG(sc, FW_MPI_DAISY_CHAIN_STATUS_REG);
		boot_exit_code = AQ_READ_REG(sc, FW_BOOT_EXIT_CODE_REG);
		if (v != 0x06000000 || boot_exit_code != 0)
			break;
	}

	if (i <= 0) {
		printf(": F/W reset failed. Neither RBL nor FLB started");
		return ETIMEDOUT;
	}

	sc->sc_rbl_enabled = (boot_exit_code != 0);

	/*
	 * Having FW version 0 is an indicator that cold start
	 * is in progress. This means two things:
	 * 1) Driver have to wait for FW/HW to finish boot (500ms giveup)
	 * 2) Driver may skip reset sequence and save time.
	 */
	if (sc->sc_fast_start_enabled && (ver != 0)) {
		if (aq1_fw_read_version(sc) == 0)
			goto faststart;
	}

	error = aq1_mac_soft_reset(sc, &mode);
	if (error != 0) {
		printf("%s: MAC reset failed: %d\n", DEVNAME(sc), error);
		return error;
	}

	switch (mode) {
	case FW_BOOT_MODE_FLB:
		DPRINTF(("%s: FLB> F/W successfully loaded from flash.",
		    DEVNAME(sc)));
		sc->sc_flash_present = 1;
		break;
	case FW_BOOT_MODE_RBL_FLASH:
		DPRINTF(("%s: RBL> F/W loaded from flash. Host Bootload "
		    "disabled.", DEVNAME(sc)));
		sc->sc_flash_present = 1;
		break;
	case FW_BOOT_MODE_UNKNOWN:
		printf("%s: F/W bootload error: unknown bootloader type",
		    DEVNAME(sc));
		return ENOTSUP;
	case FW_BOOT_MODE_RBL_HOST_BOOTLOAD:
		printf("%s: RBL> F/W Host Bootload not implemented", DEVNAME(sc));
		return ENOTSUP;
	}

 faststart:
	error = aq1_fw_read_version(sc);
	if (error != 0)
		return error;

	error = aq1_fw_version_init(sc);
	if (error != 0)
		return error;

	return aq1_hw_init_ucp(sc);
}

static int
aq1_mac_soft_reset_rbl(struct aq_softc *sc, enum aq_fw_bootloader_mode *mode)
{
	int timo;

	DPRINTF(("%s: RBL> MAC reset STARTED!\n", DEVNAME(sc)));

	AQ_WRITE_REG(sc, AQ_FW_GLB_CTL2_REG, 0x40e1);
	AQ_WRITE_REG(sc, AQ_FW_GLB_CPU_SEM_REG(0), 1);
	AQ_WRITE_REG(sc, AQ_MBOXIF_POWER_GATING_CONTROL_REG, 0);

	/* MAC FW will reload PHY FW if 1E.1000.3 was cleaned - #undone */
	AQ_WRITE_REG(sc, FW_BOOT_EXIT_CODE_REG, RBL_STATUS_DEAD);

	aq1_global_software_reset(sc);

	AQ_WRITE_REG(sc, AQ_FW_GLB_CTL2_REG, 0x40e0);

	/* Wait for RBL to finish boot process. */
#define RBL_TIMEOUT_MS	10000
	uint16_t rbl_status;
	for (timo = RBL_TIMEOUT_MS; timo > 0; timo--) {
		rbl_status = AQ_READ_REG(sc, FW_BOOT_EXIT_CODE_REG) & 0xffff;
		if (rbl_status != 0 && rbl_status != RBL_STATUS_DEAD)
			break;
		delay(1000);
	}

	if (timo <= 0) {
		printf("%s: RBL> RBL restart failed: timeout\n", DEVNAME(sc));
		return EBUSY;
	}

	switch (rbl_status) {
	case RBL_STATUS_SUCCESS:
		if (mode != NULL)
			*mode = FW_BOOT_MODE_RBL_FLASH;
		DPRINTF(("%s: RBL> reset complete! [Flash]\n", DEVNAME(sc)));
		break;
	case RBL_STATUS_HOST_BOOT:
		if (mode != NULL)
			*mode = FW_BOOT_MODE_RBL_HOST_BOOTLOAD;
		DPRINTF(("%s: RBL> reset complete! [Host Bootload]\n",
		    DEVNAME(sc)));
		break;
	case RBL_STATUS_FAILURE:
	default:
		printf("%s: unknown RBL status 0x%x\n", DEVNAME(sc),
		    rbl_status);
		return EBUSY;
	}

	return 0;
}

static int
aq1_mac_soft_reset_flb(struct aq_softc *sc)
{
	uint32_t v;
	int timo;

	AQ_WRITE_REG(sc, AQ_FW_GLB_CTL2_REG, 0x40e1);
	/*
	 * Let Felicity hardware to complete SMBUS transaction before
	 * Global software reset.
	 */
	delay(50000);

	/*
	 * If SPI burst transaction was interrupted(before running the script),
	 * global software reset may not clear SPI interface.
	 * Clean it up manually before global reset.
	 */
	AQ_WRITE_REG(sc, AQ_GLB_NVR_PROVISIONING2_REG, 0x00a0);
	AQ_WRITE_REG(sc, AQ_GLB_NVR_INTERFACE1_REG, 0x009f);
	AQ_WRITE_REG(sc, AQ_GLB_NVR_INTERFACE1_REG, 0x809f);
	delay(50000);

	v = AQ_READ_REG(sc, AQ_FW_SOFTRESET_REG);
	v &= ~AQ_FW_SOFTRESET_DIS;
	v |= AQ_FW_SOFTRESET_RESET;
	AQ_WRITE_REG(sc, AQ_FW_SOFTRESET_REG, v);

	/* Kickstart. */
	AQ_WRITE_REG(sc, AQ_FW_GLB_CTL2_REG, 0x80e0);
	AQ_WRITE_REG(sc, AQ_MBOXIF_POWER_GATING_CONTROL_REG, 0);
	if (!sc->sc_fast_start_enabled)
		AQ_WRITE_REG(sc, AQ_GLB_GENERAL_PROVISIONING9_REG, 1);

	/*
	 * For the case SPI burst transaction was interrupted (by MCP reset
	 * above), wait until it is completed by hardware.
	 */
	delay(50000);

	/* MAC Kickstart */
	if (!sc->sc_fast_start_enabled) {
		AQ_WRITE_REG(sc, AQ_FW_GLB_CTL2_REG, 0x180e0);

		uint32_t flb_status;
		for (timo = 0; timo < 1000; timo++) {
			flb_status = AQ_READ_REG(sc,
			    FW_MPI_DAISY_CHAIN_STATUS_REG) & 0x10;
			if (flb_status != 0)
				break;
			delay(1000);
		}
		if (flb_status == 0) {
			printf("%s: FLB> MAC kickstart failed: timed out\n",
			    DEVNAME(sc));
			return ETIMEDOUT;
		}
		DPRINTF(("%s: FLB> MAC kickstart done, %d ms\n", DEVNAME(sc),
		    timo));
		/* FW reset */
		AQ_WRITE_REG(sc, AQ_FW_GLB_CTL2_REG, 0x80e0);
		/*
		 * Let Felicity hardware complete SMBUS transaction before
		 * Global software reset.
		 */
		delay(50000);
		sc->sc_fast_start_enabled = true;
	}
	AQ_WRITE_REG(sc, AQ_FW_GLB_CPU_SEM_REG(0), 1);

	/* PHY Kickstart: #undone */
	aq1_global_software_reset(sc);

	for (timo = 0; timo < 1000; timo++) {
		if (AQ_READ_REG(sc, AQ_FW_VERSION_REG) != 0)
			break;
		delay(10000);
	}
	if (timo >= 1000) {
		printf("%s: FLB> Global Soft Reset failed\n", DEVNAME(sc));
		return ETIMEDOUT;
	}
	DPRINTF(("%s: FLB> F/W restart: %d ms\n", DEVNAME(sc), timo * 10));

	return 0;

}

static int
aq1_mac_soft_reset(struct aq_softc *sc, enum aq_fw_bootloader_mode *mode)
{
	if (sc->sc_rbl_enabled)
		return aq1_mac_soft_reset_rbl(sc, mode);

	if (mode != NULL)
		*mode = FW_BOOT_MODE_FLB;
	return aq1_mac_soft_reset_flb(sc);
}

static void
aq1_global_software_reset(struct aq_softc *sc)
{
        uint32_t v;

        AQ_WRITE_REG_BIT(sc, RX_SYSCONTROL_REG, RX_SYSCONTROL_RESET_DIS, 0);
        AQ_WRITE_REG_BIT(sc, TX_SYSCONTROL_REG, TX_SYSCONTROL_RESET_DIS, 0);
        AQ_WRITE_REG_BIT(sc, FW_MPI_RESETCTRL_REG,
            FW_MPI_RESETCTRL_RESET_DIS, 0);

        v = AQ_READ_REG(sc, AQ_FW_SOFTRESET_REG);
        v &= ~AQ_FW_SOFTRESET_DIS;
        v |= AQ_FW_SOFTRESET_RESET;
        AQ_WRITE_REG(sc, AQ_FW_SOFTRESET_REG, v);
}

static int
aq1_fw_read_version(struct aq_softc *sc)
{
	int i, error = EBUSY;
#define MAC_FW_START_TIMEOUT_MS 10000
	for (i = 0; i < MAC_FW_START_TIMEOUT_MS; i++) {
		sc->sc_fw_version = AQ_READ_REG(sc, AQ_FW_VERSION_REG);
		if (sc->sc_fw_version != 0 && sc->sc_fw_version != 0xffffffffu) {
			error = 0;
			break;
		}
		delay(1000);
	}
	return error;
}

static int
aq1_fw_version_init(struct aq_softc *sc)
{
	int error = 0;
	char fw_vers[sizeof("F/W version xxxxx.xxxxx.xxxxx")];

	if (FW_VERSION_MAJOR(sc) != 2 && FW_VERSION_MAJOR(sc) != 3) {
		printf(": Unsupported F/W version %d.%d.%d\n",
		    FW_VERSION_MAJOR(sc), FW_VERSION_MINOR(sc),
		    FW_VERSION_BUILD(sc));
		return ENOTSUP;
	}
	snprintf(fw_vers, sizeof(fw_vers), "F/W version %d.%d.%d",
	    FW_VERSION_MAJOR(sc), FW_VERSION_MINOR(sc), FW_VERSION_BUILD(sc));

	/* detect revision */
	uint32_t hwrev = AQ_READ_REG(sc, AQ_HW_REVISION_REG);
	switch (hwrev & 0x0000000f) {
	case 0x01:
		printf(", Atlantic A0, %s", fw_vers);
		sc->sc_features |= FEATURES_AQ1_REV_A0 |
		    FEATURES_MPI_AQ | FEATURES_MIPS;
		break;
	case 0x02:
		printf(", Atlantic B0, %s", fw_vers);
		sc->sc_features |= FEATURES_AQ1_REV_B0 |
		    FEATURES_MPI_AQ | FEATURES_MIPS |
		    FEATURES_TPO2 | FEATURES_RPF2;
		break;
	case 0x0A:
		printf(", Atlantic B1, %s", fw_vers);
		sc->sc_features |= FEATURES_AQ1_REV_B1 |
		    FEATURES_MPI_AQ | FEATURES_MIPS |
		    FEATURES_TPO2 | FEATURES_RPF2;
		break;
	default:
		printf(": Unknown revision (0x%08x)\n", hwrev);
		error = ENOTSUP;
		break;
	}
	return error;
}

static int
aq1_hw_init_ucp(struct aq_softc *sc)
{
	int timo;

	if (FW_VERSION_MAJOR(sc) == 1) {
		if (AQ_READ_REG(sc, FW1X_MPI_INIT2_REG) == 0) {
			uint32_t data;
			arc4random_buf(&data, sizeof(data));
			data &= 0xfefefefe;
			data |= 0x02020202;
			AQ_WRITE_REG(sc, FW1X_MPI_INIT2_REG, data);
		}
		AQ_WRITE_REG(sc, FW1X_MPI_INIT1_REG, 0);
	}

	for (timo = 100; timo > 0; timo--) {
		sc->sc_mbox_addr = AQ_READ_REG(sc, FW_MPI_MBOX_ADDR_REG);
		if (sc->sc_mbox_addr != 0)
			break;
		delay(1000);
	}

#define AQ_FW_MIN_VERSION	0x01050006
#define AQ_FW_MIN_VERSION_STR	"1.5.6"
	if (sc->sc_fw_version < AQ_FW_MIN_VERSION) {
		printf("%s: atlantic: wrong FW version: " AQ_FW_MIN_VERSION_STR
		    " or later required, this is %d.%d.%d\n",
		    DEVNAME(sc),
		    FW_VERSION_MAJOR(sc),
		    FW_VERSION_MINOR(sc),
		    FW_VERSION_BUILD(sc));
		return ENOTSUP;
	}

	if (sc->sc_mbox_addr == 0 || (sc->sc_mbox_addr & 3) || sc->sc_mbox_addr > 0xfffff000u) return ENXIO;

	return 0;
}

static int
aq1_get_mac_addr(struct aq_softc *sc)
{
	uint32_t mac_addr[2];
	uint32_t efuse_shadow_addr;
	int err;

	efuse_shadow_addr = 0;
	if (FW_VERSION_MAJOR(sc) >= 2)
		efuse_shadow_addr = AQ_READ_REG(sc, FW2X_MPI_EFUSEADDR_REG);
	else
		efuse_shadow_addr = AQ_READ_REG(sc, FW1X_MPI_EFUSEADDR_REG);

	if (efuse_shadow_addr == 0 || (efuse_shadow_addr & 3) || efuse_shadow_addr > 0xffffffffu-168) {
		printf(": cannot get efuse addr\n");
		return ENXIO;
	}

	DPRINTF(("%s: efuse_shadow_addr = %x\n", DEVNAME(sc), efuse_shadow_addr));

	memset(mac_addr, 0, sizeof(mac_addr));
	err = aq1_fw_downld_dwords(sc, efuse_shadow_addr + (40 * 4),
	    mac_addr, 2);
	if (err != 0)
		return err;

	if (mac_addr[0] == 0 && mac_addr[1] == 0) {
		printf(": mac address not found\n");
		return ENXIO;
	}

	DPRINTF(("%s: mac0 %x mac1 %x\n", DEVNAME(sc), mac_addr[0],
	    mac_addr[1]));

	mac_addr[0] = htobe32(mac_addr[0]);
	mac_addr[1] = htobe32(mac_addr[1]);

	DPRINTF(("%s: mac0 %x mac1 %x\n", DEVNAME(sc), mac_addr[0],
	    mac_addr[1]));

	memcpy(sc->sc_enaddr.ether_addr_octet,
	    (uint8_t *)mac_addr, ETHER_ADDR_LEN);

	return 0;
}

static int
aq1_fw_downld_dwords(struct aq_softc *sc, uint32_t addr, uint32_t *p,
    uint32_t cnt)
{
    if (!cnt || cnt > 256 || (addr & 3) || addr > 0xffffffffu-cnt*4)
        { return EINVAL; }
	int error = 0;

	WAIT_FOR(AQ_READ_REG(sc, AQ_FW_SEM_RAM_REG) == 1, 1, 10000, &error);
	/* A busy semaphore belongs to firmware; never force-release it. */
    if (error != 0) return error;

	AQ_WRITE_REG(sc, AQ_FW_MBOX_ADDR_REG, addr);

	error = 0;
	for (; cnt > 0 && error == 0; cnt--) {
		/* execute mailbox interface */
		AQ_WRITE_REG_BIT(sc, AQ_FW_MBOX_CMD_REG,
		    AQ_FW_MBOX_CMD_EXECUTE, 1);
		if (sc->sc_features & FEATURES_AQ1_REV_B1) {
			WAIT_FOR(AQ_READ_REG(sc, AQ_FW_MBOX_ADDR_REG) != addr,
			    1, 1000, &error);
		} else {
			WAIT_FOR((AQ_READ_REG(sc, AQ_FW_MBOX_CMD_REG) &
			    AQ_FW_MBOX_CMD_BUSY) == 0,
			    1, 1000, &error);
		}
		*p++ = AQ_READ_REG(sc, AQ_FW_MBOX_VAL_REG);
		addr += sizeof(uint32_t);
	}
	AQ_WRITE_REG(sc, AQ_FW_SEM_RAM_REG, 1);

	if (error != 0)
		printf("%s: %s:%d: timeout\n",
		    DEVNAME(sc), __func__, __LINE__);

	return error;
}

static int
aq_fw2x_reset(struct aq_softc *sc)
{
	struct aq_fw2x_capabilities caps = { 0 };
	int error;

	error = aq1_fw_downld_dwords(sc,
	    sc->sc_mbox_addr + offsetof(struct aq_fw2x_mailbox, caps),
	    (uint32_t *)&caps, sizeof caps / sizeof(uint32_t));
	if (error != 0) {
		printf("%s: fw2x> can't get F/W capabilities mask, error %d\n",
		    DEVNAME(sc), error);
		return error;
	}
	sc->sc_fw_caps = caps.caps_lo | ((uint64_t)caps.caps_hi << 32);

	DPRINTF(("%s: fw2x> F/W capabilities=0x%llx\n", DEVNAME(sc),
	    sc->sc_fw_caps));

	return 0;
}

static int
aq_hw_reset(struct aq_softc *sc)
{
	int error;

	/* disable irq */
	AQ_WRITE_REG_BIT(sc, AQ_INTR_CTRL_REG, AQ_INTR_CTRL_RESET_DIS, 0);

	/* apply */
	AQ_WRITE_REG_BIT(sc, AQ_INTR_CTRL_REG, AQ_INTR_CTRL_RESET_IRQ, 1);

	/* wait ack 10 times by 1ms */
	WAIT_FOR(
	    (AQ_READ_REG(sc, AQ_INTR_CTRL_REG) & AQ_INTR_CTRL_RESET_IRQ) == 0,
	    1000, 10, &error);
	if (error != 0) {
		printf(": IRQ reset failed: %d\n", error);
		return error;
	}

	return aq_fw2x_reset(sc);
}

static int
aq_fw2x_get_mode(struct aq_softc *sc, enum aq_hw_fw_mpi_state *modep,
    enum aq_link_speed *speedp, enum aq_link_fc *fcp, enum aq_link_eee *eeep)
{
	uint64_t mpi_state, mpi_ctrl;
	enum aq_link_speed speed;
	enum aq_link_fc fc;

	AQ_MPI_LOCK(sc);

	mpi_state = AQ_READ64_REG(sc, FW2X_MPI_STATE_REG);
    if (mpi_state == ~0ULL) return ENXIO;
	if (modep != NULL) {
		mpi_ctrl = AQ_READ64_REG(sc, FW2X_MPI_CONTROL_REG);
		if (mpi_ctrl & FW2X_CTRL_RATE_MASK)
			*modep = MPI_INIT;
		else
			*modep = MPI_DEINIT;
	}

	AQ_MPI_UNLOCK(sc);

	if (mpi_state & FW2X_CTRL_RATE_10G)
		speed = AQ_LINK_10G;
	else if (mpi_state & FW2X_CTRL_RATE_5G)
		speed = AQ_LINK_5G;
	else if (mpi_state & FW2X_CTRL_RATE_2G5)
		speed = AQ_LINK_2G5;
	else if (mpi_state & FW2X_CTRL_RATE_1G)
		speed = AQ_LINK_1G;
	else if (mpi_state & FW2X_CTRL_RATE_100M)
		speed = AQ_LINK_100M;
	else
		speed = AQ_LINK_NONE;
	if (speedp != NULL)
		*speedp = speed;

	fc = AQ_FC_NONE;
	if (mpi_state & FW2X_CTRL_PAUSE)
		fc |= AQ_FC_RX;
	if (mpi_state & FW2X_CTRL_ASYMMETRIC_PAUSE)
		fc |= AQ_FC_TX;
	if (fcp != NULL)
		*fcp = fc;

	if (eeep != NULL)
		*eeep = AQ_EEE_DISABLE;

	return 0;
}

static int
aq_fw2x_set_mode(struct aq_softc *sc, enum aq_hw_fw_mpi_state mode,
    enum aq_link_speed speed, enum aq_link_fc fc, enum aq_link_eee eee)
{
	uint64_t mpi_ctrl;
	int error = 0;

	AQ_MPI_LOCK(sc);

	mpi_ctrl = AQ_READ64_REG(sc, FW2X_MPI_CONTROL_REG);

	switch (mode) {
	case MPI_INIT:
		mpi_ctrl &= ~FW2X_CTRL_RATE_MASK;
		if (speed & AQ_LINK_10G)
			mpi_ctrl |= FW2X_CTRL_RATE_10G;
		if (speed & AQ_LINK_5G)
			mpi_ctrl |= FW2X_CTRL_RATE_5G;
		if (speed & AQ_LINK_2G5)
			mpi_ctrl |= FW2X_CTRL_RATE_2G5;
		if (speed & AQ_LINK_1G)
			mpi_ctrl |= FW2X_CTRL_RATE_1G;
		if (speed & AQ_LINK_100M)
			mpi_ctrl |= FW2X_CTRL_RATE_100M;

		mpi_ctrl &= ~FW2X_CTRL_LINK_DROP;

		mpi_ctrl &= ~FW2X_CTRL_EEE_MASK;
		if (eee == AQ_EEE_ENABLE)
			mpi_ctrl |= FW2X_CTRL_EEE_MASK;

		mpi_ctrl &= ~(FW2X_CTRL_PAUSE | FW2X_CTRL_ASYMMETRIC_PAUSE);
		if (fc & AQ_FC_RX)
			mpi_ctrl |= FW2X_CTRL_PAUSE;
		if (fc & AQ_FC_TX)
			mpi_ctrl |= FW2X_CTRL_ASYMMETRIC_PAUSE;
		break;
	case MPI_DEINIT:
		mpi_ctrl &= ~(FW2X_CTRL_RATE_MASK | FW2X_CTRL_EEE_MASK);
		mpi_ctrl &= ~(FW2X_CTRL_PAUSE | FW2X_CTRL_ASYMMETRIC_PAUSE);
		break;
	default:
		printf("%s: fw2x> unknown MPI state %d\n", DEVNAME(sc), mode);
		error =  EINVAL;
		goto failure;
	}
	AQ_WRITE64_REG(sc, FW2X_MPI_CONTROL_REG, mpi_ctrl);

 failure:
	AQ_MPI_UNLOCK(sc);
	return error;
}

static int
aq_hw_init(struct aq_softc *sc, int irqmode, int multivec)
{
	uint32_t v;

	if (HWTYPE_AQ1_P(sc)) {
		/* Force limit MRRS on RDM/TDM to 2K */
		v = AQ_READ_REG(sc, AQ_PCI_REG_CONTROL_6_REG);
		AQ_WRITE_REG(sc, AQ_PCI_REG_CONTROL_6_REG,
		    (v & ~0x0707) | 0x0404);

		/*
		 * TX DMA total request limit. B0 hardware is not capable to
		 * handle more than (8K-MRRS) incoming DMA data.
		 * Value 24 in 256byte units
		 */
		AQ_WRITE_REG(sc, AQ_HW_TX_DMA_TOTAL_REQ_LIMIT_REG, 24);
	}

	if (HWTYPE_AQ2_P(sc)) {
		uint32_t fpgaver, speed;
		fpgaver = AQ_READ_REG(sc, AQ2_HW_FPGA_VERSION_REG);
		if (fpgaver < 0x01000000)
			speed = AQ2_LAUNCHTIME_CTRL_RATIO_SPEED_FULL;
		else if (fpgaver >= 0x01008502)
			speed = AQ2_LAUNCHTIME_CTRL_RATIO_SPEED_HALF;
		else
			speed = AQ2_LAUNCHTIME_CTRL_RATIO_SPEED_QUARTER;
		AQ_WRITE_REG_BIT(sc, AQ2_LAUNCHTIME_CTRL_REG,
		    AQ2_LAUNCHTIME_CTRL_RATIO, speed);
	}

	aq_hw_init_tx_path(sc);
	aq_hw_init_rx_path(sc);

	if (aq_set_mac_addr(sc, AQ_HW_MAC_OWN, sc->sc_enaddr.ether_addr_octet))
		return EINVAL;

	aq_set_linkmode(sc, AQ_LINK_NONE, AQ_FC_NONE, AQ_EEE_DISABLE);

	aq_hw_qos_set(sc);

	if (HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, AQ2_RPF_NEW_CTRL_REG,
		    AQ2_RPF_NEW_CTRL_ENABLE, 1);
	}

	/* Enable interrupt */
	AQ_WRITE_REG(sc, AQ_INTR_CTRL_REG, AQ_INTR_CTRL_RESET_DIS);
	AQ_WRITE_REG_BIT(sc, AQ_INTR_CTRL_REG, AQ_INTR_CTRL_MULTIVEC, multivec);

	AQ_WRITE_REG_BIT(sc, AQ_INTR_CTRL_REG, AQ_INTR_CTRL_IRQMODE, irqmode);

	AQ_WRITE_REG(sc, AQ_INTR_AUTOMASK_REG, 0xffffffff);

	AQ_WRITE_REG(sc, AQ_GEN_INTR_MAP_REG(0),
	    ((AQ_B0_ERR_INT << 24) | (1U << 31)) |
	    ((AQ_B0_ERR_INT << 16) | (1 << 23))
	);

	/* link interrupt */
	AQ_WRITE_REG(sc, AQ_GEN_INTR_MAP_REG(3),
	    (1 << 7) | sc->sc_linkstat_irq);

	return 0;
}

static void
aq_hw_init_tx_path(struct aq_softc *sc)
{
	/* Tx TC/RSS number config */
	AQ_WRITE_REG_BIT(sc, TPB_TX_BUF_REG, TPB_TX_BUF_TC_MODE_EN, 1);

	AQ_WRITE_REG_BIT(sc, THM_LSO_TCP_FLAG1_REG,
	    THM_LSO_TCP_FLAG1_FIRST, 0x0ff6);
	AQ_WRITE_REG_BIT(sc, THM_LSO_TCP_FLAG1_REG,
	    THM_LSO_TCP_FLAG1_MID,   0x0ff6);
	AQ_WRITE_REG_BIT(sc, THM_LSO_TCP_FLAG2_REG,
	   THM_LSO_TCP_FLAG2_LAST,  0x0f7f);

	/* misc */
	AQ_WRITE_REG(sc, TX_TPO2_REG,
	   (sc->sc_features & FEATURES_TPO2) ? TX_TPO2_EN : 0);
	AQ_WRITE_REG_BIT(sc, TDM_DCA_REG, TDM_DCA_EN, 0);
	AQ_WRITE_REG_BIT(sc, TDM_DCA_REG, TDM_DCA_MODE, 0);

	AQ_WRITE_REG_BIT(sc, TPB_TX_BUF_REG, TPB_TX_BUF_SCP_INS_EN, 1);

	if ((sc->sc_features & FEATURES_AQ1_REV_B) || HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, TPB_TX_BUF_REG, TPB_TX_BUF_CLK_GATE_EN, 0);
	}
}

static void
aq_hw_init_rx_path(struct aq_softc *sc)
{
	int i;

	/* clear setting */
	AQ_WRITE_REG_BIT(sc, RPB_RPF_RX_REG, RPB_RPF_RX_TC_MODE, 0);
	AQ_WRITE_REG_BIT(sc, RPB_RPF_RX_REG, RPB_RPF_RX_FC_MODE, 0);

	if (HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, AQ2_RPF_REDIR2_REG,
		    AQ2_RPF_REDIR2_HASHTYPE, AQ2_RPF_REDIR2_HASHTYPE_ALL);
	}

	if (sc->sc_nqueues > 1) {
		uint32_t bits = 0;

		AQ_WRITE_REG_BIT(sc, RPB_RPF_RX_REG, RPB_RPF_RX_TC_MODE, 1);
		AQ_WRITE_REG_BIT(sc, RPB_RPF_RX_REG, RPB_RPF_RX_FC_MODE, 1);

		switch (sc->sc_nqueues) {
		case 2:
			bits = 0x11111111;
			break;
		case 4:
			bits = 0x22222222;
			break;
		case 8:
			bits = 0x33333333;
			break;
		}

		AQ_WRITE_REG(sc, RX_FLR_RSS_CONTROL1_REG,
		    RX_FLR_RSS_CONTROL1_EN | bits);
	} else {
		AQ_WRITE_REG(sc, RX_FLR_RSS_CONTROL1_REG, 0);
	}

	if (HWTYPE_AQ1_P(sc)) {
		for (i = 0; i < 32; i++) {
			AQ_WRITE_REG_BIT(sc, RPF_ETHERTYPE_FILTER_REG(i),
			   RPF_ETHERTYPE_FILTER_EN, 0);
		}
	}

	/* L2 and Multicast filters */
	for (i = 0; i < AQ_HW_MAC_NUM; i++) {
		AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(i), RPF_L2UC_MSW_EN, 0);
		AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(i), RPF_L2UC_MSW_ACTION,
		    RPF_ACTION_HOST);
	}
	AQ_WRITE_REG(sc, RPF_MCAST_FILTER_MASK_REG, 0);
	AQ_WRITE_REG(sc, RPF_MCAST_FILTER_REG(0), 0x00010fff);

	/* Vlan filters */
	AQ_WRITE_REG_BIT(sc, RPF_VLAN_TPID_REG, RPF_VLAN_TPID_OUTER,
	    ETHERTYPE_QINQ);
	AQ_WRITE_REG_BIT(sc, RPF_VLAN_TPID_REG, RPF_VLAN_TPID_INNER,
	    ETHERTYPE_VLAN);
	AQ_WRITE_REG_BIT(sc, RPF_VLAN_MODE_REG, RPF_VLAN_MODE_PROMISC, 1);

	if ((sc->sc_features & FEATURES_AQ1_REV_B) || HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, RPF_VLAN_MODE_REG,
		    RPF_VLAN_MODE_ACCEPT_UNTAGGED, 1);
		AQ_WRITE_REG_BIT(sc, RPF_VLAN_MODE_REG,
		    RPF_VLAN_MODE_UNTAGGED_ACTION, RPF_ACTION_HOST);
	}

	if (HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, AQ2_RPF_REC_TAB_ENABLE_REG,
		    AQ2_RPF_REC_TAB_ENABLE_MASK, 0xffff);
		AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(0),
		    RPF_L2UC_MSW_TAG, 1);
		AQ_WRITE_REG_BIT(sc, AQ2_RPF_L2BC_TAG_REG,
		    AQ2_RPF_L2BC_TAG_MASK, 1);

		aq2_filter_art_set(sc, AQ2_RPF_INDEX_L2_PROMISC_OFF,
		    0, AQ2_RPF_TAG_UC_MASK | AQ2_RPF_TAG_ALLMC_MASK,
		    AQ2_ART_ACTION_DROP);
		aq2_filter_art_set(sc, AQ2_RPF_INDEX_VLAN_PROMISC_OFF,
		    0, AQ2_RPF_TAG_VLAN_MASK | AQ2_RPF_TAG_UNTAG_MASK,
		    AQ2_ART_ACTION_DROP);

		for (i = 0; i < 8; i++) {
			aq2_filter_art_set(sc, AQ2_RPF_INDEX_PCP_TO_TC + i,
			    (i << AQ2_RPF_TAG_PCP_SHIFT), AQ2_RPF_TAG_PCP_MASK,
			    AQ2_ART_ACTION_ASSIGN_TC(i % sc->sc_nqueues));
		}

	} else if (HWTYPE_AQ1_P(sc)) {
		if (sc->sc_features & FEATURES_RPF2)
			AQ_WRITE_REG(sc, RX_TCP_RSS_HASH_REG,
			    RX_TCP_RSS_HASH_RPF2);
		else
			AQ_WRITE_REG(sc, RX_TCP_RSS_HASH_REG, 0);

		/* we might want to figure out what this magic number does */
		AQ_WRITE_REG_BIT(sc, RX_TCP_RSS_HASH_REG,
		    RX_TCP_RSS_HASH_TYPE, 0x001e);
	}

	AQ_WRITE_REG_BIT(sc, RPF_L2BC_REG, RPF_L2BC_EN, 1);
	AQ_WRITE_REG_BIT(sc, RPF_L2BC_REG, RPF_L2BC_ACTION, RPF_ACTION_HOST);
	AQ_WRITE_REG_BIT(sc, RPF_L2BC_REG, RPF_L2BC_THRESHOLD, 0xffff);

	AQ_WRITE_REG_BIT(sc, RX_DMA_DCA_REG, RX_DMA_DCA_EN, 0);
	AQ_WRITE_REG_BIT(sc, RX_DMA_DCA_REG, RX_DMA_DCA_MODE, 0);
}

static int
aq_set_mac_addr(struct aq_softc *sc, int index, uint8_t *enaddr)
{
	uint32_t h, l;

	if (index >= AQ_HW_MAC_NUM)
		return EINVAL;

	if (enaddr == NULL) {
		/* disable */
		AQ_WRITE_REG_BIT(sc,
		    RPF_L2UC_MSW_REG(index), RPF_L2UC_MSW_EN, 0);
		return 0;
	}

	h = (enaddr[0] <<  8) | (enaddr[1]);
	l = ((uint32_t)enaddr[2] << 24) | (enaddr[3] << 16) |
	    (enaddr[4] <<  8) | (enaddr[5]);

	/* disable, set, and enable */
	AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(index), RPF_L2UC_MSW_EN, 0);
	AQ_WRITE_REG(sc, RPF_L2UC_LSW_REG(index), l);
	AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(index),
	    RPF_L2UC_MSW_MACADDR_HI, h);
	AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(index),
	    RPF_L2UC_MSW_ACTION, RPF_ACTION_HOST);
	if (HWTYPE_AQ2_P(sc))
		AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(index),
		    RPF_L2UC_MSW_TAG, 1);
	AQ_WRITE_REG_BIT(sc, RPF_L2UC_MSW_REG(index), RPF_L2UC_MSW_EN, 1);

	return 0;
}

static int
aq_get_linkmode(struct aq_softc *sc, enum aq_link_speed *speed,
    enum aq_link_fc *fc, enum aq_link_eee *eee)
{
	enum aq_hw_fw_mpi_state mode;
	int error;

	error = aq_fw2x_get_mode(sc, &mode, speed, fc, eee);
	if (error != 0)
		return error;
	if (mode != MPI_INIT)
		return ENXIO;

	return 0;
}

static int
aq_set_linkmode(struct aq_softc *sc, enum aq_link_speed speed,
    enum aq_link_fc fc, enum aq_link_eee eee)
{
	return aq_fw2x_set_mode(sc, MPI_INIT, speed, fc, eee);
}

static void
aq_hw_qos_set(struct aq_softc *sc)
{
	uint32_t tc = 0;
	uint32_t buff_size;

	/* TPS Descriptor rate init */
	AQ_WRITE_REG_BIT(sc, TPS_DESC_RATE_REG, TPS_DESC_RATE_TA_RST, 0);
	AQ_WRITE_REG_BIT(sc, TPS_DESC_RATE_REG, TPS_DESC_RATE_LIM, 0xa);

	/* TPS VM init */
	AQ_WRITE_REG_BIT(sc, TPS_DESC_VM_ARB_MODE_REG, TPS_DESC_VM_ARB_MODE, 0);

	/* TPS TC credits init */
	AQ_WRITE_REG_BIT(sc, TPS_DESC_TC_ARB_MODE_REG, TPS_DESC_TC_ARB_MODE, 0);
	AQ_WRITE_REG_BIT(sc, TPS_DATA_TC_ARB_MODE_REG, TPS_DATA_TC_ARB_MODE, 0);

	if (HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, TPS_DATA_TCT_REG(tc),
		    TPS2_DATA_TCT_CREDIT_MAX, 0xfff0);
		AQ_WRITE_REG_BIT(sc, TPS_DATA_TCT_REG(tc),
		    TPS2_DATA_TCT_WEIGHT, 0x640);
	} else {
		AQ_WRITE_REG_BIT(sc, TPS_DATA_TCT_REG(tc),
		    TPS_DATA_TCT_CREDIT_MAX, 0xfff);
		AQ_WRITE_REG_BIT(sc, TPS_DATA_TCT_REG(tc),
		    TPS_DATA_TCT_WEIGHT, 0x64);
	}
	AQ_WRITE_REG_BIT(sc, TPS_DESC_TCT_REG(tc),
	    TPS_DESC_TCT_CREDIT_MAX, 0x50);
	AQ_WRITE_REG_BIT(sc, TPS_DESC_TCT_REG(tc),
	    TPS_DESC_TCT_WEIGHT, 0x1e);

	/* Tx buf size */
	tc = 0;
	buff_size = HWTYPE_AQ2_P(sc) ? AQ2_HW_TXBUF_MAX : AQ_HW_TXBUF_MAX;
	AQ_WRITE_REG_BIT(sc, TPB_TXB_BUFSIZE_REG(tc), TPB_TXB_BUFSIZE,
	    buff_size);
	AQ_WRITE_REG_BIT(sc, TPB_TXB_THRESH_REG(tc), TPB_TXB_THRESH_HI,
	    (buff_size * (1024 / 32) * 66) / 100);
	AQ_WRITE_REG_BIT(sc, TPB_TXB_THRESH_REG(tc), TPB_TXB_THRESH_LO,
	    (buff_size * (1024 / 32) * 50) / 100);

	/* QoS Rx buf size per TC */
	tc = 0;
	buff_size = HWTYPE_AQ2_P(sc) ? AQ2_HW_RXBUF_MAX : AQ_HW_RXBUF_MAX;
	AQ_WRITE_REG_BIT(sc, RPB_RXB_BUFSIZE_REG(tc), RPB_RXB_BUFSIZE,
	    buff_size);
	AQ_WRITE_REG_BIT(sc, RPB_RXB_XOFF_REG(tc), RPB_RXB_XOFF_EN, 0);
	AQ_WRITE_REG_BIT(sc, RPB_RXB_XOFF_REG(tc), RPB_RXB_XOFF_THRESH_HI,
	    (buff_size * (1024 / 32) * 66) / 100);
	AQ_WRITE_REG_BIT(sc, RPB_RXB_XOFF_REG(tc), RPB_RXB_XOFF_THRESH_LO,
	    (buff_size * (1024 / 32) * 50) / 100);

	/* QoS 802.1p priority -> TC mapping */
	int i_priority;
	for (i_priority = 0; i_priority < 8; i_priority++) {
		AQ_WRITE_REG_BIT(sc, RPF_RPB_RX_TC_UPT_REG,
		    RPF_RPB_RX_TC_UPT_MASK(i_priority), 0);
	}

	/* ring to TC mapping */
	if (HWTYPE_AQ2_P(sc)) {
		AQ_WRITE_REG_BIT(sc, TPB_TX_BUF_REG,
		    TPB_TX_BUF_TC_Q_RAND_MAP_EN, 1);

		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(0), 0x00000000);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(1), 0x00000000);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(2), 0x01010101);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(3), 0x01010101);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(4), 0x02020202);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(5), 0x02020202);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(6), 0x03030303);
		AQ_WRITE_REG(sc, AQ2_TX_Q_TC_MAP_REG(7), 0x03030303);

		AQ_WRITE_REG(sc, AQ2_RX_Q_TC_MAP_REG(0), 0x00000000);
		AQ_WRITE_REG(sc, AQ2_RX_Q_TC_MAP_REG(1), 0x11111111);
		AQ_WRITE_REG(sc, AQ2_RX_Q_TC_MAP_REG(2), 0x22222222);
		AQ_WRITE_REG(sc, AQ2_RX_Q_TC_MAP_REG(3), 0x33333333);
	}
}
