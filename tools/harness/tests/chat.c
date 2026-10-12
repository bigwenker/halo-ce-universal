#include "harness.h"
#include <wchar.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define PIN(n,a,b) ((n)<(a)?(a):(n)>(b)?(b):(n))
typedef struct {real alpha,red,green,blue;} real_argb_color;
typedef struct {short x0,y0,x1,y1;} rectangle2d;
static void draw_string_compute_bounds(rectangle2d const *bounds,char const *text,rectangle2d *measured,rectangle2d *end){end->x0=0;while(*text)end->x0+=*text++=='i'?4:9;}
#include "config.inc"
struct player_datum {wchar_t name[13];boolean quit_out_of_game;};
static struct player_datum players[3];static void *player_data=(void*)1;
struct data_iterator {int next;};
static void data_iterator_new(struct data_iterator *it,void *data){it->next=0;}
static void *data_iterator_next(struct data_iterator *it){return it->next<3?&players[it->next++]:NULL;}
static boolean request,playing=TRUE,menu,paused,console,terminal;
static int sent,commands,began,ended;static char sent_text[256],command[256];
static unsigned long system_milliseconds(void){return 1000;}
static boolean halo_chat_requested(void){boolean r=request;request=FALSE;return r;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static boolean game_time_get_paused(void){return paused;}
static boolean console_is_active(void){return console;}
static boolean terminal_gets_active(void){return terminal;}
static boolean terminal_gets_begin(struct terminal_gets_state *state){if(terminal)return FALSE;terminal=TRUE;began++;state->edit.buffer=state->result;state->edit.maximum_length=255;state->edit.insertion_point_index=0;state->edit.selection_start_index=NONE;return TRUE;}
static void terminal_gets_end(struct terminal_gets_state *state){terminal=FALSE;ended++;}
static boolean hs_compile_and_evaluate(char const *text){commands++;snprintf(command,sizeof(command),"%s",text);return TRUE;}
static boolean network_social_chat_send(char const *text){sent++;snprintf(sent_text,sizeof(sent_text),"%s",text);return TRUE;}
static short hs_tokens_enumerate(char const *prefix,long flags,char const **results,short max){static char const *words[]={"kill","suicide","viewmodel_fov","viewmodel_vis","fov_desired"};short count=0;for(int i=0;i<5;i++)if(count<max&&!strncasecmp(words[i],prefix,strlen(prefix)))results[count++]=words[i];return count;}
#include "under_test.inc"
static void open_chat(void){request=TRUE;chat_update();}
static void text(char const *s,int cursor){strcpy(chat_input.result,s);chat_input.edit.insertion_point_index=cursor;chat_completion.count=0;}
static void key(short key,boolean shift){chat_input.key_count=1;chat_input.keys[0].key_code=key;chat_input.keys[0].modifier_flags=shift;chat_update();}
int main(int argc,char **argv){CHECK(argc==2,"case");char const *case_name=argv[1];wcscpy(players[0].name,L"Host");wcscpy(players[1].name,L"Quixote");wcscpy(players[2].name,L"Quincy");
 CASE("ownership"){console=TRUE;open_chat();CHECK(!chat_is_active()&&terminal==FALSE,"console ownership stolen");console=FALSE;menu=TRUE;open_chat();CHECK(!chat_is_active(),"opened in menu");menu=FALSE;open_chat();CHECK(chat_is_active()&&terminal,"not opened");key(_key_escape,FALSE);CHECK(!chat_is_active()&&!terminal&&sent==0&&commands==0,"escape sent or leaked ownership");open_chat();paused=TRUE;chat_update();CHECK(!chat_is_active(),"pause retained editor");}
 else CASE("send"){open_chat();text("Hey guys",8);key(_key_return,FALSE);CHECK(sent==1&&!strcmp(sent_text,"Hey guys")&&!chat_is_active(),"Enter chat");open_chat();text("/kill",5);key(_key_return,FALSE);CHECK(commands==1&&!strcmp(command,"kill")&&sent==1,"slash command sent publicly");open_chat();text("/viewmodel_fov 90",17);key(_keypad_enter,FALSE);CHECK(commands==2&&!strcmp(command,"viewmodel_fov 90"),"slash setting");}
 else CASE("commands"){open_chat();text("/view",5);key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"/viewmodel_fov"),"command complete");key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"/viewmodel_vis"),"command cycle");}
 else CASE("names"){open_chat();text("hello quix",10);key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"hello Quixote"),"case insensitive name");players[1].quit_out_of_game=TRUE;text("quix",4);key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"quix"),"completed retired player");}
 else CASE("cycle"){open_chat();text("qui",3);key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"Quixote"),"first");key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"Quincy"),"second");key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"Quixote"),"wrap");key(_key_tab,TRUE);CHECK(!strcmp(chat_input.result,"Quincy"),"reverse");}
 else CASE("suffix"){open_chat();text("hello qui goodbye",9);key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"hello Quixote goodbye"),"suffix lost");CHECK(chat_input.edit.insertion_point_index==13,"cursor after suffix");key(_key_tab,FALSE);CHECK(!strcmp(chat_input.result,"hello Quincy goodbye"),"cycle suffix");}
 else CASE("bounds"){open_chat();char s[256];memset(s,'a',240);strcpy(s+240," qui");text(s,244);key(_key_tab,FALSE);CHECK(strlen(chat_input.result)==248,"bounded replacement");memset(s,'a',249);strcpy(s+249," qui");text(s,253);key(_key_tab,FALSE);CHECK(strlen(chat_input.result)==253,"overflowed line");}
 else CASE("scroll"){char s[256],saved[256];memset(s,'W',254);s[254]=0;strcpy(saved,s);short start=chat_input_display_start(s,254,420);CHECK(start>0&&(254-start)*9<=408,"long cursor outside input field");CHECK(!strcmp(s,saved),"display truncated the command");CHECK(chat_input_display_start(s,5,420)==0,"cursor near beginning scrolled away");strcpy(s,"iiiiiiiiii");CHECK(chat_input_display_start(s,10,60)==0,"variable-width glyphs ignored");}
 else return 2;
 if(!strcmp(case_name,"ownership"))CHECK(began==2&&ended==2,"terminal ownership lifecycle unbalanced");
 return 0;
}
