/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation
 * Native hardware-only adaptation; no Linux networking/kernel ABI.
 */
#ifndef BANANA_RTW8821C_NATIVE_H
#define BANANA_RTW8821C_NATIVE_H
#include "types.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef uint64_t u64;
typedef int8_t s8;typedef int16_t s16;typedef int32_t s32;
typedef u16 __le16;typedef u32 __le32;
typedef int bool;
#define true 1
#define false 0
#define __packed __attribute__((packed))
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((0xffffffffu<<(l))&(0xffffffffu>>(31-(h))))
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define FIELD_GET(m,v) (((u32)(v)&(m))>>__builtin_ctz((u32)(m)))
#define FIELD_PREP(m,v) (((u32)(v)<<__builtin_ctz((u32)(m)))&(m))
#define le32_to_cpu(v) (v)
#define WARN(c,...) do{if(c)rtwdev->ok=0;}while(0)
#define rtw_err(...) ((void)0)
#define rtw_dbg(...) ((void)0)
#define lockdep_assert_held(...) ((void)0)
#define GFP_KERNEL 0
#define EINVAL 22
#define EIO 5
#define EBUSY 16
#define EALREADY 114
#define ETIMEDOUT 110
#define ENOMEM 12
#define RTW_FLAG_POWERON 0
#define RTW_FLAG_FW_RUNNING 1
#define RTW_CHIP_TYPE_8821C 1
#define RTW_CHIP_TYPE_8822B 2
#define RTW_CHIP_TYPE_8822C 3
#define RTW_CHIP_TYPE_8723D 4
#define RTW_CHIP_TYPE_8703B 5
#define RF_PATH_A 0
#define RTW_HCI_TYPE_PCIE 0
#define RTW_HCI_TYPE_USB 1
#define RTW_HCI_TYPE_SDIO 2
#define SWITCH_TO_BTG 0
#define SWITCH_TO_WLG 1
#define SWITCH_TO_WLA 2
#define SWITCH_TO_BT 3
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "rtw8821c_reg.h"
#define DLFW_RESTORE_REG_NUM 6
#define TX_DESC_SIZE 48
enum rtw_bandwidth {
	RTW_CHANNEL_WIDTH_20	= 0,
	RTW_CHANNEL_WIDTH_40	= 1,
	RTW_CHANNEL_WIDTH_80	= 2,
	RTW_CHANNEL_WIDTH_160	= 3,
	RTW_CHANNEL_WIDTH_80_80	= 4,
	RTW_CHANNEL_WIDTH_5	= 5,
	RTW_CHANNEL_WIDTH_10	= 6,
};
enum rtw_sc_offset {
	RTW_SC_DONT_CARE	= 0,
	RTW_SC_20_UPPER		= 1,
	RTW_SC_20_LOWER		= 2,
	RTW_SC_20_UPMOST	= 3,
	RTW_SC_20_LOWEST	= 4,
	RTW_SC_40_UPPER		= 9,
	RTW_SC_40_LOWER		= 10,
};
enum rtw_dma_mapping {
	RTW_DMA_MAPPING_EXTRA	= 0,
	RTW_DMA_MAPPING_LOW	= 1,
	RTW_DMA_MAPPING_NORMAL	= 2,
	RTW_DMA_MAPPING_HIGH	= 3,

	RTW_DMA_MAPPING_MAX,
	RTW_DMA_MAPPING_UNDEF,
};
enum rtw_pwr_seq_cmd_delay_unit {
	RTW_PWR_DELAY_US,
	RTW_PWR_DELAY_MS,
};
struct rtw_pwr_seq_cmd {
	u16 offset;
	u8 cut_mask;
	u8 intf_mask;
	u8 base:4;
	u8 cmd:4;
	u8 mask;
	u8 value;
};
struct rtw_rqpn {
	enum rtw_dma_mapping dma_map_vo;
	enum rtw_dma_mapping dma_map_vi;
	enum rtw_dma_mapping dma_map_be;
	enum rtw_dma_mapping dma_map_bk;
	enum rtw_dma_mapping dma_map_mg;
	enum rtw_dma_mapping dma_map_hi;
};
struct rtw_page_table {
	u16 hq_num;
	u16 nq_num;
	u16 lq_num;
	u16 exq_num;
	u16 gapq_num;
};
struct rtw_fifo_conf {
	/* tx fifo information */
	u16 rsvd_boundary;
	u16 rsvd_pg_num;
	u16 rsvd_drv_pg_num;
	u16 txff_pg_num;
	u16 acq_pg_num;
	u16 rsvd_drv_addr;
	u16 rsvd_h2c_info_addr;
	u16 rsvd_h2c_sta_info_addr;
	u16 rsvd_h2cq_addr;
	u16 rsvd_cpu_instr_addr;
	u16 rsvd_fw_txbuf_addr;
	u16 rsvd_csibuf_addr;
	const struct rtw_rqpn *rqpn;
};
struct rtw_fw_hdr {
	__le16 signature;
	u8 category;
	u8 function;
	__le16 version;		/* 0x04 */
	u8 subversion;
	u8 subindex;
	__le32 rsvd;		/* 0x08 */
	__le32 feature;		/* 0x0C */
	u8 month;		/* 0x10 */
	u8 day;
	u8 hour;
	u8 min;
	__le16 year;		/* 0x14 */
	__le16 rsvd3;
	u8 mem_usage;		/* 0x18 */
	u8 rsvd4[3];
	__le16 h2c_fmt_ver;	/* 0x1C */
	__le16 rsvd5;
	__le32 dmem_addr;	/* 0x20 */
	__le32 dmem_size;
	__le32 rsvd6;
	__le32 rsvd7;
	__le32 imem_size;	/* 0x30 */
	__le32 emem_size;
	__le32 emem_addr;
	__le32 imem_addr;
} __packed;

