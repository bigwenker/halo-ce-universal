"""Real input latches: camera presses, context changes and weapon-wheel routing."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build,read,run,function,enum_with,mutated,CHECK_FAILED

def generated(fault=None):
    source=read('port/linux/src/xinput_sdl.c')
    start=source.index('static BOOL third_person_key_down;')
    end=source.index('void platform_third_person_active(',start)
    code=source[start:end]+'\n'+'\n'.join(function(source,n) for n in (
        'platform_third_person_active','platform_third_person_distance',
        'platform_third_person_key','third_person_keys_update','mouse_poll'))
    if fault=='repeat':code=mutated(code,'down && !third_person_key_down','down')
    if fault=='context':code=mutated(code,'if (!playing)','if (FALSE)')
    keys=enum_with(read('port/linux/include/halo_keyboard.h'),'HALO_KEYBOARD_MOVE_FORWARD')
    return (('keys.inc',keys),('under_test.inc',code))

@pytest.mark.parametrize('case',['hold','context','distance','deactivate','wheel'])
def test_case(case):
    status,output=run(build('third_person_keys',generated()),case)
    assert status==0,output

@pytest.mark.parametrize('fault,case',[('repeat','hold'),('context','context')])
def test_negative(fault,case):
    status,output=run(build('third_person_keys',generated(fault)),case)
    assert status==CHECK_FAILED,output
