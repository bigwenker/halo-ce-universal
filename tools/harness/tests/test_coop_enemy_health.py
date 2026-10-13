"""Host-only enemy health, using the production policy and damage dispatch."""
import re
import sys
from pathlib import Path
import xml.etree.ElementTree as ET

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run


def generated(fault=None):
    source = re.sub(r'^#include.*\n', '', read('port/linux/game/coop_damage.c'), flags=re.M)
    if fault:
        source = mutated(source, *fault)
    return (('under_test.inc', source),)


@pytest.mark.parametrize('case', ['multipliers', 'authority', 'targets', 'owners', 'invalid'])
def test_case(case):
    status, output = run(build('coop_enemy_health', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('return 100.f / percent;', 'return percent / 100.f;'), 'multipliers'),
    (('game_connection() != _game_connection_network_server', 'FALSE'), 'authority'),
    (('|| !network_coop_active()', ''), 'authority'),
    (('unit->unit.player_index != NONE', 'FALSE'), 'targets'),
    (('!game_team_is_enemy(player->team_index, unit->object.owner_team_index)', 'FALSE'), 'targets'),
    (('player->quit_out_of_game', 'FALSE'), 'owners'),
])
def test_negative_control(fault, case):
    status, output = run(build('coop_enemy_health', generated(fault)), case)
    assert status == CHECK_FAILED, output


def test_menu():
    import port_settings as ps
    rows = ps.COOP_SETUP_SCREENS[0][3]
    names = [row[0] for row in rows]
    assert names[names.index('coop_extra_enemies') + 1] == 'coop_enemy_health'
    row = rows[names.index('coop_enemy_health')]
    assert row[2] == ['DEFAULT', '1.25X', '1.5X', '2X', '3X']
    assert len(row[3]) == 5
    assert len(rows) <= 9  # Last row ends at help's y=341, including both amount rows.
    source = read('port/linux/game/menu_functions.c')
    option = source.split('{ "coop_enemy_health_spinner"', 1)[1].split('\n\t{', 1)[0]
    assert '"network.coop_enemy_health"' in option
    assert '"100", "125", "150", "200", "300"' in option
    config = read('port/linux/src/port_config.c')
    assert '"network.coop_enemy_health", _config_integer, "100", "HALO_NET_COOP_ENEMY_HEALTH"' in config
    xml_name = ps.MT.replace('/', '.') + '.server_settings.xml'
    assert read('port/assets/menus/ce/' + xml_name) == '\n'.join(ps.multiplayer_files()[xml_name])
    xml = ET.fromstring(read('port/assets/menus/ce/' + xml_name))
    path = ps.MT + '/server_settings/coop_options/var_coop_enemy_health'
    assert [e.get('text') for e in xml.find(f"strings[@name='{path}']")] == row[2]
