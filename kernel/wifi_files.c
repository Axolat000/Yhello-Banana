/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../net/wifi.h"
#include "fs.h"
extern const uint8_t mt7601_notices_start[],mt7601_notices_end[];
void wifi_boot_files(void) {
    const char* path="/usr/share/licenses/mt7601u/NOTICES";
    if(fs_mkdir_p("/usr/share/licenses/mt7601u")<0) return;
    int f=fs_find_file(path);
    if(f<0) f=fs_create(path);
    if(f>=0) fs_write(f,mt7601_notices_start,
        (uint32_t)((uintptr_t)mt7601_notices_end-(uintptr_t)mt7601_notices_start));
}
