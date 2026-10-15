/* Cosmetic fork extension, carried only on negotiated fork streams. Upstream
   peers receive the existing nearest palette color, with no new core fields. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_engine.h"
#include "game/players.h"
#include "memory/data.h"
#include "interface/player_ui.h"
#include "main/main.h"
#include "objects/objects.h"
#include "objects/object_definitions.h"
#include "network_coop.h"
#include "network_distributed.h"
#include "network_extensions.h"
#include "network_profile_rgb.h"
#include "player_profile_rgb.h"
#include <string.h>

enum { RGB_MESSAGE = 251, RGB_CHUNK = HALO_PORT_MAXIMUM_NETWORK_PLAYERS };
struct rgb_entry { byte player, present; word reserved; unsigned long identity, rgb; };
static struct { long handle, unit; unsigned long rgb; boolean present; } rgb_players[HALO_PORT_MAXIMUM_NETWORK_PLAYERS];
static struct { long profile; unsigned long rgb; boolean present; } local_colors[MAXIMUM_LOCAL_PLAYERS];
static unsigned long sent;
static boolean refresh_needed=TRUE, host_ready, machine_ready[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
static short probes;
boolean network_distributed_client_send_reliably(void *, word);
short network_distributed_server_machines(long *, short);

static unsigned long rgb_identity(struct player_datum const *p)
{
	unsigned long hash = 2166136261UL;
	short i;
	for (i=0;i<12;i++) hash = (hash ^ p->network_player_data.name[i])*16777619UL;
	return (hash ^ (byte)p->network_player_data.machine_index)*16777619UL ^ (byte)p->network_player_data.controller_index;
}

void network_profile_rgb_changed(void) { refresh_needed=TRUE; sent = 0; }
void network_profile_rgb_machine_joined(long machine)
{
	if (machine>=0 && machine<NUMBEROF(machine_ready)) machine_ready[machine]=FALSE;
}
void network_profile_rgb_reset(void)
{
	short i;
	memset(rgb_players,0,sizeof(rgb_players));
	memset(local_colors,0,sizeof(local_colors));
	memset(machine_ready,0,sizeof(machine_ready)); host_ready=FALSE; probes=0;
	for(i=0;i<NUMBEROF(rgb_players);i++) { rgb_players[i].handle=NONE; rgb_players[i].unit=NONE; }
	for(i=0;i<NUMBEROF(local_colors);i++) local_colors[i].profile=NONE;
	network_profile_rgb_changed();
}

static void rgb_refresh(void)
{
	short i;
	for(i=0;i<MAXIMUM_LOCAL_PLAYERS;i++)
	{
		long profile=player_ui_get_active_player_profile_index(i);
		if (!refresh_needed && local_colors[i].profile==profile) continue;
		local_colors[i].profile = profile;
		local_colors[i].present = profile_rgb_read(local_colors[i].profile,&local_colors[i].rgb);
	}
	refresh_needed=FALSE;
}

boolean network_profile_rgb_color(long index, real_rgb_color *color)
{
	struct player_datum *p = player_try_and_get(index);
	unsigned long rgb;
	long slot;
	if (!p || p->quit_out_of_game) return FALSE;
	if (p->local_player_index >= 0 && p->local_player_index < MAXIMUM_LOCAL_PLAYERS)
	{
		rgb_refresh();
		if (!local_colors[p->local_player_index].present) return FALSE;
		rgb = local_colors[p->local_player_index].rgb;
	}
	else
	{
		slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
		if (slot >= NUMBEROF(rgb_players) || rgb_players[slot].handle != index || !rgb_players[slot].present) return FALSE;
		rgb = rgb_players[slot].rgb;
	}
	color->red = ((rgb>>16)&255)/255.f;
	color->green = ((rgb>>8)&255)/255.f;
	color->blue = (rgb&255)/255.f;
	return TRUE;
}

static void rgb_apply(long index, struct player_datum *p)
{
	struct object_datum *object;
	struct object_definition *definition;
	real_rgb_color color;
	short i;
	long slot = DATUM_INDEX_TO_ABSOLUTE_INDEX(index);
	if (slot >= NUMBEROF(rgb_players) || p->quit_out_of_game || p->unit_index == NONE ||
		(game_engine_has_teams() && !network_coop_active()) || !network_profile_rgb_color(index,&color)) return;
	object = object_try_and_get(p->unit_index);
	if (!object) return;
	definition = object_definition_get(object->definition_index);
	/* Undefined channels inherit placement colors too. The multiplayer
	Spartan uses one of these; only explicit function-driven channels (the
	shield glow, for example) must retain their original values. */
	for(i=0;i<NUMBER_OF_OBJECT_CHANGE_COLORS;i++)
	{
		if (i<definition->object.change_colors.count)
		{
			struct object_change_color_definition *change = TAG_BLOCK_GET_ELEMENT(&definition->object.change_colors,i,struct object_change_color_definition);
			if (change->scaled_by || change->darken_by) continue;
		}
		object->object.base_change_colors[i] = color;
		object->object.outgoing_change_colors[i] = color;
	}
	rgb_players[slot].unit = p->unit_index;
}

