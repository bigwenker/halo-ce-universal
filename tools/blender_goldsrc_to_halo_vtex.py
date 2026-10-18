"""Merge an active GoldSrc mesh's vertex groups into Chief's 19-bone rig.

Run in Blender's Text Editor with the mesh active in Object Mode. Source
aliases, existing Chief groups and duplicate suffixes merge by destination;
unknown groups keep their names and weights. This changes weights only, not
the skeleton, geometry, materials, modifiers or the saved .blend file.
"""
from collections import defaultdict
import math
import re

import bpy


CHIEF_GROUPS = {
    'bip01 pelvis', 'bip01 spine', 'bip01 spine1', 'bip01 neck', 'bip01 head',
    *('bip01 ' + side + ' ' + bone for side in ('l', 'r')
      for bone in ('clavicle', 'upperarm', 'forearm', 'hand', 'thigh', 'calf', 'foot')),
}
ALIASES = {
    'bip01': 'bip01 pelvis',
    'bip01 spine2': 'bip01 spine1',
    'bip01 spine3': 'bip01 spine1',
    'mouth': 'bip01 head',
    'bip01 mouth': 'bip01 head',
}
for _side in ('l', 'r'):
    for _source, _target in (
        ('arm', 'upperarm'), ('arm1', 'upperarm'), ('arm2', 'forearm'),
        ('leg', 'thigh'), ('leg1', 'calf'),
    ):
        ALIASES['bip01 ' + _side + ' ' + _source] = 'bip01 ' + _side + ' ' + _target

# Extra GoldSrc joints used by Pepe. Numbered hands/wrists/fingers share
# Chief's single hand bone; leg helpers collapse to their actual joint.
HELPERS = {
    'femoral head': 'thigh', 'knee': 'calf', 'talus': 'foot', 'toe': 'foot',
    'shoulder': 'clavicle', 'elbow': 'forearm',
    'wrist': 'hand', 'hand': 'hand', 'finger': 'hand',
}
HELPER_PATTERN = re.compile(
    r'bip01 ([lr]) (femoral head|knee|talus|toe|shoulder|elbow|wrist|hand|finger)(?:\d+)?'
)

# Miyu's source SMD hierarchy places facial/hair controls under Head and
# every skirt control under Pelvis. Smoothing joints lie at the corresponding
# hand, elbow, knee or thigh joint; Chief has no separate helper bones.
HEAD_DETAIL_PATTERN = re.compile(
    r'(?:bip\d+ )?head (?:eyes|blink|[lr] eyebrow|(?:[lr] )?fakehair)[ a-z0-9-]*'
)
SKIRT_PATTERN = re.compile(r'bip\d+ (?:l|r|m|bl|br|bm) leg skirt\d*')
SMOOTH_PATTERN = re.compile(r'bip\d+ ([lr]) (leg|knee|elbow|wrist)\d*[- ]smooth\d*')
SMOOTH_TARGETS = {'leg': 'thigh', 'knee': 'calf', 'elbow': 'forearm', 'wrist': 'hand'}


def chief_group(name):
    key = re.sub(r'[_\s]+', ' ', re.sub(r'\.\d{3}$', '', name.strip())).casefold()
    if key in CHIEF_GROUPS:
        return key
    if key in ALIASES:
        return ALIASES[key]
    if HEAD_DETAIL_PATTERN.fullmatch(key):
        return 'bip01 head'
    if SKIRT_PATTERN.fullmatch(key):
        return 'bip01 pelvis'
    match = SMOOTH_PATTERN.fullmatch(key)
    if match:
        return 'bip01 ' + match[1] + ' ' + SMOOTH_TARGETS[match[2]]
    match = HELPER_PATTERN.fullmatch(key)
    if match:
        return 'bip01 ' + match[1] + ' ' + HELPERS[match[2]]
    return name


def convert_vertex_groups(obj=None):
    obj = obj or bpy.context.active_object
    if obj is None or obj.type != 'MESH':
        raise RuntimeError('Select a mesh object first.')
    if obj.mode != 'OBJECT':
        raise RuntimeError('Switch to Object Mode first.')
    original = {group.index: group.name for group in obj.vertex_groups}
    destinations = {index: chief_group(name) for index, name in original.items()}
    locks = defaultdict(bool)
    for group in obj.vertex_groups:
        locks[destinations[group.index]] |= group.lock_weight

    # Snapshot every weight before changing indices. Hand and Hand1 may both
    # contribute to an already-existing Chief hand group.
    weights = []
    unmapped = set()
    for vertex in obj.data.vertices:
        merged = defaultdict(float)
        for assignment in vertex.groups:
            if not math.isfinite(assignment.weight) or assignment.weight < 0:
                raise RuntimeError('Invalid vertex weight; no groups changed.')
            target = destinations[assignment.group]
            merged[target] += assignment.weight
            if assignment.weight > 0 and target not in CHIEF_GROUPS:
                unmapped.add(target)
        weights.append({name: min(weight, 1.0) for name, weight in merged.items() if weight > 0})

    # Weights are mesh data, but group indices are object-local. Isolate a
    # shared mesh before rewriting indices on this selected object alone.
    if obj.data.users > 1:
        obj.data = obj.data.copy()
    obj.vertex_groups.clear()
    groups = {name: obj.vertex_groups.new(name=name) for name in sorted(set(destinations.values()))}
    for vertex, merged in enumerate(weights):
        for name, weight in merged.items():
            groups[name].add([vertex], weight, 'REPLACE')
    for name, group in groups.items():
        group.lock_weight = locks[name]

    result = dict(original_groups=len(original), resulting_groups=len(groups),
                  vertices=len(weights), unmapped_weighted_groups=sorted(unmapped))
    print('Vertex group conversion complete:', result)
    return result


if __name__ == '__main__':
    convert_vertex_groups()
