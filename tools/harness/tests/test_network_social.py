"""Production host admission, co-op death attribution, scoring and damage policy."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, mutated, CHECK_FAILED


def generated(fault=None):
    code = re.sub(r'^#include "[^"\n]+"\n', '', read('port/linux/game/network_social.c'), flags=re.M)
    if fault == 'identity':
        code = mutated(code, 'p->network_player_data.machine_index != machine', 'FALSE')
    elif fault == 'authority':
        code = mutated(code, 'if (!social_host() || machine == NONE', 'if (FALSE || machine == NONE')
    elif fault == 'headshot':
        code = mutated(code, '1 + (headshot != FALSE)', '1 + 0')
    elif fault == 'chain-players':
        code = mutated(code, '!social_present(player_try_and_get(unit->unit.player_index))', 'FALSE')
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['chat', 'admission',  'score',  'chain', 'snapshots', 'rate'])
def test_case(case):
    status, output = run(build('network_social', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault,case', [('identity','admission'), ('authority','admission'),
                                      ('headshot','score'), ('chain-players','chain')])
def test_negative(fault, case):
    status, output = run(build('network_social', generated(fault)), case)
    assert status == CHECK_FAILED, output


def test_integration():
    import port_settings as ps
    rows=ps.COOP_SETUP_SCREENS[0][3]
    chain=next(row for row in rows if row[0]=='coop_chain_damage')
    assert chain[2]==['DEFAULT','75%','50%','25%']
    # Widget instances keep only 31 characters of the leaf name. A longer
    # spinner silently stops matching its config binding at runtime.
    for key, *_ in rows:
        assert len(key+'_spinner')<=31
    # Eight rows fit above the buttons, even before runtime compaction.
    assert 73+(len(rows)-1)*30+28<414
    assert 'network_social_note_death(unit_index, damage_data);' in read('source/units/units.c')
    flags=read('port/linux/game/network_damage.c').split('#define REPORT_DAMAGE_FLAGS',1)[1].split('\n\n',1)[0]
    assert '_damage_chain_reaction_bit' not in flags and '_damage_headshot_bit' not in flags
    assert 'FLAG(_effect_chain_reaction_bit)' in read('source/effects/effects.c')
    assert 'item_accelerate_from_damage' in read('source/objects/damage.c')
