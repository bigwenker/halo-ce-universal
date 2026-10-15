/* Only selection IDs cross negotiated fork streams; never paths, models,
   textures or gameplay definitions. Missing local assets display Chief. */
#include "cseries.h"
#include "custom_models.h"
#include "halo_custom_models.h"
#include "player_profile_rgb.h"
#include "game/game.h"
#include "game/players.h"
#include "interface/player_ui.h"
#include "memory/data.h"
#include "main/main.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include <string.h>
enum { MODEL_MESSAGE=252, MODEL_CHUNK=16 };
struct model_entry { byte player;byte reserved[3];unsigned long identity;char id[32]; };
static struct {long handle;char id[32];} choices[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
static struct {long profile;char id[32];} local_choices[MAXIMUM_LOCAL_PLAYERS];
static boolean dirty=TRUE;
static unsigned long last_sent;
boolean network_distributed_client_send_reliably(void *,word);
short network_distributed_server_machines(long *,short);
static unsigned long identity(struct player_datum const *p)
{
    unsigned long h=2166136261UL;short i;
    for(i=0;i<12;i++)h=(h^p->network_player_data.name[i])*16777619UL;
    return (h^(byte)p->network_player_data.machine_index)*16777619UL^(byte)p->network_player_data.controller_index;
}
void custom_models_selection_changed(void) {dirty=TRUE;last_sent=0;}
void custom_models_network_reset(void)
{
    short i;memset(choices,0,sizeof(choices));memset(local_choices,0,sizeof(local_choices));
    for(i=0;i<NUMBEROF(local_choices);i++)local_choices[i].profile=NONE;
    custom_models_selection_changed();
}
static void refresh(void)
{
    short i;
    for(i=0;i<MAXIMUM_LOCAL_PLAYERS;i++)
    {
        long profile=profile_rgb_local_profile(i);
        if(dirty || profile!=local_choices[i].profile)
        {local_choices[i].profile=profile;custom_models_profile_read(profile,local_choices[i].id);}
    }
    dirty=FALSE;
}
char const *custom_models_player_id(long index)
{
    struct player_datum *p=player_try_and_get(index);long slot;
    if(!p || p->quit_out_of_game)return "master_chief";
    if(p->local_player_index>=0 && p->local_player_index<MAXIMUM_LOCAL_PLAYERS)
    {refresh();return local_choices[p->local_player_index].id;}
    slot=DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
    return slot<NUMBEROF(choices) && choices[slot].handle==index && *choices[slot].id ? choices[slot].id : "master_chief";
}
static void send_entries(void *message,word count,word size)
{
    if(game_connection()==_game_connection_network_server)
    {
        long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];short i,n=network_distributed_server_machines(machines,NUMBEROF(machines));
        for(i=0;i<n;i++)if(network_extensions_allows(machines[i],MODEL_MESSAGE))
            distributed_send_to_machine_reliably(machines[i],message,MODEL_MESSAGE,count,size);
    }
    else if(network_extensions_allows(NONE,MODEL_MESSAGE))
    {
        struct distributed_message_header *header=message;
        build_message_header(&header->header,size,2,0);
        network_distributed_client_send_reliably(message,size);
    }
}
void custom_models_network_update(void)
{
    struct {struct distributed_message_header header;struct model_entry entries[MODEL_CHUNK];} message;
    struct data_iterator it;struct player_datum *p;
    boolean host=game_connection()==_game_connection_network_server;
    unsigned long now=system_milliseconds();
    if(!game_in_progress() || main_menu_is_active() || (!host && game_connection()!=_game_connection_network_client))return;
    refresh();
    if(last_sent && now-last_sent<2000)return;
    last_sent=now ? now : 1;
    memset(&message,0,sizeof(message));message.header.type=MODEL_MESSAGE;message.header.game_time=game_time_get();
    data_iterator_new(&it,player_data);
    while((p=data_iterator_next(&it))!=NULL)
    {
        struct model_entry *e;long slot=DATUM_INDEX_TO_ABSOLUTE_INDEX(it.datum_index);
        if(slot>=NUMBEROF(choices) || p->quit_out_of_game || (!host && p->local_player_index==NONE))continue;
        custom_models_player_changed(it.datum_index);
        e=&message.entries[message.header.count++];e->player=(byte)slot;e->identity=identity(p);
        strncpy(e->id,custom_models_player_id(it.datum_index),sizeof(e->id)-1);
        if(message.header.count==MODEL_CHUNK)
        {send_entries(&message,message.header.count,sizeof(message));message.header.count=0;memset(message.entries,0,sizeof(message.entries));}
    }
    if(message.header.count)send_entries(&message,message.header.count,sizeof(message.header)+message.header.count*sizeof(message.entries[0]));
}
void custom_models_network_handle(long machine,word const *data,word size,boolean reliable)
{
    struct distributed_message_header h;short i;
    boolean host=game_connection()==_game_connection_network_server;
    if(!reliable || size<sizeof(h) || !game_in_progress() || main_menu_is_active())return;
    memcpy(&h,data,sizeof(h));
    if(GET_MESSAGE_TYPE(h.header)!=2 || h.type!=MODEL_MESSAGE || !h.count || h.count>MODEL_CHUNK ||
        size!=sizeof(h)+h.count*sizeof(struct model_entry) ||
        (host ? machine==NONE : machine!=NONE || game_connection()!=_game_connection_network_client))return;
    for(i=0;i<h.count;i++)
    {
        struct model_entry e;struct player_datum *p;long index;
        memcpy(&e,(byte const *)data+sizeof(h)+i*sizeof(e),sizeof(e));
        if(e.player>=NUMBEROF(choices) || e.reserved[0] || e.reserved[1] || e.reserved[2] ||
            !memchr(e.id,0,sizeof(e.id)) || !*e.id || !halo_custom_model_id_valid(e.id))continue;
        index=distributed_player_from_byte(e.player);p=player_try_and_get(index);
        if(!p || p->quit_out_of_game || p->local_player_index!=NONE || e.identity!=identity(p) ||
            (host && p->network_player_data.machine_index!=machine))continue;
        choices[e.player].handle=index;memcpy(choices[e.player].id,e.id,sizeof(e.id));
        custom_models_player_changed(index);
    }
}
