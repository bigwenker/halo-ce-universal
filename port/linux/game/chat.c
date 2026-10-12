/* Match chat shares the terminal's editor, but owns its input context and
   bottom-right display. It never forwards text from another player to HS. */
#include "cseries.h"
#include "network_social.h"
#include "halo_keyboard.h"
#include "game/game.h"
#include "game/players.h"
#include "memory/data.h"
#include "main/main.h"
#include "main/console.h"
#include "interface/terminal.h"
#include "interface/interface.h"
#include "hs/hs.h"
#include "rasterizer.h"
#include "render.h"
#include "draw_string.h"
#include "font_group.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define CHAT_HISTORY 6
#define CHAT_CANDIDATES 256
static boolean chat_active;
static struct terminal_gets_state chat_input;
static struct { char text[240]; unsigned long at; } chat_history[CHAT_HISTORY];
static unsigned chat_lines;
/* Save the original edit and token range so repeated Tab cycles matches
   without consuming trailing text or turning the first match into a prefix. */
static struct {
    char original[256], last[256], names[CHAT_CANDIDATES][128];
    short start, end, count, next;
} chat_completion;

boolean chat_is_active(void) { return chat_active; }
void chat_close(void)
{
    if (chat_active) terminal_gets_end(&chat_input);
    chat_active = FALSE;
    chat_completion.count = 0;
}
void chat_clear(void)
{
    chat_close(); memset(chat_history,0,sizeof(chat_history)); chat_lines=0;
}
void chat_note_line(char const *text)
{
    unsigned slot = chat_lines++ % CHAT_HISTORY;
    snprintf(chat_history[slot].text,sizeof(chat_history[slot].text),"%s",text);
    chat_history[slot].at=system_milliseconds();
}
static boolean chat_prefix(char const *text, char const *prefix)
{
    while (*prefix) {
        if (!*text || tolower((unsigned char)*text++) != tolower((unsigned char)*prefix++)) return FALSE;
    }
    return TRUE;
}
static void chat_candidate(char const *text, char const *prefix)
{
    if (chat_completion.count >= CHAT_CANDIDATES || !chat_prefix(text,prefix) || strlen(text)>=128) return;
    snprintf(chat_completion.names[chat_completion.count++],128,"%s",text);
}
static void chat_complete(boolean backwards)
{
    short cursor = chat_input.edit.insertion_point_index;
    short length = (short)strlen(chat_input.result);
    if (!chat_completion.count || strcmp(chat_input.result,chat_completion.last) || cursor != chat_completion.end) {
        char prefix[256];
        short start = PIN(cursor,0,length), end = start;
        struct data_iterator it;
        struct player_datum *player;
        chat_completion.count=chat_completion.next=0;
        while (start>0 && chat_input.result[start-1]!=' ') start--;
        while (end<length && chat_input.result[end]!=' ') end++;
        if (!start && chat_input.result[0]=='/') start=1;
        if (cursor<start) return;
        snprintf(prefix,sizeof(prefix),"%.*s",cursor-start,chat_input.result+start);
        strcpy(chat_completion.original,chat_input.result);
        chat_completion.start=start;chat_completion.end=end;
        if (start==1 && chat_input.result[0]=='/') {
            char const *matches[CHAT_CANDIDATES];
            short count=hs_tokens_enumerate(prefix,NONE,matches,CHAT_CANDIDATES),i;
            for(i=0;i<count;i++) chat_candidate(matches[i],prefix);
        } else if (player_data) {
            data_iterator_new(&it,player_data);
            while((player=data_iterator_next(&it))!=NULL) {
                char name[64];
                unsigned n=0,i;
                if(player->quit_out_of_game) continue;
                for(i=0;i<12 && player->name[i];i++) {
                    unsigned c=player->name[i];
                    if(c<128) name[n++]=(char)c;
                    else if(c<2048) {name[n++]=(char)(0xc0|(c>>6));name[n++]=(char)(0x80|(c&63));}
                    else {name[n++]=(char)(0xe0|(c>>12));name[n++]=(char)(0x80|((c>>6)&63));name[n++]=(char)(0x80|(c&63));}
                }
                name[n]=0;chat_candidate(name,prefix);
            }
        }
        /* Original end is needed for the immutable suffix on each cycle. */
        if (chat_completion.count) {
            size_t suffix = strlen(chat_completion.original+end);
            memmove(chat_completion.original+start,chat_completion.original+end,suffix+1);
        }
        if(backwards) chat_completion.next=chat_completion.count-1;
    } else if (backwards) chat_completion.next=(chat_completion.next+chat_completion.count-2)%chat_completion.count;
    if(chat_completion.count) {
        char const *name=chat_completion.names[chat_completion.next];
        size_t n=strlen(name), suffix=strlen(chat_completion.original+chat_completion.start);
        if(chat_completion.start+n+suffix<sizeof(chat_input.result)) {
            memcpy(chat_input.result,chat_completion.original,chat_completion.start);
            memcpy(chat_input.result+chat_completion.start,name,n);
            memcpy(chat_input.result+chat_completion.start+n,chat_completion.original+chat_completion.start,suffix+1);
            chat_input.edit.insertion_point_index=chat_completion.start+(short)n;
            chat_input.edit.selection_start_index=NONE;
            chat_completion.end=chat_input.edit.insertion_point_index;
            strcpy(chat_completion.last,chat_input.result);
        }
        chat_completion.next=(chat_completion.next+1)%chat_completion.count;
    }
}
void chat_update(void)
{
    int i;
    boolean requested=halo_chat_requested();
    if(chat_active && (!game_in_progress() || main_menu_is_active() || game_time_get_paused())) chat_close();
    if(!chat_active) {
        if(requested && game_in_progress() && !main_menu_is_active() && !game_time_get_paused() && !console_is_active() && !terminal_gets_active()) {
            memset(&chat_input,0,sizeof(chat_input));
            chat_input.color.alpha=chat_input.color.red=chat_input.color.green=chat_input.color.blue=1;
            strcpy(chat_input.prompt,"> ");
            chat_active=terminal_gets_begin(&chat_input);
        }
        return;
    }
    for(i=0;i<chat_input.key_count;i++) {
        short key=chat_input.keys[i].key_code;
        if(key==_key_escape) {chat_close();break;}
        if(key==_key_return || key==_keypad_enter) {
            char text[sizeof(chat_input.result)];
            strcpy(text,chat_input.result);chat_close();
            if(text[0]=='/' && text[1]) hs_compile_and_evaluate(text+1);
            else if(text[0]) network_social_chat_send(text);
            break;
        }
        if(key==_key_tab) chat_complete(TEST_FLAG(chat_input.keys[i].modifier_flags,0 /* shift */));
        else chat_completion.count=0;
    }
    chat_input.key_count=0;
}
/* Scroll only the displayed copy. The editor retains the complete command
   and its cursor/selection, including when moving back through a long line. */
