/*-
 * Copyright (c) 2015, 2016, 2019 Kevin Lo <kevlo@openbsd.org>
 * Copyright (c) 2020 Jonathon Fletcher <jonathon.fletcher@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "usbnet.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"
#include "r8153_regs.h"

/* RTL8153/8153B and RTL8156/8156B. Initialization sequences adapted from
 * OpenBSD if_ure.c revision 1.38, 2026-10-07. Native Banana OS USB transport;
 * the already hardware-tested RTL8152 driver is intentionally separate. */
typedef struct {
    usb_device_t* dev;
    uint32_t flags, chip;
    uint16_t version, last_link;
    int ok;
    uint8_t mac[8];
} ure_state_t;
#define URE_CLRBIT_1(s,r,t,b) ure_write_1(s,r,t,ure_read_1(s,r,t)&~(uint32_t)(b))
#define URE_SETBIT_1(s,r,t,b) ure_write_1(s,r,t,ure_read_1(s,r,t)|(b))
#define URE_CLRBIT_2(s,r,t,b) ure_write_2(s,r,t,ure_read_2(s,r,t)&~(uint32_t)(b))
#define URE_SETBIT_2(s,r,t,b) ure_write_2(s,r,t,ure_read_2(s,r,t)|(b))
#define URE_CLRBIT_4(s,r,t,b) ure_write_4(s,r,t,ure_read_4(s,r,t)&~(uint32_t)(b))
static uint32_t ure_le32(const uint8_t* b) {
    return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
static void ure_put32(uint8_t* b,uint32_t n) {
    for(int i=0;i<4;i++) b[i]=(uint8_t)(n>>(i*8));
}
static int ure_mem(ure_state_t* s,int write,uint16_t addr,uint16_t index,void* b,uint16_t n) {
    if(!s->ok) return -1;
    int rc=usb_control(s->dev,USB_TYPE_VENDOR|(write?0:USB_DIR_IN),5,addr,index,b,n);
    if(rc!=n) { s->ok=0; return -1; }
    return 0;
}
static int ure_write_mem(ure_state_t* s,uint16_t r,uint16_t t,void* b,uint16_t n) {
    return ure_mem(s,1,r,t,b,n);
}
static uint32_t ure_read_4(ure_state_t* s,uint16_t r,uint16_t t) {
    uint8_t b[4]={0}; ure_mem(s,0,r,t,b,4); return ure_le32(b);
}
static uint16_t ure_read_2(ure_state_t* s,uint16_t r,uint16_t t) {
    return ure_read_4(s,r&~3u,t)>>((r&2)*8);
}
static uint8_t ure_read_1(ure_state_t* s,uint16_t r,uint16_t t) {
    return ure_read_4(s,r&~3u,t)>>((r&3)*8);
}
static void ure_write_4(ure_state_t* s,uint16_t r,uint16_t t,uint32_t v) {
    uint8_t b[4]; ure_put32(b,v); ure_write_mem(s,r,t|0xff,b,4);
}
static void ure_write_2(ure_state_t* s,uint16_t r,uint16_t t,uint32_t v) {
    uint8_t b[4]; unsigned shift=r&2;
    ure_put32(b,(v&0xffff)<<(shift*8)); ure_write_mem(s,r&~3u,t|(0x33<<shift),b,4);
}
static void ure_write_1(ure_state_t* s,uint16_t r,uint16_t t,uint32_t v) {
    uint8_t b[4]; unsigned shift=r&3;
    ure_put32(b,(v&0xff)<<(shift*8)); ure_write_mem(s,r&~3u,t|(0x11<<shift),b,4);
}
static uint16_t ure_phy_read(ure_state_t* s,uint16_t r) {
    ure_write_2(s,URE_PLA_OCP_GPHY_BASE,URE_MCU_TYPE_PLA,r&0xf000);
    return ure_read_2(s,(r&0xfff)|0xb000,URE_MCU_TYPE_PLA);
}
static void ure_phy_write(ure_state_t* s,uint16_t r,uint16_t v) {
    ure_write_2(s,URE_PLA_OCP_GPHY_BASE,URE_MCU_TYPE_PLA,r&0xf000);
    ure_write_2(s,(r&0xfff)|0xb000,URE_MCU_TYPE_PLA,v);
}
static void ure_reset(ure_state_t* s) {
    if(s->flags&URE_FLAG_8156) {
        URE_CLRBIT_1(s,URE_PLA_CR,URE_MCU_TYPE_PLA,URE_CR_TE);
        URE_CLRBIT_2(s,URE_USB_BMU_RESET,URE_MCU_TYPE_USB,BMU_RESET_EP_IN);
        URE_SETBIT_2(s,URE_USB_USB_CTRL,URE_MCU_TYPE_USB,URE_CDC_ECM_EN);
        URE_CLRBIT_1(s,URE_PLA_CR,URE_MCU_TYPE_PLA,URE_CR_RE);
        URE_SETBIT_2(s,URE_USB_BMU_RESET,URE_MCU_TYPE_USB,BMU_RESET_EP_IN);
        URE_CLRBIT_2(s,URE_USB_USB_CTRL,URE_MCU_TYPE_USB,URE_CDC_ECM_EN);
    } else {
        ure_write_1(s,URE_PLA_CR,URE_MCU_TYPE_PLA,URE_CR_RST);
        for(int i=0;s->ok && i<1000;i++) {
            if(!(ure_read_1(s,URE_PLA_CR,URE_MCU_TYPE_PLA)&URE_CR_RST)) return;
            usb_delay_ms(1);
        }
        s->ok=0;
    }
}
static void ure_reset_bmu(ure_state_t* s) {
    uint8_t r=ure_read_1(s,URE_USB_BMU_RESET,URE_MCU_TYPE_USB);
    ure_write_1(s,URE_USB_BMU_RESET,URE_MCU_TYPE_USB,r&~(BMU_RESET_EP_IN|BMU_RESET_EP_OUT));
    ure_write_1(s,URE_USB_BMU_RESET,URE_MCU_TYPE_USB,r|BMU_RESET_EP_IN|BMU_RESET_EP_OUT);
}
static void ure_disable_teredo(ure_state_t* s) {
    if(s->flags) ure_write_1(s,URE_PLA_TEREDO_CFG,URE_MCU_TYPE_PLA,0xff);
    else URE_CLRBIT_2(s,URE_PLA_TEREDO_CFG,URE_MCU_TYPE_PLA,
                     URE_TEREDO_SEL|URE_TEREDO_RS_EVENT_MASK|URE_OOB_TEREDO_EN);
    ure_write_2(s,URE_PLA_WDT6_CTRL,URE_MCU_TYPE_PLA,URE_WDT6_SET_MODE);
    ure_write_2(s,URE_PLA_REALWOW_TIMER,URE_MCU_TYPE_PLA,0);
    ure_write_4(s,URE_PLA_TEREDO_TIMER,URE_MCU_TYPE_PLA,0);
}

static uint16_t
ure_rtl8153_phy_status(ure_state_t *sc, int desired)
{
	uint16_t	reg = 0;
	int		i;

	for (i = 0; sc->ok && i < 500; i++) {
		reg = ure_phy_read(sc, URE_OCP_PHY_STATUS) &
		    URE_PHY_STAT_MASK;
		if (desired) {
			if (reg == desired)
				break;
		} else {
			if (reg == URE_PHY_STAT_LAN_ON ||
			    reg == URE_PHY_STAT_PWRDN ||
			    reg == URE_PHY_STAT_EXT_INIT)
				break;
		}
		usb_delay_ms(20);
	}
	if (!sc->ok || i == 500) {
        sc->ok = 0;
        klog("r8153: timeout waiting for phy to stabilize\n");
    }

	return reg;
}

static int
ure_wait_for_flash(ure_state_t *sc)
{
	int i;

	if ((ure_read_2(sc, URE_PLA_GPHY_CTRL, URE_MCU_TYPE_PLA) &
	    URE_GPHY_FLASH) &&
	    !(ure_read_2(sc, URE_USB_GPHY_CTRL, URE_MCU_TYPE_USB) &
	    URE_BYPASS_FLASH)) {
		for (i = 0; sc->ok && i < 100; i++) {
			if (ure_read_2(sc, URE_USB_GPHY_CTRL,
			    URE_MCU_TYPE_USB) & URE_GPHY_PATCH_DONE)
				break;
			usb_delay_ms(1);
		}
		if (!sc->ok || i == 100) {
			klog("r8153: timeout waiting for loading flash\n");
			return (-1);
		}
	}

	return (0);
}

static int
ure_rtl8153_init(ure_state_t *sc)
{
	uint16_t	reg = 0;
	uint8_t		u1u2[8];
	int		i;

	memset(u1u2, 0x00, sizeof(u1u2));
	ure_write_mem(sc, URE_USB_TOLERANCE, URE_BYTE_EN_SIX_BYTES, u1u2,
	    sizeof(u1u2));

        for (i = 0; sc->ok && i < 500; i++) {
		if (ure_read_2(sc, URE_PLA_BOOT_CTRL, URE_MCU_TYPE_PLA) &
		    URE_AUTOLOAD_DONE)
			break;
		usb_delay_ms(20);
	}
	if (!sc->ok || i == 500) {
		klog("r8153: timeout waiting for chip autoload\n");
		return (-1);
	}

	ure_rtl8153_phy_status(sc, 0);

	if (sc->chip & (URE_CHIP_VER_5C00 | URE_CHIP_VER_5C10 |
	    URE_CHIP_VER_5C20)) {
		ure_phy_write(sc, URE_OCP_ADC_CFG,
		    URE_CKADSEL_L | URE_ADC_EN | URE_EN_EMI_L);
	}

	ure_rtl8153_phy_status(sc, URE_PHY_STAT_LAN_ON);

	URE_CLRBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, URE_U2P3_ENABLE);

	if (sc->chip & URE_CHIP_VER_5C10) {
		reg = ure_read_2(sc, URE_USB_SSPHYLINK2, URE_MCU_TYPE_USB);
		reg &= ~URE_PWD_DN_SCALE_MASK;
		reg |= URE_PWD_DN_SCALE(96);
		ure_write_2(sc, URE_USB_SSPHYLINK2, URE_MCU_TYPE_USB, reg);

		URE_SETBIT_1(sc, URE_USB_USB2PHY, URE_MCU_TYPE_USB,
		    URE_USB2PHY_L1 | URE_USB2PHY_SUSPEND);
	} else if (sc->chip & URE_CHIP_VER_5C20) {
		URE_CLRBIT_1(sc, URE_PLA_DMY_REG0, URE_MCU_TYPE_PLA,
		    URE_ECM_ALDPS);
	}
	if (sc->chip & (URE_CHIP_VER_5C20 | URE_CHIP_VER_5C30)) {
		if (ure_read_2(sc, URE_USB_BURST_SIZE, URE_MCU_TYPE_USB))
			URE_SETBIT_1(sc, URE_USB_CSR_DUMMY1, URE_MCU_TYPE_USB,
			    URE_DYNAMIC_BURST);
		else
			URE_CLRBIT_1(sc, URE_USB_CSR_DUMMY1, URE_MCU_TYPE_USB,
			    URE_DYNAMIC_BURST);
	}

	URE_SETBIT_1(sc, URE_USB_CSR_DUMMY2, URE_MCU_TYPE_USB, URE_EP4_FULL_FC);

	URE_CLRBIT_2(sc, URE_USB_WDT11_CTRL, URE_MCU_TYPE_USB, URE_TIMER11_EN);

	URE_CLRBIT_2(sc, URE_PLA_LED_FEATURE, URE_MCU_TYPE_PLA,
	    URE_LED_MODE_MASK);

	if ((sc->chip & URE_CHIP_VER_5C10) &&
	    sc->dev->speed != USB_SPEED_SUPER)
		reg = URE_LPM_TIMER_500MS;
	else
		reg = URE_LPM_TIMER_500US;
	ure_write_1(sc, URE_USB_LPM_CTRL, URE_MCU_TYPE_USB,
	    URE_FIFO_EMPTY_1FB | URE_ROK_EXIT_LPM | reg);

	reg = ure_read_2(sc, URE_USB_AFE_CTRL2, URE_MCU_TYPE_USB);
	reg &= ~URE_SEN_VAL_MASK;
	reg |= URE_SEN_VAL_NORMAL | URE_SEL_RXIDLE;
	ure_write_2(sc, URE_USB_AFE_CTRL2, URE_MCU_TYPE_USB, reg);

	ure_write_2(sc, URE_USB_CONNECT_TIMER, URE_MCU_TYPE_USB, 0x0001);

	URE_CLRBIT_2(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB,
	    URE_PWR_EN | URE_PHASE2_EN);
	URE_CLRBIT_2(sc, URE_USB_MISC_0, URE_MCU_TYPE_USB, URE_PCUT_STATUS);

	memset(u1u2, 0xff, sizeof(u1u2));
	ure_write_mem(sc, URE_USB_TOLERANCE, URE_BYTE_EN_SIX_BYTES, u1u2,
	    sizeof(u1u2));

	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL, URE_MCU_TYPE_PLA, 0);
	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL2, URE_MCU_TYPE_PLA, 0);
	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL3, URE_MCU_TYPE_PLA, 0);
	ure_write_2(sc, URE_PLA_MAC_PWR_CTRL4, URE_MCU_TYPE_PLA, 0);

	/* Enable Rx aggregation. */
	URE_CLRBIT_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB,
	    URE_RX_AGG_DISABLE | URE_RX_ZERO_EN);

	URE_SETBIT_2(sc, URE_PLA_RSTTALLY, URE_MCU_TYPE_PLA, URE_TALLY_RESET);

	return (0);
}

