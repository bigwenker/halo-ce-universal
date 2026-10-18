#include "harness.h"
#include <math.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,a,b) MIN(MAX(x,a),b)
typedef struct {short x0,y0,x1,y1;} rectangle2d;
#include "pointer.inc"
static struct {int active,drag;float h,s,v;rectangle2d square_bounds,hue_bounds;int reset;} picker={1,0,0,0,1,{50,50,250,250},{280,50,300,250},0};
struct widget_instance {struct widget_instance *focused_child;};
static struct {short chosen,count;} profile_list;
static struct widget_instance *opened_from;
static char const *opened_target;
static int profile_ok=1;
static boolean profile_choose(short controller){(void)controller;return profile_ok;}
static boolean campaign_fail(void){return FALSE;}
struct player_profile {short primary_color_index;unsigned long preserved;};
static struct player_profile saved_profile={3,0xabcdef};
static struct {long index;} profile_edit={5};
static int override_present=1,write_calls,clear_failed,profile_writes,notifications,invalidations,save_failed,model_default;
static boolean player_profile_get(long index,struct player_profile *profile){CHECK(index==5,"selected profile");*profile=saved_profile;return TRUE;}
typedef struct {real red,green,blue;} real_rgb_color;
static boolean profile_rgb_read(long index,unsigned long *rgb){CHECK(index==5,"selected RGB");*rgb=0x123456;return override_present;}
static boolean custom_models_profile_default_rgb(long index,unsigned long *rgb){(void)index;*rgb=0x6A0001;return model_default;}
static real_rgb_color player_profile_get_rgb_color(long palette){(void)palette;return (real_rgb_color){0,1,0};}
static unsigned long profile_color_menu_rgb(void){return 0x123456;}
static boolean profile_rgb_write(long index,unsigned long rgb){CHECK(index==5&&rgb==0x123456,"save selected RGB");override_present=1;write_calls++;return TRUE;}
static short profile_rgb_nearest(unsigned long rgb){(void)rgb;return 7;}
static void profile_color_menu_invalidate(void);
static void network_profile_rgb_changed(void);
static boolean profile_edit_save(struct player_profile *profile){CHECK(profile->preserved==0xabcdef,"unrelated profile fields changed");if(save_failed)return FALSE;saved_profile=*profile;profile_writes++;profile_color_menu_invalidate();network_profile_rgb_changed();return TRUE;}
static boolean profile_rgb_clear(long index){CHECK(index==5,"reset selected profile");if(clear_failed)return FALSE;override_present=0;return TRUE;}
static void profile_color_menu_invalidate(void){invalidations++;}
static void profile_color_menu_portrait(long index){CHECK(index==5,"refresh selected portrait");}
static void network_profile_rgb_changed(void){notifications++;}
static boolean ui_widget_port_open(struct widget_instance *from,char const *target,boolean *deleted){opened_from=from;opened_target=target;*deleted=TRUE;return TRUE;}
#include "under_test.inc"
int main(int argc,char**argv){CHECK(argc==2,"case");struct halo_ui_pointer p={0};p.x=p.click_x=150;p.y=p.click_y=150;p.left_clicks=1;p.held=1;
 if(!strcmp(argv[1],"drag")||!strcmp(argv[1],"touch")){
  p.touch=!strcmp(argv[1],"touch");CHECK(profile_color_menu_pointer(&p),"press consumed");CHECK(picker.s>.5&&picker.s<.51,"press saturation");
  p.left_clicks=0;p.x=249;p.y=50;CHECK(profile_color_menu_pointer(&p),"held move consumed");CHECK(picker.s==1&&picker.v==1,"drag updated");
  p.held=0;p.x=100;CHECK(!profile_color_menu_pointer(&p)&&picker.s==1,"release held previous selection");
 }else if(!strcmp(argv[1],"hue")){
  p.click_x=290;p.click_y=249;CHECK(profile_color_menu_pointer(&p)&&picker.h==359,"hue end");
  p.left_clicks=0;p.y=-100;CHECK(profile_color_menu_pointer(&p)&&picker.h==0,"clamped drag");
 }else if(!strcmp(argv[1],"inactive")){
  picker.active=0;CHECK(!profile_color_menu_pointer(&p)&&picker.s==0,"closed picker consumed input");
 }else if(!strcmp(argv[1],"reset")){
  profile_color_menu_reset(5,3);CHECK(profile_color_menu_is_reset(),"reset selects fallback without an override");
  profile_color_menu_adjust("unrelated",1);CHECK(profile_color_menu_is_reset(),"unrelated input must not save fallback");
  p.click_x=10;CHECK(!profile_color_menu_pointer(&p)&&profile_color_menu_is_reset(),"click outside picker preserves unset");
  real before_h=picker.h;profile_color_menu_adjust("rgb_hue_value",1);CHECK(!profile_color_menu_is_reset()&&picker.h==before_h+1,"keyboard creates a new selection");
  profile_color_menu_reset(5,3);p.click_x=150;CHECK(profile_color_menu_pointer(&p)&&!profile_color_menu_is_reset(),"pointer creates a new selection");
 }else if(!strcmp(argv[1],"save-reset")){
  CHECK(profile_edit_color_save()&&override_present&&write_calls==1&&profile_writes==1,"Done saves selection");
  profile_writes=write_calls=notifications=invalidations=0;
  CHECK(profile_edit_color_reset()&&!override_present&&notifications==1&&invalidations==1,"Reset clears and refreshes immediately");
  CHECK(saved_profile.primary_color_index==NONE,"Reset left a legacy palette selection");
  CHECK(profile_edit_color_save()&&!override_present&&!write_calls&&profile_writes==1,"Done after Reset must keep color unset and other profile data intact");
  profile_color_menu_adjust("rgb_brightness_value",-1);
  CHECK(profile_edit_color_save()&&override_present&&write_calls==1,"editing after Reset can be saved");
  int previous_notifications=notifications;clear_failed=1;CHECK(!profile_edit_color_reset()&&override_present&&notifications==previous_notifications,"failed Reset keeps selection and does not notify");
 }else if(!strcmp(argv[1],"reopen")||!strcmp(argv[1],"model-default")){
  model_default=!strcmp(argv[1],"model-default");CHECK(profile_edit_color_reset(),"reset");
  profile_color_menu_begin(5,saved_profile.primary_color_index);
  CHECK(profile_color_menu_is_reset(),"reopening an unset profile creates a selection");
  int writes=profile_writes;
  CHECK(profile_edit_color_save()&&!override_present&&saved_profile.primary_color_index==NONE&&profile_writes==writes&&!write_calls,"Done after reopen saved a fallback color");
 }else if(!strcmp(argv[1],"save-failure")){
  save_failed=1;CHECK(!profile_edit_color_reset()&&override_present&&saved_profile.primary_color_index==3,"failed profile save lost previous selection");
 }else if(!strcmp(argv[1],"navigation")){
  struct widget_instance row={0},list={&row};boolean deleted=FALSE;profile_list.count=1;
  CHECK(profile_edit_choose(&list,0,&deleted)&&deleted&&opened_from==&row,"return history must preserve the selected row");
  CHECK(!strcmp(opened_target,"pc\\port_profile\\edit"),"profile must open editor");
  {struct widget_instance ok={0};row.focused_child=&ok;CHECK(profile_edit_choose(&list,0,&deleted)&&opened_from==&ok,"nested OK button history");row.focused_child=NULL;}
  profile_list.chosen=1;CHECK(profile_edit_choose(&list,0,&deleted)&&strstr(opened_target,"pm_empty_selected")&&opened_from==&row,"new profile row");
  profile_list.chosen=0;profile_ok=0;opened_from=NULL;CHECK(!profile_edit_choose(&list,0,&deleted)&&!opened_from,"failed profile read must stay put");
 }else CHECK(0,"unknown");return 0;}
