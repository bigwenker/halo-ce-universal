"""Model defaults supplement explicit RGB without changing profile or wire data."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, run, mutated, CHECK_FAILED


def executable(fault=False):
    source = read('port/linux/game/custom_models.c')
    defaults = '\n'.join(function(source, name) for name in
                         ('custom_models_default_rgb', 'custom_models_profile_default_rgb'))
    if fault:
        defaults = mutated(defaults, 'info && info->colorable &&', 'info &&')
    return build('custom_model_colors', (
        ('halo_custom_models.h', read('port/linux/include/halo_custom_models.h')),
        ('parse.inc', function(read('port/linux/game/player_profile_rgb.c'), 'profile_rgb_parse')),
        ('defaults.inc', defaults),
        ('color.inc', function(read('port/linux/game/network_profile_rgb.c'), 'network_profile_rgb_color')),
    ))


@pytest.mark.parametrize('case', ['local_default', 'profile_priority', 'remote_default',
                                 'remote_priority', 'noncolorable', 'missing_pack',
                                 'invalid_default', 'black_default', 'profile_picker',
                                 'invalid_player'])
def test_color_priority(case):
    status, out = run(executable(), case)
    assert status == 0, out


def test_negative_noncolorable():
    status, out = run(executable(True), 'noncolorable')
    assert status == CHECK_FAILED, out
