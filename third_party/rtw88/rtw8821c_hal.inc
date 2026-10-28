/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018-2019 Realtek Corporation
 * Adapted from Linux v6.12 rtw88 hardware routines under BSD terms.
 */
static int __priority_queue_cfg(struct rtw_dev *rtwdev,
				const struct rtw_page_table *pg_tbl,
				u16 pubq_num);
static int __rtw_download_firmware(struct rtw_dev *rtwdev,
				   struct rtw_fw_state *fw);
static int __rtw_mac_init_system_cfg(struct rtw_dev *rtwdev);
static bool check_firmware_size(const u8 *data, u32 size);
static bool
check_fw_checksum(struct rtw_dev *rtwdev, u32 addr);
static bool do_pwr_poll_cmd(struct rtw_dev *rtwdev, u32 addr, u32 mask, u32 target);
static void download_firmware_end_flow(struct rtw_dev *rtwdev);
static void download_firmware_reg_backup(struct rtw_dev *rtwdev,
					 struct rtw_backup_info *bckp);
static void download_firmware_reg_restore(struct rtw_dev *rtwdev,
					  struct rtw_backup_info *bckp,
					  u8 bckp_num);
static void download_firmware_reset_platform(struct rtw_dev *rtwdev);
static int
download_firmware_to_mem(struct rtw_dev *rtwdev, const u8 *data,
			 u32 src, u32 dst, u32 size);
static int download_firmware_validate(struct rtw_dev *rtwdev);
static int iddma_download_firmware(struct rtw_dev *rtwdev, u32 src, u32 dst,
				   u32 len, u8 first);
static int
iddma_enable(struct rtw_dev *rtwdev, u32 src, u32 dst, u32 ctrl);
static int init_h2c(struct rtw_dev *rtwdev);
static int priority_queue_cfg(struct rtw_dev *rtwdev);
static void rtw8821c_cck_tx_filter_srrc(struct rtw_dev *rtwdev, u8 channel, u8 bw);
static u32 rtw8821c_get_bb_swing(struct rtw_dev *rtwdev, u8 channel);
static int rtw8821c_mac_init(struct rtw_dev *rtwdev);
static void rtw8821c_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx);
static void rtw8821c_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx);
static void rtw8821c_set_channel_bb_swing(struct rtw_dev *rtwdev, u8 channel,
					  u8 bw, u8 primary_ch_idx);
static void rtw8821c_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw);
static void rtw8821c_set_channel_rxdfir(struct rtw_dev *rtwdev, u8 bw);
static void rtw8821c_switch_rf_set(struct rtw_dev *rtwdev, u8 rf_set);
static int rtw_drv_info_cfg(struct rtw_dev *rtwdev);
static int rtw_fw_write_data_rsvd_page(struct rtw_dev *rtwdev, u16 pg_addr,
				u8 *buf, u32 size);
static int rtw_init_trx_cfg(struct rtw_dev *rtwdev);
static int rtw_mac_init_system_cfg(struct rtw_dev *rtwdev);
static int rtw_mac_power_on(struct rtw_dev *rtwdev);
static int rtw_mac_power_switch(struct rtw_dev *rtwdev, bool pwr_on);
static int rtw_mac_pre_system_cfg(struct rtw_dev *rtwdev);
static int rtw_pwr_cmd_polling(struct rtw_dev *rtwdev,
			       const struct rtw_pwr_seq_cmd *cmd);
static int rtw_pwr_seq_parser(struct rtw_dev *rtwdev,
			      const struct rtw_pwr_seq_cmd **cmd_seq);
static int rtw_sub_pwr_seq_parser(struct rtw_dev *rtwdev, u8 intf_mask,
				  u8 cut_mask,
				  const struct rtw_pwr_seq_cmd *cmd);
static int
send_firmware_pkt(struct rtw_dev *rtwdev, u16 pg_addr, const u8 *data, u32 size);
static int send_firmware_pkt_rsvd_page(struct rtw_dev *rtwdev, u16 pg_addr,
				       const u8 *data, u32 size);
static int set_trx_fifo_info(struct rtw_dev *rtwdev);
static int
start_download_firmware(struct rtw_dev *rtwdev, const u8 *data, u32 size);
static int txdma_queue_mapping(struct rtw_dev *rtwdev);
static void wlan_cpu_enable(struct rtw_dev *rtwdev, bool enable);

static int __priority_queue_cfg(struct rtw_dev *rtwdev,
				const struct rtw_page_table *pg_tbl,
				u16 pubq_num)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;

	rtw_write16(rtwdev, REG_FIFOPAGE_INFO_1, pg_tbl->hq_num);
	rtw_write16(rtwdev, REG_FIFOPAGE_INFO_2, pg_tbl->lq_num);
	rtw_write16(rtwdev, REG_FIFOPAGE_INFO_3, pg_tbl->nq_num);
	rtw_write16(rtwdev, REG_FIFOPAGE_INFO_4, pg_tbl->exq_num);
	rtw_write16(rtwdev, REG_FIFOPAGE_INFO_5, pubq_num);
	rtw_write32_set(rtwdev, REG_RQPN_CTRL_2, BIT_LD_RQPN);

	rtw_write16(rtwdev, REG_FIFOPAGE_CTRL_2, fifo->rsvd_boundary);
	rtw_write8_set(rtwdev, REG_FWHW_TXQ_CTRL + 2, BIT_EN_WR_FREE_TAIL >> 16);

	rtw_write16(rtwdev, REG_BCNQ_BDNY_V1, fifo->rsvd_boundary);
	rtw_write16(rtwdev, REG_FIFOPAGE_CTRL_2 + 2, fifo->rsvd_boundary);
	rtw_write16(rtwdev, REG_BCNQ1_BDNY_V1, fifo->rsvd_boundary);
	rtw_write32(rtwdev, REG_RXFF_BNDY, chip->rxff_size - C2H_PKT_BUF - 1);

	if (rtwdev->hci.type == RTW_HCI_TYPE_USB) {
		rtw_write8_mask(rtwdev, REG_AUTO_LLT_V1, BIT_MASK_BLK_DESC_NUM,
				chip->usb_tx_agg_desc_num);

		rtw_write8(rtwdev, REG_AUTO_LLT_V1 + 3, chip->usb_tx_agg_desc_num);
		rtw_write8_set(rtwdev, REG_TXDMA_OFFSET_CHK + 1, BIT(1));
	}

	rtw_write8_set(rtwdev, REG_AUTO_LLT_V1, BIT_AUTO_INIT_LLT_V1);

	if (!check_hw_ready(rtwdev, REG_AUTO_LLT_V1, BIT_AUTO_INIT_LLT_V1, 0))
		return -EBUSY;

	rtw_write8(rtwdev, REG_CR + 3, 0);

	return 0;
}

static int __rtw_download_firmware(struct rtw_dev *rtwdev,
				   struct rtw_fw_state *fw)
{
	struct rtw_backup_info bckp[DLFW_RESTORE_REG_NUM];
	const u8 *data = fw->firmware->data;
	u32 size = fw->firmware->size;
	u32 ltecoex_bckp;
	int ret;

	if (!check_firmware_size(data, size))
		return -EINVAL;

	if (!ltecoex_read_reg(rtwdev, 0x38, &ltecoex_bckp))
		return -EBUSY;

	wlan_cpu_enable(rtwdev, false);

	download_firmware_reg_backup(rtwdev, bckp);
	download_firmware_reset_platform(rtwdev);

	ret = start_download_firmware(rtwdev, data, size);
	if (ret)
		goto dlfw_fail;

	download_firmware_reg_restore(rtwdev, bckp, DLFW_RESTORE_REG_NUM);

