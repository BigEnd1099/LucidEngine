import WebSocket from 'ws';
import { spawn, execSync } from 'child_process';
import fs from 'fs';

try {
  if (process.platform === 'win32') {
    execSync('powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue"');
  }
} catch (e) {}

const engine = spawn('.\\engine\\build\\Release\\engine.exe', ['--headless'], {
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

  // Spawn ground plane (metallic = 0.0, roughness = 0.8)
  await callRpc('engine.spawnEntity', {
    tag: 'GroundPlane',
    transform: { x: 0, y: -0.05, z: 0, sx: 40, sy: 0.1, sz: 40 },
    mesh: { meshId: 1 },
    material: {
      metallic: 0.0,
      roughness: 0.8,
      color: '#555555'
    }
  });

  // Spawn metallic cube at (0, 1, 0) (metallic = 1.0, roughness = 0.2)
  await callRpc('engine.spawnEntity', {
    tag: 'MetallicCube',
    transform: { x: 0, y: 1.0, z: 0, sx: 2.0, sy: 2.0, sz: 2.0 },
    mesh: { meshId: 2 },
    material: {
      metallic: 1.0,
      roughness: 0.2,
      color: '#ffffff'
    }
  });

  // Spawn metallic sphere to the right (metallic = 1.0, roughness = 0.15)
  await callRpc('engine.spawnEntity', {
    tag: 'MetallicSphere',
    transform: { x: 2.2, y: 1.0, z: 0.5, sx: 1.8, sy: 1.8, sz: 1.8 },
    mesh: { meshId: 3 },
    material: {
      metallic: 1.0,
      roughness: 0.15,
      color: '#ffffff'
    }
  });

  // Spawn spotlight pointing directly at the front face of the cube (yaw = -90 points towards -Z)
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

  // Position camera to see both the cube, floor in front of it, and the sphere on the right
  await callRpc('engine.setCamera', {
    x: 0.8, y: 2.0, z: 4.5,
    yaw: -100, pitch: -12, fov: 60
  });

  // Step 20 frames for temporal convergence
  for (let f = 0; f < 20; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  // Capture frame
  const cap = await callRpc('engine.captureFrame', { width: 1920, height: 1080 });
  if (cap && cap.frame) {
    const b64 = cap.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('metallic_spotlight_gi.jpg', Buffer.from(b64, 'base64'));
    console.log(`Saved metallic_spotlight_gi.jpg (${b64.length} bytes)`);
  }

} catch (err) {
  console.error('Error:', err);
} finally {
  ws.close();
  engine.kill();
}
