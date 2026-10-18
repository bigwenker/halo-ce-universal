#include "harness.h"
#include <wchar.h>
#define MIN(a,b) ((a)<(b)?(a):(b))
#define PIN(n,a,b) ((n)<(a)?(a):(n)>(b)?(b):(n))
#define csmemset memset
#define csstrlen strlen
#define csstrncpy strncpy
#define NUMBEROF(a) (sizeof(a)/sizeof((a)[0]))
#include "config.inc"
enum {HALO_KEYBOARD_CHAT, HALO_KEYBOARD_TEAM_CHAT};
struct player_datum {wchar_t name[13];boolean quit_out_of_game;};
static struct player_datum players[3];static void *player_data=(void*)1;
struct data_iterator {int next;};
static void data_iterator_new(struct data_iterator *it,void *data){it->next=0;}
static void *data_iterator_next(struct data_iterator *it){return it->next<3?&players[it->next++]:NULL;}
static char player_name_character_ascii(wchar_t c){return c>=32&&c<=126?(char)c:'?';}
static boolean network=TRUE,playing=TRUE,menu,paused,console;
static short speaker;
static unsigned long actions,chat_actions_held;
static int sent,commands;static short sent_channel;
static char sent_text[64],command[64],clipboard[256];
static struct key_stroke queued;static boolean has_key;
static boolean input_get_key(struct key_stroke *key){if(!has_key)return FALSE;*key=queued;has_key=FALSE;return TRUE;}
static boolean chat_network_game(void){return network;}
static boolean game_in_progress(void){return playing;}
static boolean main_menu_is_active(void){return menu;}
static boolean game_time_get_paused(void){return paused;}
static boolean console_is_active(void){return console;}
static short chat_speaker(void){return speaker;}
static unsigned long halo_keyboard_actions(int n){return actions;}
static void chat_send(short channel,char const *text,boolean checked){CHECK(checked,"editor bypassed host policy");sent++;sent_channel=channel;snprintf(sent_text,sizeof(sent_text),"%s",text);}
static void hs_compile_and_evaluate(char const *text){commands++;snprintf(command,sizeof(command),"%s",text);}
static short hs_tokens_enumerate(char const *prefix,long type,char const **matches,short count){
 static char const *words[]={"kill","suicide","viewmodel_fov","viewmodel_vis","fov_desired"};
 for(int i=0;i<5;i++)matches[i]=words[i];return 5;
}
static int platform_clipboard_get(char *out,int size){snprintf(out,size,"%s",clipboard);return clipboard[0]!=0;}
enum {HALO_CHAT_PAD_UP, _chat_config_allowed_bit, _error_log};
static struct {boolean open,drawn,shift,symbols;short row,column;unsigned long held,repeat_at;} chat_keyboard;
static struct {boolean known;} chat_config;
static boolean chat_in_game,chat_off_noticed,allowed=TRUE;
static unsigned long chat_off_noticed_at;
static short chat_line_count,chat_rates[4];
static unsigned long system_milliseconds(void){return 1000;}
static unsigned long halo_chat_pad_buttons(void){return 0;}
static boolean chat_host(void){return FALSE;}
static void chat_host_update_config(void){}
static void chat_host_update_rates(void){}
static boolean chat_shown(void){return TRUE;}
static boolean chat_hidden(void){return menu||paused;}
static unsigned long chat_config_flags(void){return allowed?FLAG(_chat_config_allowed_bit):0;}
static void chat_add_notice(char const *text){}
static void chat_keyboard_update(unsigned long pressed,unsigned long held){}
#define error(...) ((void)0)
#include "under_test.inc"
static void open_chat(void){actions=0;network_chat_update();actions=FLAG(HALO_KEYBOARD_CHAT);network_chat_update();actions=0;}
static void key(short code,byte mods){queued.key_code=code;queued.modifier_flags=mods;queued.ascii_code=0;has_key=TRUE;network_chat_update();}
static void text(char const *s,short cursor){snprintf(chat_input.text,sizeof(chat_input.text),"%s",s);chat_input.cursor=cursor;chat_completion.count=0;}
int main(int argc,char **argv){CHECK(argc==2,"case");char const *case_name=argv[1];
 wcscpy(players[0].name,L"Host");wcscpy(players[1].name,L"Quixote");wcscpy(players[2].name,L"Quincy");
 CASE("ownership"){
  console=TRUE;open_chat();CHECK(!network_chat_typing(),"console ownership stolen");console=FALSE;menu=TRUE;open_chat();CHECK(!network_chat_typing(),"opened in menu");
  menu=FALSE;open_chat();CHECK(network_chat_typing(),"not opened");key(_key_escape,0);CHECK(!network_chat_typing()&&sent==0&&commands==0,"escape sent");
  open_chat();paused=TRUE;network_chat_update();CHECK(!network_chat_typing(),"pause retained editor");paused=FALSE;speaker=NONE;open_chat();CHECK(!network_chat_typing(),"no player");
  speaker=0;network=FALSE;open_chat();CHECK(!network_chat_typing(),"offline editor");
 }
 else CASE("send"){
  open_chat();text("Hey guys",8);key(_key_return,0);CHECK(sent==1&&!strcmp(sent_text,"Hey guys")&&!network_chat_typing(),"Enter chat");
  open_chat();text("/kill",5);key(_key_return,0);CHECK(commands==1&&!strcmp(command,"kill")&&sent==1,"slash command sent publicly");
  open_chat();text("/viewmodel_fov 90",17);key(_keypad_enter,0);CHECK(commands==2&&!strcmp(command,"viewmodel_fov 90"),"slash setting");
 }
 else CASE("commands"){
  open_chat();text("/view",5);key(_key_tab,0);CHECK(!strcmp(chat_input.text,"/viewmodel_fov"),"command complete");key(_key_tab,0);CHECK(!strcmp(chat_input.text,"/viewmodel_vis"),"command cycle");
  text("/mu",3);key(_key_tab,0);CHECK(!strcmp(chat_input.text,"/mute"),"native mute missing");
 }
 else CASE("names"){
  open_chat();text("hello quix",10);key(_key_tab,0);CHECK(!strcmp(chat_input.text,"hello Quixote"),"case insensitive name");
  players[1].quit_out_of_game=TRUE;text("quix",4);key(_key_tab,0);CHECK(!strcmp(chat_input.text,"quix"),"completed retired player");
 }
 else CASE("cycle"){
  open_chat();text("qui",3);key(_key_tab,0);CHECK(!strcmp(chat_input.text,"Quixote"),"first");key(_key_tab,0);CHECK(!strcmp(chat_input.text,"Quincy"),"second");
  key(_key_tab,0);CHECK(!strcmp(chat_input.text,"Quixote"),"wrap");key(_key_tab,1);CHECK(!strcmp(chat_input.text,"Quincy"),"reverse");
 }
 else CASE("suffix"){
  open_chat();text("hello qui goodbye",9);key(_key_tab,0);CHECK(!strcmp(chat_input.text,"hello Quixote goodbye"),"suffix lost");CHECK(chat_input.cursor==13,"cursor after suffix");
  key(_key_tab,0);CHECK(!strcmp(chat_input.text,"hello Quincy goodbye"),"cycle suffix");
 }
 else CASE("bounds"){
  char s[256];open_chat();memset(s,'a',52);strcpy(s+52," qui");text(s,56);key(_key_tab,0);CHECK(strlen(chat_input.text)==60,"bounded replacement");
  memset(s,'a',59);strcpy(s+59," qui");text(s,63);key(_key_tab,0);CHECK(strlen(chat_input.text)==63,"overflowed line");
  queued.ascii_code='z';queued.key_code=_key_z;has_key=TRUE;network_chat_update();CHECK(strlen(chat_input.text)==63,"overflowed insertion");
 }
 else CASE("channels"){
  open_chat();key(_key_tab,0);CHECK(chat_input.channel==_chat_channel_team,"empty Tab");key(_key_tab,1);CHECK(chat_input.channel==_chat_channel_everyone,"reverse empty Tab");
  text("no match",8);key(_key_tab,0);CHECK(chat_input.channel==_chat_channel_everyone,"autocomplete changed recipients");
  key(_key_tab,2);CHECK(chat_input.channel==_chat_channel_team,"Ctrl Tab");key(_key_tab,3);CHECK(chat_input.channel==_chat_channel_everyone,"Ctrl Shift Tab");
  key(_key_return,0);CHECK(sent_channel==_chat_channel_everyone&&!strcmp(sent_text,"no match"),"wrong channel sent");
 }
 else CASE("editing"){
  open_chat();text("hello qui goodbye",9);key(_key_tab,0);key(_key_left_arrow,0);CHECK(!chat_completion.count,"cursor kept stale completion");
  text("abcd",2);key(_key_backspace,0);CHECK(!strcmp(chat_input.text,"acd")&&chat_input.cursor==1,"Backspace suffix");
  key(_key_delete,0);CHECK(!strcmp(chat_input.text,"ad"),"Delete suffix");key(_key_home,0);CHECK(chat_input.cursor==0,"Home");key(_key_end,0);CHECK(chat_input.cursor==2,"End");
 }
 else CASE("paste"){
  open_chat();text("abcd",2);strcpy(clipboard," X\nY\tZ");key(_key_v,2);CHECK(!strcmp(chat_input.text,"ab XYZcd")&&chat_input.cursor==6,"filtered paste/suffix");
  memset(clipboard,'z',255);clipboard[255]=0;key(_key_v,2);CHECK(strlen(chat_input.text)==63&&chat_input.text[62]=='d',"bounded paste lost suffix");
 }
 else CASE("mute"){
  open_chat();text("/mute Quixote 5",15);key(_key_return,0);CHECK(sent==1&&commands==0&&!strcmp(sent_text,"/mute Quixote 5"),"mute bypassed native policy");
  open_chat();text("/unmute Quixote",15);key(_key_return,0);CHECK(sent==2&&commands==0,"unmute executed locally");
  key(_key_escape,0);allowed=FALSE;open_chat();CHECK(!network_chat_typing(),"host chat-off policy bypassed");
 }
 else return 2;return 0;
}
