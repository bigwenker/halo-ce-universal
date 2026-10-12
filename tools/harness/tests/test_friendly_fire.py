"""Friendly-fire policy and the production shield/body/parent damage dispatch."""
import re
import sys
from pathlib import Path
import xml.etree.ElementTree as ET
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, read, run, mutated, CHECK_FAILED


def generated(fault=None):
    header = read('source/game/game_engine.h')
    policy = function(read('source/game/game_engine.c'), 'game_engine_friendly_damage')
    damage = function(read('source/objects/damage.c'), 'object_cause_damage')
    block = damage[damage.index('friendly_damage_scale *= network_social_chain_damage_scale'):damage.index('if (!material_effect_recorded &&')]
    if fault == 'no-reduction':
        policy = mutated(policy, '*damage_scale = 0.5f;', '*damage_scale = 1.f;')
    if fault == 'double-reduction':
        block = mutated(block, 'object_total_damage, friendly_damage_scale == 1.f', 'object_total_damage * friendly_damage_scale, friendly_damage_scale == 1.f')
    if fault == 'instant-kill':
        block = mutated(block, ' && friendly_damage_scale == 1.f', '')
    if fault == 'parent-reduction':
        block = mutated(block, 'object_total_damage / friendly_damage_scale;', 'object_total_damage;')
    return (('config.inc', enum_with(header, '_friendly_fire_on') + enum_with(header, '_friendly_damage_all')),
            ('policy.inc', policy), ('damage.inc', block))


@pytest.mark.parametrize('case', ['policy', 'damage', 'parent', 'instant', 'combined'])
def test_case(case):
    status, output = run(build('friendly_fire', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('no-reduction','policy'), ('double-reduction','damage'),
                                       ('instant-kill','instant'), ('parent-reduction','parent')])
def test_negative(fault, case):
    status, output = run(build('friendly_fire', generated(fault)), case)
    assert status == CHECK_FAILED, output


def test_menus():
    import port_settings as ps
    expected = ['ON', '50% LESS', '75% LESS', 'OFF']
    assert ps.FRIENDLY_FIRE_CHOICES == expected
    source = read('port/linux/game/menu_functions.c')
    competitive = source.split('{ "friendly_fire_spinner"', 1)[1].split('},', 1)[0]
    assert re.findall(r'_friendly_fire_\w+', competitive) == [
        '_friendly_fire_on', '_friendly_fire_half_damage', '_friendly_fire_quarter_damage', '_friendly_fire_off']
    coop = source.split('{ "coop_friendly_fire_spinner"', 1)[1].split('{ "coop_extra_enemies_spinner"', 1)[0]
    assert '"on", "half_damage", "quarter_damage", "off"' in coop
    xml = ET.fromstring(read('port/assets/menus/ce/strings.xml'))
    assert [e.get('text') for e in xml.find(f"strings[@name='{ps.TEAMPLAY_EDIT}/var_friendly_fire']")] == expected
    row = ps.COOP_SETUP_SCREENS[0][3][0]
    assert row[2] == expected and len(row[3]) == 4
    checked = read('port/assets/menus/ce/' + ps.MT.replace('/', '.') + '.server_settings.xml')
    assert checked == '\n'.join(ps.multiplayer_files()[ps.MT.replace('/', '.') + '.server_settings.xml'])