struct firmware {const u8*data;u32 size;};
struct rtw_fw_state {const struct firmware*firmware;};
struct rtw_chip_info {
 unsigned id,sys_func_en,tx_pkt_desc_sz,csi_buf_pg_num,rsvd_drv_pg_num,txff_size,rxff_size,usb_tx_agg_desc_num;
 const struct rtw_pwr_seq_cmd **pwr_on_seq,**pwr_off_seq;
 const struct rtw_rqpn*rqpn_table;const struct rtw_page_table*page_table;
};
struct rtw_hal {u8 cut_version,rfe_btg,pkg_type;u32 ch_param[3];};
struct rtw_efuse {u8 tx_bb_swing_setting_2g,tx_bb_swing_setting_5g;};
struct rtw_backup_info {u32 reg,val;u8 len;};
struct rtw_dev {
 int ok;void*priv;
 int (*control)(struct rtw_dev*,u32,void*,unsigned,int);
 int (*tx)(struct rtw_dev*,const u8*,unsigned,unsigned);
 void (*wait)(struct rtw_dev*,unsigned);
 struct rtw_hal hal;struct rtw_efuse efuse;
 struct {u8 type,bulkout_num;u32 rpwm_addr;}hci;
 struct {u8 last_box_num;u16 seq;}h2c;
 struct rtw_fifo_conf fifo;
 const struct rtw_chip_info*chip;
 u32 flags[1];u8 rfe;
};
static void cu_wait(struct rtw_dev*d,unsigned us){d->wait(d,us);}
#define udelay(n) cu_wait(rtwdev,n)
#define mdelay(n) cu_wait(rtwdev,(n)*1000u)
#define usleep_range(a,b) cu_wait(rtwdev,a)
#define rtw_hci_type(d) ((d)->hci.type)
#define rtw_chip_wcpu_11n(d) 0
#define rtw_chip_wcpu_11ac(d) 1
#define rtw_sdio_is_sdio30_supported(d) 0
#define set_bit(b,a) (*(a)|=BIT(b))
#define clear_bit(b,a) (*(a)&=~BIT(b))
static u32 cu_read(struct rtw_dev*d,u32 addr,unsigned len){
 u32 v=0;if(!d->ok)return 0;
 if(addr>65535||len>4||addr+len>65536||d->control(d,addr,&v,len,1)<0)d->ok=0;
 return v;
}
static void cu_write(struct rtw_dev*d,u32 addr,u32 v,unsigned len){
 if(!d->ok)return;
 if(addr>65535||len>4||addr+len>65536||d->control(d,addr,&v,len,0)<0)d->ok=0;
}
static u8 rtw_read8(struct rtw_dev*d,u32 addr){return cu_read(d,addr,1);}
static void rtw_write8(struct rtw_dev*d,u32 addr,u8 v){cu_write(d,addr,v,1);}
static void rtw_write8_set(struct rtw_dev*d,u32 addr,u8 mask){u8 v=rtw_read8(d,addr);rtw_write8(d,addr,v|mask);}
static void rtw_write8_clr(struct rtw_dev*d,u32 addr,u8 mask){u8 v=rtw_read8(d,addr);rtw_write8(d,addr,v&~mask);}
static u8 rtw_read8_mask(struct rtw_dev*d,u32 addr,u8 mask){if(!mask){d->ok=0;return 0;}return FIELD_GET(mask,rtw_read8(d,addr));}
static void rtw_write8_mask(struct rtw_dev*d,u32 addr,u8 mask,u32 data){if(!mask){d->ok=0;return;}u8 v=rtw_read8(d,addr);rtw_write8(d,addr,(v&~mask)|FIELD_PREP(mask,data));}
static u16 rtw_read16(struct rtw_dev*d,u32 addr){return cu_read(d,addr,2);}
static void rtw_write16(struct rtw_dev*d,u32 addr,u16 v){cu_write(d,addr,v,2);}
static void rtw_write16_set(struct rtw_dev*d,u32 addr,u16 mask){u16 v=rtw_read16(d,addr);rtw_write16(d,addr,v|mask);}
static void rtw_write16_clr(struct rtw_dev*d,u32 addr,u16 mask){u16 v=rtw_read16(d,addr);rtw_write16(d,addr,v&~mask);}
static u16 rtw_read16_mask(struct rtw_dev*d,u32 addr,u16 mask){if(!mask){d->ok=0;return 0;}return FIELD_GET(mask,rtw_read16(d,addr));}
static void rtw_write16_mask(struct rtw_dev*d,u32 addr,u16 mask,u32 data){if(!mask){d->ok=0;return;}u16 v=rtw_read16(d,addr);rtw_write16(d,addr,(v&~mask)|FIELD_PREP(mask,data));}
static u32 rtw_read32(struct rtw_dev*d,u32 addr){return cu_read(d,addr,4);}
static void rtw_write32(struct rtw_dev*d,u32 addr,u32 v){cu_write(d,addr,v,4);}
static void rtw_write32_set(struct rtw_dev*d,u32 addr,u32 mask){u32 v=rtw_read32(d,addr);rtw_write32(d,addr,v|mask);}
static void rtw_write32_clr(struct rtw_dev*d,u32 addr,u32 mask){u32 v=rtw_read32(d,addr);rtw_write32(d,addr,v&~mask);}
static u32 rtw_read32_mask(struct rtw_dev*d,u32 addr,u32 mask){if(!mask){d->ok=0;return 0;}return FIELD_GET(mask,rtw_read32(d,addr));}
static void rtw_write32_mask(struct rtw_dev*d,u32 addr,u32 mask,u32 data){if(!mask){d->ok=0;return;}u32 v=rtw_read32(d,addr);rtw_write32(d,addr,(v&~mask)|FIELD_PREP(mask,data));}

