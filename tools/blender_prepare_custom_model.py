"""Prepare a custom-model armature for JMS export without moving its mesh.

Run in Blender's Text Editor with the export armature (or its mesh) active,
in Object Mode and Rest Position. The Halo 8200 exporter reads pose-bone
matrices in armature space, but mesh/marker positions in world space. An
unapplied armature object transform therefore exports inconsistent pivots.

This applies transforms to the ARMATURE ONLY, preserving child/marker world
transforms. It never applies transforms to marker Empties, exports a file,
or saves the .blend. Inspect the result, then export normally. It is intended
for rest-mesh export, not rescaling existing authored animation actions.
"""
import math

import bpy


def prepare_custom_model():
    if bpy.context.mode != 'OBJECT':
        raise RuntimeError('Switch to Object Mode before preparing the export rig.')
    active = bpy.context.view_layer.objects.active
    rig = active if active and active.type == 'ARMATURE' else active.find_armature() if active else None
    if rig is None:
        raise RuntimeError('Select the custom-model armature or a mesh using it.')
    if rig.parent is not None:
        raise RuntimeError('The export armature must be an unparented root object.')
    if rig.data.pose_position != 'REST':
        raise RuntimeError('Set the armature to Rest Position before preparing it.')
    scale = rig.matrix_world.to_scale()
    if (not all(math.isfinite(x) and x > 0 for x in scale)
            or max(scale) - min(scale) > max(scale) * 1e-5
            or rig.matrix_world.determinant() <= 0):
        raise RuntimeError('Use a positive uniform armature scale; mirrored/nonuniform rigs need authoring corrections.')

    def affected(obj):
        parent = obj.parent
        while parent is not None:
            if parent == rig:
                return True
            parent = parent.parent
        return obj.type == 'MESH' and any(m.type == 'ARMATURE' and m.object == rig for m in obj.modifiers)

    def depth(obj):
        result = 0
        while obj.parent is not None:
            result += 1
            obj = obj.parent
        return result

    bpy.context.view_layer.update()
    world = {obj: obj.matrix_world.copy() for obj in bpy.context.scene.objects if obj != rig and affected(obj)}
    bones = {bone.name: (rig.matrix_world @ bone.head_local,
                         rig.matrix_world @ bone.tail_local,
                         (rig.matrix_world @ bone.matrix_local).to_quaternion()) for bone in rig.data.bones}
    selected = list(bpy.context.selected_objects)
    previous_active = bpy.context.view_layer.objects.active
    if rig.data.users > 1:
        rig.data = rig.data.copy()
    try:
        for obj in selected:
            obj.select_set(False)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        bpy.context.view_layer.update()
        for obj in sorted(world, key=depth):
            obj.matrix_world = world[obj]
            bpy.context.view_layer.update()
    finally:
        rig.select_set(False)
        for obj in selected:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = previous_active

    for bone in rig.data.bones:
        actual = rig.matrix_world @ bone.matrix_local
        head, tail, rotation = bones[bone.name]
        if ((rig.matrix_world @ bone.head_local - head).length > 1e-4
                or (rig.matrix_world @ bone.tail_local - tail).length > 1e-4
                or actual.to_quaternion().rotation_difference(rotation).angle > 1e-4):
            raise RuntimeError('A bone changed its world transform; undo the preparation before exporting: ' + bone.name)
    for obj, expected in world.items():
        if any(abs(obj.matrix_world[i][j] - expected[i][j]) > 1e-4 for i in range(4) for j in range(4)):
            raise RuntimeError('A child changed its world transform; undo the preparation before exporting: ' + obj.name)
    print('Custom model prepared: armature transforms applied; mesh/marker placement retained. Export the named JMS; save the .blend yourself.')
    return rig


if __name__ == '__main__':
    prepare_custom_model()
