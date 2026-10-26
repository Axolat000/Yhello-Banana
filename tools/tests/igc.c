/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Runs the real native DMA frontend and Intel HAL against a register model.
 * No claim of electrical/PHY validation on physical hardware. */
#include "../../kernel/types.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void* calloc(size_t,size_t);
extern void free(void*);
extern int posix_memalign(void**,size_t,size_t);
#define IGC_MODEL
#include "../../net/igc.c"
#define CHECK(c) do { checks++; if(!(c)) { printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort(); } } while(0)
static unsigned checks,registered,received,enabled,reads,writes,reset_count,mdic_count;
static uint32_t now;
static struct model {
    uint32_t regs[0x20000/4];
    uint16_t phy[32],mmd[32][256],eeprom[64],command,devad,address;
    int stuck_mdic,mdic_error,bad_phyreset,bad_autoload,bad_mac,fw_busy,absent,stuck_reset,stuck_semaphore;
} models[8];
static netdev_t* devs[8];
static void* allocations[8];
static unsigned nalloc;
uint32_t timer_ms(void) { return now; }
void igc_delay_us(uint32_t us) { now+=(us+999)/1000;CHECK(now<10000000); }
void klog(const char* f,...) { (void)f; }
void* kmalloc_aligned(size_t n,size_t align) {
    void* p=NULL;if(posix_memalign(&p,align,n)) return NULL;
    CHECK(nalloc<8);allocations[nalloc++]=p;return p;
}
void net_register_device(netdev_t* nd) {
    CHECK(registered<8);devs[registered++]=nd;nd->present=1;
}
void net_rx(netdev_t* nd,const uint8_t* frame,uint32_t len) {
    CHECK(nd && len>=14 && len<=1514 && frame[0]==0xa5);received++;
}
int pci_scan(int (*cb)(const pci_dev_t*,void*),void* arg) {
    pci_dev_t devices[]={
        {.bus=1,.dev=0,.vendor=0x8086,.device=0x15f3,.class_code=2},
        {.bus=1,.dev=1,.vendor=0x8086,.device=0x125c,.class_code=2},
        {.bus=1,.dev=2,.vendor=0x8086,.device=0x100e,.class_code=2},
        {.bus=1,.dev=3,.vendor=0x10ec,.device=0x8168,.class_code=2},
        {.bus=1,.dev=4,.vendor=0x8086,.device=0x15f3,.class_code=1}};
    for(unsigned i=0;i<5;i++) if(cb(devices+i,arg)) return 1;
    return 0;
}
uintptr_t pci_bar(const pci_dev_t* pd,int bar,int* io) { CHECK(bar==0);*io=0;return (uintptr_t)&models[pd->dev]; }
void pci_enable_from(const pci_dev_t* pd,const char* file) { CHECK(pd->dev<2 && file);enabled++; }
uint16_t pci_read16(uint8_t b,uint8_t d,uint8_t f,uint8_t off) {
    (void)b;(void)f;CHECK(off==4);return models[d].command;
}
void pci_write16(uint8_t b,uint8_t d,uint8_t f,uint8_t off,uint16_t value) {
    (void)b;(void)f;CHECK(off==4);models[d].command=value;
}
static uint16_t phy_read(struct model* m,unsigned reg) {
    if(reg==14 && (m->devad&0x4000)) return m->mmd[m->devad&31][m->address&255];
    return m->phy[reg];
}
static void phy_write(struct model* m,unsigned reg,uint16_t v) {
    if(reg==13) m->devad=v;
    else if(reg==14) {
        if(m->devad&0x4000) m->mmd[m->devad&31][m->address&255]=v;
        else m->address=v;
    }
    m->phy[reg]=v;
}
uint32_t igc_mmio_read(struct igc_hw* hw,uint32_t reg) {
    struct model* m=(void*)hw->hw_addr;
    CHECK(reg<sizeof(m->regs) && !(reg&3));reads++;
    if(m->absent) { hw->native_fault=true;return 0xffffffffu; }
    if(reg==IGC_SWSM && m->stuck_semaphore) return IGC_SWSM_SMBI;
    if(reg==IGC_SW_FW_SYNC && m->fw_busy) return 0xffff0000;
    return m->regs[reg/4];
}
void igc_mmio_write(struct igc_hw* hw,uint32_t reg,uint32_t value) {
    struct model* m=(void*)hw->hw_addr;
    CHECK(reg<sizeof(m->regs) && !(reg&3));writes++;
    CHECK(reg!=IGC_EEWR && reg!=IGC_SRWR); /* never program NVM */
    if(reg==IGC_MDIC) {
        mdic_count++;
        unsigned phy=(value>>21)&31,idx=(value>>16)&31;
        CHECK(phy==0);
        if(value&IGC_MDIC_OP_READ) value=(value&~0xffffu)|phy_read(m,idx);
        else phy_write(m,idx,(uint16_t)value);
        if(!m->stuck_mdic) value|=IGC_MDIC_READY;
        if(m->mdic_error) value|=IGC_MDIC_ERROR;
    }
    if(reg==IGC_EERD) {
        unsigned idx=(value>>2)&0x3fff;CHECK(idx<64);
        value|=2u|(uint32_t)m->eeprom[idx]<<16;
    }
    if(reg==IGC_CTRL && (value&IGC_CTRL_DEV_RST)) {
        reset_count++;
        if(!m->stuck_reset) value&=~(IGC_CTRL_DEV_RST|IGC_CTRL_GIO_MASTER_DISABLE);
        if(m->bad_autoload) m->regs[IGC_EECD/4]&=~IGC_EECD_AUTO_RD;
    }
    if(reg==IGC_CTRL && (value&IGC_CTRL_PHY_RST) && !m->bad_phyreset)
        m->regs[IGC_I225_PHPM/4]|=IGC_I225_PHPM_RST_COMPL;
    m->regs[reg/4]=value;
}
static void init(void) {
    for(unsigned i=0;i<nalloc;i++) free(allocations[i]);
    nalloc=igc_ncards=registered=received=enabled=reads=writes=reset_count=mdic_count=now=0;
    memset(models,0,sizeof(models));memset(igc_cards,0,sizeof(igc_cards));
    for(unsigned i=0;i<8;i++) {
        struct model* m=&models[i];
        m->regs[IGC_RXPBS/4]=34;
        m->regs[IGC_EECD/4]=IGC_EECD_AUTO_RD|IGC_EECD_FLASH_DETECTED_I225;
        m->regs[IGC_RAL(0)/4]=0x33221102;
        m->regs[IGC_RAH(0)/4]=IGC_RAH_AV|0x5544;
        m->regs[IGC_STATUS/4]=IGC_STATUS_LU|IGC_STATUS_FD|IGC_STATUS_SPEED_1000;
        m->regs[IGC_I225_PHPM/4]=IGC_I225_PHPM_RST_COMPL;
        m->phy[PHY_ID1]=0x67c9;m->phy[PHY_ID2]=0xdc00;
        m->phy[PHY_STATUS]=MII_SR_LINK_STATUS|MII_SR_AUTONEG_COMPLETE;
        m->phy[PHY_AUTONEG_ADV]=0x0de1;m->phy[PHY_LP_ABILITY]=0x0de1;
        m->eeprom[NVM_COMPAT]=0; /* no alternate MAC */
        unsigned sum=0;for(unsigned j=0;j<63;j++) sum+=m->eeprom[j];
        m->eeprom[63]=(uint16_t)(NVM_SUM-sum);
    }
}
static void rx(igc_card_t* c,uint32_t status,uint16_t length) {
    unsigned i=c->rxcur;
    memset(c->rxbuf[i],0xa5,IGC_BUFFER);
    c->rx[i].wb.upper.length=length;c->rx[i].wb.upper.status_error=status;
    igc_poll(&c->nd);
    CHECK(c->rx[i].read.pkt_addr==(uint64_t)(uintptr_t)c->rxbuf[i] &&
          c->rx[i].read.hdr_addr==0);
}
int main(void) {
    init();igc_probe_all();
    CHECK(registered==2 && enabled==2 && reset_count==2 && mdic_count>20);
    CHECK(!memcmp(devs[0]->mac,"\x02\x11\x22\x33\x44\x55",6));
    igc_probe_all();CHECK(registered==2); /* no duplicate attachment */
    for(unsigned n=0;n<2;n++) {
        igc_card_t* c=devs[n]->priv;struct model* m=&models[n];
        CHECK(c->nd.link_up(&c->nd));
        CHECK((m->command&(1u<<10)) && m->regs[IGC_IMC/4]==0xffffffffu);
        CHECK(m->regs[IGC_RDLEN(0)/4]==4096 && m->regs[IGC_TDLEN(0)/4]==1024);
        CHECK((m->regs[IGC_SRRCTL(0)/4]&IGC_SRRCTL_DESCTYPE_ADV_ONEBUF)!=0);
        uint8_t frame[1515];memset(frame,0xa5,sizeof(frame));
        CHECK(igc_send(&c->nd,frame,13)<0 && igc_send(&c->nd,frame,1515)<0);
        for(unsigned i=0;i<IGC_TX_COUNT-1;i++) CHECK(igc_send(&c->nd,frame,1514)==0);
        CHECK(igc_send(&c->nd,frame,1514)<0);
        CHECK(!memcmp(c->txbuf[0],frame,1514));
        CHECK(c->tx[0].read.olinfo_status==(1514u<<14));
        for(unsigned i=0;i<IGC_TX_COUNT-1;i++) c->tx[i].wb.status=IGC_TXD_STAT_DD;
        igc_poll(&c->nd);CHECK(c->txused==0);
        for(unsigned i=0;i<300;i++) {
            unsigned j=c->txcur;
            CHECK(igc_send(&c->nd,frame,60)==0);
            c->tx[j].wb.status=IGC_TXD_STAT_DD;igc_poll(&c->nd);
            rx(c,IGC_RXD_STAT_DD|IGC_RXD_STAT_EOP,1514);
        }
        unsigned before=received;
        rx(c,IGC_RXD_STAT_DD,100);
        rx(c,IGC_RXD_STAT_DD|IGC_RXD_STAT_EOP,100); /* drop entire fragmented chain */
        rx(c,IGC_RXD_STAT_DD|IGC_RXD_STAT_EOP|IGC_RXDEXT_STATERR_RXE,60);
        rx(c,IGC_RXD_STAT_DD|IGC_RXD_STAT_EOP,0xffff);
        CHECK(received==before);
        rx(c,IGC_RXD_STAT_DD|IGC_RXD_STAT_EOP,60);CHECK(received==before+1);
        m->regs[IGC_STATUS/4]&=~IGC_STATUS_LU;m->phy[PHY_STATUS]=0;now+=501;
        CHECK(!igc_carrier(&c->nd) && igc_send(&c->nd,frame,60)<0);
        m->regs[IGC_STATUS/4]|=IGC_STATUS_LU;
        m->phy[PHY_STATUS]=MII_SR_LINK_STATUS|MII_SR_AUTONEG_COMPLETE;now+=501;
        CHECK(igc_carrier(&c->nd));
        m->mdic_error=1;now+=501;
        CHECK(!igc_carrier(&c->nd));
        m->mdic_error=0;now+=501;CHECK(igc_carrier(&c->nd));
        m->mdic_error=1;
        CHECK(igc_write_phy_reg_gpy(&c->hw,PHY_CONTROL,0)<0);
        CHECK(!(m->regs[IGC_SW_FW_SYNC/4]&IGC_SWFW_PHY0_SM));
        m->mdic_error=0;now+=501;CHECK(igc_carrier(&c->nd));
        igc_multicast(&c->nd,0);CHECK(!(m->regs[IGC_RCTL/4]&IGC_RCTL_MPE));
        igc_multicast(&c->nd,1);CHECK(m->regs[IGC_RCTL/4]&IGC_RCTL_MPE);
    }
    pci_dev_t pd={.bus=1,.vendor=0x8086,.device=0x15f3,.class_code=2};
    init();models[0].stuck_mdic=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].mdic_error=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].regs[IGC_EECD/4]&=~IGC_EECD_FLASH_DETECTED_I225;
    CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].bad_autoload=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].regs[IGC_RAL(0)/4]=0;models[0].regs[IGC_RAH(0)/4]=0;
    CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].eeprom[1]=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].fw_busy=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].phy[PHY_ID1]=0xffff;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].stuck_reset=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].absent=1;CHECK(igc_attach(&pd)<0 && !registered);
    init();models[0].bad_phyreset=1;models[0].regs[IGC_I225_PHPM/4]=0;
    CHECK(igc_attach(&pd)<0 && !registered);
    init();
    struct igc_hw dummy={.hw_addr=(void*)&models[0]};
    dummy.nvm.word_size=64;models[0].stuck_semaphore=1;
    igc_release_swfw_sync_i225(&dummy,IGC_SWFW_PHY0_SM);
    CHECK(dummy.native_fault && now<1000);
    init();printf("igc: %u checks passed (native HAL/register/DMA model)\n",checks);
    return 0;
}