static bool check_hw_ready(struct rtw_dev*d,u32 addr,u32 mask,u32 target){
 for(unsigned i=0;i<1000&&d->ok;i++){
  if(rtw_read32_mask(d,addr,mask)==target)return true;
  cu_wait(d,10);
 }
 return false;
}
static void*kmemdup(const void*p,u32 n,int flags){(void)flags;void*q=kmalloc(n);if(q)memcpy(q,p,n);return q;}
static int __rtw_mac_init_system_cfg_legacy(struct rtw_dev*d){d->ok=0;return -EINVAL;}
static int __priority_queue_cfg_legacy(struct rtw_dev*d,const struct rtw_page_table*p,u16 q){(void)p;(void)q;d->ok=0;return -EINVAL;}
static int rtw_hci_write_data_rsvd_page(struct rtw_dev*d,const u8*p,u32 n){return d->ok?d->tx(d,p,n,0x10):-EIO;}
static void rtw_hci_setup(struct rtw_dev*d){(void)d;}
static int ltecoex_read_reg(struct rtw_dev*d,u16 addr,u32*v){
 if(!check_hw_ready(d,LTECOEX_ACCESS_CTRL,LTECOEX_READY,1))return 0;
 rtw_write32(d,LTECOEX_ACCESS_CTRL,0x800f0000u|addr);*v=rtw_read32(d,LTECOEX_READ_DATA);return d->ok;
}
static int ltecoex_reg_write(struct rtw_dev*d,u16 addr,u32 v){
 if(!check_hw_ready(d,LTECOEX_ACCESS_CTRL,LTECOEX_READY,1))return 0;
 rtw_write32(d,LTECOEX_WRITE_DATA,v);rtw_write32(d,LTECOEX_ACCESS_CTRL,0xc00f0000u|addr);return d->ok;
}
static void rtw_restore_reg(struct rtw_dev*d,struct rtw_backup_info*b,u8 n){
 for(unsigned i=0;i<n;i++)cu_write(d,b[i].reg,b[i].val,b[i].len);
}
static int rtw_regd_srrc(struct rtw_dev*d){(void)d;return 0;}
static u32 rtw_read_rf(struct rtw_dev*d,unsigned path,u32 addr,u32 mask){
 if(path||addr>255||!mask){d->ok=0;return 0;}
 return rtw_read32_mask(d,0x2800+addr*4,mask);
}
static void rtw_write_rf(struct rtw_dev*d,unsigned path,u32 addr,u32 mask,u32 val){
 if(path||addr>255||!mask){d->ok=0;return;}
 if(mask!=RFREG_MASK){u32 old=rtw_read_rf(d,path,addr,RFREG_MASK);val=(old&~mask)|FIELD_PREP(mask,val);}
 rtw_write32(d,0xc90,((addr&255)<<20)|(val&RFREG_MASK));cu_wait(d,13);
}
static void rtw_set_channel_mac(struct rtw_dev*d,u8 channel,u8 bw,u8 primary){
 (void)channel;(void)primary;
 if(bw){d->ok=0;return;}
 rtw_write8(d,REG_DATA_SC,0);rtw_write32_clr(d,REG_WMAC_TRXPTCL_CTL,BIT_RFMOD);
 u32 v=rtw_read32(d,REG_AFE_CTRL1)&~BIT_MAC_CLK_SEL;
 rtw_write32(d,REG_AFE_CTRL1,v|(MAC_CLK_HW_DEF_80M<<BIT_SHIFT_MAC_CLK_SEL));
 rtw_write8(d,REG_USTIME_TSF,MAC_CLK_SPEED);rtw_write8(d,REG_USTIME_EDCA,MAC_CLK_SPEED);
}
static void rtw8821c_pwrtrack_init(struct rtw_dev*d){(void)d;}
#include "rtw8821c_data.inc"