static int
ure_rtl8153b_init(ure_state_t *sc)
{
	uint16_t	reg = 0;
	int		i;

	if (sc->flags & (URE_FLAG_8156 | URE_FLAG_8156B)) {
		URE_CLRBIT_1(sc, URE_USB_ECM_OP, URE_MCU_TYPE_USB,
		    URE_EN_ALL_SPEED);
		ure_write_2(sc, URE_USB_SPEED_OPTION, URE_MCU_TYPE_USB, 0);
		URE_SETBIT_2(sc, URE_USB_ECM_OPTION, URE_MCU_TYPE_USB,
		    URE_BYPASS_MAC_RESET);

		if (sc->flags & URE_FLAG_8156B)
			URE_SETBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB,
			    URE_RX_DETECT8);
	}

	URE_CLRBIT_2(sc, URE_USB_LPM_CONFIG, URE_MCU_TYPE_USB, LPM_U1U2_EN);

	if (sc->flags & URE_FLAG_8156B) {
		if (ure_wait_for_flash(sc) != 0)
			return (-1);
	}

        for (i = 0; sc->ok && i < 500; i++) {
		if (ure_read_2(sc, URE_PLA_BOOT_CTRL, URE_MCU_TYPE_PLA) &
		    URE_AUTOLOAD_DONE)
			break;
		usb_delay_ms(20);
	}
	if (!sc->ok || i == 500) {
		klog("r8153: timeout waiting for chip autoload\n");
		return (-1);
	}

	ure_rtl8153_phy_status(sc, 0);
	ure_rtl8153_phy_status(sc, URE_PHY_STAT_LAN_ON);

	URE_CLRBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, URE_U2P3_ENABLE);

	/* MSC timer, 32760 ms. */
	ure_write_2(sc, URE_USB_MSC_TIMER, URE_MCU_TYPE_USB, 4095);

	if (!(sc->flags & URE_FLAG_8153B)) {
		/* U1/U2/L1 idle timer, 500 us. */
		ure_write_2(sc, URE_USB_U1U2_TIMER, URE_MCU_TYPE_USB, 500);
	}

	URE_CLRBIT_2(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB, URE_PWR_EN);
	URE_CLRBIT_2(sc, URE_USB_MISC_0, URE_MCU_TYPE_USB, URE_PCUT_STATUS);

	URE_CLRBIT_1(sc, URE_USB_POWER_CUT, URE_MCU_TYPE_USB,
	    URE_UPS_EN | URE_USP_PREWAKE);
	URE_CLRBIT_1(sc, URE_USB_MISC_2, URE_MCU_TYPE_USB,
	    URE_UPS_FORCE_PWR_DOWN | URE_UPS_NO_UPS);

	URE_CLRBIT_1(sc, URE_PLA_INDICATE_FALG, URE_MCU_TYPE_PLA,
	    URE_UPCOMING_RUNTIME_D3);
	URE_CLRBIT_1(sc, URE_PLA_SUSPEND_FLAG, URE_MCU_TYPE_PLA,
	    URE_LINK_CHG_EVENT);
	URE_CLRBIT_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA,
	    URE_LINK_CHANGE_FLAG);

	ure_write_1(sc, URE_PLA_CRWECR, URE_MCU_TYPE_PLA, URE_CRWECR_CONFIG);
	URE_CLRBIT_2(sc, URE_PLA_CONFIG34, URE_MCU_TYPE_PLA,
	    URE_LINK_OFF_WAKE_EN);
	ure_write_1(sc, URE_PLA_CRWECR, URE_MCU_TYPE_PLA, URE_CRWECR_NORAML);

	if (sc->flags & URE_FLAG_8153B) {
		reg = ure_read_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA);
		if (ure_read_2(sc, URE_PLA_PHYSTATUS, URE_MCU_TYPE_PLA) &
		    URE_PHYSTATUS_LINK)
			reg |= URE_CUR_LINK_OK;
		else
			reg &= ~URE_CUR_LINK_OK;
		ure_write_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA,
		    reg | URE_POLL_LINK_CHG);
	}

	if (sc->dev->speed == USB_SPEED_SUPER) {
		URE_SETBIT_2(sc, URE_USB_LPM_CONFIG, URE_MCU_TYPE_USB,
		    LPM_U1U2_EN);
	}

	if (sc->flags & URE_FLAG_8156B) {
		URE_CLRBIT_2(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA, URE_SLOT_EN);
		URE_SETBIT_2(sc, URE_PLA_CPCR, URE_MCU_TYPE_PLA,
		    URE_FLOW_CTRL_EN);

		/* Enable fc timer and set timer to 600 ms. */
		ure_write_2(sc, URE_USB_FC_TIMER, URE_MCU_TYPE_USB,
		    URE_CTRL_TIMER_EN | 75);

		reg = ure_read_2(sc, URE_USB_FW_CTRL, URE_MCU_TYPE_USB);
		if (!(ure_read_2(sc, URE_PLA_POL_GPIO_CTRL, URE_MCU_TYPE_PLA) &
		    URE_DACK_DET_EN))
			reg |= URE_FLOW_CTRL_PATCH_2;
		reg &= ~URE_AUTO_SPEEDUP;
		ure_write_2(sc, URE_USB_FW_CTRL, URE_MCU_TYPE_USB, reg);

		URE_SETBIT_2(sc, URE_USB_FW_TASK, URE_MCU_TYPE_USB,
		    URE_FC_PATCH_TASK);
	}

	if (sc->flags & (URE_FLAG_8156 | URE_FLAG_8156B)) {
		/* MAC clock speed down. */
		URE_CLRBIT_2(sc, URE_PLA_MAC_PWR_CTRL3, URE_MCU_TYPE_PLA,
		    URE_PLA_MCU_SPDWN_EN);

		reg = ure_read_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA);
		reg &= ~URE_CUR_LINK_OK;
		if (ure_read_2(sc, URE_PLA_PHYSTATUS, URE_MCU_TYPE_PLA) &
		    URE_PHYSTATUS_LINK)
			reg |= URE_CUR_LINK_OK;
		ure_write_2(sc, URE_PLA_EXTRA_STATUS, URE_MCU_TYPE_PLA,
		    reg | URE_POLL_LINK_CHG);
	} else
		URE_SETBIT_2(sc, URE_PLA_MAC_PWR_CTRL2, URE_MCU_TYPE_PLA,
		    URE_MAC_CLK_SPDWN_EN);

	/* Enable Rx aggregation. */
	URE_CLRBIT_2(sc, URE_USB_USB_CTRL, URE_MCU_TYPE_USB,
	    URE_RX_AGG_DISABLE | URE_RX_ZERO_EN);

	if (sc->flags & URE_FLAG_8156)
		URE_SETBIT_1(sc, URE_USB_BMU_CONFIG, URE_MCU_TYPE_USB,
		    URE_ACT_ODMA);

	if (!(sc->flags & URE_FLAG_8153B)) {
		/*
		 * Select force mode through 0xa5b4 bit 15
		 * 0: MDIO force mode
		 * 1: MMD force mode
		 */
		ure_phy_write(sc, 0xa5b4,
		    ure_phy_read(sc, 0xa5b4) & ~0x8000);
	}

	URE_SETBIT_2(sc, URE_PLA_RSTTALLY, URE_MCU_TYPE_PLA, URE_TALLY_RESET);

	return (0);
}

