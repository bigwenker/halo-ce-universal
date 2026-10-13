#include "harness.h"
#include <math.h>

enum { _game_connection_local, _game_connection_network_client, _game_connection_network_server };
struct damage_data { long owner_player_index; };
struct player_datum { boolean quit_out_of_game; short team_index; };
struct unit_datum { struct { short owner_team_index; } object; struct { long player_index; } unit; };
static struct player_datum player = { FALSE, 0 };
static struct unit_datum enemy = {{ 1 }, { NONE }};
static long player_handle = 0x10000, unit_handle = 0x20001, percent = 100;
static int role = _game_connection_network_server;
static boolean coop = TRUE;
static int game_connection(void) { return role; }
static boolean network_coop_active(void) { return coop; }
static long config_integer(char const *name) { return percent; }
static struct player_datum *player_try_and_get(long index) { return index == player_handle ? &player : NULL; }
static struct unit_datum *unit_try_and_get(long index) { return index == unit_handle ? &enemy : NULL; }
static boolean game_team_is_enemy(short a, short b) { return a != b; }
#include "under_test.inc"

int main(int argc, char **argv)
{
    char const *case_name = argc > 1 ? argv[1] : "";
    struct damage_data damage = { player_handle };
    long choices[] = { 100, 125, 150, 200, 300 };
    CASE("multipliers") {
        for (int i = 0; i < 5; i++) {
            percent = choices[i];
            real scale = coop_enemy_damage_scale(&damage, unit_handle);
            CHECK(fabsf(scale * percent - 100.f) < .00002f, "wrong inverse at %ld", percent);
            CHECK(fabsf(120.f * scale - 12000.f / percent) < .00002f, "wrong damage at %ld", percent);
        }
    } else CASE("authority") {
        percent = 300;
        for (role = 0; role <= 2; role++) for (coop = 0; coop <= 1; coop++) {
            real expected = role == _game_connection_network_server && coop ? 1.f/3.f : 1.f;
            CHECK(coop_enemy_damage_scale(&damage, unit_handle) == expected, "wrong mode/authority");
        }
    } else CASE("targets") {
        percent = 200;
        enemy.unit.player_index = player_handle;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "player target changed");
        enemy.unit.player_index = 0x30000;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "stale player target changed");
        enemy.unit.player_index = NONE;
        enemy.object.owner_team_index = player.team_index;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "ally changed");
        enemy.object.owner_team_index = NONE;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "unassigned target changed");
        enemy.object.owner_team_index = 1;
        player.team_index = NONE;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "unassigned owner changed");
        player.team_index = 0;
        CHECK(coop_enemy_damage_scale(&damage, NONE) == 1.f, "scenery/missing target changed");
        CHECK(coop_enemy_damage_scale(&damage, unit_handle + 0x10000) == 1.f, "stale target changed");
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == .5f, "enemy not scaled");
    } else CASE("owners") {
        percent = 200;
        damage.owner_player_index = NONE;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "AI/environment damage changed");
        damage.owner_player_index = player_handle + 0x10000;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "stale owner changed");
        damage.owner_player_index = player_handle;
        player.quit_out_of_game = TRUE;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "departed owner changed");
        player.quit_out_of_game = FALSE;
        CHECK(coop_enemy_damage_scale(&damage, unit_handle) == .5f, "valid owner not scaled");
    } else CASE("invalid") {
        long invalid[] = { -2147483647L-1, -1, 0, 1, 99, 101, 124, 126, 149, 151, 199, 201, 299, 301, 2147483647L };
        for (int i = 0; i < (int)(sizeof(invalid)/sizeof(invalid[0])); i++) {
            percent = invalid[i];
            CHECK(coop_enemy_damage_scale(&damage, unit_handle) == 1.f, "invalid value not Default");
        }
    } else return 2;
    return 0;
}
