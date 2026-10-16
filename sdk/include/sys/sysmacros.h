#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H
/* a device number's parts (st_dev, st_rdev), Linux's encoding */
#define major(d) ((unsigned int)((((d) >> 32) & 0xfffff000u) | (((d) >> 8) & 0xfffu)))
#define minor(d) ((unsigned int)((((d) >> 12) & 0xffffff00u) | ((d) & 0xffu)))
#define makedev(ma, mi) ((((unsigned long long)(ma) & 0xfffff000ull) << 32) | (((unsigned long long)(ma) & 0xfffull) << 8) | \
                         (((unsigned long long)(mi) & 0xffffff00ull) << 12) | ((unsigned long long)(mi) & 0xffull))
#endif
