import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const rootDir = path.resolve(__dirname, '..');
const targetDir = 'D:/Lucid_Game_test';

console.log('========================================================');
console.log('🎮 CREATING LUCID ODYSSEY: THE LOST RELICS');
console.log(`📁 Target Directory: ${targetDir}`);
console.log('========================================================');

// Ensure target directory exists
fs.mkdirSync(targetDir, { recursive: true });
fs.mkdirSync(path.join(targetDir, 'assets', 'models'), { recursive: true });
fs.mkdirSync(path.join(targetDir, 'assets', 'textures'), { recursive: true });
fs.mkdirSync(path.join(targetDir, 'shaders'), { recursive: true });

// Copy shaders strictly from build output
const shaderDirs = [
    path.join(rootDir, 'engine/build/Release'),
    path.join(rootDir, 'engine/build')
];

let shadersCopied = 0;
for (const sDir of shaderDirs) {
    if (fs.existsSync(sDir)) {
        const files = fs.readdirSync(sDir);
        for (const f of files) {
            if (f.endsWith('.spv')) {
                const srcPath = path.join(sDir, f);
                const dstPath1 = path.join(targetDir, f);
                const dstPath2 = path.join(targetDir, 'shaders', f);
                fs.copyFileSync(srcPath, dstPath1);
                fs.copyFileSync(srcPath, dstPath2);
                shadersCopied++;
            }
        }
    }
}
console.log(`✅ Copied ${shadersCopied} compiled SPIR-V shaders to ${targetDir}`);

// Copy binaries
const exeCandidates = [
    path.join(rootDir, 'engine/build/Release/engine.exe'),
    path.join(rootDir, 'engine/build/Debug/engine.exe'),
    path.join(rootDir, 'engine.exe')
];
let exeFound = false;
for (const c of exeCandidates) {
    if (fs.existsSync(c)) {
        fs.copyFileSync(c, path.join(targetDir, 'LucidGame.exe'));
        fs.copyFileSync(c, path.join(targetDir, 'engine.exe'));
        console.log(`✅ Copied engine binary from ${c} -> LucidGame.exe & engine.exe`);
        exeFound = true;
        break;
    }
}
if (!exeFound) {
    console.error('❌ Could not find engine binary!');
    process.exit(1);
}

// Recursively copy assets
function copyRecursiveSync(src, dest) {
    if (!fs.existsSync(src)) return;
    fs.mkdirSync(dest, { recursive: true });
    const entries = fs.readdirSync(src, { withFileTypes: true });
    for (const entry of entries) {
        const srcPath = path.join(src, entry.name);
        const destPath = path.join(dest, entry.name);
        if (entry.isDirectory()) {
            copyRecursiveSync(srcPath, destPath);
        } else {
            fs.copyFileSync(srcPath, destPath);
        }
    }
}

copyRecursiveSync(path.join(rootDir, 'engine/assets'), path.join(targetDir, 'assets'));
copyRecursiveSync(path.join(rootDir, 'engine/assets'), path.join(targetDir, 'engine/assets'));
console.log('✅ Synchronized all engine assets (HDR textures, fonts, icons) into game directory.');

// Create RunGame.bat
const batContent = `@echo off
title Lucid Odyssey: The Lost Relics - Lucid Engine
cd /d "%~dp0"
echo ========================================================
echo   Launching Lucid Odyssey: The Lost Relics
echo   Vulkan 1.3 AAA Native 3D Game Runtime
echo ========================================================
start "" "%~dp0LucidGame.exe" --game game.json
`;
fs.writeFileSync(path.join(targetDir, 'RunGame.bat'), batContent, 'utf8');
console.log('✅ Created 1-click launcher script: RunGame.bat');

