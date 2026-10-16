#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H
#ifdef __cplusplus
extern "C" {
#endif
struct sysinfo {
    long uptime;
    unsigned long loads[3];
    unsigned long totalram, freeram, sharedram, bufferram, totalswap, freeswap;
    unsigned short procs, pad;
    unsigned long totalhigh, freehigh;
    unsigned int mem_unit;
    char _f[20 - 2 * sizeof(long) - sizeof(int) > 0 ? 20 - 2 * sizeof(long) - sizeof(int) : 1];
};
int sysinfo(struct sysinfo* i);
int get_nprocs(void);
int get_nprocs_conf(void);
#ifdef __cplusplus
}
#endif
#endif