static int
ure_rtl8153_nic_reset(ure_state_t *sc)
{
	uint32_t	reg = 0;
	uint8_t		u1u2[8] = { 0 };
	int		i;

	switch (sc->flags & URE_FLAG_CHIP_MASK) {
	case URE_FLAG_8153B:
	case URE_FLAG_8156:
	case URE_FLAG_8156B:
		URE_CLRBIT_2(sc, URE_USB_LPM_CONFIG, URE_MCU_TYPE_USB,
		    LPM_U1U2_EN);
		break;
	default:
		memset(u1u2, 0x00, sizeof(u1u2));
		ure_write_mem(sc, URE_USB_TOLERANCE, URE_BYTE_EN_SIX_BYTES,
		    u1u2, sizeof(u1u2));
		break;
	}
	URE_CLRBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB, URE_U2P3_ENABLE);

	/* Disable ALDPS. */
	ure_phy_write(sc, URE_OCP_POWER_CFG,
	    ure_phy_read(sc, URE_OCP_POWER_CFG) & ~URE_EN_ALDPS);
	for (i = 0; sc->ok && i < 20; i++) {
		usb_delay_ms(1);
		if (ure_read_2(sc, 0xe000, URE_MCU_TYPE_PLA) & 0x0100)
			break;
	}

	URE_SETBIT_2(sc, URE_PLA_MISC_1, URE_MCU_TYPE_PLA, URE_RXDY_GATED_EN);
	ure_disable_teredo(sc);

	URE_CLRBIT_4(sc, URE_PLA_RCR, URE_MCU_TYPE_PLA, URE_RCR_ACPT_ALL);

	ure_reset(sc);
	ure_reset_bmu(sc);

	URE_CLRBIT_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA, URE_NOW_IS_OOB);
	URE_CLRBIT_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA, URE_MCU_BORW_EN);

	if (!(sc->flags &
	    (URE_FLAG_8156 | URE_FLAG_8156B | URE_FLAG_8157 |
	    URE_FLAG_8159))) {
		for (i = 0; sc->ok && i < URE_TIMEOUT; i++) {
			if (ure_read_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA) &
			    URE_LINK_LIST_READY)
				break;
			usb_delay_ms(1);
		}
		if (!sc->ok || i == URE_TIMEOUT) {
			klog("r8153: timeout waiting for OOB control\n");
			return (-1);
		}
		URE_SETBIT_2(sc, URE_PLA_SFF_STS_7, URE_MCU_TYPE_PLA,
		    URE_RE_INIT_LL);
		for (i = 0; sc->ok && i < URE_TIMEOUT; i++) {
			if (ure_read_1(sc, URE_PLA_OOB_CTRL, URE_MCU_TYPE_PLA) &
			    URE_LINK_LIST_READY)
				break;
			usb_delay_ms(1);
		}
		if (!sc->ok || i == URE_TIMEOUT) {
			klog("r8153: timeout waiting for OOB control\n");
			return (-1);
		}
	}

	URE_CLRBIT_2(sc, URE_PLA_CPCR, URE_MCU_TYPE_PLA, URE_CPCR_RX_VLAN);

	ure_write_2(sc, URE_PLA_RMS, URE_MCU_TYPE_PLA,
	    1522);
	ure_write_1(sc, URE_PLA_MTPS, URE_MCU_TYPE_PLA,
	    (sc->flags & (URE_FLAG_8157 | URE_FLAG_8159)) ?
	    MTPS_MAX : MTPS_JUMBO);

	if (sc->flags & (URE_FLAG_8156 | URE_FLAG_8156B | URE_FLAG_8157 |
	    URE_FLAG_8159)) {
		ure_write_2(sc, URE_PLA_RX_FIFO_FULL, URE_MCU_TYPE_PLA,
		    (sc->flags & URE_FLAG_8156) ? 1024 : 512);
		ure_write_2(sc, URE_PLA_RX_FIFO_EMPTY, URE_MCU_TYPE_PLA,
		    (sc->flags & URE_FLAG_8156) ? 2048 : 1024);

		/* Tx share fifo free credit full threshold. */
		ure_write_2(sc, URE_PLA_TXFIFO_CTRL, URE_MCU_TYPE_PLA, 8);
		ure_write_2(sc, URE_PLA_TXFIFO_FULL, URE_MCU_TYPE_PLA, 128);

		if (sc->flags & URE_FLAG_8156)
			URE_SETBIT_2(sc, URE_USB_BMU_CONFIG, URE_MCU_TYPE_USB,
			    URE_ACT_ODMA);

		/* FIFO settings */
		reg = ure_read_2(sc, URE_PLA_RXFIFO_FULL, URE_MCU_TYPE_PLA);
		reg &= ~URE_RXFIFO_FULL_MASK;
		ure_write_2(sc, URE_PLA_RXFIFO_FULL, URE_MCU_TYPE_PLA,
		    reg | 0x0008);

		URE_CLRBIT_2(sc, URE_PLA_MAC_PWR_CTRL3, URE_MCU_TYPE_PLA,
		    URE_PLA_MCU_SPDWN_EN);

		if (!(sc->flags & (URE_FLAG_8157 | URE_FLAG_8159)))
			URE_CLRBIT_2(sc, URE_USB_SPEED_OPTION, URE_MCU_TYPE_USB,
			    URE_RG_PWRDN_EN | URE_ALL_SPEED_OFF);

		ure_write_4(sc, URE_USB_RX_BUF_TH, URE_MCU_TYPE_USB,
		    0x00600400);
	} else {
		URE_SETBIT_2(sc, URE_PLA_TCR0, URE_MCU_TYPE_PLA,
		    URE_TCR0_AUTO_FIFO);
		ure_reset(sc);

		/* Configure Rx FIFO threshold. */
		ure_write_4(sc, URE_PLA_RXFIFO_CTRL0, URE_MCU_TYPE_PLA,
		    URE_RXFIFO_THR1_NORMAL);
		ure_write_2(sc, URE_PLA_RXFIFO_CTRL1, URE_MCU_TYPE_PLA,
		    URE_RXFIFO_THR2_NORMAL);
		ure_write_2(sc, URE_PLA_RXFIFO_CTRL2, URE_MCU_TYPE_PLA,
		    URE_RXFIFO_THR3_NORMAL);

		/* Configure Tx FIFO threshold. */
		ure_write_4(sc, URE_PLA_TXFIFO_CTRL, URE_MCU_TYPE_PLA,
		    URE_TXFIFO_THR_NORMAL2);

		if (sc->flags & URE_FLAG_8153B) {
			ure_write_4(sc, URE_USB_RX_BUF_TH, URE_MCU_TYPE_USB,
			    URE_RX_THR_B);

			URE_CLRBIT_2(sc, URE_PLA_MAC_PWR_CTRL3,
			    URE_MCU_TYPE_PLA, URE_PLA_MCU_SPDWN_EN);
		} else {
			URE_SETBIT_1(sc, URE_PLA_CONFIG6, URE_MCU_TYPE_PLA,
			    URE_LANWAKE_CLR_EN);
			URE_CLRBIT_1(sc, URE_PLA_LWAKE_CTRL_REG,
			    URE_MCU_TYPE_PLA, URE_LANWAKE_PIN);
			URE_CLRBIT_2(sc, URE_USB_SSPHYLINK1, URE_MCU_TYPE_USB,
			    URE_DELAY_PHY_PWR_CHG);
		}
	}

	/* Enable ALDPS. */
	ure_phy_write(sc, URE_OCP_POWER_CFG,
	    ure_phy_read(sc, URE_OCP_POWER_CFG) | URE_EN_ALDPS);

	if ((sc->chip & (URE_CHIP_VER_5C20 | URE_CHIP_VER_5C30)) ||
	    (sc->flags & (URE_FLAG_8156 | URE_FLAG_8156B)))
		URE_SETBIT_2(sc, URE_USB_U2P3_CTRL, URE_MCU_TYPE_USB,
		    URE_U2P3_ENABLE);

	if (sc->flags & (URE_FLAG_8153B | URE_FLAG_8156 | URE_FLAG_8156B)) {
		if (sc->dev->speed == USB_SPEED_SUPER)
			URE_SETBIT_2(sc, URE_USB_LPM_CONFIG, URE_MCU_TYPE_USB,
			    LPM_U1U2_EN);
	} else if (!(sc->flags & (URE_FLAG_8157 | URE_FLAG_8159))) {
		memset(u1u2, 0xff, sizeof(u1u2));
		ure_write_mem(sc, URE_USB_TOLERANCE, URE_BYTE_EN_SIX_BYTES,
		    u1u2, sizeof(u1u2));
	}

	return (0);
}