static int cu_table(struct rtw_dev*d,const u32*words,unsigned n,unsigned kind){
 if(!words||(n&1)||kind>3)return -EINVAL;
 int matched=1,skipped=0,pending=0;u32 cond=0;
 for(unsigned i=0;i<n&&d->ok;i+=2){
  u32 addr=words[i],val=words[i+1];
  if(addr&BIT(31)){
   unsigned branch=(addr>>28)&3;
   if(branch==3){if(pending)return -EINVAL;matched=1;skipped=0;}
   else if(branch==2){if(pending)return -EINVAL;matched=!skipped;}
   else{cond=addr;pending=1;}
  }else if(addr&BIT(30)){
   if(!pending)return -EINVAL;
   unsigned cut=(cond>>24)&15,pkg=(cond>>12)&15,intf=(cond>>8)&15,rfe=cond&255;
   matched=!skipped&&(!cut||cut==(d->hal.cut_version?d->hal.cut_version:15))&&(!pkg||pkg==(d->hal.pkg_type?d->hal.pkg_type:15))&&
           (!intf||intf==2)&&rfe==d->rfe;
   if(matched)skipped=1;
   pending=0;
  }else if(matched){
   if(pending)return -EINVAL;
   if(kind==0){if(addr>65535||val>255)return -EINVAL;rtw_write8(d,addr,val);}
   else if(kind==3){
    if(addr==0xffe)cu_wait(d,50000);
    else if(addr==0xfe)cu_wait(d,100);
    else if(addr==0xfd)cu_wait(d,5000);
    else if(addr==0xfc)cu_wait(d,1000);
    else if(addr==0xfb)cu_wait(d,50);
    else if(addr==0xfa)cu_wait(d,5);
    else if(addr==0xf9)cu_wait(d,1);
    else rtw_write_rf(d,0,addr,RFREG_MASK,val);
   }else{
    if(addr==0xfe)cu_wait(d,50000);
    else if(addr==0xfd)cu_wait(d,5000);
    else if(addr==0xfc)cu_wait(d,1000);
    else if(addr==0xfb)cu_wait(d,50);
    else if(addr==0xfa)cu_wait(d,5);
    else if(addr==0xf9)cu_wait(d,1);
    else{if(addr>0x1ffc||(addr&3))return -EINVAL;rtw_write32(d,addr,val);cu_wait(d,1);}
   }
  }
 }
 return d->ok&&!pending?0:-EIO;
}
static int cu_phy_init(struct rtw_dev*d,u8 xtal){
 u8 v=rtw_read8(d,REG_SYS_FUNC_EN)|BIT_FEN_PCIEA;
 rtw_write8(d,REG_SYS_FUNC_EN,v|BIT_FEN_BB_RSTB|BIT_FEN_BB_GLB_RST);
 rtw_write8(d,REG_SYS_FUNC_EN,v&~(BIT_FEN_BB_RSTB|BIT_FEN_BB_GLB_RST));
 rtw_write8(d,REG_SYS_FUNC_EN,v|BIT_FEN_BB_RSTB|BIT_FEN_BB_GLB_RST);
 rtw_write8(d,REG_RF_CTRL,BIT_RF_EN|BIT_RF_RSTB|BIT_RF_SDM_RSTB);cu_wait(d,10);
 rtw_write8(d,REG_WLRF1+3,BIT_RF_EN|BIT_RF_RSTB|BIT_RF_SDM_RSTB);cu_wait(d,10);
 rtw_write32_clr(d,REG_RXPSEL,BIT_RX_PSEL_RST);
 if(cu_table(d,rtw8821c_mac,ARRAY_SIZE(rtw8821c_mac),0)||
    cu_table(d,rtw8821c_bb,ARRAY_SIZE(rtw8821c_bb),1)||
    cu_table(d,rtw8821c_agc,ARRAY_SIZE(rtw8821c_agc),2)||
    (d->hal.rfe_btg&&cu_table(d,rtw8821c_agc_btg_type2,ARRAY_SIZE(rtw8821c_agc_btg_type2),2))||
    cu_table(d,rtw8821c_rf_a,ARRAY_SIZE(rtw8821c_rf_a),3))return -EIO;
 rtw_write32_mask(d,REG_AFE_XTAL_CTRL,0x7e000000,xtal&63);
 rtw_write32_mask(d,REG_AFE_PLL_CTRL,0x7e,xtal&63);
 rtw_write32_mask(d,REG_CCK0_FAREPORT,BIT(18)|BIT(22),0);
 rtw_write32_set(d,REG_RXPSEL,BIT_RX_PSEL_RST);
 d->hal.ch_param[0]=rtw_read32(d,REG_TXSF2);
 d->hal.ch_param[1]=rtw_read32(d,REG_TXSF6);
 d->hal.ch_param[2]=rtw_read32(d,REG_TXFILTER);
 rtw_write32_mask(d,0xc50,0x7f,0x32);
 return d->ok?0:-EIO;
}
static const struct rtw_chip_info cu_chip={
 .id=RTW_CHIP_TYPE_8821C,.sys_func_en=0xd8,.tx_pkt_desc_sz=48,.rsvd_drv_pg_num=8,
 .txff_size=65536,.rxff_size=16384,.usb_tx_agg_desc_num=3,
 .pwr_on_seq=card_enable_flow_8821c,.pwr_off_seq=card_disable_flow_8821c,
 .rqpn_table=rqpn_table_8821c,.page_table=page_table_8821c,
};


