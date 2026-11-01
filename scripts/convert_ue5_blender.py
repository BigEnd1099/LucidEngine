import bpy
import os
import shutil

asset_map = [
    (r"D:\UE_5.8\Templates\TemplateResources\Standard\Vehicles\Content\SportsCar\SM_SportsCar.FBX", "SM_SportsCar.glb"),
    (r"D:\UE_5.8\Templates\TemplateResources\Standard\1M_Cube.FBX", "1M_Cube.glb"),
    (r"D:\UE_5.8\Templates\TemplateResources\Standard\1M_Cube_Chamfer.FBX", "1M_Cube_Chamfer.glb"),
    (r"D:\UE_5.8\Templates\TP_UEIntro_BP\Content\DemoTemplate\Transform\SM_Gizmo_Location.fbx", "SM_Gizmo_Location.glb"),
    (r"D:\UE_5.8\Templates\TP_UEIntro_BP\Content\DemoTemplate\Transform\SM_Gizmo_Rotate.fbx", "SM_Gizmo_Rotate.glb"),
    (r"D:\UE_5.8\Templates\TP_UEIntro_BP\Content\DemoTemplate\Transform\SM_Gizmo_Scale.fbx", "SM_Gizmo_Scale.glb"),
    (r"D:\UE_5.8\Engine\Content\FbxEditorAutomation\BlenderCube.fbx", "BlenderCube.glb"),
    (r"D:\UE_5.8\Engine\Content\FbxEditorAutomation\Cube_Blue.fbx", "Cube_Blue.glb"),
    (r"D:\UE_5.8\Engine\Content\VREditor\TransformGizmo\TranslateArrowHandle.fbx", "TranslateArrowHandle.glb"),
    (r"D:\UE_5.8\Engine\Content\VREditor\TransformGizmo\RotationHandle.fbx", "RotationHandle.glb"),
]

out_dirs = [
    r"D:\Lucid Engine\public\assets\models",
    r"D:\Lucid Engine\assets\models"
]

for d in out_dirs:
    os.makedirs(d, exist_ok=True)

print("--- Starting Unreal Engine 5.8 to glTF/GLB conversion via Blender 5.1 ---")

for fbx_path, glb_name in asset_map:
    if not os.path.exists(fbx_path):
        print(f"[SKIP] Not found: {fbx_path}")
        continue
    
    # Reset scene
    bpy.ops.wm.read_factory_settings(use_empty=True)
    
    print(f"[CONVERTING] {fbx_path} -> {glb_name}")
    try:
        bpy.ops.import_scene.fbx(filepath=fbx_path)
    except Exception as e:
        print(f"[ERROR] Import failed for {fbx_path}: {e}")
        continue
    
    primary_out = os.path.join(out_dirs[0], glb_name)
    try:
        bpy.ops.export_scene.gltf(
            filepath=primary_out,
            export_format='GLB',
            use_selection=False,
            export_apply=True,
            export_yup=True
        )
        print(f"[OK] Exported {primary_out} ({os.path.getsize(primary_out)} bytes)")
        
        # Copy to secondary
        secondary_out = os.path.join(out_dirs[1], glb_name)
        shutil.copy2(primary_out, secondary_out)
    except Exception as e:
        print(f"[ERROR] glTF export failed for {glb_name}: {e}")

print("--- UE 5.8 Asset Conversion Complete! ---")
