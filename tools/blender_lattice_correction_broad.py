"""Correct Chief marker positions and hand rotations for an edited Chief rig.

In Blender, open the EDITED model, select its armature (or a mesh using it),
open this script in the Text Editor, and choose Run Script. Use Object Mode.
If several rigs exist, select the intended rig or set TARGET_ARMATURE_NAME.
Save the .blend yourself after inspecting the result; this script never saves.

Reference bindings below were read from the adjacent original chief.blend in
REST position. All eight "lattices" are actually EMPTY objects with explicit
bone parents, so no closest-bone guess was needed. For an unparented reference
marker, the appropriate fallback would be the nearest original rest joint,
not the nearest joint AFTER editing the skeleton.

blender_goldsrc_to_halo_vtex.py only renames/merges vertex groups; it does not record
bone displacements. Instead, apply the measured reference offset as follows:

    offset = original_bone_rest.inverse() @ original_marker_position
    corrected_world = edited_rig.world @ edited_bone_rest @ offset

This follows cumulative changes to a bone and all its ancestors, including
rotations, without confusing an animated pose with the edited rest pose.
Offsets are in the original Blender armature's units, not metres. Armature
object transforms are included. Offsets are NOT stretched by bone length:
the imported Chief bones have display tails unrelated to anatomical length.

Marker positions and, by default, the two HAND marker rotations change.
Hand orientations use Chief's original bone-local grip frame: moving a marker
alone cannot repair a reset/rotated grip frame that makes the in-game wrist
bend down. This calibrates marker axes, not the direction of a bone's display
tail. Other marker rotations, parenting, world scale, marker properties,
mesh weights and bones remain intact. Absolute reference transforms make
reruns idempotent. Manual positions and hand rotations are reset to the
approximate stock bindings; inspect/customize palm positions after running.
Missing markers are reported; extra/unknown markers are left alone because
their original offsets cannot be recovered reliably from an edited rig.

This is attachment-location assistance, not animation/bind-pose retargeting.
This fork imports visual contact markers for retargeting. Gameplay attachment
markers remain Chief's. Export markers with their exact names (for example,
"left hand", without a Blender duplicate suffix such as ".001").
"""

import math
import re

import bpy
from mathutils import Matrix, Quaternion, Vector


TARGET_ARMATURE_NAME = ""  # Optional exact Blender object name.
SELECTED_MARKERS_ONLY = False
CORRECT_HAND_ROTATIONS = True  # False keeps the old position-only behavior.

# marker: (original parent bone, original bone-local marker position)
# Captured from chief.blend, 2026-10-18. Both reference and target use REST.
REFERENCE_MARKERS = {
    "#antenna": (
        "bip01 spine1", (12.907745361328125, -5.089168548583984, -5.893179893493652)
    ),
    "#body": (
        "bip01 spine1", (1.7806965112686157, 0.9790979027748108, -0.000003000007836817531)
    ),
    "#flashlight": (
        "bip01 head", (2.6406710147857666, 1.917576789855957, 4.239999771118164)
    ),
    "#head": (
        "bip01 head", (3.9520184993743896, 0.4048228859901428, -0.000021999980162945576)
    ),
    "#left foot": (
        "bip01 l foot", (6.509589672088623, 5.351779460906982, -0.002970546018332243)
    ),
    "#left hand": (
        "bip01 l hand", (4.472444534301758, 0.8636599779129028, 0.09478513896465302)
    ),
    "#right foot": (
        "bip01 r foot", (6.50895881652832, 5.359609127044678, 0.12181112915277481)
    ),
    "#right hand": (
        "bip01 r hand", (4.472416400909424, 0.863659679889679, -0.0948343575000763)
    ),
}

