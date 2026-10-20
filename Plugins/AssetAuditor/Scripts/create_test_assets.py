"""
Creates deliberately broken assets under /Game/AuditTests so Asset Auditor has something to find.

Run headless (from the project folder):
    UnrealEditor-Cmd.exe ToolsLab.uproject -run=pythonscript -script="Plugins/AssetAuditor/Scripts/create_test_assets.py"
or inside the editor: Tools > Execute Python Script...

It is idempotent: /Game/AuditTests is deleted and rebuilt on every run, so you can reset the
test content after you've let the auditor "fix" it.

Source images and meshes are generated here in pure Python (no PIL, no art files in the repo),
then imported through the normal import pipeline, so the assets are indistinguishable from
artist-imported ones (same registry tags, same import data).
"""

import math
import os
import struct
import tempfile
import zlib

import unreal

ROOT = "/Game/AuditTests"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
asset_subsystem = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
# Editor subsystems only exist once their module is loaded; nothing loads StaticMeshEditor in a
# commandlet (or in an editor session where no mesh editor has been opened yet).
unreal.load_module("StaticMeshEditor")
mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
mel = unreal.MaterialEditingLibrary


# --------------------------------------------------------------------------------------------
# Source file generation
# --------------------------------------------------------------------------------------------

def write_png(path, width, height, rgb=True):
    """Minimal PNG writer: 8-bit RGB (checkerboard) or grayscale (gradient)."""
    bytes_per_pixel = 3 if rgb else 1
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter type: none
        if rgb:
            # 64px checkerboard so the texture is recognisable in a material preview.
            dark = bytes((40, 40, 48))
            light = bytes((200, 120, 40))
            row = bytearray()
            for x in range(0, width, 64):
                cell = light if ((x // 64) + (y // 64)) % 2 == 0 else dark
                row += cell * min(64, width - x)
            rows += row
        else:
            rows += bytes([y * 255 // max(height - 1, 1)]) * width

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    color_type = 2 if rgb else 0
    header = struct.pack(">IIBBBBB", width, height, 8, color_type, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))
    assert len(rows) == height * (1 + width * bytes_per_pixel)


def write_sphere_obj(path, radius=50.0, rings=24, segments=48):
    """UV sphere, ~2.2k triangles: dense enough that the LOD rule cares about it."""
    lines = []
    for r in range(rings + 1):
        phi = math.pi * r / rings
        for s in range(segments):
            theta = 2.0 * math.pi * s / segments
            lines.append("v %f %f %f" % (radius * math.sin(phi) * math.cos(theta),
                                         radius * math.sin(phi) * math.sin(theta),
                                         radius * math.cos(phi)))
    for r in range(rings):
        for s in range(segments):
            a = r * segments + s + 1  # OBJ indices are 1-based
            b = r * segments + (s + 1) % segments + 1
            c = a + segments
            d = b + segments
            if r != 0:
                lines.append("f %d %d %d" % (a, b, c))
            if r != rings - 1:
                lines.append("f %d %d %d" % (b, d, c))
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


# --------------------------------------------------------------------------------------------
# Import helpers
# --------------------------------------------------------------------------------------------

def import_file(source_path, asset_name):
    task = unreal.AssetImportTask()
    task.filename = source_path
    task.destination_path = ROOT
    task.destination_name = asset_name
    task.automated = True          # no dialogs: required for headless runs
    task.replace_existing = True
    task.save = False
    asset_tools.import_asset_tasks([task])

    imported = [unreal.load_asset(p) for p in task.get_editor_property("imported_object_paths")]
    # Importers can also create side assets (e.g. a default material for an OBJ); pick the main one.
    wanted = unreal.StaticMesh if source_path.endswith(".obj") else unreal.Texture2D
    asset = next((a for a in imported if isinstance(a, wanted)), None)
    if asset is None:
        raise RuntimeError("Import of %s produced no %s" % (source_path, wanted.__name__))

    # Some import pipelines name assets after the file's internal name; force the name we want.
    if asset.get_name() != asset_name:
        asset_subsystem.rename_loaded_asset(asset, "%s/%s" % (ROOT, asset_name))
    return asset


def import_texture(temp_dir, name, width, height, rgb=True):
    path = os.path.join(temp_dir, name + ".png")
    write_png(path, width, height, rgb)
    return import_file(path, name)


def create_asset(name, asset_class, factory):
    return asset_tools.create_asset(name, ROOT, asset_class, factory)


# --------------------------------------------------------------------------------------------
# Build the test content
# --------------------------------------------------------------------------------------------

def main():
    if asset_subsystem.does_directory_exist(ROOT):
        asset_subsystem.delete_directory(ROOT)

    temp_dir = tempfile.mkdtemp(prefix="AssetAuditorTests_")
    created = []

    # Textures ------------------------------------------------------------------------------
    # Control: correctly named, power of two, within budget, used by a material -> no issues.
    good_texture = import_texture(temp_dir, "T_Checker_1024", 1024, 1024)
    created.append(good_texture)
    # Missing prefix -> Naming (expects T_Rock_Diffuse)
    created.append(import_texture(temp_dir, "Rock_Diffuse", 512, 512))
    # Wrong prefix: a texture named like a static mesh -> Naming (expects T_WrongPrefix)
    created.append(import_texture(temp_dir, "SM_WrongPrefix", 256, 256))
    # 1000x600 -> Texture Size: not power of two
    created.append(import_texture(temp_dir, "T_Banner_NPOT", 1000, 600))
    # 4096x4096 -> Texture Size: over the 2048 budget (grayscale keeps the import small)
    created.append(import_texture(temp_dir, "T_Skybox_4096", 4096, 4096, rgb=False))

    # Materials -----------------------------------------------------------------------------
    # Missing prefix -> Naming (expects M_RockMaterial). Samples the good texture so it's "used".
    material = create_asset("RockMaterial", unreal.Material, unreal.MaterialFactoryNew())
    sample = mel.create_material_expression(material, unreal.MaterialExpressionTextureSample, -400, 0)
    sample.set_editor_property("texture", good_texture)
    mel.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.recompile_material(material)
    created.append(material)

    # Instance using the material prefix -> Naming (expects MI_RockInstance)
    instance = create_asset("M_RockInstance", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(instance, material)
    created.append(instance)

    # Meshes --------------------------------------------------------------------------------
    # Dense sphere with collision and LODs stripped -> Static Mesh: no simple collision (Error)
    # and too few LODs (Warning). Uses the instance, which keeps the material chain referenced.
    obj_path = os.path.join(temp_dir, "SM_Sphere_NoCollision.obj")
    write_sphere_obj(obj_path)
    sphere = import_file(obj_path, "SM_Sphere_NoCollision")
    nanite = sphere.get_editor_property("nanite_settings")
    nanite.enabled = False  # Nanite meshes are exempt from the LOD check; make sure this one isn't
    sphere.set_editor_property("nanite_settings", nanite)
    mesh_subsystem.remove_lods(sphere)
    mesh_subsystem.remove_collisions(sphere)
    sphere.set_material(0, instance)
    created.append(sphere)

    # Copy of the engine cube without a prefix -> Naming (expects SM_Crate).
    # 12 triangles and box collision, so the LOD/collision checks correctly stay quiet.
    crate = asset_subsystem.duplicate_asset("/Engine/BasicShapes/Cube", ROOT + "/Crate")
    created.append(crate)

    # Blueprint -----------------------------------------------------------------------------
    # Missing prefix -> Naming (expects BP_PlayerSpawner)
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.Actor)
    created.append(create_asset("PlayerSpawner", unreal.Blueprint, factory))

    # Everything except the texture/material chain is unreferenced -> Unused (Info).

    asset_subsystem.save_directory(ROOT, only_if_is_dirty=False, recursive=True)
    unreal.log("AssetAuditor test content: created %d assets in %s" % (len(created), ROOT))


main()
