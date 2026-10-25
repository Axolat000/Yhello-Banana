/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../apps/terminal/prefs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv){
 assert(argc==2);assert(!setenv("HOME",argv[1],1));
 term_prefs_t p,q;term_prefs_load(&p);
 assert(p.font==15&&p.shell==0&&p.bg==0x1D232C);
 const char* bad[]={"0","7","41","15px","99999999999999999999999999",""};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(term_prefs_set(&p,"font",bad[i])<0&&p.font==15);
 assert(term_prefs_set(&p,"shell","/bin/evil")<0&&p.shell==0);
 assert(term_prefs_set(&p,"background","000000;touch")<0);
 assert(term_prefs_set(&p,"cursor","invisible")<0);
 assert(!term_prefs_set(&p,"theme","light"));
 assert(!term_prefs_set(&p,"shell","zsh"));
 assert(!term_prefs_set(&p,"font","22"));
 assert(!term_prefs_set(&p,"cursor","bar"));
 assert(!term_prefs_set(&p,"foreground","#123aBc"));
 assert(!term_prefs_set(&p,"cursor_color","FFAA00"));
 assert(!term_prefs_save(&p));term_prefs_load(&q);
 assert(!memcmp(&p,&q,sizeof(p)));
 assert(q.theme==-1&&q.fg==0x123abc&&q.bg==0xF8FAFC);
 assert(!term_prefs_set(&q,"theme","solarized"));assert(!term_prefs_save(&q));term_prefs_load(&p);
 assert(p.theme==2&&p.bg==0x002B36&&p.font==22&&p.shell==2&&p.cursor==2);
 char file[1024];snprintf(file,sizeof(file),"%s/.config/banana-terminal.conf",argv[1]);
 FILE* f=fopen(file,"w");assert(f);
 fputs(" font = 18 \r\nshell=system\ncursor=nope\nforeground=#BADA55\n",f);
 for(int i=0;i<1024;i++)fputc('a',f);fputs("font=40\nfont=19\n",f);fclose(f);
 term_prefs_load(&p);assert(p.font==19&&p.shell==0&&p.cursor==0&&p.fg==0xBADA55);
 puts("PASS: terminal preferences validation, custom colors, atomic save and reload");
}