static int cu_efuse_decode(const u8*physical,unsigned n,u8 logical[512]){
 if(!physical||!logical||n>416)return -EINVAL;
 u8 candidate[512];memset(candidate,255,sizeof(candidate));
 for(unsigned at=0;at<n;){
  u8 head=physical[at++];if(head==255)break;
  unsigned block=head>>4,mask=head&15;
  if((head&31)==15){
   if(at>=n)return -EINVAL;
   unsigned second=physical[at++];
   if(second==255)return -EINVAL;
   block=((second&240)>>1)|((head>>5)&7);mask=second&15;
  }
  for(unsigned word=0;word<4;word++)if(!(mask&BIT(word))){
   unsigned to=block*8+word*2;
   if(at+2>n||to+2>512)return -EINVAL;
   candidate[to]=physical[at++];candidate[to+1]=physical[at++];
  }
 }
 memcpy(logical,candidate,512);return 0;
}
static int cu_efuse_read(struct rtw_dev*d,u8 logical[512]){
 u8 physical[416];
 rtw_write32_mask(d,REG_LDO_EFUSE_CTRL,BIT_MASK_EFUSE_BANK_SEL,0);
 rtw_write8_clr(d,REG_LDO_EFUSE_CTRL+3,BIT(7));
 for(unsigned at=0;at<416&&d->ok;at++){
  u32 v=rtw_read32(d,REG_EFUSE_CTRL);
  v=(v&~(BITS_EF_ADDR|BIT_MASK_EF_DATA|BIT_EF_FLAG))|((at&BIT_MASK_EF_ADDR)<<BIT_SHIFT_EF_ADDR);
  rtw_write32(d,REG_EFUSE_CTRL,v);
  unsigned wait;
  for(wait=0;wait<1000&&d->ok;wait++){
   v=rtw_read32(d,REG_EFUSE_CTRL);
   if(v&BIT_EF_FLAG)break;
   cu_wait(d,10);
  }
  if(wait==1000||!d->ok)return -EIO;
  physical[at]=v&255;
 }
 return d->ok?cu_efuse_decode(physical,sizeof(physical),logical):-EIO;
}

#include "rtw8821c_hal.inc"
#pragma GCC diagnostic pop
#endif