// Build Level Entities
// Primitive Mesh IDs in AssetManager:
// 1 = Plane, 2 = Cube, 3 = Sphere, 4 = Cylinder, 5 = Capsule
const entities = [
    // 1. Player
    {
        id: 1,
        tag: "Player",
        transform: { x: 0.0, y: 1.5, z: 0.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 1.0, sy: 1.0, sz: 1.0 },
        mesh: { meshId: 5, materialId: 0 },
        rigidBody: { mass: 75.0, shapeType: 2, restitution: 0.0, friction: 0.6, capsuleRadius: 0.5, capsuleHalfHeight: 0.9, lockRotationX: true, lockRotationZ: true, isSensor: false },
        material: { color: [0.15, 0.80, 1.0, 1.0], metallic: 0.35, roughness: 0.25 },
        movement: { maxWalkSpeed: 9.0, jumpZVelocity: 11.5, airControl: 0.35, gravityScale: 1.0 },
        cameraBoom: { targetArmLength: 7.5, socketOffsetY: 2.2, cameraLag: true, lagSpeed: 12.0, isFirstPerson: false }
    },
    // 2. Main Arena Ground
    {
        id: 2,
        tag: "Ground",
        transform: { x: 0.0, y: -0.5, z: 0.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 44.0, sy: 1.0, sz: 44.0 },
        mesh: { meshId: 2, materialId: 1 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8, isSensor: false },
        material: { color: [0.22, 0.24, 0.28, 1.0], metallic: 0.15, roughness: 0.85 }
    },
    // 3. Ancient Pillars
    {
        id: 3,
        tag: "Pillar_NW",
        transform: { x: -14.0, y: 4.0, z: -14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 2.0, sy: 8.0, sz: 2.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.35, 0.33, 0.30, 1.0], metallic: 0.1, roughness: 0.9 }
    },
    {
        id: 4,
        tag: "Torch_NW",
        transform: { x: -14.0, y: 8.5, z: -14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.7, sy: 0.7, sz: 0.7 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.65, 0.2, 1.0], emissive: [1.0, 0.65, 0.2], emissiveIntensity: 3.5 },
        light: { type: "point", color: [1.0, 0.65, 0.2], intensity: 4.5, range: 15.0 }
    },
    {
        id: 5,
        tag: "Pillar_NE",
        transform: { x: 14.0, y: 4.0, z: -14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 2.0, sy: 8.0, sz: 2.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.35, 0.33, 0.30, 1.0], metallic: 0.1, roughness: 0.9 }
    },
    {
        id: 6,
        tag: "Torch_NE",
        transform: { x: 14.0, y: 8.5, z: -14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.7, sy: 0.7, sz: 0.7 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.65, 0.2, 1.0], emissive: [1.0, 0.65, 0.2], emissiveIntensity: 3.5 },
        light: { type: "point", color: [1.0, 0.65, 0.2], intensity: 4.5, range: 15.0 }
    },
    {
        id: 7,
        tag: "Pillar_SW",
        transform: { x: -14.0, y: 4.0, z: 14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 2.0, sy: 8.0, sz: 2.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.35, 0.33, 0.30, 1.0], metallic: 0.1, roughness: 0.9 }
    },
    {
        id: 8,
        tag: "Torch_SW",
        transform: { x: -14.0, y: 8.5, z: 14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.7, sy: 0.7, sz: 0.7 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.65, 0.2, 1.0], emissive: [1.0, 0.65, 0.2], emissiveIntensity: 3.5 },
        light: { type: "point", color: [1.0, 0.65, 0.2], intensity: 4.5, range: 15.0 }
    },
    {
        id: 9,
        tag: "Pillar_SE",
        transform: { x: 14.0, y: 4.0, z: 14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 2.0, sy: 8.0, sz: 2.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.35, 0.33, 0.30, 1.0], metallic: 0.1, roughness: 0.9 }
    },
    {
        id: 10,
        tag: "Torch_SE",
        transform: { x: 14.0, y: 8.5, z: 14.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.7, sy: 0.7, sz: 0.7 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.65, 0.2, 1.0], emissive: [1.0, 0.65, 0.2], emissiveIntensity: 3.5 },
        light: { type: "point", color: [1.0, 0.65, 0.2], intensity: 4.5, range: 15.0 }
    },
    // 4. Platforms (Multi-Tier Ascension)
    {
        id: 11,
        tag: "Platform_1",
        transform: { x: 0.0, y: 3.0, z: -10.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 8.0, sy: 0.8, sz: 6.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.32, 0.36, 0.42, 1.0], metallic: 0.2, roughness: 0.7 }
    },
    {
        id: 12,
        tag: "Platform_2",
        transform: { x: 11.0, y: 6.0, z: -6.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 6.0, sy: 0.8, sz: 6.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.32, 0.36, 0.42, 1.0], metallic: 0.2, roughness: 0.7 }
    },
    {
        id: 13,
        tag: "Platform_3",
        transform: { x: 10.0, y: 9.0, z: 8.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 6.0, sy: 0.8, sz: 6.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.32, 0.36, 0.42, 1.0], metallic: 0.2, roughness: 0.7 }
    },
    {
        id: 14,
        tag: "Platform_4",
        transform: { x: -10.0, y: 7.0, z: 6.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 6.0, sy: 0.8, sz: 6.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.32, 0.36, 0.42, 1.0], metallic: 0.2, roughness: 0.7 }
    },
    {
        id: 15,
        tag: "Summit_Platform",
        transform: { x: 0.0, y: 12.0, z: 10.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 9.0, sy: 0.8, sz: 9.0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.1, friction: 0.8 },
        material: { color: [0.26, 0.29, 0.36, 1.0], metallic: 0.3, roughness: 0.5 }
    },
    // 5. Jump Pads
    {
        id: 16,
        tag: "JumpPad",
        transform: { x: 6.5, y: 0.2, z: -4.5, rx: 0.0, ry: 0.0, rz: 0.0, sx: 3.0, sy: 0.4, sz: 3.0 },
        mesh: { meshId: 4 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.5, friction: 0.4 },
        material: { color: [0.0, 0.9, 1.0, 1.0], emissive: [0.0, 0.9, 1.0], emissiveIntensity: 3.5, metallic: 0.8, roughness: 0.1 },
        light: { type: "point", color: [0.0, 0.9, 1.0], intensity: 3.5, range: 6.0 }
    },
    {
        id: 17,
        tag: "JumpPad",
        transform: { x: -8.0, y: 0.2, z: 3.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 3.0, sy: 0.4, sz: 3.0 },
        mesh: { meshId: 4 },
        rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.5, friction: 0.4 },
        material: { color: [1.0, 0.2, 0.8, 1.0], emissive: [1.0, 0.2, 0.8], emissiveIntensity: 3.5, metallic: 0.8, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.2, 0.8], intensity: 3.5, range: 6.0 }
    },
    // 6. The 5 Golden Power Orbs (Relics)
    {
        id: 18,
        tag: "Relic_1",
        transform: { x: 0.0, y: 4.8, z: -10.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.8, sy: 0.8, sz: 0.8 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.85, 0.0, 1.0], emissive: [1.0, 0.8, 0.0], emissiveIntensity: 3.0, metallic: 0.9, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.85, 0.0], intensity: 2.5, range: 5.0 }
    },
    {
        id: 19,
        tag: "Relic_2",
        transform: { x: 11.0, y: 7.8, z: -6.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.8, sy: 0.8, sz: 0.8 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.85, 0.0, 1.0], emissive: [1.0, 0.8, 0.0], emissiveIntensity: 3.0, metallic: 0.9, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.85, 0.0], intensity: 2.5, range: 5.0 }
    },
    {
        id: 20,
        tag: "Relic_3",
        transform: { x: 10.0, y: 10.8, z: 8.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.8, sy: 0.8, sz: 0.8 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.85, 0.0, 1.0], emissive: [1.0, 0.8, 0.0], emissiveIntensity: 3.0, metallic: 0.9, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.85, 0.0], intensity: 2.5, range: 5.0 }
    },
    {
        id: 21,
        tag: "Relic_4",
        transform: { x: -10.0, y: 8.8, z: 6.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.8, sy: 0.8, sz: 0.8 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.85, 0.0, 1.0], emissive: [1.0, 0.8, 0.0], emissiveIntensity: 3.0, metallic: 0.9, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.85, 0.0], intensity: 2.5, range: 5.0 }
    },
    {
        id: 22,
        tag: "Relic_5",
        transform: { x: 0.0, y: 1.8, z: 12.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 0.8, sy: 0.8, sz: 0.8 },
        mesh: { meshId: 3 },
        material: { color: [1.0, 0.85, 0.0, 1.0], emissive: [1.0, 0.8, 0.0], emissiveIntensity: 3.0, metallic: 0.9, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.85, 0.0], intensity: 2.5, range: 5.0 }
    },
    // 7. Rotating Hazard Laser Bar
    {
        id: 23,
        tag: "Laser_Base",
        transform: { x: 0.0, y: 0.5, z: 12.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 1.2, sy: 1.2, sz: 1.2 },
        mesh: { meshId: 4 },
        rigidBody: { mass: 0.0, shapeType: 0 },
        material: { color: [0.15, 0.15, 0.15, 1.0], metallic: 0.8, roughness: 0.3 }
    },
    {
        id: 24,
        tag: "HazardLaser",
        transform: { x: 0.0, y: 1.1, z: 12.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 8.5, sy: 0.35, sz: 0.35 },
        mesh: { meshId: 2 },
        material: { color: [1.0, 0.1, 0.1, 1.0], emissive: [1.0, 0.05, 0.05], emissiveIntensity: 4.5, metallic: 0.9, roughness: 0.1 },
        light: { type: "point", color: [1.0, 0.1, 0.1], intensity: 4.0, range: 9.0 }
    },
    // 8. Ancient Golden Portal
    {
        id: 25,
        tag: "Portal_Base",
        transform: { x: 0.0, y: 12.5, z: 10.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 4.0, sy: 0.4, sz: 4.0 },
        mesh: { meshId: 4 },
        rigidBody: { mass: 0.0, shapeType: 0 },
        material: { color: [0.1, 0.8, 0.9, 1.0], emissive: [0.1, 0.8, 0.9], emissiveIntensity: 2.0, metallic: 0.8, roughness: 0.2 }
    },
    {
        id: 26,
        tag: "PortalCore",
        transform: { x: 0.0, y: 14.5, z: 10.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 2.2, sy: 2.2, sz: 2.2 },
        mesh: { meshId: 3 },
        material: { color: [0.2, 0.9, 1.0, 1.0], emissive: [0.2, 0.9, 1.0], emissiveIntensity: 4.0, metallic: 0.95, roughness: 0.05 },
        light: { type: "point", color: [0.2, 0.9, 1.0], intensity: 5.0, range: 12.0 },
        particles: { type: "magic", color: [0.2, 0.9, 1.0], count: 80, speed: 2.5, size: 0.25 }
    },
    // 9. Physics Supply Crates
    {
        id: 27,
        tag: "Crate",
        transform: { x: -4.0, y: 1.0, z: -3.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 1.5, sy: 1.5, sz: 1.5 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 25.0, shapeType: 0, restitution: 0.2, friction: 0.6 },
        material: { color: [0.65, 0.45, 0.25, 1.0], metallic: 0.1, roughness: 0.7 }
    },
    {
        id: 28,
        tag: "Crate",
        transform: { x: -3.8, y: 2.5, z: -3.0, rx: 0.0, ry: 15.0, rz: 0.0, sx: 1.4, sy: 1.4, sz: 1.4 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 20.0, shapeType: 0, restitution: 0.2, friction: 0.6 },
        material: { color: [0.60, 0.40, 0.20, 1.0], metallic: 0.1, roughness: 0.7 }
    },
    {
        id: 29,
        tag: "Crate",
        transform: { x: 4.0, y: 1.0, z: 5.0, rx: 0.0, ry: 0.0, rz: 0.0, sx: 1.6, sy: 1.6, sz: 1.6 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 30.0, shapeType: 0, restitution: 0.2, friction: 0.6 },
        material: { color: [0.70, 0.50, 0.30, 1.0], metallic: 0.1, roughness: 0.7 }
    }
];

