"""SDL's guest-local atomic operations preserve return values and concurrency."""
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, function, mutated, read, run, CHECK_FAILED


def generated(fault=None):
    source = read('port/android/guest/runtime/guest_sdl.c')
    code = '\n'.join(function(source, name) for name in
                     ('SDL_CompareAndSwapAtomicInt', 'SDL_SetAtomicInt'))
    if fault == 'compare':
        code = mutated(code, 'return __atomic_compare_exchange_n',
                       'return !__atomic_compare_exchange_n')
    if fault == 'previous':
        code = mutated(code, 'return __atomic_exchange_n',
                       'return newvalue + 0 * __atomic_exchange_n')
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['values', 'threads'])
def test_case(case):
    status, output = run(build('android_sdl_atomic', generated(), flags=('-pthread',)), case)
    assert status == 0, output


@pytest.mark.parametrize('fault', ['compare', 'previous'])
def test_negative(fault):
    status, output = run(build('android_sdl_atomic', generated(fault), flags=('-pthread',)), 'values')
    assert status == CHECK_FAILED, output