	download_firmware_end_flow(rtwdev);

	wlan_cpu_enable(rtwdev, true);

	if (!ltecoex_reg_write(rtwdev, 0x38, ltecoex_bckp)) {
		ret = -EBUSY;
		goto dlfw_fail;
	}

	ret = download_firmware_validate(rtwdev);
	if (ret)
		goto dlfw_fail;

	/* reset desc and index */
	rtw_hci_setup(rtwdev);

	rtwdev->h2c.last_box_num = 0;
	rtwdev->h2c.seq = 0;

	set_bit(RTW_FLAG_FW_RUNNING, rtwdev->flags);

	return 0;

dlfw_fail:
	/* Disable FWDL_EN */
	rtw_write8_clr(rtwdev, REG_MCUFW_CTRL, BIT_MCUFWDL_EN);
	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN + 1, BIT_FEN_CPUEN);

	return ret;
}

static int __rtw_mac_init_system_cfg(struct rtw_dev *rtwdev)
{
	u8 sys_func_en = rtwdev->chip->sys_func_en;
	u8 value8;
	u32 value, tmp;

	value = rtw_read32(rtwdev, REG_CPU_DMEM_CON);
	value |= BIT_WL_PLATFORM_RST | BIT_DDMA_EN;
	rtw_write32(rtwdev, REG_CPU_DMEM_CON, value);

	rtw_write8_set(rtwdev, REG_SYS_FUNC_EN + 1, sys_func_en);
	value8 = (rtw_read8(rtwdev, REG_CR_EXT + 3) & 0xF0) | 0x0C;
	rtw_write8(rtwdev, REG_CR_EXT + 3, value8);

	/* disable boot-from-flash for driver's DL FW */
	tmp = rtw_read32(rtwdev, REG_MCUFW_CTRL);
	if (tmp & BIT_BOOT_FSPI_EN) {
		rtw_write32(rtwdev, REG_MCUFW_CTRL, tmp & (~BIT_BOOT_FSPI_EN));
		value = rtw_read32(rtwdev, REG_GPIO_MUXCFG) & (~BIT_FSPI_EN);
		rtw_write32(rtwdev, REG_GPIO_MUXCFG, value);
	}

	return 0;
}

static bool check_firmware_size(const u8*data,u32 size){
 if(!data||size<64||size>512*1024)return false;
 const struct rtw_fw_hdr*h=(const void*)data;
 if(h->signature!=0x8821)return false;
 u32 d=h->dmem_size,i=h->imem_size,e=(h->mem_usage&BIT(4))?h->emem_size:0;
 if(!d||!i||d>0x20000||i>0x40000||e>0x20000)return false;
 u64 expected=64u+(u64)d+8+(u64)i+8+(e?(u64)e+8:0);
 if(expected!=size)return false;
 if((h->dmem_addr&0x7fffffffu)!=0x200000u||
    (h->imem_addr&0x7fffffffu)!=0x30000u||
    (e&&(h->emem_addr&0x7fffffffu)!=0x100000u))return false;
 return true;
}

static bool
check_fw_checksum(struct rtw_dev *rtwdev, u32 addr)
{
	u8 fw_ctrl;

	fw_ctrl = rtw_read8(rtwdev, REG_MCUFW_CTRL);

	if (rtw_read32(rtwdev, REG_DDMA_CH0CTRL) & BIT_DDMACH0_CHKSUM_STS) {
		if (addr < OCPBASE_DMEM_88XX) {
			fw_ctrl |= BIT_IMEM_DW_OK;
			fw_ctrl &= ~BIT_IMEM_CHKSUM_OK;
			rtw_write8(rtwdev, REG_MCUFW_CTRL, fw_ctrl);
		} else {
			fw_ctrl |= BIT_DMEM_DW_OK;
			fw_ctrl &= ~BIT_DMEM_CHKSUM_OK;
			rtw_write8(rtwdev, REG_MCUFW_CTRL, fw_ctrl);
		}

		rtw_err(rtwdev, "invalid fw checksum\n");

		return false;
	}

	if (addr < OCPBASE_DMEM_88XX) {
		fw_ctrl |= (BIT_IMEM_DW_OK | BIT_IMEM_CHKSUM_OK);
		rtw_write8(rtwdev, REG_MCUFW_CTRL, fw_ctrl);
	} else {
		fw_ctrl |= (BIT_DMEM_DW_OK | BIT_DMEM_CHKSUM_OK);
		rtw_write8(rtwdev, REG_MCUFW_CTRL, fw_ctrl);
	}

	return true;
}

static bool do_pwr_poll_cmd(struct rtw_dev*rtwdev,u32 addr,u32 mask,u32 target){
 for(unsigned i=0;i<10000&&rtwdev->ok;i++){
  if((rtw_read8(rtwdev,addr)&mask)==(target&mask))return true;
  cu_wait(rtwdev,10);
 }
 return false;
}

static void download_firmware_end_flow(struct rtw_dev *rtwdev)
{
	u16 fw_ctrl;

	rtw_write32(rtwdev, REG_TXDMA_STATUS, BTI_PAGE_OVF);

	/* Check IMEM & DMEM checksum is OK or not */
	fw_ctrl = rtw_read16(rtwdev, REG_MCUFW_CTRL);
	if ((fw_ctrl & BIT_CHECK_SUM_OK) != BIT_CHECK_SUM_OK)
		return;

	fw_ctrl = (fw_ctrl | BIT_FW_DW_RDY) & ~BIT_MCUFWDL_EN;
	rtw_write16(rtwdev, REG_MCUFW_CTRL, fw_ctrl);
}

static void download_firmware_reg_backup(struct rtw_dev *rtwdev,
					 struct rtw_backup_info *bckp)
{
	u8 tmp;
	u8 bckp_idx = 0;

	/* set HIQ to hi priority */
	bckp[bckp_idx].len = 1;
	bckp[bckp_idx].reg = REG_TXDMA_PQ_MAP + 1;
	bckp[bckp_idx].val = rtw_read8(rtwdev, REG_TXDMA_PQ_MAP + 1);
	bckp_idx++;
	tmp = RTW_DMA_MAPPING_HIGH << 6;
	rtw_write8(rtwdev, REG_TXDMA_PQ_MAP + 1, tmp);

	/* DLFW only use HIQ, map HIQ to hi priority */
	bckp[bckp_idx].len = 1;
	bckp[bckp_idx].reg = REG_CR;
	bckp[bckp_idx].val = rtw_read8(rtwdev, REG_CR);
	bckp_idx++;
	bckp[bckp_idx].len = 4;
	bckp[bckp_idx].reg = REG_H2CQ_CSR;
	bckp[bckp_idx].val = BIT_H2CQ_FULL;
	bckp_idx++;
	tmp = BIT_HCI_TXDMA_EN | BIT_TXDMA_EN;
	rtw_write8(rtwdev, REG_CR, tmp);
	rtw_write32(rtwdev, REG_H2CQ_CSR, BIT_H2CQ_FULL);

	/* Config hi priority queue and public priority queue page number */
	bckp[bckp_idx].len = 2;
	bckp[bckp_idx].reg = REG_FIFOPAGE_INFO_1;
	bckp[bckp_idx].val = rtw_read16(rtwdev, REG_FIFOPAGE_INFO_1);
	bckp_idx++;
	bckp[bckp_idx].len = 4;
	bckp[bckp_idx].reg = REG_RQPN_CTRL_2;
	bckp[bckp_idx].val = rtw_read32(rtwdev, REG_RQPN_CTRL_2) | BIT_LD_RQPN;
	bckp_idx++;
	rtw_write16(rtwdev, REG_FIFOPAGE_INFO_1, 0x200);
	rtw_write32(rtwdev, REG_RQPN_CTRL_2, bckp[bckp_idx - 1].val);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_SDIO)
		rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);

	/* Disable beacon related functions */
	tmp = rtw_read8(rtwdev, REG_BCN_CTRL);
	bckp[bckp_idx].len = 1;
	bckp[bckp_idx].reg = REG_BCN_CTRL;
	bckp[bckp_idx].val = tmp;
	bckp_idx++;
	tmp = (u8)((tmp & (~BIT_EN_BCN_FUNCTION)) | BIT_DIS_TSF_UDT);
	rtw_write8(rtwdev, REG_BCN_CTRL, tmp);

	WARN(bckp_idx != DLFW_RESTORE_REG_NUM, "wrong backup number\n");
}

