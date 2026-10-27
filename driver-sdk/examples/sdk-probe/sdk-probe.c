/* SPDX-License-Identifier: BSD-2-Clause */
#include "banana_driver.h"
int banana_driver_main(const banana_driver_api_t* api) {
    const bdrv_net_api_t* net=bdrv_extension(api,BDRV_EXT_NET,1,sizeof(*net));
    const bdrv_wifi_api_t* wifi=bdrv_extension(api,BDRV_EXT_WIFI,1,sizeof(*wifi));
    const bdrv_usb_api_t* usb=bdrv_extension(api,BDRV_EXT_USB,1,sizeof(*usb));
    const bdrv_power_api_t* power=bdrv_extension(api,BDRV_EXT_POWER,1,sizeof(*power));
    const bdrv_firmware_api_t* fw=bdrv_extension(api,BDRV_EXT_FIRMWARE,1,sizeof(*fw));
    if(!net || !wifi || !usb || !power || !fw) { api->log("extensions unavailable");return -1; }
    char bytes[32]={0};const char* expected="Driver SDK package data\n";
    int n=fw->read("/apps/sdk-probe/probe.txt",0,bytes,sizeof(bytes));
    if(n!=(int)bdrv_strlen(expected) || bdrv_memcmp(bytes,expected,(unsigned long)n)) return -1;
    api->log("firmware/package read passed");
    bdrv_power_sample_t p={.size=sizeof(p)};
    if(power->snapshot(&p)<0) return -1;
    api->log("optional NET/WIFI/USB/POWER/FIRMWARE services ready");
    if(p.battery_count) api->log("battery %d%%, brightness %d%%",p.batteries[0].percent,p.brightness);
    bdrv_usbc_status_t c={.size=sizeof(c)};
    if(!power->usbc_status(1,&c) && c.contract_valid)
        api->log("PD contract %u mV %u mA",c.millivolts,c.milliamps);
    return 0;
}
