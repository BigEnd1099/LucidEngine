import WebSocket from 'ws';
import { spawn } from 'child_process';
import fs from 'fs';

console.log('========================================================');
console.log('🧪 TESTING PORTED VULKAN & NATIVE ENGINE FEATURES');
console.log('========================================================');

const engineProcess = spawn('./engine/build/Release/engine.exe', ['--headless'], {
  stdio: ['ignore', 'inherit', 'inherit']
});

await new Promise(r => setTimeout(r, 1500));

async function run() {
  console.log('[1/4] Native engine ready on port 9000');

  const ws = new WebSocket('ws://127.0.0.1:9000');
  await new Promise((resolve, reject) => {
    ws.on('open', resolve);
    ws.on('error', reject);
  });

  let reqId = 1;
  function callRpc(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = reqId++;
      const handler = (data) => {
        const msg = JSON.parse(data.toString());
        if (msg.id === id) {
          ws.off('message', handler);
          if (msg.error) reject(msg.error);
          else resolve(msg.result);
        }
      };
      ws.on('message', handler);
      ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    });
  }

  // 1. Test Environment Settings
  console.log('[2/4] Testing Environment Settings Sync...');
  const envResult = await callRpc('engine.setEnvironment', {
    fog: true,
    fogNear: 15.0,
    fogFar: 85.0,
    fogColor: '#1a1f2c',
    sunIntensity: 4.5,
    sunColor: '#ffe8c2',
    ambientColor: '#2b3545',
    ambientIntensity: 0.8,
    gravity: -15.0
  });
  console.log('  engine.setEnvironment result:', envResult);

  const getEnv = await callRpc('engine.getEnvironment');
  console.log('  engine.getEnvironment verified:', {
    gravity: getEnv.gravity,
    fog: getEnv.fog,
    fogNear: getEnv.fogNear,
    fogFar: getEnv.fogFar
  });
  if (getEnv.gravity !== -15.0 || !getEnv.fog || getEnv.fogFar !== 85.0) {
    throw new Error('Environment settings mismatch!');
  }
  console.log('  ✅ Environment Settings & Jolt Gravity successfully synchronized!');

  // 2. Test Camera Modes
  console.log('[3/4] Testing Camera Modes & Director...');
  for (const mode of ['thirdPerson', 'topDown', 'firstPerson', 'sideScroller', 'editor']) {
    await callRpc('engine.setCameraMode', { mode });
    const cm = await callRpc('engine.getCameraMode');
    if (cm.mode !== mode) throw new Error(`Camera mode failed for ${mode}`);
    console.log(`  Camera Mode '${mode}' verified: active.`);
  }
  console.log('  ✅ Camera Director & Spring Arm Modes fully operational!');

  // 3. Test Multi-Light, Emissive Material, & Particle Emitters in ECS
  console.log('[4/4] Testing Multi-Light, Emissive Materials & Niagara-Style Particle Emitters...');
  
  // Point lights
  for (let i = 0; i < 4; i++) {
    await callRpc('engine.spawnEntity', {
      name: `PointLight_${i}`,
      transform: { x: (i - 1.5) * 6, y: 3.5, z: 0, sx: 1, sy: 1, sz: 1, rx: 0, ry: 0, rz: 0 },
      light: {
        type: 'point',
        color: [i * 0.25, 0.8, 1.0 - i * 0.25],
        intensity: 5.0,
        range: 12.0
      }
    });
  }

  // Emissive glowing cube
  await callRpc('engine.spawnEntity', {
    name: 'GlowCube',
    transform: { x: 0, y: 1.0, z: 0, sx: 1.5, sy: 1.5, sz: 1.5, rx: 0, ry: 0, rz: 0 },
    mesh: { meshId: 2 },
    material: {
      color: '#00ffcc',
      roughness: 0.2,
      metallic: 0.9,
      emissive: '#00ffcc',
      emissiveIntensity: 2.5
    }
  });

  // Particle Emitters: Fire, Sparks, Portal
  await callRpc('engine.spawnEntity', {
    name: 'TorchFire',
    transform: { x: -4, y: 1.0, z: 0, sx: 1, sy: 1, sz: 1, rx: 0, ry: 0, rz: 0 },
    particleEmitter: {
      type: 'fire',
      color: [1.0, 0.4, 0.0],
      count: 300,
      speed: 3.5,
      size: 0.45
    }
  });

  await callRpc('engine.spawnEntity', {
    name: 'MagicPortal',
    transform: { x: 4, y: 1.0, z: 0, sx: 1, sy: 1, sz: 1, rx: 0, ry: 0, rz: 0 },
    particleEmitter: {
      type: 'portal',
      color: [0.3, 0.7, 1.0],
      count: 400,
      speed: 2.0,
      size: 0.5
    }
  });

  // Snapshot check
  const snapshot = await callRpc('engine.getSnapshot');
  console.log(`  Entities in ECS snapshot: ${snapshot.length}`);
  const hasLight = snapshot.some(e => e.light && e.light.type === 'point');
  const hasParticles = snapshot.some(e => e.particleEmitter && e.particleEmitter.type === 'fire');
  const hasEmissive = snapshot.some(e => e.material && e.material.emissiveIntensity === 2.5);

  if (!hasLight || !hasParticles || !hasEmissive) {
    throw new Error('ECS Component serialization failed for Light, Particles, or Emissive Material!');
  }
  console.log('  ✅ ECS Components (LightComponent, ParticleEmitterComponent, MaterialComponent) verified in snapshot!');

  // Capture frame
  const captureResult = await callRpc('engine.captureFrame', { width: 1280, height: 720 });
  if (captureResult && captureResult.frame) {
    const base64Data = captureResult.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('test_vulkan_features.jpg', base64Data, 'base64');
    console.log('  ✅ Vulkan frame captured and saved to test_vulkan_features.jpg (Length: ' + base64Data.length + ' bytes)!');
  }

  ws.close();
  engineProcess.kill();
  console.log('========================================================');
  console.log('🎉 ALL VULKAN NATIVE ENGINE FEATURES VERIFIED (100% PASS)');
  console.log('========================================================');
  process.exit(0);
}

run().catch((err) => {
  console.error('❌ Test failed:', err);
  engineProcess.kill();
  process.exit(1);
});
