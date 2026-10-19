"""Host countdowns, late packets, pause, life reuse and optional compatibility."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, run, CHECK_FAILED, mutated

def generated(fault=None):
    code = re.sub(r'^#include.*\n', '', read('port/linux/game/coop_respawn.c'), flags=re.M)
    if fault:
        code = mutated(code, *fault)
    return (('under_test.inc', code),)

@pytest.mark.parametrize('case', ['timing', 'pause', 'client', 'packets', 'reuse', 'legacy'])
def test_case(case):
    status, output = run(build('coop_respawn', generated()), case)
    assert status == 0, output

def test_negative_control():
    status, output = run(build('coop_respawn', generated(('clock->remaining = MAX(0, old - elapsed);',
        'clock->remaining = MAX(0, old - 2 * elapsed);'))), 'timing')
    assert status == CHECK_FAILED, output
