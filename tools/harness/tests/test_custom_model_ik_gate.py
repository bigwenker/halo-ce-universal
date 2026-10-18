"""Rendering observes the existing grenade-release blend without advancing it."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import read, function, build, run, constant, mutated, CHECK_FAILED


def executable(fault=False):
    source = read('source/units/units.c')
    code = function(source, 'unit_grenade_throw_ik_release')
    if fault:
        code = mutated(code, 'if (record) grenade_throw_ik_release_times', 'grenade_throw_ik_release_times')
    definitions = '\n'.join(f'#define {name} {constant(source, name)}' for name in (
        'GRENADE_THROW_BLEND_TICKS', 'GRENADE_THROW_MAXIMUM_TRACKED_UNITS'))
    return build('custom_model_ik_gate', (('gate.inc', definitions + '\n' + code),))


@pytest.mark.parametrize('case', ['readonly', 'record', 'blend', 'disabled'])
def test_case(case):
    status, output = run(executable(), case)
    assert status == 0, output


def test_no_render_timer_writes_negative_control():
    assert run(executable(True), 'readonly')[0] == CHECK_FAILED