static void download_firmware_reg_restore(struct rtw_dev *rtwdev,
					  struct rtw_backup_info *bckp,
					  u8 bckp_num)
{
	rtw_restore_reg(rtwdev, bckp, bckp_num);
}

static void download_firmware_reset_platform(struct rtw_dev *rtwdev)
{
	rtw_write8_clr(rtwdev, REG_CPU_DMEM_CON + 2, BIT_WL_PLATFORM_RST >> 16);
	rtw_write8_clr(rtwdev, REG_SYS_CLK_CTRL + 1, BIT_CPU_CLK_EN >> 8);
	rtw_write8_set(rtwdev, REG_CPU_DMEM_CON + 2, BIT_WL_PLATFORM_RST >> 16);
	rtw_write8_set(rtwdev, REG_SYS_CLK_CTRL + 1, BIT_CPU_CLK_EN >> 8);
}

static int
download_firmware_to_mem(struct rtw_dev *rtwdev, const u8 *data,
			 u32 src, u32 dst, u32 size)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	u32 desc_size = chip->tx_pkt_desc_sz;
	u8 first_part;
	u32 mem_offset;
	u32 residue_size;
	u32 pkt_size;
	u32 max_size = 0x1000;
	u32 val;
	int ret;

	mem_offset = 0;
	first_part = 1;
	residue_size = size;

	val = rtw_read32(rtwdev, REG_DDMA_CH0CTRL);
	val |= BIT_DDMACH0_RESET_CHKSUM_STS;
	rtw_write32(rtwdev, REG_DDMA_CH0CTRL, val);

	while (residue_size) {
		if (residue_size >= max_size)
			pkt_size = max_size;
		else
			pkt_size = residue_size;

		ret = send_firmware_pkt(rtwdev, (u16)(src >> 7),
					data + mem_offset, pkt_size);
		if (ret)
			return ret;

		ret = iddma_download_firmware(rtwdev, OCPBASE_TXBUF_88XX +
					      src + desc_size,
					      dst + mem_offset, pkt_size,
					      first_part);
		if (ret)
			return ret;

		first_part = 0;
		mem_offset += pkt_size;
		residue_size -= pkt_size;
	}

	if (!check_fw_checksum(rtwdev, dst))
		return -EINVAL;

	return 0;
}

static int download_firmware_validate(struct rtw_dev *rtwdev)
{
	u32 fw_key;

	if (!check_hw_ready(rtwdev, REG_MCUFW_CTRL, FW_READY_MASK, FW_READY)) {
		fw_key = rtw_read32(rtwdev, REG_FW_DBG7) & FW_KEY_MASK;
		if (fw_key == ILLEGAL_KEY_GROUP)
			rtw_err(rtwdev, "invalid fw key\n");
		return -EINVAL;
	}

	return 0;
}

static int iddma_download_firmware(struct rtw_dev *rtwdev, u32 src, u32 dst,
				   u32 len, u8 first)
{
	u32 ch0_ctrl = BIT_DDMACH0_CHKSUM_EN | BIT_DDMACH0_OWN;

	if (!check_hw_ready(rtwdev, REG_DDMA_CH0CTRL, BIT_DDMACH0_OWN, 0))
		return -EBUSY;

	ch0_ctrl |= len & BIT_MASK_DDMACH0_DLEN;
	if (!first)
		ch0_ctrl |= BIT_DDMACH0_CHKSUM_CONT;

	if (iddma_enable(rtwdev, src, dst, ch0_ctrl))
		return -EBUSY;

	return 0;
}

static int
iddma_enable(struct rtw_dev *rtwdev, u32 src, u32 dst, u32 ctrl)
{
	rtw_write32(rtwdev, REG_DDMA_CH0SA, src);
	rtw_write32(rtwdev, REG_DDMA_CH0DA, dst);
	rtw_write32(rtwdev, REG_DDMA_CH0CTRL, ctrl);

	if (!check_hw_ready(rtwdev, REG_DDMA_CH0CTRL, BIT_DDMACH0_OWN, 0))
		return -EBUSY;

	return 0;
}

static int init_h2c(struct rtw_dev *rtwdev)
{
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;
	u8 value8;
	u32 value32;
	u32 h2cq_addr;
	u32 h2cq_size;
	u32 h2cq_free;
	u32 wp, rp;

	if (rtw_chip_wcpu_11n(rtwdev))
		return 0;

	h2cq_addr = fifo->rsvd_h2cq_addr << TX_PAGE_SIZE_SHIFT;
	h2cq_size = RSVD_PG_H2CQ_NUM << TX_PAGE_SIZE_SHIFT;

	value32 = rtw_read32(rtwdev, REG_H2C_HEAD);
	value32 = (value32 & 0xFFFC0000) | h2cq_addr;
	rtw_write32(rtwdev, REG_H2C_HEAD, value32);

	value32 = rtw_read32(rtwdev, REG_H2C_READ_ADDR);
	value32 = (value32 & 0xFFFC0000) | h2cq_addr;
	rtw_write32(rtwdev, REG_H2C_READ_ADDR, value32);

	value32 = rtw_read32(rtwdev, REG_H2C_TAIL);
	value32 &= 0xFFFC0000;
	value32 |= (h2cq_addr + h2cq_size);
	rtw_write32(rtwdev, REG_H2C_TAIL, value32);

	value8 = rtw_read8(rtwdev, REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFC) | 0x01);
	rtw_write8(rtwdev, REG_H2C_INFO, value8);

	value8 = rtw_read8(rtwdev, REG_H2C_INFO);
	value8 = (u8)((value8 & 0xFB) | 0x04);
	rtw_write8(rtwdev, REG_H2C_INFO, value8);

	value8 = rtw_read8(rtwdev, REG_TXDMA_OFFSET_CHK + 1);
	value8 = (u8)((value8 & 0x7f) | 0x80);
	rtw_write8(rtwdev, REG_TXDMA_OFFSET_CHK + 1, value8);

	wp = rtw_read32(rtwdev, REG_H2C_PKT_WRITEADDR) & 0x3FFFF;
	rp = rtw_read32(rtwdev, REG_H2C_PKT_READADDR) & 0x3FFFF;
	h2cq_free = wp >= rp ? h2cq_size - (wp - rp) : rp - wp;

	if (h2cq_size != h2cq_free) {
		rtw_err(rtwdev, "H2C queue mismatch\n");
		return -EINVAL;
	}

	return 0;
}

