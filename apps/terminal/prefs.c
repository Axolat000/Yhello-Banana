/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "prefs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
const char* const term_themes[4]={"midnight","light","solarized","mono"};
const char* const term_cursors[3]={"block","underline","bar"};
const char* const term_shells[3]={"system","bash","zsh"};
static const uint32_t palettes[4][3]={
 {0xE5E5E5,0x1D232C,0xF4D35E},{0x1E293B,0xF8FAFC,0x2563EB},
 {0x93A1A1,0x002B36,0x2AA198},{0xD4D4D4,0x101010,0xD4D4D4}
};
static int choice(const char* value,const char* const* names,int n){
 for(int i=0;i<n;i++)if(!strcmp(value,names[i]))return i;
 return -1;
}
void term_prefs_defaults(term_prefs_t* p){
 *p=(term_prefs_t){15,0,0,0,0xE5E5E5,0x1D232C,0xF4D35E};
}
int term_prefs_set(term_prefs_t* p,const char* key,const char* value){
 int n;
 if(!strcmp(key,"theme")){
  if(!strcmp(value,"custom")){p->theme=-1;return 0;}
  n=choice(value,term_themes,4);if(n<0)return -1;
  p->theme=n;p->fg=palettes[n][0];p->bg=palettes[n][1];p->cursor_color=palettes[n][2];return 0;
 }
 if(!strcmp(key,"font")){
  char* end;long v=strtol(value,&end,10);
  if(!*value||*end||v<8||v>40)return -1;
  p->font=(int)v;return 0;
 }
 if(!strcmp(key,"cursor")){n=choice(value,term_cursors,3);if(n<0)return -1;p->cursor=n;return 0;}
 if(!strcmp(key,"shell")){n=choice(value,term_shells,3);if(n<0)return -1;p->shell=n;return 0;}
 uint32_t* color=!strcmp(key,"foreground")?&p->fg:!strcmp(key,"background")?&p->bg:!strcmp(key,"cursor_color")?&p->cursor_color:NULL;
 if(!color)return -1;
 if(*value=='#')value++;
 if(strlen(value)!=6)return -1;
 uint32_t rgb=0;
 for(int i=0;i<6;i++){
  unsigned c=(unsigned char)value[i],v;
  if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;else if(c>='A'&&c<='F')v=c-'A'+10;else return -1;
  rgb=(rgb<<4)|v;
 }
 if(*color!=rgb&&color!=&p->cursor_color)p->theme=-1;
 *color=rgb;return 0;
}
static int paths(char* dir,char* file,char* temporary){
 const char* home=getenv("HOME");if(!home||!*home)home="/home/banana";
 int n=snprintf(dir,512,"%s/.config",home);if(n<0||n>=512)return -1;
 n=snprintf(file,512,"%s/banana-terminal.conf",dir);if(n<0||n>=512)return -1;
 n=snprintf(temporary,512,"%s.tmp.%ld",file,(long)getpid());return n<0||n>=512?-1:0;
}
void term_prefs_print(const term_prefs_t* p,FILE* f){
 fprintf(f,"theme=%s\nfont=%d\ncursor=%s\nshell=%s\nforeground=%06X\nbackground=%06X\ncursor_color=%06X\n",
 p->theme<0?"custom":term_themes[p->theme],p->font,term_cursors[p->cursor],term_shells[p->shell],p->fg,p->bg,p->cursor_color);
}
void term_prefs_load(term_prefs_t* p){
 char dir[512],file[512],tmp[512],line[256];term_prefs_defaults(p);
 if(paths(dir,file,tmp)<0)return;
 FILE* f=fopen(file,"r");if(!f)return;
 while(fgets(line,sizeof(line),f)){
  if(!strchr(line,'\n')&&!feof(f)){int c;while((c=fgetc(f))!='\n'&&c!=EOF){}continue;}
  char* key=line;while(*key==' '||*key=='\t')key++;
  char* eq=strchr(key,'=');if(!eq||*key=='#')continue;*eq++=0;
  char* end=key+strlen(key);while(end>key&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
  eq+=strspn(eq," \t");eq[strcspn(eq,"\r\n")]=0;
  end=eq+strlen(eq);while(end>eq&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
  term_prefs_set(p,key,eq);
 }
 fclose(f);
}
int term_prefs_save(const term_prefs_t* p){
 char dir[512],file[512],tmp[512];if(paths(dir,file,tmp)<0)return -1;
 if(mkdir(dir,0700)<0&&errno!=EEXIST)return -1;
 FILE* f=fopen(tmp,"w");if(!f)return -1;
 term_prefs_print(p,f);
 int ok=!ferror(f)&&fflush(f)==0&&fsync(fileno(f))==0;
 if(fclose(f))ok=0;
 if(ok&&rename(tmp,file)==0)return 0;
 unlink(tmp);return -1;
}