# Blender quaternions are (w, x, y, z), unlike JMS's (x, y, z, w).
# Original chief.blend: inverse(rig_world @ bone_rest) @ marker_world,
# with the rig in REST position. Do not use the marker's object Euler angles:
# bone parenting and matrix_parent_inverse contribute to the exported frame.
REFERENCE_HAND_ROTATIONS = {
    "#left hand": (0.016078036278486252, -0.9935210347175598,
                   0.08508001267910004, -0.07361301779747009),
    "#right hand": (0.993520975112915, -0.01607999950647354,
                    -0.07361301779747009, 0.08507997542619705),
}


def corrected_hand_transform(frame, original, offset, reference_rotation):
    """Replace the grip frame while retaining the marker's world scale."""
    frame_scale = frame.to_scale()
    scale = original.to_scale()
    values = [component for matrix in (frame, original) for row in matrix for component in row]
    if not all(math.isfinite(value) for value in values):
        raise RuntimeError("Non-finite marker or armature transform; nothing changed.")
    if (frame.determinant() <= 0 or original.determinant() <= 0
            or min(frame_scale) <= 1e-8 or min(scale) <= 1e-8):
        raise RuntimeError("Apply reflected/zero armature or marker scales before correcting hand rotations.")
    if max(frame_scale) - min(frame_scale) > max(frame_scale) * 1e-5:
        raise RuntimeError("Apply non-uniform armature scale before correcting hand rotations.")
    # A quaternion cannot retain shear. Reject it instead of silently changing scale.
    reconstructed = Matrix.LocRotScale(original.translation, original.to_quaternion(), scale)
    if any(abs(original[i][j] - reconstructed[i][j]) > max(scale) * 1e-4
           for i in range(3) for j in range(3)):
        raise RuntimeError("Remove marker transform shear before correcting hand rotations.")
    rotation = frame.to_quaternion().normalized() @ Quaternion(reference_rotation).normalized()
    return Matrix.LocRotScale(frame @ offset, rotation.normalized(), scale)


def normalized_name(name):
    """Allow Blender duplicate suffixes such as #left hand.001."""
    return re.sub(r"\.\d{3,}$", "", name.strip()).casefold()


def armature_owner(obj):
    while obj is not None:
        if obj.type == "ARMATURE":
            return obj
        obj = obj.parent
    return None


def target_armature():
    if TARGET_ARMATURE_NAME:
        rig = bpy.context.scene.objects.get(TARGET_ARMATURE_NAME)
        if rig is None or rig.type != "ARMATURE":
            raise RuntimeError("TARGET_ARMATURE_NAME must name an armature in this scene.")
        return rig

    active = bpy.context.view_layer.objects.active
    if active is not None:
        rig = armature_owner(active) or active.find_armature()
        if rig is not None:
            return rig

    selected = set()
    for obj in bpy.context.selected_objects:
        rig = armature_owner(obj) or obj.find_armature()
        if rig is not None:
            selected.add(rig)
    if len(selected) == 1:
        return selected.pop()

    rigs = [obj for obj in bpy.context.scene.objects if obj.type == "ARMATURE"]
    if len(rigs) == 1:
        return rigs[0]
    raise RuntimeError("Select the edited Chief armature or set TARGET_ARMATURE_NAME.")


