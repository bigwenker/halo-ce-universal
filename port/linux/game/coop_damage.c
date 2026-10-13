/* Co-op's ENEMY HEALTH is an inverse scale on the host's player damage,
   before shields and body spillover. The host's existing unit states and
   damage events carry the result; clients need no setting or new message. */
#include "cseries.h"
#include "game/game.h"
#include "game/game_allegiance.h"
#include "game/players.h"
#include "objects/damage.h"
#include "units/units.h"
#include "network_coop.h"
#include "coop_damage.h"

long config_integer(char const *name);

real coop_enemy_damage_scale(struct damage_data const *damage, long object_index)
{
	struct player_datum *player;
	struct unit_datum *unit;
	long percent;

	if (game_connection() != _game_connection_network_server || !network_coop_active())
		return 1.f;

	percent = config_integer("network.coop_enemy_health");
	if (percent != 125 && percent != 150 && percent != 200 && percent != 300)
		return 1.f;

	player = player_try_and_get(damage->owner_player_index);
	unit = unit_try_and_get(object_index);
	if (!player || player->quit_out_of_game || !unit || unit->unit.player_index != NONE ||
		player->team_index == NONE || unit->object.owner_team_index == NONE ||
		!game_team_is_enemy(player->team_index, unit->object.owner_team_index))
		return 1.f;

	return 100.f / percent;
}
