"""Exercise the Android guest's file and screenshot bridge without a device."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, mutated, read, run
CASES = ['file', 'empty', 'failure', 'surface', 'format', 'overflow', 'save-failure', 'path', 'time', 'error', 'thread-path']
CONTROLS = {
 'shared-base-buffer': ('static __thread char path[4096];', 'static char path[4096];', 'thread-path'),
 'wrong-pitch': ('surface->pitch = width * 4;', 'surface->pitch = width;', 'surface'),
 'missing-close': ('if (closeio) SDL_CloseIO(stream);', ';', 'file'),
 'ignored-save-failure': ('return host_sdl_save_rgba_png(surface->w, surface->h, surface->pitch, surface->pixels, path) != 0;', '(void)host_sdl_save_rgba_png(surface->w, surface->h, surface->pitch, surface->pixels, path); return true;', 'save-failure'),
}
def generated(control=None):
 code = re.sub(r'^#include[^\n]*\n', '', read('port/android/guest/runtime/guest_sdl_image.c'), flags=re.M)
 if control:
  before, after, _ = CONTROLS[control]
  code = mutated(code, before, after)
 return (('under_test.inc', code),)
@pytest.mark.parametrize('case', CASES)
def test_case(case):
 status, output = run(build('android_sdl_image', generated(), flags=('-pthread',)), case)
 assert status == 0, output
@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
 status, output = run(build('android_sdl_image', generated(control), flags=('-pthread',)), CONTROLS[control][2])
 assert status == 1, output
