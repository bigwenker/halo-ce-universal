"""Retargeting failures: moved joints, elbow singularities and attached branches."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import read, build, run, mutated, CHECK_FAILED


def executable(fault=False):
    code = read('port/linux/src/custom_model_retarget.c')
    if fault:
        code = mutated(code, 'aim_segment(&alignment,a,b);', ';')
    crypto = read('port/linux/src/p2p_crypto.c')
    sha = crypto[crypto.index('struct sha256\n'):crypto.index('void p2p_hmac_sha256(')]
    return build('custom_model_contacts', (
        ('halo_custom_models.h', read('port/linux/include/halo_custom_models.h')),
        ('halo_custom_retarget.h', read('port/linux/include/halo_custom_retarget.h')),
        ('sha.inc', sha), ('under_test.inc', code)), flags=('-ffp-contract=off',))


@pytest.mark.parametrize('case', ['axes', 'pole', 'source-pole', 'shoulder', 'limits',
    'descendants', 'scale', 'invalid', 'frames', 'cache-version'])
def test_case(case):
    status, output = run(executable(), case)
    assert status == 0, output


def test_edited_joint_axes_negative_control():
    assert run(executable(True), 'axes')[0] == CHECK_FAILED
