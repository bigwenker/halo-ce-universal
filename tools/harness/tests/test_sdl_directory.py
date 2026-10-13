"""Android marshals names, never native-to-guest directory callbacks."""
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, read, function, structure, run


def generated():
    host = read('port/android/host/host_sdl.c')
    guest = read('port/android/guest/runtime/guest_sdl_image.c')
    code = structure(host, 'host_directory_buffer') + '\n'
    code += function(host, 'host_directory_name') + '\n'
    code += function(host, 'host_sdl_directory_list').replace('SDL_EnumerateDirectory(', 'native_enumerate(') + '\n'
    code += function(guest, 'SDL_EnumerateDirectory').replace('host_sdl_directory_list(', 'import_list(')
    return (('under_test.inc', code),)


@pytest.mark.parametrize('case', ['complete', 'stop', 'failure', 'host-error', 'overflow', 'malformed', 'empty', 'path-limit'])
def test_enumeration(case):
    status, output = run(build('sdl_directory', generated()), case)
    assert status == 0, output
