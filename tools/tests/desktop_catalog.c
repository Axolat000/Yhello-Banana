/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifdef TEST_STARTMENU
#include "../../kernel/startmenu.c"
#else
#include "../../kernel/launcher.c"
#endif
extern char* strcpy(char*,const char*);
extern int printf(const char*, ...);
extern void exit(int);
extern void* malloc(size_t);
extern void free(void*);
void* kmalloc(size_t n) { return malloc(n); }
void kfree(void* p) { free(p); }
#define CHECK(x) do { if (!(x)) { printf("FAIL catalog line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int pkg_list(pkg_info_t* out,int max) {
    static const struct { const char* name; const char* type; } data[]={
        {"bash","console"},{"zsh","console"},{"coreutils","console"},
        {"terminal","gui"},{"mediaplayer","gui"},{"sample-driver","driver"}};
    int n=6<max?6:max;
    for(int i=0;i<n;i++) { memset(&out[i],0,sizeof(out[i])); strcpy(out[i].name,data[i].name); strcpy(out[i].title,data[i].name); strcpy(out[i].type,data[i].type); }
    return n;
}
uint32_t timer_ms(void) { return 10; }
const char* tr(const char* text) { return text; }
int fs_list_files(const char* path,int* idx,int max) { (void)path;(void)idx;(void)max;return 0; }
int fs_list_dirs(const char* path,int* idx,int max) { (void)path;(void)idx;(void)max;return 0; }
fs_file_t* fs_get_file(int idx) { (void)idx;return 0; }
fs_file_t* fs_file_info(int idx) { (void)idx;return 0; }
const fs_dir_t* fs_get_dir(int idx) { (void)idx;return 0; }
int main(void) {
#ifdef TEST_STARTMENU
    g_mode=1; build(); int terminals=0,gui=0;
    for(int i=0;i<g_ne;i++) {
        CHECK(strcmp(g_e[i].app,"bash") && strcmp(g_e[i].app,"zsh") && strcmp(g_e[i].app,"coreutils"));
        if(g_e[i].sm==SM_TERMINAL) terminals++;
        if(!strcmp(g_e[i].app,"mediaplayer")) gui++;
    }
    CHECK(terminals==1 && gui==1);
    strcpy(g_query,"bash"); build(); CHECK(g_ne==1 && g_e[0].kind==E_NONE);
    strcpy(g_query,"terminal"); build(); terminals=0;
    for(int i=0;i<g_ne;i++) if(g_e[i].kind==E_PROG) { CHECK(g_e[i].sm==SM_TERMINAL); terminals++; }
    CHECK(terminals==1);
    g_query[0]=0; g_mode=0; build(); terminals=0;
    for(int i=0;i<g_ne;i++) {
        CHECK(strcmp(g_e[i].app,"bash") && strcmp(g_e[i].app,"zsh") && strcmp(g_e[i].app,"coreutils"));
        if(g_e[i].sm==SM_TERMINAL) terminals++;
    }
    CHECK(terminals==1);
    printf("PASS: Start menu, All Programs and search hide CLI packages and show one Terminal\n");
#else
    rescan(); CHECK(g_napps==1);
    CHECK(!strcmp(g_apps[0].name,"mediaplayer"));
    CHECK(banana_system_component("terminal") && banana_system_component("bash") && banana_system_component("zsh"));
    CHECK(!banana_system_component("terminal-extra") && !banana_system_component("zshell") && !banana_system_component(0));
    printf("PASS: Apps launcher shows graphical packages only\n");
#endif
    return 0;
}