static int ure_media(usbnet_t* u) {
    ure_state_t* s=u->priv;
    ure_write_1(s,URE_PLA_CRWECR,URE_MCU_TYPE_PLA,URE_CRWECR_CONFIG);
    ure_write_mem(s,URE_PLA_IDR,URE_MCU_TYPE_PLA|0x3f,s->mac,8);
    ure_write_1(s,URE_PLA_CRWECR,URE_MCU_TYPE_PLA,URE_CRWECR_NORAML);
    uint32_t room=u->rx_size-1522-24-8;
    ure_write_2(s,URE_USB_RX_EARLY_SIZE,URE_MCU_TYPE_USB,room/(s->flags?8:4));
    uint16_t agg=s->flags&URE_FLAG_8153B?158:s->flags?80:
        (s->dev->speed==USB_SPEED_SUPER?URE_COALESCE_SUPER:
         s->dev->speed==USB_SPEED_HIGH?URE_COALESCE_HIGH:URE_COALESCE_SLOW)/8;
    ure_write_2(s,URE_USB_RX_EARLY_AGG,URE_MCU_TYPE_USB,agg);
    if(s->flags) {
        ure_write_2(s,URE_USB_PM_CTRL_STATUS,URE_MCU_TYPE_USB,1875);
        if(s->dev->speed==USB_SPEED_HIGH) {
            uint16_t v=ure_read_2(s,URE_USB_L1_CTRL,URE_MCU_TYPE_USB);
            ure_write_2(s,URE_USB_L1_CTRL,URE_MCU_TYPE_USB,(v&~0xf)|1);
        }
    }
    if(s->flags&URE_FLAG_8156B)
        URE_CLRBIT_2(s,URE_USB_RX_AGGR_NUM,URE_MCU_TYPE_USB,URE_RX_AGGR_NUM_MASK);
    if((s->chip&URE_CHIP_VER_6010) || (s->flags&URE_FLAG_8156B)) {
        URE_CLRBIT_2(s,URE_USB_FW_TASK,URE_MCU_TYPE_USB,URE_FC_PATCH_TASK);
        usb_delay_ms(1);
        URE_SETBIT_2(s,URE_USB_FW_TASK,URE_MCU_TYPE_USB,URE_FC_PATCH_TASK);
    }
    URE_CLRBIT_2(s,URE_PLA_FMC,URE_MCU_TYPE_PLA,URE_FMC_FCR_MCU_EN);
    URE_SETBIT_2(s,URE_PLA_FMC,URE_MCU_TYPE_PLA,URE_FMC_FCR_MCU_EN);
    URE_SETBIT_1(s,URE_PLA_CR,URE_MCU_TYPE_PLA,URE_CR_RE|URE_CR_TE);
    if(s->flags) ure_write_1(s,URE_USB_UPT_RXDMA_OWN,URE_MCU_TYPE_USB,URE_OWN_UPDATE|URE_OWN_CLEAR);
    URE_CLRBIT_2(s,URE_PLA_MISC_1,URE_MCU_TYPE_PLA,URE_RXDY_GATED_EN);
    return s->ok?0:-1;
}
static int ure_link(usbnet_t* u) {
    ure_state_t* s=u->priv;
    s->ok=1;
    uint16_t status=ure_read_2(s,URE_PLA_PHYSTATUS,URE_MCU_TYPE_PLA);
    if(!s->ok || !(status&URE_PHYSTATUS_LINK)) { s->last_link=0; return 0; }
    if(status!=s->last_link) {
        if(ure_media(u)<0) return 0;
        s->last_link=status;
    }
    return 1;
}
static int ure_tx(usbnet_t* u,uint8_t* b,const uint8_t* frame,uint32_t n) {
    if(n<14 || n>ETH_FRAME_MAX || n+8>u->tx_size) return -1;
    ure_put32(b,n|0xc0000000u); ure_put32(b+4,0);
    memcpy(b+8,frame,n); return n+8;
}
static void ure_rx(usbnet_t* u,const uint8_t* b,uint32_t n) {
    uint32_t off=0;
    /* Check the entire aggregate before handing any frame to the stack. */
    while(off<n) {
        if(n-off<24) goto bad;
        uint32_t size=ure_le32(b+off)&0x7fff;
        if(size<18 || size>ETH_FRAME_MAX+4 || size>n-off-24) goto bad;
        uint32_t step=24+((size+7)&~7u);
        if(step>n-off) {
            if(24+size!=n-off) goto bad;
            step=n-off; /* final datagram need not carry alignment padding */
        }
        off+=step;
    }
    off=0;
    while(off<n) {
        uint32_t size=ure_le32(b+off)&0x7fff;
        u->nd.rx_packets++; u->nd.rx_bytes+=size-4;
        net_rx(&u->nd,b+off+24,size-4);
        uint32_t step=24+((size+7)&~7u);
        off+=step>n-off?n-off:step;
    }
    return;
bad: u->nd.rx_errors++;
}
typedef struct { usb_ep_desc_t in,out; int vendor,have_in,have_out,done; } ure_eps_t;
static int ure_ep(const uint8_t* b,void* ctx) {
    ure_eps_t* e=ctx;
    if(e->done) return 1;
    if(b[1]==USB_DT_INTERFACE && b[0]>=sizeof(usb_iface_desc_t)) {
        const usb_iface_desc_t* i=(const usb_iface_desc_t*)b;
        e->vendor=i->bInterfaceClass==0xff && i->bAlternateSetting==0;
        e->have_in=e->have_out=0;
    } else if(b[1]==USB_DT_ENDPOINT && b[0]>=sizeof(usb_ep_desc_t) && e->vendor) {
        const usb_ep_desc_t* ep=(const usb_ep_desc_t*)b;
        if((ep->bmAttributes&3)!=USB_EP_BULK || !(ep->bEndpointAddress&15)) return 0;
        if(ep->bEndpointAddress&0x80) { e->in=*ep; e->have_in=1; }
        else { e->out=*ep; e->have_out=1; }
        if(e->have_in && e->have_out) { e->done=1; return 1; }
    }
    return 0;
}
static int ure_config(usb_device_t* d,int c,ure_eps_t* e) {
    memset(e,0,sizeof(*e));
    return usb_walk_config(d,c,ure_ep,e)>=0 && e->done?0:-1;
}
static int ure_probe(usb_device_t* d) {
    if(d->desc.idVendor!=0x0bda || (d->desc.idProduct!=0x8153 && d->desc.idProduct!=0x8156)) return -1;
    for(int c=0;c<USB_MAX_CONFIGS;c++) {
        ure_eps_t e;
        if(d->configs[c] && !ure_config(d,c,&e)) return c;
    }
    return -1;
}
static int ure_attach(usb_device_t* d) {
    ure_eps_t e;
    if(ure_config(d,d->active_config,&e)) return -1;
    usbnet_t* u=kzalloc(sizeof(*u)); ure_state_t* s=kzalloc(sizeof(*s));
    if(!u || !s) { kfree(u); kfree(s); return -1; }
    s->dev=d; s->ok=1; u->dev=d; u->priv=s;
    s->version=ure_read_2(s,URE_PLA_TCR1,URE_MCU_TYPE_PLA)&0x7cf0;
    switch(s->version) {
    case 0x5c00:s->chip=URE_CHIP_VER_5C00;break;
    case 0x5c10:s->chip=URE_CHIP_VER_5C10;break;
    case 0x5c20:s->chip=URE_CHIP_VER_5C20;break;
    case 0x5c30:s->chip=URE_CHIP_VER_5C30;break;
    case 0x6000:s->flags=URE_FLAG_8153B;break;
    case 0x6010:s->flags=URE_FLAG_8153B;s->chip=URE_CHIP_VER_6010;break;
    case 0x7020:case 0x7030:s->flags=URE_FLAG_8156;break;
    case 0x7410:s->flags=URE_FLAG_8156B;break;
    default: goto fail; /* no unknown-family or RTL8152 fallback */
    }
    if(!s->ok || (s->flags?ure_rtl8153b_init(s):ure_rtl8153_init(s))) goto fail;
    if(ure_mem(s,0,URE_PLA_BACKUP,URE_MCU_TYPE_PLA,s->mac,8)<0) goto fail;
    uint8_t any=0;
    for(int i=0;i<6;i++) any|=s->mac[i];
    if(!any || (s->mac[0]&1)) goto fail;
    memcpy(u->nd.mac,s->mac,6);
    if(ure_rtl8153_nic_reset(s) || !s->ok) goto fail;
    u->rx_size=32768; u->tx_size=ETH_FRAME_MAX+8;
    u->ep_in=e.in.bEndpointAddress; u->ep_out=e.out.bEndpointAddress;
    u->mps_out=e.out.wMaxPacketSize&0x7ff;
    if(!u->mps_out || usb_open_endpoint(d,&e.in) || usb_open_endpoint(d,&e.out)) goto fail;
    /* Software checksums and unstripped VLAN frames, no offload descriptors. */
    ure_write_4(s,URE_PLA_MAR,URE_MCU_TYPE_PLA,0xffffffffu);
    ure_write_4(s,URE_PLA_MAR+4,URE_MCU_TYPE_PLA,0xffffffffu);
    uint32_t filter=ure_read_4(s,URE_PLA_RCR,URE_MCU_TYPE_PLA);
    ure_write_4(s,URE_PLA_RCR,URE_MCU_TYPE_PLA,(filter&~URE_RCR_ACPT_ALL)|URE_RCR_APM|URE_RCR_AB|URE_RCR_AM);
    /* Advertise 10/100/1000; 2.5G only for the supported RTL8156 revisions. */
    ure_phy_write(s,URE_OCP_BASE_MII+8,0x0de1);
    ure_phy_write(s,URE_OCP_BASE_MII+18,0x0200);
    if(s->flags&(URE_FLAG_8156|URE_FLAG_8156B)) {
        uint16_t adv=ure_phy_read(s,URE_OCP_10GBT_CTRL);
        if(adv==0xffff) goto fail;
        ure_phy_write(s,URE_OCP_10GBT_CTRL,adv|URE_ADV_2500TFDX);
    }
    ure_phy_write(s,URE_OCP_BASE_MII,0x1200);
    if(ure_media(u)<0) goto fail;
    u->zlp=1; u->encap=ure_tx; u->decap=ure_rx; u->get_link=ure_link;
    u->nd.name="r8153";
    u->nd.model=s->flags&URE_FLAG_8156B?"Realtek RTL8156B USB 2.5GbE":
        s->flags&URE_FLAG_8156?"Realtek RTL8156 USB 2.5GbE":
        s->flags&URE_FLAG_8153B?"Realtek RTL8153B USB Gigabit":"Realtek RTL8153 USB Gigabit";
    if(usbnet_start(u)<0) goto fail;
    d->drvdata=u;
    klog("r8153: version %04x initialized; experimental physical hardware support\n",s->version);
    return 0;
fail:
    /* Queued URBs exist only after a successful usbnet_start. */
    kfree(s); kfree(u); return -1;
}
static void ure_detach(usb_device_t* d) { if(d->drvdata) usbnet_stop(d->drvdata); }
const usb_driver_t r8153_driver={"r8153",ure_probe,ure_attach,0,ure_detach};
