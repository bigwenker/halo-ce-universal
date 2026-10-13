"""Real spray gameplay and decal clipping against a small collision BSP."""
import re
import sys
from pathlib import Path
import pytest
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import build, enum_with, function, mutated, read, run, structure

CASES = ['wall', 'edge', 'aspect', 'floor', 'invisible', 'broken-ring', 'capacity',
         'cooldown', 'failed-cooldown', 'singleplayer', 'network', 'multiplayer', 'split-screen', 'menu', 'pause', 'cinematic',
         'dead', 'blocked', 'missing-image', 'replace', 'reset', 'input', 'shared', 'shared-bsp', 'shared-nan',
         'preparing', 'preparing-reset', 'shared-local', 'shared-echo', 'shared-identical', 'shared-local-upload-failed', 'shared-remote-cache']
CONTROLS = {
    'short-cooldown': ('>= 4 * TICKS_PER_SECOND', '>= 3 * TICKS_PER_SECOND', 'cooldown'),
    'failed-cooldown': ('if (placed)\n\t\t\t{', 'if (TRUE || placed)\n\t\t\t{', 'failed-cooldown'),
    'invisible-allowed': ('FLAG(_collision_surface_invisible_bit) |', '', 'invisible'),
    'stale-checkpoint': ('spray_vertex_count = 0;', ';', 'reset'),
    'repeat-input': ('down && !was_down && gameplay', 'down && was_down && gameplay', 'input'),
    'own-download': ('!(local||network_spray_is_local(owner))', '1', 'shared-echo'),
}


def generated(control=None):
    math = read('source/math/real_math.h')
    # The same anonymous unions as the engine, including indexed coordinates.
    types = '#define real_point3d spray_point3d\n#define real_vector3d spray_vector3d\n'
    types += math[math.index('union real_point2d\n'):math.index('union real_rgb_color\n')]
    decal = read('source/effects/decals.c')
    for name in ['decal_vertex', 'decal_geometry', 'decal_projection', 'decal_wrap_parameters']:
        types += structure(decal, name) + '\n'
    for member, source in [('_decal_type_painted_sign', read('source/effects/decal_definitions.h')),
                           ('_collision_result_structure', read('source/physics/collisions.h')),
                           ('_collision_test_structure_bit', read('source/physics/collisions.h')),
                           ('_game_connection_local', read('source/game/game.h')),
                           ('_decal_layer_primary', read('source/render/render.c')),
                           ('_collision_surface_two_sided_bit', decal)]:
        types += enum_with(source, member) + '\n'
    types += '#define MAXIMUM_DECAL_VERTICES 1024\n#define MAXIMUM_DECAL_SURFACE_QUEUE_SIZE 1024\n'
    # Array-dependent structures follow the capacities.
    start = types.index('struct decal_vertex')
    end = types.index('enum', start)
    declarations = types[start:end]
    types = types[:start] + types[end:] + declarations
    for name in ['collision_surface', 'collision_edge', 'collision_vertex']:
        types += structure(read('source/physics/collision_bsp_definitions.h'), name) + '\n'
    types += read('port/linux/include/halo_spray.h').replace('#include "spray_share.h"',read('port/linux/include/spray_share.h')) + '\n'
    table = read('source/math/real_math.c')
    types += table[table.index('short const global_projection3d_mappings'):table.index('};', table.index('short const global_projection3d_mappings')) + 2]
    roots = [function(decal, name) for name in ['decal_projection_create', 'decal_clip_to_surface',
                                             'decal_collision_edge_vertices_valid', 'decal_build_spray_geometry']]
    # Compile their math dependencies, rather than reimplementing the clipper.
    seen, helpers = set(), []
    def dependency(name):
        if name in seen:
            return
        seen.add(name)
        for source in [math, table, read('source/math/geometry.c')]:
            try:
                code = function(source, name).replace('__inline', 'static', 1)
                break
            except LookupError:
                code = None
        if not code:
            return
        for called in re.findall(r'\b(\w+)\s*\(', code[code.index('{'):]):
            dependency(called)
        helpers.append(code)
    for code in roots:
        for called in re.findall(r'\b(\w+)\s*\(', code[code.index('{'):]):
            dependency(called)
    # Roots are supplied below, not duplicated as recursively collected helpers.
    roots_names = ['decal_projection_create', 'decal_clip_to_surface', 'decal_collision_edge_vertices_valid', 'decal_build_spray_geometry']
    helpers = [h for h in helpers if not any(re.search(r'\b' + n + r'\(', h[:h.index('{')]) for n in roots_names)]
    geometry = '\n'.join(helpers) + '\n'
    geometry += 'static boolean decal_collision_edge_vertices_valid(struct collision_bsp const *,long);\n'
    geometry += '\n'.join(roots)
    game = re.sub(r'^#include[^\n]*\n', '', read('port/linux/game/spray.c'), flags=re.M)
    code = geometry + '\n' + game + '\n' + function(read('port/linux/src/xinput_sdl.c'), 'keyboard_spray')
    if control:
        before, after, _ = CONTROLS[control]
        code = mutated(code, before, after)
    return (('types.inc', types), ('under_test.inc', code))


@pytest.mark.parametrize('case', CASES)
def test_case(case):
    status, output = run(build('spray', generated(), ('-Wno-error=missing-braces', '-Wno-pointer-sign')), case)
    assert status == 0, output


@pytest.mark.parametrize('control', CONTROLS)
def test_negative_control(control):
    status, output = run(build('spray', generated(control), ('-Wno-error=missing-braces', '-Wno-pointer-sign')), CONTROLS[control][2])
    assert status == 1, output
