#include "harness.h"
#define MAXIMUM_GAMEPADS 4
#define FIRST_GAMEPAD_ANALOG_BUTTON 0
#define _gamepad_analog_button_a 0
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3
#define BOOL boolean
struct platform_input_state {byte mouse_buttons[8];boolean mouse_released;};
struct player_datum {long unit_index;boolean quit_out_of_game;};
struct unit_datum {struct {unsigned long damage_flags;}object;};
enum {_object_dead_bit=2};
struct data_iterator {long datum_index;int next;};
static struct player_datum people[5];static struct unit_datum units[5];static long handles[]={0x10000,0x20001,0x30002,0x40003,0x50004};
static void *player_data;static byte jump;
struct gamepad_state {byte buttons[8];};static struct gamepad_state pad;
static struct gamepad_state const *input_get_gamepad_state(short c){return &pad;}
static byte input_abstraction_port_accept(short c){return jump;}
static struct unit_datum *unit_try_and_get(long index){return index>=10&&index<15?units+index-10:NULL;}
static void data_iterator_new(struct data_iterator *i,void *p){i->next=0;}
static struct player_datum *data_iterator_next(struct data_iterator *i){if(i->next==5)return NULL;i->datum_index=handles[i->next];return people+i->next++;}
#include "mouse.inc"
#include "under_test.inc"
int main(int argc,char **argv){char const *case_name=argc>1?argv[1]:"";struct platform_input_state input={{0},FALSE};
 for(int i=0;i<5;i++)people[i].unit_index=10+i;people[0].unit_index=NONE;
 CASE("order") {
  CHECK(cycle_living_player(handles[0],NONE,1)==handles[1],"first target");
  CHECK(cycle_living_player(handles[0],handles[1],1)==handles[2],"next target");
  CHECK(cycle_living_player(handles[0],handles[1],-1)==handles[4],"previous wraps");
  CHECK(cycle_living_player(handles[0],handles[4],1)==handles[1],"next wraps");
  CHECK(cycle_living_player(handles[0],0x60002,-1)==handles[1],"reused slot order");
 } else CASE("invalid") {
  people[1].quit_out_of_game=TRUE;units[2].object.damage_flags=4;people[3].unit_index=99;
  CHECK(cycle_living_player(handles[0],NONE,1)==handles[4],"quit/dead/stale followed");
  people[4].unit_index=NONE;CHECK(cycle_living_player(handles[0],NONE,-1)==NONE,"empty session");
 } else CASE("edges") {
  input.mouse_buttons[1]=1;mouse_spectate_update(&input,FALSE);CHECK(!direction_pressed(0,TRUE),"held fire on death");
  CHECK(!direction_pressed(0,FALSE),"held click repeated");input.mouse_buttons[1]=0;mouse_spectate_update(&input,FALSE);direction_pressed(0,FALSE);
  input.mouse_buttons[1]=1;mouse_spectate_update(&input,FALSE);CHECK(direction_pressed(0,FALSE)==1,"left click not next");
  input.mouse_buttons[1]=0;input.mouse_buttons[3]=1;mouse_spectate_update(&input,FALSE);CHECK(direction_pressed(0,FALSE)==-1,"right click not previous");
  CHECK(!direction_pressed(0,FALSE),"repeated frame cycles");CHECK(!direction_pressed(1,FALSE),"mouse drove second controller");
 } else CASE("menus") {
  input.mouse_buttons[1]=1;mouse_spectate_update(&input,TRUE);CHECK(!halo_spectate_mouse_buttons(0),"menu click leaked");
  mouse_spectate_update(&input,FALSE);CHECK(!halo_spectate_mouse_buttons(0),"held click after close");
  input.mouse_buttons[1]=0;mouse_spectate_update(&input,FALSE);input.mouse_buttons[1]=1;mouse_spectate_update(&input,FALSE);
  CHECK(halo_spectate_mouse_buttons(0)==1,"release did not clear suppression");
 } else return 2;return 0;}
