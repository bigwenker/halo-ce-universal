/*
VOICE_ROUTE.C (test)

The real rules of port/linux/game/network_voice.c that the host sends each
voice by: who hears whom in the lobby and in each of network.voice_mode's
modes, and the largest packet a quality allows. test_voice_route.py takes
the modes and routes (config.inc) and the code (under_test.inc).
*/

#include "harness.h"

#define PIN(value, low, high) ((value) < (low) ? (low) : (value) > (high) ? (high) : (value))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define csstrcmp strcmp
#define csmemset memset
typedef unsigned short word;

#include "config.inc"
enum { _distributed_message_voice_config = 0, _voice_config_lobby_bit = 0 };
struct distributed_message_header { byte bytes[8]; };
static struct { unsigned long key, told_at; boolean told; } voice_machines[4];
static struct { short mode, kbps; boolean lobby; real proximity; } voice_host_settings;
static struct distributed_voice_config advertised;
static unsigned long voice_heard_at[4];
static boolean voice_heard[4], muted;
static int plays, spatializations;
static real played_gain, played_pan;
static unsigned long system_milliseconds(void) { return 12345; }
static void voice_fill_header(struct distributed_message_header *h, byte type, word size)
{ (void)h; (void)type; (void)size; }
static boolean network_distributed_server_send_to_machine_reliably(long m, void const *message, word size)
{
    (void)m; (void)size;
    memcpy(&advertised,(byte const *)message+sizeof(struct distributed_message_header),sizeof(advertised));
    return TRUE;
}
static boolean voice_machine_valid(long m) { return m>=0 && m<4; }
static boolean voice_audible(long m) { (void)m; return !muted; }
static void voice_proximity_gain(short p, real *gain, real *pan)
{ (void)p; spatializations++; *gain=.2f; *pan=.9f; }
static void voice_audio_play(int m, word sequence, byte const *packet, int length, real gain, real pan)
{ (void)m; (void)sequence; (void)packet; (void)length; plays++; played_gain=gain; played_pan=pan; }
#include "under_test.inc"

/* a listener's route to a speaker in a game: teammates or not, near (both
alive, within the range) or far, or one of them dead */
static short in_game(short mode, boolean teammates, real distance, boolean both_alive)
{
	return voice_route(mode, FALSE, TRUE, teammates, both_alive, distance, 15.0f);
}

