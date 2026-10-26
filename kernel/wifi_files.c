/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../net/wifi.h"
#include "fs.h"
extern const uint8_t mt7601_notices_start[],mt7601_notices_end[];
extern const uint8_t igc_notices_start[],igc_notices_end[];
static void install_notice(const char* dir,const char* path,const uint8_t* start,const uint8_t* end) {
    if(fs_mkdir_p(dir)<0) return;
    int f=fs_find_file(path);
    if(f<0) f=fs_create(path);
    if(f>=0) fs_write(f,start,(uint32_t)((uintptr_t)end-(uintptr_t)start));
}
void wifi_boot_files(void) {
    install_notice("/usr/share/licenses/mt7601u","/usr/share/licenses/mt7601u/NOTICES",
                   mt7601_notices_start,mt7601_notices_end);
    install_notice("/usr/share/licenses/igc","/usr/share/licenses/igc/NOTICES",
                   igc_notices_start,igc_notices_end);
}
