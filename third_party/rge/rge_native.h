/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BANANA_RGE_NATIVE_H
#define BANANA_RGE_NATIVE_H
#include "types.h"
#include "kstring.h"
#include "serial.h"
#define __packed __attribute__((packed))
#define __aligned(n) __attribute__((aligned(n)))
#define ETHER_ADDR_LEN 6
#define ETHER_HDR_LEN 14
#define RGE_FLAG_DESCV2 1u
#define RGE_FLAG_MSI 2u
#define RGE_FLAG_MULTIVEC 4u
#define ETHER_CRC_LEN 4
#define ETHER_VLAN_ENCAP_LEN 4
#define PCI_MAPREG_START 0x10
#include "rge_regs.h"
struct rge_softc {
    enum rge_mac_type rge_type;
    uint32_t rge_flags;
    uint16_t rge_rcodever;
    uint64_t rge_mcodever;
    uint8_t* mmio;
    int fault;
    unsigned sc_nqueues,sc_ntxqs;
    uintptr_t rx_addr,tx_addr;
    struct { const char* dv_xname; } sc_dev;
};
uint32_t rge_native_read(struct rge_softc*,uint32_t,unsigned);
void rge_native_write(struct rge_softc*,uint32_t,uint32_t,unsigned);
void rge_delay_us(uint32_t);
#define RGE_READ_1(s,r) ((uint8_t)rge_native_read((s),(r),1))
#define RGE_READ_2(s,r) ((uint16_t)rge_native_read((s),(r),2))
#define RGE_READ_4(s,r) rge_native_read((s),(r),4)
#define RGE_WRITE_1(s,r,v) rge_native_write((s),(r),(uint8_t)(v),1)
#define RGE_WRITE_2(s,r,v) rge_native_write((s),(r),(uint16_t)(v),2)
#define RGE_WRITE_4(s,r,v) rge_native_write((s),(r),(uint32_t)(v),4)
#define DELAY(us) do { if(!sc->fault) rge_delay_us(us); } while(0)
#define printf klog
#define ETIMEDOUT 1
#define MII_BMCR 0
#define MII_ANAR 4
#define MII_100T2CR 9
#define BMCR_RESET 0x8000
#define BMCR_AUTOEN 0x1000
#define BMCR_STARTNEG 0x0200
#define BMCR_PDOWN 0x0800
#define ANAR_TX_FD 0x0100
#define ANAR_TX 0x0080
#define ANAR_10_FD 0x0040
#define ANAR_10 0x0020
#define ANAR_PAUSE_ASYM 0x0800
#define ANAR_FC 0x0400
#define GTCR_ADV_1000TFDX 0x0200
#define GTCR_ADV_1000THDX 0x0100
#define nitems(a) ((int)(sizeof(a)/sizeof((a)[0])))
int rge_chipinit(struct rge_softc*);
int rge_phy_config(struct rge_softc*);
int rge_reset(struct rge_softc*);
void rge_set_macaddr(struct rge_softc*,const uint8_t*);
void rge_get_macaddr(struct rge_softc*,uint8_t*);
void rge_disable_aspm_clkreq(struct rge_softc*);
uint16_t rge_read_mac_ocp(struct rge_softc*,uint16_t);
void rge_write_mac_ocp(struct rge_softc*,uint16_t,uint16_t);
uint16_t rge_read_phy(struct rge_softc*,uint16_t,uint16_t);
void rge_write_phy(struct rge_softc*,uint16_t,uint16_t,uint16_t);
uint16_t rge_read_phy_ocp(struct rge_softc*,uint16_t);
void rge_write_phy_ocp(struct rge_softc*,uint16_t,uint16_t);
uint32_t rge_read_csi(struct rge_softc*,uint32_t);
void rge_write_csi(struct rge_softc*,uint32_t,uint32_t);
int rge_native_start(struct rge_softc*);
#endif