const gameplayScript = `
// Lucid Odyssey: The Lost Relics
// High-Octane 3D Action-Platformer running on Lucid Vulkan Runtime

let g_gameTime = 0;
let g_collectedRelics = {
  Relic_1: false,
  Relic_2: false,
  Relic_3: false,
  Relic_4: false,
  Relic_5: false
};
let g_relicCount = 0;
let g_damageCooldown = 0;
let g_jumpPadCooldown = 0;
let g_gameOver = false;
let g_won = false;

// Store base Y positions of relics for floating sine-wave motion
const g_baseY = {
  Relic_1: 4.8,
  Relic_2: 7.8,
  Relic_3: 10.8,
  Relic_4: 8.8,
  Relic_5: 1.8
};

export function update(entities, dt, input, contacts) {
  g_gameTime += dt;
  if (g_damageCooldown > 0) g_damageCooldown -= dt;
  if (g_jumpPadCooldown > 0) g_jumpPadCooldown -= dt;

  // Handle R key restart
  if (input && input.keys && input.keys.KeyR) {
    if (g_gameOver || g_won) {
      engine.restart();
      return;
    }
  }

  // Find player entity
  let player = null;
  for (let i = 0; i < entities.length; i++) {
    const e = entities[i];
    if (e.tag === 'Player') {
      player = e;
      break;
    }
  }

  if (!player) return;

  const px = player.transform.x;
  const py = player.transform.y;
  const pz = player.transform.z;

  // Check if player fell out of arena
  if (py < -8.0 && !g_gameOver) {
    g_gameOver = true;
    engine.setHealth(0);
    engine.setGameStatus("GAME_OVER");
    engine.playSfx("laser", 1.0);
    engine.shakeCamera(0.8, 0.4);
    engine.log("Player fell into the abyss! Game Over.");
    return;
  }

  // 1. Animate and check Relic Collections
  for (let i = 0; i < entities.length; i++) {
    const e = entities[i];
    if (!e.tag) continue;

    if (e.tag.startsWith('Relic_')) {
      const relicId = e.tag;
      if (!g_collectedRelics[relicId]) {
        // Floating sine wave animation + rotation
        const base = g_baseY[relicId] || 3.0;
        const phase = (relicId.charCodeAt(relicId.length - 1)) * 1.2;
        e.transform.y = base + Math.sin(g_gameTime * 3.2 + phase) * 0.28;
        e.transform.ry = (e.transform.ry || 0) + 75.0 * dt;

        // Check distance to player
        const dx = px - e.transform.x;
        const dy = py - e.transform.y;
        const dz = pz - e.transform.z;
        const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);

        if (dist < 1.9) {
          // Relic collected!
          g_collectedRelics[relicId] = true;
          g_relicCount++;
          engine.setScore(g_relicCount);
          engine.playSfx("coin", 1.0);
          engine.shakeCamera(0.25, 0.15);

          // Hide relic mesh & shrink it out
          if (e.mesh) e.mesh.visible = false;
          e.transform.y = -999.0;
          e.transform.sx = 0.01;
          e.transform.sy = 0.01;
          e.transform.sz = 0.01;

          engine.log('Relic collected! [' + g_relicCount + '/5]');
        }
      }
    } else if (e.tag === 'HazardLaser') {
      // Rotate hazard bar continuously
      e.transform.ry = (e.transform.ry || 0) + 95.0 * dt;

      // Check distance to player
      const dx = px - e.transform.x;
      const dy = py - e.transform.y;
      const dz = pz - e.transform.z;
      const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);

      if (dist < 4.2 && Math.abs(dy) < 1.6 && g_damageCooldown <= 0 && !g_gameOver && !g_won) {
        g_damageCooldown = 0.8;
        const curHp = engine.getHealth();
        const newHp = Math.max(0, curHp - 25);
        engine.setHealth(newHp);
        engine.playSfx("laser", 1.0);
        engine.shakeCamera(0.65, 0.3);

        // Knock player away from hazard
        const knockDirX = (dx !== 0) ? dx / dist : 1.0;
        const knockDirZ = (dz !== 0) ? dz / dist : 1.0;
        engine.applyImpulse(player.id, knockDirX * 18.0, 7.0, knockDirZ * 18.0);

        engine.log('Hazard Laser Hit! Health: ' + newHp + '%');

        if (newHp <= 0) {
          g_gameOver = true;
          engine.setGameStatus("GAME_OVER");
          engine.log("Player eliminated by hazard laser! Press R to restart.");
        }
      }
    } else if (e.tag === 'JumpPad') {
      const dx = px - e.transform.x;
      const dy = py - e.transform.y;
      const dz = pz - e.transform.z;
      const distH = Math.sqrt(dx * dx + dz * dz);

      if (distH < 2.2 && Math.abs(dy) < 1.4 && g_jumpPadCooldown <= 0) {
        g_jumpPadCooldown = 0.5;
        // Launch player skyward
        engine.setVelocity(player.id, player.velocity ? player.velocity.vx * 1.2 : 0, 16.5, player.velocity ? player.velocity.vz * 1.2 : 0);
        engine.playSfx("jump", 1.0);
        engine.shakeCamera(0.35, 0.2);
        engine.log("Jump pad activated! Super leap!");
      }
    } else if (e.tag === 'PortalCore') {
      // Pulse portal core
      const pulse = 1.0 + Math.sin(g_gameTime * 4.0) * 0.15;
      e.transform.sx = 2.2 * pulse;
      e.transform.sy = 2.2 * pulse;
      e.transform.sz = 2.2 * pulse;
      e.transform.ry = (e.transform.ry || 0) + 60.0 * dt;

      // Check win condition
      const dx = px - e.transform.x;
      const dy = py - e.transform.y;
      const dz = pz - e.transform.z;
      const dist = Math.sqrt(dx * dx + dy * dy + dz * dz);

      if (dist < 3.0 && g_relicCount >= 5 && !g_won && !g_gameOver) {
        g_won = true;
        engine.setGameStatus("VICTORY");
        engine.playSfx("coin", 1.0);
        engine.shakeCamera(0.4, 0.5);
        engine.log("VICTORY! All 5 Relics returned to the Ancient Portal!");
      }
    }
  }
}
`;

