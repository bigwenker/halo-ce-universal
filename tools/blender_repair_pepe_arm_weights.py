"""Restore arm joint blends on the converted Pepe mesh (Blender 4.3.1).

Select Pepe's mesh in Object Mode and run in Blender's Text Editor. This
changes arm weights only; inspect the result and save/export yourself.
Reruns produce the same weights. The script never saves a .blend file.

Chief uses normalized, adjacent-bone blends (at most two influences), with
50/50 elbow rows and graded forearm/hand weights. Pepe's six former wrist
helpers need a longer transition than Chief's wrist band. A cubic ramp
keeps the proximal forearm stable and progressively follows the wrist.
This approximates the lost twist controls with Chief's 19-bone skeleton;
it cannot reproduce all 48 original bones or preserve volume at any twist.
"""
import math

import bpy


# Distances are fractions of the relevant limb, independent of object scale.
ELBOW_START = -0.10
ELBOW_END = 0.22
WRIST_START = 0.30
WRIST_END = 1.03


def _clamp(value):
    return min(1.0, max(0.0, value))


def repair_pepe_arm_weights(obj=None):
    obj = obj or bpy.context.active_object
    if obj is None or obj.type != 'MESH' or obj.mode != 'OBJECT':
        raise RuntimeError('Select Pepe mesh in Object Mode.')
    if not obj.name.casefold().startswith('pepe'):
        raise RuntimeError('This repair is calibrated for Pepe, not other models.')
    rig = obj.find_armature()
    if rig is None:
        raise RuntimeError('The mesh needs its Chief armature modifier.')
    if obj.data.users != 1:
        raise RuntimeError('Make the mesh single-user before repairing its weights.')
    if not ELBOW_START < ELBOW_END or not WRIST_START < WRIST_END:
        raise RuntimeError('Invalid blend intervals.')
    space = rig.matrix_world.inverted() @ obj.matrix_world
    plans = []
    for side in ('l', 'r'):
        names = ['bip01 ' + side + ' ' + part for part in ('upperarm', 'forearm', 'hand')]
        groups = [obj.vertex_groups.get(name) for name in names]
        bones = [rig.data.bones.get(name) for name in names]
        if not all(groups) or not all(bones) or any(g.lock_weight for g in groups):
            raise RuntimeError('Missing or locked arm groups/bones: ' + side)
        shoulder, elbow, wrist = [bone.head_local.copy() for bone in bones]
        upper = elbow - shoulder
        lower = wrist - elbow
        if min(upper.length, lower.length) < 1e-6:
            raise RuntimeError('Zero-length arm segment: ' + side)
        upper_length, lower_length = upper.length, lower.length
        upper.normalize()
        lower.normalize()
        indices = {g.index for g in groups}
        vertices = []
        for vertex in obj.data.vertices:
            old = {g.group: g.weight for g in vertex.groups if g.weight > 0}
            if not any(index in indices for index in old):
                continue
            if not set(old) <= indices or abs(sum(old.values()) - 1.0) > 1e-5:
                raise RuntimeError('Unexpected overlapping/non-normalized weights at vertex %d.' % vertex.index)
            co = space @ vertex.co
            e = (co - elbow).dot(upper) / upper_length
            t = (co - elbow).dot(lower) / lower_length
            if not all(math.isfinite(value) for value in (e, t)):
                raise RuntimeError('Invalid vertex position.')
            if groups[0].index in old or (e < ELBOW_END and t < 0.45):
                fraction = _clamp((e - ELBOW_START) / (ELBOW_END - ELBOW_START))
                weight = fraction * fraction * (3.0 - 2.0 * fraction)
                weights = {groups[0].index: 1.0 - weight, groups[1].index: weight}
            else:
                fraction = _clamp((t - WRIST_START) / (WRIST_END - WRIST_START))
                weight = fraction ** 3
                weights = {groups[1].index: 1.0 - weight, groups[2].index: weight}
            vertices.append((vertex.index, old, {i: w for i, w in weights.items() if w > 1e-7}))
        plans.append((groups, vertices))
    # Validate both sides before any mutation. Group names/indices never change.
    try:
        for groups, vertices in plans:
            indices = [i for i, old, weights in vertices]
            for group in groups:
                group.remove(indices)
            for i, old, weights in vertices:
                for group, weight in weights.items():
                    obj.vertex_groups[group].add([i], weight, 'REPLACE')
    except Exception:
        for groups, vertices in plans:
            for group in groups:
                group.remove([i for i, old, weights in vertices])
            for i, old, weights in vertices:
                for group, weight in old.items():
                    obj.vertex_groups[group].add([i], weight, 'REPLACE')
        raise
    result = {side: len(vertices) for side, (groups, vertices) in zip(('left', 'right'), plans)}
    print('Pepe arm blends restored:', result, '(inspect, save and export yourself)')
    return result


if __name__ == '__main__':
    repair_pepe_arm_weights()
