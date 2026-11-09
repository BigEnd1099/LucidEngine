import WebSocket from 'ws';
import { spawn, execSync } from 'child_process';
import fs from 'fs';

try {
  if (process.platform === 'win32') {
    execSync('powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue"');
  }
} catch (e) {}

const engine = spawn('./engine/build/Release/engine.exe', ['--headless'], {
  stdio: ['ignore', 'inherit', 'inherit']
});

await new Promise(r => setTimeout(r, 2000));

const ws = new WebSocket('ws://127.0.0.1:9000');
let msgId = 1;

function callRpc(method, params = {}) {
  return new Promise((resolve, reject) => {
    const curId = msgId++;
    const req = { jsonrpc: '2.0', id: curId, method, params };
    const timeout = setTimeout(() => {
      reject(new Error(`Timeout waiting for RPC response: ${method}`));
    }, 5000);

    const onMessage = (data) => {
      try {
        const msg = JSON.parse(data.toString());
        if (msg.id === curId) {
          clearTimeout(timeout);
          ws.off('message', onMessage);
          if (msg.error) reject(new Error(msg.error.message || 'RPC Error'));
          else resolve(msg.result);
        }
      } catch (e) {}
    };

    ws.on('message', onMessage);
    ws.send(JSON.stringify(req));
  });
}

await new Promise((resolve, reject) => {
  ws.on('open', resolve);
  ws.on('error', reject);
});

console.log('Connected to Lucid Engine via WebSocket.\n');

try {
  // Reset scene to completely EMPTY
  await callRpc('engine.resetScene', { preset: 'empty' });

  // 1. Ground plane
  await callRpc('engine.spawnEntity', {
    tag: 'GroundPlane',
    transform: { x: 0, y: -0.05, z: 0, sx: 40, sy: 0.1, sz: 40 },
    mesh: { meshId: 1 },
    material: { metallic: 0.0, roughness: 0.8, color: '#444444' }
  });

  // 2. Green Wall on left side
  await callRpc('engine.spawnEntity', {
    tag: 'GreenWall',
    transform: { x: -3.5, y: 2.0, z: 0.0, sx: 0.5, sy: 4.0, sz: 6.0 },
    mesh: { meshId: 2 },
    material: { metallic: 0.0, roughness: 0.7, color: '#22cc44' }
  });

  // 3. Purple Sphere (reflected in cube)
  await callRpc('engine.spawnEntity', {
    tag: 'PurpleSphere',
    transform: { x: -0.6, y: 2.8, z: -0.8, sx: 1.2, sy: 1.2, sz: 1.2 },
    mesh: { meshId: 3 },
    material: { metallic: 0.1, roughness: 0.5, color: '#9933cc' }
  });

  // 4. Blue Sphere (glossy metal)
  await callRpc('engine.spawnEntity', {
    tag: 'BlueSphere',
    transform: { x: 2.2, y: 1.0, z: 0.5, sx: 1.6, sy: 1.6, sz: 1.6 },
    mesh: { meshId: 3 },
    material: { metallic: 0.85, roughness: 0.12, color: '#3388ff' }
  });

  // 5. Spotlight pointing at front face of the cube
  await callRpc('engine.spawnEntity', {
    tag: 'Spotlight',
    transform: { x: 0.0, y: 1.5, z: 4.5, rx: -10, ry: -90, rz: 0 },
    light: {
      type: 'spot',
      color: '#ffffff',
      intensity: 15.0,
      range: 25.0,
      innerConeAngle: 15.0,
      outerConeAngle: 28.0
    }
  });

  // Set environment: isolate spotlight (HDRI OFF, Sun OFF)
  await callRpc('engine.setEnvironment', {
    hdriEnabled: false,
    skyboxEnabled: false,
    iblEnabled: false,
    ambientIntensity: 0.0,
    sunEnabled: false,
    sunIntensity: 0.0
  });

  // Position camera to see cube, green wall, purple sphere reflection, and blue sphere
  await callRpc('engine.setCamera', {
    x: 0.5, y: 2.2, z: 4.8,
    yaw: -96, pitch: -12, fov: 60
  });

  // TEST CASE A: Metallic Cube with Roughness = 0.0% (Mirror metal)
  console.log('Testing Metallic = 1.0, Roughness = 0.0%...');
  const cubeEntity = await callRpc('engine.spawnEntity', {
    tag: 'MetallicCube',
    transform: { x: 0.0, y: 1.0, z: 0.0, sx: 2.0, sy: 2.0, sz: 2.0 },
    mesh: { meshId: 2 },
    material: { metallic: 1.0, roughness: 0.0, color: '#ffffff' }
  });

  for (let f = 0; f < 20; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  const capA = await callRpc('engine.captureFrame', { width: 1920, height: 1080 });
  if (capA && capA.frame) {
    const b64 = capA.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('scene_mirror_metal.jpg', Buffer.from(b64, 'base64'));
    console.log(`Saved scene_mirror_metal.jpg (${b64.length} bytes)`);
  }

  // TEST CASE B: Metallic Cube with Roughness = 0.20% (Slightly rough metal)
  console.log('Testing Metallic = 1.0, Roughness = 0.20%...');
  await callRpc('engine.destroyEntity', { id: cubeEntity.entityId });
  await callRpc('engine.spawnEntity', {
    tag: 'MetallicCube',
    transform: { x: 0.0, y: 1.0, z: 0.0, sx: 2.0, sy: 2.0, sz: 2.0 },
    mesh: { meshId: 2 },
    material: { metallic: 1.0, roughness: 0.20, color: '#ffffff' }
  });

  for (let f = 0; f < 20; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  const capB = await callRpc('engine.captureFrame', { width: 1920, height: 1080 });
  if (capB && capB.frame) {
    const b64 = capB.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('scene_rough_metal.jpg', Buffer.from(b64, 'base64'));
    console.log(`Saved scene_rough_metal.jpg (${b64.length} bytes)`);
  }

} catch (err) {
  console.error('Error:', err);
} finally {
  ws.close();
  engine.kill();
}
