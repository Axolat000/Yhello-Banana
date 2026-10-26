/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef USB_UCSI_H
#define USB_UCSI_H
#include "types.h"
#define UCSI_PORT_MAX 16
typedef struct {
    int connected,source,mode,charging,valid,contract_valid;
    uint32_t rdo,millivolts,milliamps,milliwatts;
    uint32_t source_pdos[7];
    uint8_t nr_pdos,partner_type;
    uint16_t change;
} ucsi_port_t;
typedef struct {
    int (*read)(void* ctx,unsigned off,void* data,unsigned n);
    int (*write)(void* ctx,uint64_t command);
    uint32_t (*now)(void*);
    void (*wait)(void*,unsigned ms);
} ucsi_transport_t;
typedef struct {
    const ucsi_transport_t* ops;
    void* ctx;
    uint16_t version,features,pd_version,typec_version;
    uint8_t num_ports,role_capabilities[UCSI_PORT_MAX];
    uint32_t attributes,last_cci,errors,last_ms;
    int ready,busy,failed;
    ucsi_port_t ports[UCSI_PORT_MAX];
} ucsi_t;
int ucsi_init(ucsi_t*,const ucsi_transport_t*,void*);
int ucsi_refresh(ucsi_t*);
int ucsi_power_role(ucsi_t*,unsigned port,int source);
void ucsi_decode_contract(ucsi_port_t*);
#endif