static int priority_queue_cfg(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;
	const struct rtw_page_table *pg_tbl = NULL;
	u16 pubq_num;
	int ret;

	ret = set_trx_fifo_info(rtwdev);
	if (ret)
		return ret;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		pg_tbl = &chip->page_table[1];
		break;
	case RTW_HCI_TYPE_USB:
		if (rtwdev->hci.bulkout_num == 2)
			pg_tbl = &chip->page_table[2];
		else if (rtwdev->hci.bulkout_num == 3)
			pg_tbl = &chip->page_table[3];
		else if (rtwdev->hci.bulkout_num == 4)
			pg_tbl = &chip->page_table[4];
		else
			return -EINVAL;
		break;
	case RTW_HCI_TYPE_SDIO:
		pg_tbl = &chip->page_table[0];
		break;
	default:
		return -EINVAL;
	}

	pubq_num = fifo->acq_pg_num - pg_tbl->hq_num - pg_tbl->lq_num -
		   pg_tbl->nq_num - pg_tbl->exq_num - pg_tbl->gapq_num;
	if (rtw_chip_wcpu_11n(rtwdev))
		return __priority_queue_cfg_legacy(rtwdev, pg_tbl, pubq_num);
	else
		return __priority_queue_cfg(rtwdev, pg_tbl, pubq_num);
}

static void rtw8821c_cck_tx_filter_srrc(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;

	if (channel == 14) {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xe82c);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x0000b81c);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x00003667);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	} else if (channel == 13 ||
		   (channel == 11 && bw == RTW_CHANNEL_WIDTH_40)) {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xf8fe);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x64b80c1c);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x8810);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x01235667);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00027);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00027);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00029);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00026);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	} else {
		rtw_write32_mask(rtwdev, REG_CCA_FLTR, MASKHWORD, 0xe82c);
		rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD,
				 hal->ch_param[0]);
		rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD,
				 hal->ch_param[1] & MASKLWORD);
		rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD,
				 hal->ch_param[2]);

		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00002);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0001c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000e);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWA, RFREG_MASK, 0x0000c);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWD0, RFREG_MASK, 0x00000);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTWE2, RFREG_MASK, 0x00000);
	}
}

static u32 rtw8821c_get_bb_swing(struct rtw_dev *rtwdev, u8 channel)
{
	struct rtw_efuse efuse = rtwdev->efuse;
	u8 tx_bb_swing;
	u32 swing2setting[4] = {0x200, 0x16a, 0x101, 0x0b6};

	tx_bb_swing = channel <= 14 ? efuse.tx_bb_swing_setting_2g :
				      efuse.tx_bb_swing_setting_5g;
	if (tx_bb_swing > 9)
		tx_bb_swing = 0;

	return swing2setting[(tx_bb_swing / 3)];
}

static int rtw8821c_mac_init(struct rtw_dev *rtwdev)
{
	u32 value32;
	u16 pre_txcnt;

	/* protocol configuration */
	rtw_write8(rtwdev, REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
	rtw_write8_set(rtwdev, REG_TX_HANG_CTRL, BIT_EN_EOF_V1);
	pre_txcnt = WLAN_PRE_TXCNT_TIME_TH | BIT_EN_PRECNT;
	rtw_write8(rtwdev, REG_PRECNT_CTRL, (u8)(pre_txcnt & 0xFF));
	rtw_write8(rtwdev, REG_PRECNT_CTRL + 1, (u8)(pre_txcnt >> 8));
	value32 = WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) |
		  (WLAN_MAX_AGG_PKT_LIMIT << 16) |
		  (WLAN_RTS_MAX_AGG_PKT_LIMIT << 24);
	rtw_write32(rtwdev, REG_PROT_MODE_CTRL, value32);
	rtw_write16(rtwdev, REG_BAR_MODE_CTRL + 2,
		    WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING, FAST_EDCA_VO_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_VOVI_SETTING + 2, FAST_EDCA_VI_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING, FAST_EDCA_BE_TH);
	rtw_write8(rtwdev, REG_FAST_EDCA_BEBK_SETTING + 2, FAST_EDCA_BK_TH);
	rtw_write8_set(rtwdev, REG_INIRTS_RATE_SEL, BIT(5));

	/* EDCA configuration */
	rtw_write8_clr(rtwdev, REG_TIMER0_SRC_SEL, BIT_TSFT_SEL_TIMER0);
	rtw_write16(rtwdev, REG_TXPAUSE, 0);
	rtw_write8(rtwdev, REG_SLOT, WLAN_SLOT_TIME);
	rtw_write8(rtwdev, REG_PIFS, WLAN_PIFS_TIME);
	rtw_write32(rtwdev, REG_SIFS, WLAN_SIFS_CFG);
	rtw_write16(rtwdev, REG_EDCA_VO_PARAM + 2, WLAN_VO_TXOP_LIMIT);
	rtw_write16(rtwdev, REG_EDCA_VI_PARAM + 2, WLAN_VI_TXOP_LIMIT);
	rtw_write32(rtwdev, REG_RD_NAV_NXT, WLAN_NAV_CFG);
	rtw_write16(rtwdev, REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);

	/* Set beacon cotnrol - enable TSF and other related functions */
	rtw_write8_set(rtwdev, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* Set send beacon related registers */
	rtw_write32(rtwdev, REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
	rtw_write8(rtwdev, REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
	rtw_write8(rtwdev, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	rtw_write8_clr(rtwdev, REG_TX_PTCL_CTRL + 1, BIT_SIFS_BK_EN >> 8);

	/* WMAC configuration */
	rtw_write32(rtwdev, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	rtw_write16(rtwdev, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	rtw_write32(rtwdev, REG_RCR, WLAN_RCR_CFG);
	rtw_write8(rtwdev, REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);
	rtw_write8(rtwdev, REG_TCR + 2, WLAN_TX_FUNC_CFG2);
	rtw_write8(rtwdev, REG_TCR + 1, WLAN_TX_FUNC_CFG1);
	rtw_write8(rtwdev, REG_ACKTO_CCK, 0x40);
	rtw_write8_set(rtwdev, REG_WMAC_TRXPTCL_CTL_H, BIT(1));
	rtw_write8_set(rtwdev, REG_SND_PTCL_CTRL,
		       BIT_DIS_CHK_VHTSIGB_CRC);
	rtw_write32(rtwdev, REG_WMAC_OPTION_FUNCTION + 8, WLAN_MAC_OPT_FUNC2);
	rtw_write8(rtwdev, REG_WMAC_OPTION_FUNCTION + 4, WLAN_MAC_OPT_NORM_FUNC1);

	return 0;
}

static void rtw8821c_set_channel(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				 u8 primary_chan_idx)
{
	rtw8821c_set_channel_bb(rtwdev, channel, bw, primary_chan_idx);
	rtw8821c_set_channel_bb_swing(rtwdev, channel, bw, primary_chan_idx);
	rtw_set_channel_mac(rtwdev, channel, bw, primary_chan_idx);
	rtw8821c_set_channel_rf(rtwdev, channel, bw);
	rtw8821c_set_channel_rxdfir(rtwdev, bw);
}

static void rtw8821c_set_channel_bb(struct rtw_dev *rtwdev, u8 channel, u8 bw,
				    u8 primary_ch_idx)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 val32;

	if (channel <= 14) {
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x0);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x0);
		rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x96a);

		if (rtw_regd_srrc(rtwdev)) {
			rtw8821c_cck_tx_filter_srrc(rtwdev, channel, bw);
			goto set_bw;
		}

		/* CCK TX filter parameters for default case */
		if (channel == 14) {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD, 0x0000b81c);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD, 0x0000);
			rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD, 0x00003667);
		} else {
			rtw_write32_mask(rtwdev, REG_TXSF2, MASKDWORD,
					 hal->ch_param[0]);
			rtw_write32_mask(rtwdev, REG_TXSF6, MASKLWORD,
					 hal->ch_param[1] & MASKLWORD);
			rtw_write32_mask(rtwdev, REG_TXFILTER, MASKDWORD,
					 hal->ch_param[2]);
		}
	} else if (channel > 35) {
		rtw_write32_mask(rtwdev, REG_ENTXCCK, BIT(18), 0x1);
		rtw_write32_mask(rtwdev, REG_CCK_CHECK, BIT(7), 0x1);
		rtw_write32_mask(rtwdev, REG_RXPSEL, BIT(28), 0x0);
		rtw_write32_mask(rtwdev, REG_RXCCAMSK, 0x0000FC00, 15);

		if (channel >= 36 && channel <= 64)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x1);
		else if (channel >= 100 && channel <= 144)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x2);
		else if (channel >= 149)
			rtw_write32_mask(rtwdev, REG_TXSCALE_A, 0xf00, 0x3);

		if (channel >= 36 && channel <= 48)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x494);
		else if (channel >= 52 && channel <= 64)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x453);
		else if (channel >= 100 && channel <= 116)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x452);
		else if (channel >= 118 && channel <= 177)
			rtw_write32_mask(rtwdev, REG_CLKTRK, 0x1ffe0000, 0x412);
	}