const gameJson = {
    name: "LucidOdyssey",
    engineAssociation: "Lucid Native Vulkan Runtime",
    preset: "Platformer",
    camera: {
        x: 0.0,
        y: 5.0,
        z: 8.0,
        yaw: -90.0,
        pitch: -15.0,
        fov: 65.0
    },
    cameraMode: "thirdPerson",
    environment: {
        sunColor: [1.0, 0.95, 0.88],
        sunIntensity: 3.8,
        sunDirection: [-0.45, 0.78, -0.42],
        ambientIntensity: 0.42,
        environmentMap: "assets/textures/sunset.hdr",
        fogEnabled: true,
        fogDensity: 0.008,
        fogColor: [0.65, 0.75, 0.9]
    },
    settings: {
        gravity: -9.81,
        fullscreen: false,
        windowWidth: 1920,
        windowHeight: 1080
    },
    entities: entities,
    script: gameplayScript
};

fs.writeFileSync(path.join(targetDir, 'game.json'), JSON.stringify(gameJson, null, 2), 'utf8');
console.log('✅ Created standalone game project definition: game.json');

console.log('========================================================');
console.log('🎉 STANDALONE GAME DEPLOYED SUCCESSFULLY TO D:\\Lucid_Game_test');
console.log('========================================================');
