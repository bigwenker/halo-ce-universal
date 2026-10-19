"""Needler tag rules, negotiated fallback and both real supercombine checks."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, run, mutated, CHECK_FAILED


def generated(fault=None):
    code = re.sub(r'^#include "[^"\n]+"\n', '', read('port/linux/game/needler_balance.c'), flags=re.M)
    if fault == 'velocity':
        code = mutated(code, '6.f / TICKS_PER_SECOND : needler_balance.initial_velocity', '6.f : needler_balance.initial_velocity')
    if fault == 'radius':
        code = mutated(code, 'enabled ? 0.5f : 1.f', '0.5f')
    if fault == 'threshold':
        code = mutated(code, 'stock_limit + 1 : stock_limit', 'stock_limit : stock_limit')
    if fault == 'mixed':
        code = mutated(code, 'network_extensions_needler_matches()', 'TRUE')
    projectiles = read('source/items/projectiles.c')
    detonate = function(projectiles, 'projectile_detonate')
    detonate = detonate[detonate.index('\tif (TEST_FLAG(definition->projectile.flags,'):detonate.index('\n\tif (first_collision &&')]
    collision = function(projectiles, 'projectile_collision')
    start = collision.index('\t\tif (collision->type == _collision_result_object &&\n\t\t\tTEST_FLAG(definition->projectile.flags, _projectile_super_combining_explosion_bit))')
    collision = collision[start:collision.index('\n\t\tprojectile->object.translational_velocity', start)]
    return (('under_test.inc', code), ('detonate.inc', detonate), ('attach.inc', collision),
            ('reload.inc', function(read('source/items/weapons.c'), 'weapon_magazine_finish_reload')))


@pytest.mark.parametrize('case', ['solo', 'restore', 'mixed', 'upstream-host', 'policy',
                                 'bad-policy', 'missing', 'reload-map', 'combine', 'reload'])
def test_case(case):
    status, output = run(build('needler_balance', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('velocity', 'solo'), ('radius', 'restore'),
                                      ('threshold', 'combine'), ('mixed', 'mixed')])
def test_negative(fault, case):
    status, output = run(build('needler_balance', generated(fault)), case)
    assert status == CHECK_FAILED, output
