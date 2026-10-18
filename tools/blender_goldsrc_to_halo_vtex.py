
import bpy
from collections import defaultdict

obj = bpy.context.active_object

if not obj or obj.type != 'MESH':
    raise RuntimeError("Select a mesh object first.")

if obj.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')

# Source vertex group -> target vertex group
mapping = {
    "Bip01": "bip01 pelvis",
    "Bip01 Pelvis": "bip01 pelvis",

    "Bip01 Spine": "bip01 spine",
    "Bip01 Spine1": "bip01 spine1",
    "Bip01 Spine2": "bip01 spine1",
    "Bip01 Spine3": "bip01 spine1",

    "Bip01 Neck": "bip01 head",
    "Bip01 Head": "bip01 head",

    "Bip01 L Arm": "bip01 l upperarm",
    "Bip01 L Arm1": "bip01 l upperarm",
    "Bip01 L Arm2": "bip01 l forearm",
    "Bip01 L Hand": "bip01 l hand",

    "Bip01 R Arm": "bip01 r upperarm",
    "Bip01 R Arm1": "bip01 r upperarm",
    "Bip01 R Arm2": "bip01 r forearm",
    "Bip01 R Hand": "bip01 r hand",

    "Bip01 L Leg": "bip01 l thigh",
    "Bip01 L Leg1": "bip01 l calf",
    "Bip01 L Foot": "bip01 l foot",

    "Bip01 R Leg": "bip01 r thigh",
    "Bip01 R Leg1": "bip01 r calf",
    "Bip01 R Foot": "bip01 r foot",
}

# Make a case-insensitive mapping
mapping = {k.lower(): v for k, v in mapping.items()}

# Snapshot original group names and indices
original_groups = {
    vg.index: vg.name for vg in obj.vertex_groups
}

# Collect and merge weights by destination name
vertex_weights = {}

for vertex in obj.data.vertices:
    merged = defaultdict(float)

    for assignment in vertex.groups:
        name = original_groups[assignment.group]
        target = mapping.get(name.lower(), name)

        merged[target] += assignment.weight

    vertex_weights[vertex.index] = {
        name: min(weight, 1.0)
        for name, weight in merged.items()
    }

# Remove original groups
obj.vertex_groups.clear()

# Create destination groups
target_names = sorted({
    name
    for weights in vertex_weights.values()
    for name in weights
})

groups = {
    name: obj.vertex_groups.new(name=name)
    for name in target_names
}

# Assign merged weights
for vertex_index, weights in vertex_weights.items():
    for name, weight in weights.items():
        if weight > 0:
            groups[name].add(
                [vertex_index],
                weight,
                'REPLACE'
            )

print("Vertex group conversion complete.")
print(f"Original groups: {len(original_groups)}")
print(f"Resulting groups: {len(obj.vertex_groups)}")
