#include "harness.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct { float red,green,blue; } real_rgb_color;
#define TRUE 1
#define FALSE 0
#define NONE (-1)
#define _permission_read_bit 0
#define _permission_write_bit 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,a,b) MIN(MAX(x,a),b)
struct file_reference { char path[300]; };
static char content[16];static unsigned size;static int exists,failed;
static int player_profile_get_enclosing_directory_path(long n,char *s){if(n!=5)return 0;strcpy(s,"U:\\UDATA\\stable\\");return 1;}
static void *file_reference_create_from_path(struct file_reference*f,char const*p,int d){(void)d;strcpy(f->path,p);return f;}
static int file_open(struct file_reference*f,unsigned flags){(void)f;(void)flags;return exists;}
static int file_close(struct file_reference*f){(void)f;return 1;}
static unsigned file_get_eof(struct file_reference*f){(void)f;return size;}
static int file_read(struct file_reference*f,unsigned n,void*out){(void)f;if(n>size)return 0;memcpy(out,content,n);return 1;}
static int file_write(struct file_reference*f,unsigned n,void const*data){(void)f;if(failed)return 0;memcpy(content,data,n);return 1;}
static int file_set_eof(struct file_reference*f,unsigned n){(void)f;size=n;return 1;}
static int file_exists(struct file_reference*f){(void)f;return exists;}
static int file_create(struct file_reference*f){(void)f;exists=1;return 1;}
#include "palette.inc"
static short player_profile_number_of_available_primary_colors(void){return sizeof(palette)/sizeof(palette[0]);}
static real_rgb_color player_profile_get_rgb_color(long n){real_rgb_color c={((palette[n]>>16)&255)/255.f,((palette[n]>>8)&255)/255.f,(palette[n]&255)/255.f};return c;}
static long active_profile=NONE,remembered_profile=5;static int remembered_reads;
static long player_ui_get_active_player_profile_index(short local){(void)local;return active_profile;}
static long player_ui_get_player1_last_used_profile_index(void){remembered_reads++;return remembered_profile;}
#include "under_test.inc"
int main(int argc,char**argv){CHECK(argc==2,"case");unsigned long rgb=99;char text[8];
 if(!strcmp(argv[1],"local")){
  CHECK(profile_rgb_local_profile(0)==5,"join-time remembered profile");
  CHECK(profile_rgb_local_profile(0)==5 && remembered_reads==1,"repeated fallback file lookup");
  CHECK(profile_rgb_local_profile(1)==NONE,"split-screen guest inherited player 1");
  active_profile=7;CHECK(profile_rgb_local_profile(0)==7,"active selection ignored");
  active_profile=NONE;remembered_profile=9;profile_rgb_local_profile_reset();
  CHECK(profile_rgb_local_profile(0)==9,"new selection not refreshed");
 }else if(!strcmp(argv[1],"parse")){
  CHECK(profile_rgb_parse("#000000",&rgb)&&rgb==0,"black is a provided color");
  CHECK(profile_rgb_parse("#aE80e8",&rgb)&&rgb==0xAE80E8,"hex");profile_rgb_format(rgb,text);CHECK(!strcmp(text,"#AE80E8"),"canonical hex");
  CHECK(!profile_rgb_parse("#123456junk",&rgb),"trailing garbage");
  CHECK(!profile_rgb_parse("#12345",&rgb)&&!profile_rgb_parse("#12345G",&rgb)&&!profile_rgb_parse("123456",&rgb),"invalid");
 }else if(!strcmp(argv[1],"hsv")){
  unsigned long seed=1;for(int i=0;i<20000;i++){float h,s,v;seed=seed*1664525u+1013904223u;rgb=seed&0xffffff;profile_rgb_to_hsv(rgb,&h,&s,&v);CHECK(profile_rgb_from_hsv(h,s,v)==rgb,"HSV roundtrip");}
  CHECK(profile_rgb_from_hsv(0,1,1)==0xff0000&&profile_rgb_from_hsv(120,1,1)==0x00ff00&&profile_rgb_from_hsv(240,1,1)==0x0000ff,"primary hues");
  for(int h=0;h<=360;h++)CHECK(profile_rgb_from_hsv(h,1,0)==0,"brightness zero");
 }else if(!strcmp(argv[1],"storage")){
  CHECK(profile_rgb_write(5,0)&&size==7&&!memcmp(content,"#000000",7),"stored hex");CHECK(profile_rgb_read(5,&rgb)&&rgb==0,"black loaded");
  CHECK(profile_rgb_write(5,0x12abef)&&profile_rgb_read(5,&rgb)&&rgb==0x12abef,"roundtrip");
  failed=1;CHECK(!profile_rgb_write(5,1)&&!memcmp(content,"#12ABEF",7),"failed write");
 }else if(!strcmp(argv[1],"fallback")){
  CHECK(!profile_rgb_read(5,&rgb),"missing sidecar uses palette");exists=1;strcpy(content,"#abcdef");size=6;CHECK(!profile_rgb_read(5,&rgb),"truncated");size=8;CHECK(!profile_rgb_read(5,&rgb),"extra data");size=7;content[6]='z';CHECK(!profile_rgb_read(5,&rgb),"malformed");CHECK(!profile_rgb_write(NONE,1),"no profile");
 }else if(!strcmp(argv[1],"nearest")){
  for(int i=0;i<18;i++)CHECK(profile_rgb_nearest(palette[i])==i,"legacy exact palette fallback");
 }else CHECK(0,"unknown case");return 0;}
