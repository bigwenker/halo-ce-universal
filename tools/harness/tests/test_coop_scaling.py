"""Enemy counts use the same per-player growth as pickups, with the existing guards."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, constant, enum_with, function, mutated, read, run


def generated(fault=None):
    source = read('port/linux/game/coop_enemies.c')
    code = '#define MAXIMUM_ACTORS %d\n' % constant(read('source/ai/actors.h'), 'MAXIMUM_ACTORS')
    code += enum_with(source, 'COOP_ENEMIES_LEVEL_ACTORS') + '\n'
    code += enum_with(source, '_coop_enemies_none') + '\n'
    code += read('port/linux/game/coop_scaling.h') + '\n'
    code += '\n'.join(function(source, name) for name in (
        'coop_enemies_host', 'coop_enemies_on', 'coop_enemies_player_count',
        'coop_enemies_new_game', 'coop_enemies_extra_count'))
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['counts', 'guards', 'config'])
def test_case(case):
    status, output = run(build('coop_scaling', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [
    (('(COOP_SCALING_MAXIMUM_GROWTH - 1) * 100', '100000'), 'counts'),
    (('MAXIMUM_ACTORS - COOP_ENEMIES_LEVEL_ACTORS - actor_data->actual_count', 'MAXIMUM_ACTORS'), 'guards'),
])
def test_negative_control(fault, case):
    status, output = run(build('coop_scaling', generated(fault)), case)
    assert status == CHECK_FAILED, output
