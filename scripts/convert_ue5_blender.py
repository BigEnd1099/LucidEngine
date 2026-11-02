import bpy
import os
import sys
import shutil

# Configurable source directories and output directories
source_dir = os.environ.get("LUCID_ASSET_SOURCE_DIR", os.path.join(os.path.dirname(__file__), "..", "raw_assets"))
workspace_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

out_dirs = [
    os.path.join(workspace_root, "public", "assets", "models"),
    os.path.join(workspace_root, "assets", "models")
]

for d in out_dirs:
    os.makedirs(d, exist_ok=True)

print("--- Lucid Engine - Asset Conversion via Blender ---")

if os.path.exists(source_dir):
    for root, dirs, files in os.walk(source_dir):
        for file in files:
            if file.lower().endswith(".fbx"):
                fbx_path = os.path.join(root, file)
                glb_name = os.path.splitext(file)[0] + ".glb"
                
                bpy.ops.wm.read_factory_settings(use_empty=True)
                print(f"[CONVERTING] {file} -> {glb_name}")
                try:
                    bpy.ops.import_scene.fbx(filepath=fbx_path)
                    primary_out = os.path.join(out_dirs[0], glb_name)
                    bpy.ops.export_scene.gltf(
                        filepath=primary_out,
                        export_format='GLB',
                        use_selection=False,
                        export_apply=True,
                        export_yup=True
                    )
                    print(f"[OK] Exported {primary_out}")
                    secondary_out = os.path.join(out_dirs[1], glb_name)
                    shutil.copy2(primary_out, secondary_out)
                except Exception as e:
                    print(f"[ERROR] Failed {file}: {e}")
else:
    print(f"[INFO] Source dir '{source_dir}' does not exist, skipping batch conversion.")

print("--- Asset Conversion Complete ---")
