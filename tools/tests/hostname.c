/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../kernel/hostname.c"
#include "../../net/dhcp.c"
extern int printf(const char*, ...);
extern void exit(int);
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
static char saved[200];
static int have, fail_write, saves;
static uint32_t saved_len;
static netif_t iface;
static netdev_t device;
static uint8_t packet[512];
static uint32_t packet_len;
int fs_find_file(const char* p) { CHECK(!strcmp(p,HOSTNAME_PATH)); return have ? 0 : -1; }
int fs_read(int idx,uint32_t off,void* out,uint32_t n) {
    CHECK(idx==0 && off==0);
    if (n>saved_len) n=saved_len;
    memcpy(out,saved,n); return (int)n;
}
int fs_write_path(const char* p,const void* data,uint32_t n) {
    CHECK(!strcmp(p,HOSTNAME_PATH));
    if (fail_write) return -1;
    CHECK(n<sizeof(saved)); memcpy(saved,data,n); saved[n]=0; saved_len=n; have=1; return 0;
}
int cfg_persist(void) { saves++; return 1; }
netif_t* net_if(void) { return &iface; }
int udp_send(ip4_t dst,uint16_t sport,uint16_t dport,const void* data,uint32_t n) {
    CHECK(dst==IP4_BROADCAST && sport==68 && dport==67);
    CHECK(n<=sizeof(packet)); memcpy(packet,data,n); packet_len=n; return 0;
}
int main(void) {
    CHECK(!hostname_valid(0)); CHECK(!hostname_valid(""));
    const char* bad[]={"-pc","pc-","two words","pc.local","pc_name","a\nb","a/b","a=b","\xc3\xa9"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) CHECK(!hostname_valid(bad[i]));
    hostname_boot(); CHECK(!strcmp(hostname_get(),"banana-os"));
    CHECK(!hostname_set("Banana-PC2")); CHECK(!strcmp(hostname_get(),"banana-pc2"));
    CHECK(!strcmp(saved,"banana-pc2\n") && saves==1);
    CHECK(hostname_set("bad name")==-1 && saves==1);
    fail_write=1; CHECK(hostname_set("new-pc")==-2); fail_write=0;
    CHECK(!strcmp(hostname_get(),"banana-pc2") && !strcmp(saved,"banana-pc2\n"));
    hostname_boot(); CHECK(!strcmp(hostname_get(),"banana-pc2"));
    char max[65]; memset(max,'a',64); max[63]=0;
    CHECK(!hostname_set(max)); CHECK(strlen(hostname_get())==63);
    max[63]='a'; max[64]=0; CHECK(hostname_set(max)==-1);
    iface.dev=&device;
    send_msg(MSG_REQUEST,IP4(10,0,2,15),IP4(10,0,2,2));
    CHECK(packet_len<=384 && packet_len>300);
    int found=0;
    for (uint32_t i=240;i<packet_len && packet[i]!=255;) {
        unsigned opt=packet[i++]; if (!opt) continue;
        unsigned n=packet[i++]; CHECK(i+n<=packet_len);
        if (opt==12) { CHECK(n==63 && !memcmp(packet+i,hostname_get(),n)); found=1; }
        i+=n;
    }
    CHECK(found && packet[packet_len-1]==255);
    memcpy(saved,"INVALID!\n",9); saved_len=9; hostname_boot(); CHECK(!strcmp(hostname_get(),"banana-os"));
    memcpy(saved,"pc\0hidden\n",10); saved_len=10; hostname_boot(); CHECK(!strcmp(hostname_get(),"banana-os"));
    memset(saved,'x',100); saved_len=100; hostname_boot(); CHECK(!strcmp(hostname_get(),"banana-os"));
    printf("PASS: hostname validation, persistence, write failures and maximum-length DHCP option\n");
    return 0;
}