short chat_input_display_start(char *text, short cursor, short width)
{
    rectangle2d bounds, measured, end;
    short start=0;
    char saved;
    cursor=PIN(cursor,0,(short)strlen(text));
    saved=text[cursor];text[cursor]=0;
    bounds.x0=bounds.y0=0;bounds.x1=bounds.y1=30000;
    while(start<cursor) {
        draw_string_compute_bounds(&bounds,text+start,&measured,&end);
        if(end.x0<=width-12) break;
        do { start++; } while(start<cursor && ((unsigned char)text[start]&0xc0)==0x80);
    }
    text[cursor]=saved;
    return start;
}
void chat_draw(void)
{
    long font_index;
    struct font_header *font;
    short height, bottom;
    unsigned age;
    real_argb_color color={1,1,1,1};
    if(!game_in_progress() || main_menu_is_active() || console_is_active()) return;
    font_index=interface_get_tag_index(_interface_font_terminal);
    if(font_index==NONE) return;
    font=font_definition_get(font_index);
    height=font->ascending_height+font->descending_height+font->leading_height;
    bottom=render.camera.window_bounds.y1-20-height;
    for(age=0;age<MIN(chat_lines,CHAT_HISTORY);age++) {
        unsigned slot=(chat_lines-1-age)%CHAT_HISTORY;
        unsigned long elapsed=system_milliseconds()-chat_history[slot].at;
        rectangle2d bounds, measured, end;
        short used;
        if(!chat_active && elapsed>10000) break;
        bounds.x1=render.camera.window_bounds.x1-12;
        bounds.x0=MAX(render.camera.window_bounds.x0+12,bounds.x1-420);
        bounds.y0=0;bounds.y1=3*height;
        draw_string_set_draw_mode(font_index,NONE,0,0,&color);
        draw_string_compute_bounds(&bounds,chat_history[slot].text,&measured,&end);
        used=PIN(end.y1+font->leading_height,height,3*height);
        bounds.y1=bottom;bounds.y0=MAX(render.camera.window_bounds.y0,bottom-used);
        if(bounds.y0>=bounds.y1) break;
        offset_rectangle2d(&bounds,-render.camera.viewport_bounds.x0,-render.camera.viewport_bounds.y0);
        color.alpha=chat_active?1.f:MIN(1.f,(10000-elapsed)/1000.f);
        draw_string_set_draw_mode(font_index,NONE,0,0,&color);
        rasterizer_draw_string(&bounds,NULL,NULL,0,chat_history[slot].text);
        bottom-=used+2;
    }
}