int main(int argc, char **argv)
{
	char const *case_name = argc > 1 ? argv[1] : "";
	short mode;
	CASE("all-global")
	{
		byte packet=0;
		for (int teammate=0;teammate<=1;teammate++)
		for (int alive=0;alive<=1;alive++)
		for (int far=0;far<=1;far++)
		{
			short route=in_game(_voice_mode_all_global,teammate,far ? 1000.0f : 1.0f,alive);
			CHECK(route==_voice_route_global,"All filtered/spatialized a player");
			voice_play(1,2,route,0,&packet,1);
			CHECK(played_gain==1.0f && played_pan==0.0f && spatializations==0,"All changed gain/pan");
		}
		CHECK(plays==8,"missing voice playback");
		CHECK(voice_heard[1] && voice_heard_at[1]==system_milliseconds(),"talking indicator not updated");
		muted=TRUE; voice_play(1,2,_voice_route_global,0,&packet,1);
		CHECK(plays==8,"All bypassed local mute");
		return 0;
	}
	CASE("settings")
	{
		char const *names[]={"off","team_proximity","team_enemy_proximity","team_global","team_global_enemy_proximity","all_global"};
		CHECK(NUMBER_OF_VOICE_MODES==6 && _voice_mode_all_global==5,"existing wire mode IDs changed");
		voice_host_settings.kbps=24; voice_host_settings.proximity=15; voice_host_settings.lobby=TRUE;
		voice_machines[1].key=123;
		for (mode=0;mode<NUMBER_OF_VOICE_MODES;mode++)
		{
			CHECK(voice_mode_from_text(names[mode])==mode,"wrong config mode");
			voice_host_settings.mode=mode; voice_host_tell(1);
			CHECK(advertised.mode==(mode==_voice_mode_all_global ? _voice_mode_team_global : mode),"legacy client cannot speak, or existing mode changed");
			CHECK(advertised.key==123 && advertised.kbps==24 && advertised.proximity_tenths==150 && advertised.flags==1,"other voice settings changed");
			CHECK(voice_machines[1].told && voice_machines[1].told_at==system_milliseconds(),"settings not sent");
		}
		CHECK(voice_mode_from_text(NULL)==_voice_mode_team_global_enemy_proximity && voice_mode_from_text("bad")==_voice_mode_team_global_enemy_proximity,"default changed");
		return 0;
	}

	/* the lobby: everyone, or no one, whatever the game's mode */
	CASE("lobby")
	{
		for (mode = 0; mode < NUMBER_OF_VOICE_MODES; mode++)
		{
			CHECK(voice_route(mode, TRUE, TRUE, FALSE, FALSE, 1000.0f, 15.0f) == _voice_route_global,
				"mode %d: the lobby's voice not heard by everyone", mode);
			CHECK(voice_route(mode, TRUE, FALSE, TRUE, TRUE, 0.0f, 15.0f) == _voice_route_none,
				"mode %d: the lobby heard with its voice off", mode);
		}
		return 0;
	}
	CASE("off")
	{
		CHECK(in_game(_voice_mode_off, TRUE, 0.0f, TRUE) == _voice_route_none, "off: a teammate heard");
		CHECK(in_game(_voice_mode_off, FALSE, 0.0f, TRUE) == _voice_route_none, "off: an enemy heard");
		return 0;
	}
	CASE("team-proximity")
	{
		CHECK(in_game(_voice_mode_team_proximity, TRUE, 10.0f, TRUE) == _voice_route_proximity, "a near teammate");
		CHECK(in_game(_voice_mode_team_proximity, TRUE, 20.0f, TRUE) == _voice_route_none, "a far teammate heard");
		CHECK(in_game(_voice_mode_team_proximity, FALSE, 1.0f, TRUE) == _voice_route_none, "a near enemy heard");
		CHECK(in_game(_voice_mode_team_proximity, TRUE, 1.0f, FALSE) == _voice_route_none, "the dead heard near");
		return 0;
	}
	CASE("team-enemy-proximity")
	{
		CHECK(in_game(_voice_mode_team_enemy_proximity, TRUE, 10.0f, TRUE) == _voice_route_proximity,
			"a near teammate");
		CHECK(in_game(_voice_mode_team_enemy_proximity, FALSE, 10.0f, TRUE) == _voice_route_proximity,
			"a near enemy");
		CHECK(in_game(_voice_mode_team_enemy_proximity, TRUE, 20.0f, TRUE) == _voice_route_none,
			"a far teammate heard");
		CHECK(in_game(_voice_mode_team_enemy_proximity, FALSE, 20.0f, TRUE) == _voice_route_none,
			"a far enemy heard");
		return 0;
	}
	CASE("team-global")
	{
		CHECK(in_game(_voice_mode_team_global, TRUE, 1000.0f, FALSE) == _voice_route_global,
			"a teammate anywhere, alive or not");
		CHECK(in_game(_voice_mode_team_global, FALSE, 1.0f, TRUE) == _voice_route_none, "a near enemy heard");
		return 0;
	}
	CASE("team-global-enemy-proximity")
	{
		CHECK(in_game(_voice_mode_team_global_enemy_proximity, TRUE, 1000.0f, FALSE) == _voice_route_global,
			"a teammate anywhere");
		CHECK(in_game(_voice_mode_team_global_enemy_proximity, FALSE, 10.0f, TRUE) == _voice_route_proximity,
			"a near enemy");
		CHECK(in_game(_voice_mode_team_global_enemy_proximity, FALSE, 20.0f, TRUE) == _voice_route_none,
			"a far enemy heard");
		CHECK(in_game(_voice_mode_team_global_enemy_proximity, FALSE, 1.0f, FALSE) == _voice_route_none,
			"a dead enemy heard");
		return 0;
	}
	/* a quality's packets fit, and no more than twice their share */
	CASE("packet-limit")
	{
		short kbps;

		for (kbps = 8; kbps <= 64; kbps++)
		{
			long share = (long)kbps * 1000 / 8 / 50;

			CHECK(voice_packet_limit(kbps) >= share, "%d kbps: its own packets refused", kbps);
			CHECK(voice_packet_limit(kbps) <= 2 * share + 16, "%d kbps: %ld bytes taken", kbps, voice_packet_limit(kbps));
			CHECK(voice_packet_limit(kbps) <= VOICE_MAXIMUM_PACKET, "%d kbps: more than a packet", kbps);
		}
		return 0;
	}
	fprintf(stderr, "unknown case: %s\n", case_name);
	return 2;
}