set_bw:
	switch (bw) {
	case RTW_CHANNEL_WIDTH_20:
	default:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xffcffc00;
		val32 |= 0x10010000;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_40:
		if (primary_ch_idx == 1)
			rtw_write32_set(rtwdev, REG_RXSB, BIT(4));
		else
			rtw_write32_clr(rtwdev, REG_RXSB, BIT(4));

		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xff3ff300;
		val32 |= 0x20020000 | ((primary_ch_idx & 0xf) << 2) |
			 RTW_CHANNEL_WIDTH_40;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_80:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xfcffcf00;
		val32 |= 0x40040000 | ((primary_ch_idx & 0xf) << 2) |
			 RTW_CHANNEL_WIDTH_80;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_5:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xefcefc00;
		val32 |= 0x200240;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	case RTW_CHANNEL_WIDTH_10:
		val32 = rtw_read32_mask(rtwdev, REG_ADCCLK, MASKDWORD);
		val32 &= 0xefcefc00;
		val32 |= 0x300380;
		rtw_write32_mask(rtwdev, REG_ADCCLK, MASKDWORD, val32);

		rtw_write32_mask(rtwdev, REG_ADC160, BIT(30), 0x0);
		rtw_write32_mask(rtwdev, REG_ADC40, BIT(31), 0x1);
		break;
	}
}

static void rtw8821c_set_channel_bb_swing(struct rtw_dev *rtwdev, u8 channel,
					  u8 bw, u8 primary_ch_idx)
{
	rtw_write32_mask(rtwdev, REG_TXSCALE_A, GENMASK(31, 21),
			 rtw8821c_get_bb_swing(rtwdev, channel));
	rtw8821c_pwrtrack_init(rtwdev);
}

static void rtw8821c_set_channel_rf(struct rtw_dev *rtwdev, u8 channel, u8 bw)
{
	struct rtw_hal *hal = &rtwdev->hal;
	u32 rf_reg18;

	rf_reg18 = rtw_read_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK);

	rf_reg18 &= ~(RF18_BAND_MASK | RF18_CHANNEL_MASK | RF18_RFSI_MASK |
		      RF18_BW_MASK);

	rf_reg18 |= (channel <= 14 ? RF18_BAND_2G : RF18_BAND_5G);
	rf_reg18 |= (channel & RF18_CHANNEL_MASK);

	if (channel >= 100 && channel <= 140)
		rf_reg18 |= RF18_RFSI_GE;
	else if (channel > 140)
		rf_reg18 |= RF18_RFSI_GT;

	switch (bw) {
	case RTW_CHANNEL_WIDTH_5:
	case RTW_CHANNEL_WIDTH_10:
	case RTW_CHANNEL_WIDTH_20:
	default:
		rf_reg18 |= RF18_BW_20M;
		break;
	case RTW_CHANNEL_WIDTH_40:
		rf_reg18 |= RF18_BW_40M;
		break;
	case RTW_CHANNEL_WIDTH_80:
		rf_reg18 |= RF18_BW_80M;
		break;
	}

	if (channel <= 14) {
		if (hal->rfe_btg)
			rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_BTG);
		else
			rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_WLG);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(6), 0x1);
		rtw_write_rf(rtwdev, RF_PATH_A, 0x64, 0xf, 0xf);
	} else {
		rtw8821c_switch_rf_set(rtwdev, SWITCH_TO_WLA);
		rtw_write_rf(rtwdev, RF_PATH_A, RF_LUTDBG, BIT(6), 0x0);
	}

	rtw_write_rf(rtwdev, RF_PATH_A, 0x18, RFREG_MASK, rf_reg18);

	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 0);
	rtw_write_rf(rtwdev, RF_PATH_A, RF_XTALX2, BIT(19), 1);
}

static void rtw8821c_set_channel_rxdfir(struct rtw_dev *rtwdev, u8 bw)
{
	if (bw == RTW_CHANNEL_WIDTH_40) {
		/* RX DFIR for BW40 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x0);
	} else if (bw == RTW_CHANNEL_WIDTH_80) {
		/* RX DFIR for BW80 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x1);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x0);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x1);
	} else {
		/* RX DFIR for BW20, BW10 and BW5 */
		rtw_write32_mask(rtwdev, REG_ACBB0, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_ACBBRXFIR, BIT(29) | BIT(28), 0x2);
		rtw_write32_mask(rtwdev, REG_TXDFIR, BIT(31), 0x1);
		rtw_write32_mask(rtwdev, REG_CHFIR, BIT(31), 0x0);
	}
}

static void rtw8821c_switch_rf_set(struct rtw_dev *rtwdev, u8 rf_set)
{
	u32 reg;

	rtw_write32_set(rtwdev, REG_DMEM_CTRL, BIT_WL_RST);
	rtw_write32_set(rtwdev, REG_SYS_CTRL, BIT_FEN_EN);

	reg = rtw_read32(rtwdev, REG_RFECTL);
	switch (rf_set) {
	case SWITCH_TO_BTG:
		reg |= B_BTG_SWITCH;
		reg &= ~(B_CTRL_SWITCH | B_WL_SWITCH | B_WLG_SWITCH |
			 B_WLA_SWITCH);
		rtw_write32_mask(rtwdev, REG_ENRXCCA, MASKBYTE2, BTG_CCA);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, MASKLWORD, BTG_LNA);
		break;
	case SWITCH_TO_WLG:
		reg |= B_WL_SWITCH | B_WLG_SWITCH;
		reg &= ~(B_BTG_SWITCH | B_CTRL_SWITCH | B_WLA_SWITCH);
		rtw_write32_mask(rtwdev, REG_ENRXCCA, MASKBYTE2, WLG_CCA);
		rtw_write32_mask(rtwdev, REG_ENTXCCK, MASKLWORD, WLG_LNA);
		break;
	case SWITCH_TO_WLA:
		reg |= B_WL_SWITCH | B_WLA_SWITCH;
		reg &= ~(B_BTG_SWITCH | B_CTRL_SWITCH | B_WLG_SWITCH);
		break;
	case SWITCH_TO_BT:
	default:
		break;
	}

	rtw_write32(rtwdev, REG_RFECTL, reg);
}

static int rtw_drv_info_cfg(struct rtw_dev *rtwdev)
{
	u8 value8;

	rtw_write8(rtwdev, REG_RX_DRVINFO_SZ, PHY_STATUS_SIZE);
	if (rtw_chip_wcpu_11ac(rtwdev)) {
		value8 = rtw_read8(rtwdev, REG_TRXFF_BNDY + 1);
		value8 &= 0xF0;
		/* For rxdesc len = 0 issue */
		value8 |= 0xF;
		rtw_write8(rtwdev, REG_TRXFF_BNDY + 1, value8);
	}
	rtw_write32_set(rtwdev, REG_RCR, BIT_APP_PHYSTS);
	rtw_write32_clr(rtwdev, REG_WMAC_OPTION_FUNCTION + 4, BIT(8) | BIT(9));

	return 0;
}