void network_profile_rgb_update(void)
{
	struct data_iterator it;
	struct player_datum *p;
	struct { struct distributed_message_header header; struct rgb_entry entries[RGB_CHUNK]; } message;
	unsigned long now = system_milliseconds();
	boolean host = game_connection() == _game_connection_network_server;
	boolean send;
	if (!game_in_progress() || main_menu_is_active()) return;
	rgb_refresh();
	send = !sent || now-sent >= 2000;
	if (send) sent = now ? now : 1;
	memset(&message,0,sizeof(message));
	message.header.type=RGB_MESSAGE; message.header.game_time=game_time_get();
	data_iterator_new(&it,player_data);
	while((p=data_iterator_next(&it)) != NULL)
	{
		long slot=DATUM_INDEX_TO_ABSOLUTE_INDEX(it.datum_index);
		struct rgb_entry *entry;
		if (slot>=NUMBEROF(rgb_players) || p->quit_out_of_game) continue;
		if (p->local_player_index>=0 && p->local_player_index<MAXIMUM_LOCAL_PLAYERS)
		{
			short local=p->local_player_index;
			if (rgb_players[slot].handle!=it.datum_index || rgb_players[slot].rgb!=local_colors[local].rgb ||
				rgb_players[slot].present!=local_colors[local].present) rgb_players[slot].unit=NONE;
			rgb_players[slot].handle=it.datum_index; rgb_players[slot].present=local_colors[local].present;
			rgb_players[slot].rgb=local_colors[local].rgb;
		}
		rgb_apply(it.datum_index,p);
		if (!send || (!host && p->local_player_index==NONE)) continue;
		entry=&message.entries[message.header.count++];
		entry->player=(byte)slot; entry->identity=rgb_identity(p);
		entry->present=rgb_players[slot].handle==it.datum_index && rgb_players[slot].present;
		entry->rgb=entry->present ? rgb_players[slot].rgb : 0;
		if (message.header.count==RGB_CHUNK) break;
	}
	if (!message.header.count || !send) return;
	if (host)
	{
		long machines[HALO_PORT_MAXIMUM_NETWORK_MACHINES];
		short i,count=network_distributed_server_machines(machines,NUMBEROF(machines));
		for(i=0;i<count;i++)
			if (machines[i]>=0 && machines[i]<NUMBEROF(machine_ready) && machine_ready[machines[i]])
				distributed_send_to_machine_reliably(machines[i],&message,RGB_MESSAGE,message.header.count,
					sizeof(message.header)+message.header.count*sizeof(struct rgb_entry));
	}
	else if (game_connection()==_game_connection_network_client && network_extensions_allows(NONE,RGB_MESSAGE) &&
		(host_ready || probes<3))
	{
		word size=sizeof(message.header)+message.header.count*sizeof(struct rgb_entry);
		build_message_header(&message.header.header,size,2,0);
		if (network_distributed_client_send_reliably(&message,size) && !host_ready) probes++;
	}
}

void network_profile_rgb_handle(long machine, word const *data, word size, boolean reliable)
{
	struct distributed_message_header h;
	short i;
	boolean host=game_connection()==_game_connection_network_server;
	if (!reliable || size<sizeof(h) || !game_in_progress() || main_menu_is_active()) return;
	memcpy(&h,data,sizeof(h));
	if (GET_MESSAGE_TYPE(h.header)!=2 || h.type!=RGB_MESSAGE || h.count>RGB_CHUNK ||
		size!=sizeof(h)+h.count*sizeof(struct rgb_entry) ||
		(host ? machine==NONE : machine!=NONE || game_connection()!=_game_connection_network_client)) return;
	for(i=0;i<h.count;i++)
	{
		struct rgb_entry e;
		struct player_datum *p;
		long index;
		memcpy(&e,(byte const *)data+sizeof(h)+i*sizeof(e),sizeof(e));
		index=distributed_player_from_byte(e.player); p=player_try_and_get(index);
		if (e.player>=NUMBEROF(rgb_players) || !p || p->quit_out_of_game || e.present>1 || e.reserved ||
			(e.rgb&0xFF000000UL) || e.identity!=rgb_identity(p) ||
			(host && p->network_player_data.machine_index!=machine)) continue;
		if (p->local_player_index!=NONE) continue;
		/* A request from an authenticated player is also the optional RGB
		capability probe. Older forks ignore at most three probes; the host
		never sends them RGB snapshots without this opt-in. */
		if (host && machine>=0 && machine<NUMBEROF(machine_ready))
		{
			if (!machine_ready[machine]) sent=0;
			machine_ready[machine]=TRUE;
		}
		else if (!host) host_ready=TRUE;
		if (rgb_players[e.player].handle!=index || rgb_players[e.player].rgb!=e.rgb || rgb_players[e.player].present!=e.present)
			rgb_players[e.player].unit=NONE;
		rgb_players[e.player].handle=index; rgb_players[e.player].rgb=e.rgb; rgb_players[e.player].present=e.present;
	}
}
