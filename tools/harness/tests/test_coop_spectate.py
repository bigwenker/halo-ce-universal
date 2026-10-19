"""Real spectator ordering and physical mouse edge handling."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, run

def generated():
    s=read('port/linux/game/coop_spectate.c')
    x=read('port/linux/src/xinput_sdl.c')
    code='\n'.join(function(s,n) for n in ('spectate_player_alive','cycle_living_player','direction_pressed'))
    mouse='static unsigned char spectate_mouse_buttons, spectate_mouse_suppressed;\n'
    mouse+='\n'.join(function(x,n) for n in ('mouse_spectate_update','halo_spectate_mouse_buttons'))
    return (('under_test.inc',code),('mouse.inc',mouse))
@pytest.mark.parametrize('case',['order','invalid','edges','menus'])
def test_case(case):
    status,output=run(build('coop_spectate',generated()),case)
    assert status==0,output