static int rtw_fw_write_data_rsvd_page(struct rtw_dev *rtwdev, u16 pg_addr,
				u8 *buf, u32 size)
{
	u8 bckp[2]={0};
	u8 val;
	u16 rsvd_pg_head;
	u32 bcn_valid_addr;
	u32 bcn_valid_mask;
	int ret;

	lockdep_assert_held(&rtwdev->mutex);

	if (!size)
		return -EINVAL;

	if (rtw_chip_wcpu_11n(rtwdev)) {
		rtw_write32_set(rtwdev, REG_DWBCN0_CTRL, BIT_BCN_VALID);
	} else {
		pg_addr &= BIT_MASK_BCN_HEAD_1_V1;
		pg_addr |= BIT_BCN_VALID_V1;
		rtw_write16(rtwdev, REG_FIFOPAGE_CTRL_2, pg_addr);
	}

	val = rtw_read8(rtwdev, REG_CR + 1);
	bckp[0] = val;
	val |= BIT_ENSWBCN >> 8;
	rtw_write8(rtwdev, REG_CR + 1, val);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE) {
		val = rtw_read8(rtwdev, REG_FWHW_TXQ_CTRL + 2);
		bckp[1] = val;
		val &= ~(BIT_EN_BCNQ_DL >> 16);
		rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 2, val);
	}

	ret = rtw_hci_write_data_rsvd_page(rtwdev, buf, size);
	if (ret) {
		rtw_err(rtwdev, "failed to write data to rsvd page\n");
		goto restore;
	}

	if (rtw_chip_wcpu_11n(rtwdev)) {
		bcn_valid_addr = REG_DWBCN0_CTRL;
		bcn_valid_mask = BIT_BCN_VALID;
	} else {
		bcn_valid_addr = REG_FIFOPAGE_CTRL_2;
		bcn_valid_mask = BIT_BCN_VALID_V1;
	}

	if (!check_hw_ready(rtwdev, bcn_valid_addr, bcn_valid_mask, 1)) {
		rtw_err(rtwdev, "error beacon valid\n");
		ret = -EBUSY;
	}

restore:
	rsvd_pg_head = rtwdev->fifo.rsvd_boundary;
	rtw_write16(rtwdev, REG_FIFOPAGE_CTRL_2,
		    rsvd_pg_head | BIT_BCN_VALID_V1);
	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE)
		rtw_write8(rtwdev, REG_FWHW_TXQ_CTRL + 2, bckp[1]);
	rtw_write8(rtwdev, REG_CR + 1, bckp[0]);

	return ret;
}

static int rtw_init_trx_cfg(struct rtw_dev *rtwdev)
{
	int ret;

	ret = txdma_queue_mapping(rtwdev);
	if (ret)
		return ret;

	ret = priority_queue_cfg(rtwdev);
	if (ret)
		return ret;

	ret = init_h2c(rtwdev);
	if (ret)
		return ret;

	return 0;
}

static int rtw_mac_init_system_cfg(struct rtw_dev *rtwdev)
{
	if (rtw_chip_wcpu_11n(rtwdev))
		return __rtw_mac_init_system_cfg_legacy(rtwdev);

	return __rtw_mac_init_system_cfg(rtwdev);
}

static int rtw_mac_power_on(struct rtw_dev *rtwdev)
{
	int ret = 0;

	ret = rtw_mac_pre_system_cfg(rtwdev);
	if (ret)
		goto err;

	ret = rtw_mac_power_switch(rtwdev, true);
	if (ret == -EALREADY) {
		rtw_mac_power_switch(rtwdev, false);

		ret = rtw_mac_pre_system_cfg(rtwdev);
		if (ret)
			goto err;

		ret = rtw_mac_power_switch(rtwdev, true);
		if (ret)
			goto err;
	} else if (ret) {
		goto err;
	}

	ret = rtw_mac_init_system_cfg(rtwdev);
	if (ret)
		goto err;

	return 0;

err:
	rtw_err(rtwdev, "mac power on failed");
	return ret;
}

static int rtw_mac_power_switch(struct rtw_dev *rtwdev, bool pwr_on)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_pwr_seq_cmd **pwr_seq;
	u32 imr = 0;
	u8 rpwm;
	bool cur_pwr;
	int ret;

	if (rtw_chip_wcpu_11ac(rtwdev)) {
		rpwm = rtw_read8(rtwdev, rtwdev->hci.rpwm_addr);

		/* Check FW still exist or not */
		if (rtw_read16(rtwdev, REG_MCUFW_CTRL) == 0xC078) {
			rpwm = (rpwm ^ BIT_RPWM_TOGGLE) & BIT_RPWM_TOGGLE;
			rtw_write8(rtwdev, rtwdev->hci.rpwm_addr, rpwm);
		}
	}

	if (rtw_read8(rtwdev, REG_CR) == 0xea)
		cur_pwr = false;
	else if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB &&
		 (rtw_read8(rtwdev, REG_SYS_STATUS1 + 1) & BIT(0)))
		cur_pwr = false;
	else
		cur_pwr = true;

	if (pwr_on == cur_pwr)
		return -EALREADY;

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_SDIO) {
		imr = rtw_read32(rtwdev, REG_SDIO_HIMR);
		rtw_write32(rtwdev, REG_SDIO_HIMR, 0);
	}

	if (!pwr_on)
		clear_bit(RTW_FLAG_POWERON, rtwdev->flags);

	pwr_seq = pwr_on ? chip->pwr_on_seq : chip->pwr_off_seq;
	ret = rtw_pwr_seq_parser(rtwdev, pwr_seq);

	if (pwr_on && rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB) {
		if (chip->id == RTW_CHIP_TYPE_8822C ||
		    chip->id == RTW_CHIP_TYPE_8822B ||
		    chip->id == RTW_CHIP_TYPE_8821C)
			rtw_write8_clr(rtwdev, REG_SYS_STATUS1 + 1, BIT(0));
	}

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_SDIO)
		rtw_write32(rtwdev, REG_SDIO_HIMR, imr);

	if (!ret && pwr_on)
		set_bit(RTW_FLAG_POWERON, rtwdev->flags);

	return ret;
}

