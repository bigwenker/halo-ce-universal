"""Real retargeter: bind poses, articulated motion, contacts and cache invalidation."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import read, build, run, mutated, CHECK_FAILED


def executable(fault=False):
    code = read('port/linux/src/custom_model_retarget.c')
    if fault:
        code = mutated(code, 'halo_custom_matrix_inverse(&p->target[at],&p->target_inverse[at]);',
                       'p->target_inverse[at]=p->source_inverse[at];')
    crypto = read('port/linux/src/p2p_crypto.c')
    sha = crypto[crypto.index('struct sha256\n'):crypto.index('void p2p_hmac_sha256(')]
    return build('custom_model_retarget', (
        ('halo_custom_models.h', read('port/linux/include/halo_custom_models.h')),
        ('halo_custom_retarget.h', read('port/linux/include/halo_custom_retarget.h')),
        ('sha.inc', sha), ('under_test.inc', code)), flags=('-ffp-contract=off',))


@pytest.mark.parametrize('case', ['identity', 'rest', 'motion', 'axes', 'axes-motion', 'world', 'translation',
    'hash', 'invalid', 'cache', 'corrupt', 'ik', 'unreachable', 'fold', 'straight', 'zero'])
def test_case(case):
    status, output = run(executable(), case)
    assert status == 0, output


def test_old_inverse_bind_negative_control():
    assert run(executable(True), 'rest')[0] == CHECK_FAILED