def correct_marker_locations():
    if bpy.context.mode != "OBJECT":
        raise RuntimeError("Switch to Object Mode first so edited bone positions are committed.")

    rig = target_armature()
    bones = {}
    for bone in rig.data.bones:
        key = bone.name.strip().casefold()
        if key in bones:
            raise RuntimeError("Ambiguous bone name: " + key)
        bones[key] = bone

    candidates = (
        bpy.context.selected_objects
        if SELECTED_MARKERS_ONLY else bpy.context.scene.objects
    )
    plan = []
    found = set()
    for marker in candidates:
        if marker.type not in {"EMPTY", "LATTICE"}:
            continue
        reference_name = normalized_name(marker.name)
        binding = REFERENCE_MARKERS.get(reference_name)
        if binding is None:
            if marker.type == "LATTICE" or marker.name.startswith("#"):
                print("[Marker correction] Unrecognized; left alone:", marker.name)
            continue
        owner = armature_owner(marker)
        if owner is not None and owner != rig:
            continue
        if marker.library is not None and marker.override_library is None:
            raise RuntimeError("Make this linked marker local first: " + marker.name)
        if marker.constraints or marker.animation_data is not None:
            raise RuntimeError("Remove/bake marker constraints or animation first: " + marker.name)

        bone_name, offset = binding
        bone = bones.get(bone_name)
        if bone is None:
            raise RuntimeError("Missing target bone %r for %s; nothing changed." % (bone_name, marker.name))
        # A different bone parent would move it incorrectly during animation.
        if (marker.parent_type == "BONE" and marker.parent is not None
                and marker.parent_bone.strip().casefold() != bone_name):
            raise RuntimeError("Unexpected bone parent for %s; expected %s." % (marker.name, bone_name))
        plan.append((marker, bone, Vector(offset)))
        found.add(reference_name)

    if not plan:
        raise RuntimeError("No matching Chief markers found for " + rig.name)
    for name in sorted(set(REFERENCE_MARKERS) - found):
        print("[Marker correction] Not present/selected:", name)

    old_pose_position = rig.data.pose_position
    originals = {}
    channels = {}
    try:
        rig.data.pose_position = "REST"
        bpy.context.view_layer.update()
        # Snapshot everything before changing parents' evaluated transforms.
        originals = {marker: marker.matrix_world.copy() for marker, _, _ in plan}
        channels = {marker: (marker.location.copy(), marker.rotation_euler.copy(),
                             marker.rotation_quaternion.copy(), tuple(marker.rotation_axis_angle),
                             marker.scale.copy()) for marker in originals}
        changes = []
        for marker, bone, offset in plan:
            frame = rig.matrix_world @ bone.matrix_local
            original = originals[marker]
            reference_rotation = REFERENCE_HAND_ROTATIONS.get(normalized_name(marker.name))
            if CORRECT_HAND_ROTATIONS and reference_rotation is not None:
                changed = corrected_hand_transform(frame, original, offset, reference_rotation)
            else:
                changed = original.copy()
                changed.translation = frame @ offset
            changes.append((marker, bone, changed))
        # Validate every transform before applying any; errors also roll back below.
        for marker, bone, changed in changes:
            original = originals[marker]
            if abs(original.to_quaternion().normalized().dot(changed.to_quaternion().normalized())) < 1.0 - 1e-7:
                marker.matrix_world = changed
            else:
                # Avoid decomposing unchanged Euler rotations near gimbal lock.
                # Move in parent space without touching rotation/scale channels.
                parent = original.to_3x3() @ marker.matrix_basis.to_3x3().inverted()
                marker.location += parent.inverted() @ (changed.translation - original.translation)
            bpy.context.view_layer.update()
            if any(abs(marker.matrix_world[i][j] - changed[i][j]) > 1e-4
                   for i in range(4) for j in range(4)):
                raise RuntimeError("Marker could not retain its corrected transform: " + marker.name)
            displacement = changed.translation - original.translation
            angle = math.degrees(original.to_quaternion().rotation_difference(changed.to_quaternion()).angle)
            angle = min(angle, 360.0 - angle)
            print("[Marker correction] %s -> %s; displacement %s; rotation %.3f degrees" % (
                marker.name, bone.name,
                tuple(round(component, 6) for component in displacement),
                angle,
            ))
    except Exception:
        for marker, values in channels.items():
            marker.location, marker.rotation_euler, marker.rotation_quaternion = values[:3]
            marker.rotation_axis_angle, marker.scale = values[3:]
        bpy.context.view_layer.update()
        raise
    finally:
        rig.data.pose_position = old_pose_position
        bpy.context.view_layer.update()

    print("[Marker correction] Adjusted %d markers on %s. Inspect before saving." % (
        len(plan), rig.name,
    ))


if __name__ == "__main__":
    correct_marker_locations()