static int rtw_mac_pre_system_cfg(struct rtw_dev *rtwdev)
{
	unsigned int retry;
	u32 value32;
	u8 value8;

	rtw_write8(rtwdev, REG_RSV_CTRL, 0);

	if (rtw_chip_wcpu_11n(rtwdev)) {
		if (rtw_read32(rtwdev, REG_SYS_CFG1) & BIT_LDO)
			rtw_write8(rtwdev, REG_LDO_SWR_CTRL, LDO_SEL);
		else
			rtw_write8(rtwdev, REG_LDO_SWR_CTRL, SPS_SEL);
		return 0;
	}

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rtw_write32_set(rtwdev, REG_HCI_OPT_CTRL, BIT_USB_SUS_DIS);
		break;
	case RTW_HCI_TYPE_SDIO:
		rtw_write8_clr(rtwdev, REG_SDIO_HSUS_CTRL, BIT_HCI_SUS_REQ);

		for (retry = 0; retry < RTW_PWR_POLLING_CNT; retry++) {
			if (rtw_read8(rtwdev, REG_SDIO_HSUS_CTRL) & BIT_HCI_RESUME_RDY)
				break;

			usleep_range(10, 50);
		}

		if (retry == RTW_PWR_POLLING_CNT) {
			rtw_err(rtwdev, "failed to poll REG_SDIO_HSUS_CTRL[1]");
			return -ETIMEDOUT;
		}

		if (rtw_sdio_is_sdio30_supported(rtwdev))
			rtw_write8_set(rtwdev, REG_HCI_OPT_CTRL + 2,
				       BIT_SDIO_PAD_E5 >> 16);
		else
			rtw_write8_clr(rtwdev, REG_HCI_OPT_CTRL + 2,
				       BIT_SDIO_PAD_E5 >> 16);
		break;
	case RTW_HCI_TYPE_USB:
		break;
	default:
		return -EINVAL;
	}

	/* config PIN Mux */
	value32 = rtw_read32(rtwdev, REG_PAD_CTRL1);
	value32 |= BIT_PAPE_WLBT_SEL | BIT_LNAON_WLBT_SEL;
	rtw_write32(rtwdev, REG_PAD_CTRL1, value32);

	value32 = rtw_read32(rtwdev, REG_LED_CFG);
	value32 &= ~(BIT_PAPE_SEL_EN | BIT_LNAON_SEL_EN);
	rtw_write32(rtwdev, REG_LED_CFG, value32);

	value32 = rtw_read32(rtwdev, REG_GPIO_MUXCFG);
	value32 |= BIT_WLRFE_4_5_EN;
	rtw_write32(rtwdev, REG_GPIO_MUXCFG, value32);

	/* disable BB/RF */
	value8 = rtw_read8(rtwdev, REG_SYS_FUNC_EN);
	value8 &= ~(BIT_FEN_BB_RSTB | BIT_FEN_BB_GLB_RST);
	rtw_write8(rtwdev, REG_SYS_FUNC_EN, value8);

	value8 = rtw_read8(rtwdev, REG_RF_CTRL);
	value8 &= ~(BIT_RF_SDM_RSTB | BIT_RF_RSTB | BIT_RF_EN);
	rtw_write8(rtwdev, REG_RF_CTRL, value8);

	value32 = rtw_read32(rtwdev, REG_WLRF1);
	value32 &= ~BIT_WLRF1_BBRF_EN;
	rtw_write32(rtwdev, REG_WLRF1, value32);

	return 0;
}

static int rtw_pwr_cmd_polling(struct rtw_dev *rtwdev,
			       const struct rtw_pwr_seq_cmd *cmd)
{
	u8 value;
	u32 offset;

	if (cmd->base == RTW_PWR_ADDR_SDIO)
		offset = cmd->offset | SDIO_LOCAL_OFFSET;
	else
		offset = cmd->offset;

	if (do_pwr_poll_cmd(rtwdev, offset, cmd->mask, cmd->value))
		return 0;

	if (rtw_hci_type(rtwdev) != RTW_HCI_TYPE_PCIE)
		goto err;

	/* if PCIE, toggle BIT_PFM_WOWL and try again */
	value = rtw_read8(rtwdev, REG_SYS_PW_CTRL);
	if (rtwdev->chip->id == RTW_CHIP_TYPE_8723D)
		rtw_write8(rtwdev, REG_SYS_PW_CTRL, value & ~BIT_PFM_WOWL);
	rtw_write8(rtwdev, REG_SYS_PW_CTRL, value | BIT_PFM_WOWL);
	rtw_write8(rtwdev, REG_SYS_PW_CTRL, value & ~BIT_PFM_WOWL);
	if (rtwdev->chip->id == RTW_CHIP_TYPE_8723D)
		rtw_write8(rtwdev, REG_SYS_PW_CTRL, value | BIT_PFM_WOWL);

	if (do_pwr_poll_cmd(rtwdev, offset, cmd->mask, cmd->value))
		return 0;

err:
	rtw_err(rtwdev, "failed to poll offset=0x%x mask=0x%x value=0x%x\n",
		offset, cmd->mask, cmd->value);
	return -EBUSY;
}

static int rtw_pwr_seq_parser(struct rtw_dev *rtwdev,
			      const struct rtw_pwr_seq_cmd **cmd_seq)
{
	u8 cut_mask;
	u8 intf_mask;
	u8 cut;
	u32 idx = 0;
	const struct rtw_pwr_seq_cmd *cmd;
	int ret;

	cut = rtwdev->hal.cut_version;
	cut_mask = cut_version_to_mask(cut);
	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		intf_mask = RTW_PWR_INTF_PCI_MSK;
		break;
	case RTW_HCI_TYPE_USB:
		intf_mask = RTW_PWR_INTF_USB_MSK;
		break;
	case RTW_HCI_TYPE_SDIO:
		intf_mask = RTW_PWR_INTF_SDIO_MSK;
		break;
	default:
		return -EINVAL;
	}

	do {
		cmd = cmd_seq[idx];
		if (!cmd)
			break;

		ret = rtw_sub_pwr_seq_parser(rtwdev, intf_mask, cut_mask, cmd);
		if (ret)
			return ret;

		idx++;
        if(idx>=8||!rtwdev->ok)return -EIO;
	} while (1);

	return 0;
}

static int rtw_sub_pwr_seq_parser(struct rtw_dev *rtwdev, u8 intf_mask,
				  u8 cut_mask,
				  const struct rtw_pwr_seq_cmd *cmd)
{
	const struct rtw_pwr_seq_cmd *cur_cmd;
	u32 offset;
	u8 value;

	for (cur_cmd = cmd; cur_cmd->cmd != RTW_PWR_CMD_END; cur_cmd++) {
        if(cur_cmd-cmd>=256||!rtwdev->ok)return -EIO;
		if (!(cur_cmd->intf_mask & intf_mask) ||
		    !(cur_cmd->cut_mask & cut_mask))
			continue;

		switch (cur_cmd->cmd) {
		case RTW_PWR_CMD_WRITE:
			offset = cur_cmd->offset;

			if (cur_cmd->base == RTW_PWR_ADDR_SDIO)
				offset |= SDIO_LOCAL_OFFSET;

			value = rtw_read8(rtwdev, offset);
			value &= ~cur_cmd->mask;
			value |= (cur_cmd->value & cur_cmd->mask);
			rtw_write8(rtwdev, offset, value);
			break;
		case RTW_PWR_CMD_POLLING:
			if (rtw_pwr_cmd_polling(rtwdev, cur_cmd))
				return -EBUSY;
			break;
		case RTW_PWR_CMD_DELAY:
			if (cur_cmd->value == RTW_PWR_DELAY_US)
				udelay(cur_cmd->offset);
			else
				mdelay(cur_cmd->offset);
			break;
		case RTW_PWR_CMD_READ:
			break;
		default:
			return -EINVAL;
		}
	}

	return 0;
}

static int send_firmware_pkt(struct rtw_dev*d,u16 pg,const u8*data,u32 n){
 if(!data||!n||n>4096)return -EINVAL;
 unsigned padded=n+(((n+48)&511)==0);
 u8*buf=kmalloc(padded);if(!buf)return -ENOMEM;
 memcpy(buf,data,n);if(padded>n)buf[n]=0;
 int rc=rtw_fw_write_data_rsvd_page(d,pg,buf,padded);kfree(buf);return rc;
}

