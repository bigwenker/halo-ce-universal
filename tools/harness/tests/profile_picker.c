#include "harness.h"
#include <math.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define PIN(x,a,b) MIN(MAX(x,a),b)
typedef struct {short x0,y0,x1,y1;} rectangle2d;
#include "pointer.inc"
static struct {int active,drag;float h,s,v;rectangle2d square_bounds,hue_bounds;} picker={1,0,0,0,1,{50,50,250,250},{280,50,300,250}};
struct widget_instance {struct widget_instance *focused_child;};
static struct {short chosen,count;} profile_list;
static struct widget_instance *opened_from;
static char const *opened_target;
static int profile_ok=1;
static boolean profile_choose(short controller){(void)controller;return profile_ok;}
static boolean campaign_fail(void){return FALSE;}
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
 }else if(!strcmp(argv[1],"navigation")){
  struct widget_instance row={0},list={&row};boolean deleted=FALSE;profile_list.count=1;
  CHECK(profile_edit_choose(&list,0,&deleted)&&deleted&&opened_from==&row,"return history must preserve the selected row");
  CHECK(!strcmp(opened_target,"pc\\port_profile\\edit"),"profile must open editor");
  {struct widget_instance ok={0};row.focused_child=&ok;CHECK(profile_edit_choose(&list,0,&deleted)&&opened_from==&ok,"nested OK button history");row.focused_child=NULL;}
  profile_list.chosen=1;CHECK(profile_edit_choose(&list,0,&deleted)&&strstr(opened_target,"pm_empty_selected")&&opened_from==&row,"new profile row");
  profile_list.chosen=0;profile_ok=0;opened_from=NULL;CHECK(!profile_edit_choose(&list,0,&deleted)&&!opened_from,"failed profile read must stay put");
 }else CHECK(0,"unknown");return 0;}
