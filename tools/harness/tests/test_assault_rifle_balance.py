"""AR spread/damage multipliers, exact restoration and negotiated policy."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, run, mutated, CHECK_FAILED


def generated(fault=None):
    code = re.sub(r'^#include "[^"\n]+"\n', '', read('port/linux/game/assault_rifle_balance.c'), flags=re.M)
    if fault == 'spread':
        code = mutated(code, 'enabled ? 0.75f : 1.f', '1.f')
    if fault == 'damage':
        code = mutated(code, 'enabled ? 1.2f : 1.f', '1.f')
    if fault == 'restore':
        code = mutated(code, 'enabled ? 1.2f : 1.f', '1.2f')
    if fault == 'minimum':
        code = mutated(code, 'assault_rifle_balance.minimum * damage', 'assault_rifle_balance.minimum')
    damage = read('source/objects/damage.c')
    start = damage.index('\t{\n\t\treal random_damage = real_random_range(')
    end = damage.index('\n\t}\n', start) + 4
    damage = 'static real actual_damage(real scale, real multiplier)\n{\nstruct damage_definition *damage_definition = &impact.damage;\nstruct {real scale,multiplier;} value={scale,multiplier}, *damage=&value;\nreal total_damage;\n' + damage[start:end] + '\nreturn total_damage;\n}\n'
    return (('under_test.inc', code), ('damage.inc', damage))


@pytest.mark.parametrize('case', ['solo', 'restore', 'cycles', 'mixed', 'upstream-host',
                                 'policy', 'bad-policy', 'missing', 'reload-map', 'damage'])
def test_case(case):
    status, output = run(build('assault_rifle_balance', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('spread', 'solo'), ('damage', 'damage'),
                                      ('minimum', 'damage'), ('restore', 'restore')])
def test_negative(fault, case):
    status, output = run(build('assault_rifle_balance', generated(fault)), case)
    assert status == CHECK_FAILED, output