static int send_firmware_pkt_rsvd_page(struct rtw_dev *rtwdev, u16 pg_addr,
				       const u8 *data, u32 size)
{
	u8 *buf;
	int ret;

	buf = kmemdup(data, size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = rtw_fw_write_data_rsvd_page(rtwdev, pg_addr, buf, size);
	kfree(buf);
	return ret;
}

static int set_trx_fifo_info(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	struct rtw_fifo_conf *fifo = &rtwdev->fifo;
	u16 cur_pg_addr;
	u8 csi_buf_pg_num = chip->csi_buf_pg_num;

	/* config rsvd page num */
	fifo->rsvd_drv_pg_num = chip->rsvd_drv_pg_num;
	fifo->txff_pg_num = chip->txff_size >> 7;
	if (rtw_chip_wcpu_11n(rtwdev))
		fifo->rsvd_pg_num = fifo->rsvd_drv_pg_num;
	else
		fifo->rsvd_pg_num = fifo->rsvd_drv_pg_num +
				   RSVD_PG_H2C_EXTRAINFO_NUM +
				   RSVD_PG_H2C_STATICINFO_NUM +
				   RSVD_PG_H2CQ_NUM +
				   RSVD_PG_CPU_INSTRUCTION_NUM +
				   RSVD_PG_FW_TXBUF_NUM +
				   csi_buf_pg_num;

	if (fifo->rsvd_pg_num > fifo->txff_pg_num)
		return -ENOMEM;

	fifo->acq_pg_num = fifo->txff_pg_num - fifo->rsvd_pg_num;
	fifo->rsvd_boundary = fifo->txff_pg_num - fifo->rsvd_pg_num;

	cur_pg_addr = fifo->txff_pg_num;
	if (rtw_chip_wcpu_11ac(rtwdev)) {
		cur_pg_addr -= csi_buf_pg_num;
		fifo->rsvd_csibuf_addr = cur_pg_addr;
		cur_pg_addr -= RSVD_PG_FW_TXBUF_NUM;
		fifo->rsvd_fw_txbuf_addr = cur_pg_addr;
		cur_pg_addr -= RSVD_PG_CPU_INSTRUCTION_NUM;
		fifo->rsvd_cpu_instr_addr = cur_pg_addr;
		cur_pg_addr -= RSVD_PG_H2CQ_NUM;
		fifo->rsvd_h2cq_addr = cur_pg_addr;
		cur_pg_addr -= RSVD_PG_H2C_STATICINFO_NUM;
		fifo->rsvd_h2c_sta_info_addr = cur_pg_addr;
		cur_pg_addr -= RSVD_PG_H2C_EXTRAINFO_NUM;
		fifo->rsvd_h2c_info_addr = cur_pg_addr;
	}
	cur_pg_addr -= fifo->rsvd_drv_pg_num;
	fifo->rsvd_drv_addr = cur_pg_addr;

	if (fifo->rsvd_boundary != fifo->rsvd_drv_addr) {
		rtw_err(rtwdev, "wrong rsvd driver address\n");
		return -EINVAL;
	}

	return 0;
}

static int
start_download_firmware(struct rtw_dev *rtwdev, const u8 *data, u32 size)
{
	const struct rtw_fw_hdr *fw_hdr = (const struct rtw_fw_hdr *)data;
	const u8 *cur_fw;
	u16 val;
	u32 imem_size;
	u32 dmem_size;
	u32 emem_size;
	u32 addr;
	int ret;

	dmem_size = le32_to_cpu(fw_hdr->dmem_size);
	imem_size = le32_to_cpu(fw_hdr->imem_size);
	emem_size = (fw_hdr->mem_usage & BIT(4)) ?
		    le32_to_cpu(fw_hdr->emem_size) : 0;
	dmem_size += FW_HDR_CHKSUM_SIZE;
	imem_size += FW_HDR_CHKSUM_SIZE;
	emem_size += emem_size ? FW_HDR_CHKSUM_SIZE : 0;

	val = (u16)(rtw_read16(rtwdev, REG_MCUFW_CTRL) & 0x3800);
	val |= BIT_MCUFWDL_EN;
	rtw_write16(rtwdev, REG_MCUFW_CTRL, val);

	cur_fw = data + FW_HDR_SIZE;
	addr = le32_to_cpu(fw_hdr->dmem_addr);
	addr &= ~BIT(31);
	ret = download_firmware_to_mem(rtwdev, cur_fw, 0, addr, dmem_size);
	if (ret)
		return ret;

	cur_fw = data + FW_HDR_SIZE + dmem_size;
	addr = le32_to_cpu(fw_hdr->imem_addr);
	addr &= ~BIT(31);
	ret = download_firmware_to_mem(rtwdev, cur_fw, 0, addr, imem_size);
	if (ret)
		return ret;

	if (emem_size) {
		cur_fw = data + FW_HDR_SIZE + dmem_size + imem_size;
		addr = le32_to_cpu(fw_hdr->emem_addr);
		addr &= ~BIT(31);
		ret = download_firmware_to_mem(rtwdev, cur_fw, 0, addr,
					       emem_size);
		if (ret)
			return ret;
	}

	return 0;
}

static int txdma_queue_mapping(struct rtw_dev *rtwdev)
{
	const struct rtw_chip_info *chip = rtwdev->chip;
	const struct rtw_rqpn *rqpn = NULL;
	u16 txdma_pq_map = 0;

	switch (rtw_hci_type(rtwdev)) {
	case RTW_HCI_TYPE_PCIE:
		rqpn = &chip->rqpn_table[1];
		break;
	case RTW_HCI_TYPE_USB:
		if (rtwdev->hci.bulkout_num == 2)
			rqpn = &chip->rqpn_table[2];
		else if (rtwdev->hci.bulkout_num == 3)
			rqpn = &chip->rqpn_table[3];
		else if (rtwdev->hci.bulkout_num == 4)
			rqpn = &chip->rqpn_table[4];
		else
			return -EINVAL;
		break;
	case RTW_HCI_TYPE_SDIO:
		rqpn = &chip->rqpn_table[0];
		break;
	default:
		return -EINVAL;
	}

	rtwdev->fifo.rqpn = rqpn;
	txdma_pq_map |= BIT_TXDMA_HIQ_MAP(rqpn->dma_map_hi);
	txdma_pq_map |= BIT_TXDMA_MGQ_MAP(rqpn->dma_map_mg);
	txdma_pq_map |= BIT_TXDMA_BKQ_MAP(rqpn->dma_map_bk);
	txdma_pq_map |= BIT_TXDMA_BEQ_MAP(rqpn->dma_map_be);
	txdma_pq_map |= BIT_TXDMA_VIQ_MAP(rqpn->dma_map_vi);
	txdma_pq_map |= BIT_TXDMA_VOQ_MAP(rqpn->dma_map_vo);
	rtw_write16(rtwdev, REG_TXDMA_PQ_MAP, txdma_pq_map);

	rtw_write8(rtwdev, REG_CR, 0);
	rtw_write8(rtwdev, REG_CR, MAC_TRX_ENABLE);
	if (rtw_chip_wcpu_11ac(rtwdev))
		rtw_write32(rtwdev, REG_H2CQ_CSR, BIT_H2CQ_FULL);

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_SDIO) {
		rtw_read32(rtwdev, REG_SDIO_FREE_TXPG);
		rtw_write32(rtwdev, REG_SDIO_TX_CTRL, 0);
	} else if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_USB) {
		rtw_write8_set(rtwdev, REG_TXDMA_PQ_MAP, BIT_RXDMA_ARBBW_EN);
	}

	return 0;
}

static void wlan_cpu_enable(struct rtw_dev *rtwdev, bool enable)
{
	if (enable) {
		/* cpu io interface enable */
		rtw_write8_set(rtwdev, REG_RSV_CTRL + 1, BIT_WLMCU_IOIF);

		/* cpu enable */
		rtw_write8_set(rtwdev, REG_SYS_FUNC_EN + 1, BIT_FEN_CPUEN);
	} else {
		/* cpu io interface disable */
		rtw_write8_clr(rtwdev, REG_SYS_FUNC_EN + 1, BIT_FEN_CPUEN);

		/* cpu disable */
		rtw_write8_clr(rtwdev, REG_RSV_CTRL + 1, BIT_WLMCU_IOIF);
	}
}
