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

  // Spawn ground plane (50x50 at y = -1.0)
  await callRpc('engine.spawnEntity', {
    tag: 'GroundPlane',
    transform: { x: 0, y: -0.05, z: 0, sx: 40, sy: 0.1, sz: 40 },
    mesh: { meshId: 1 }, // Plane
    material: {
      metallic: 0.0,
      roughness: 0.8,
      color: '#555555'
    }
  });

  // Spawn isolated 100% metallic box at (0, 1, 0)
  const box = await callRpc('engine.spawnEntity', {
    tag: 'MetallicBox',
    transform: { x: 0, y: 1.0, z: 0, sx: 2.0, sy: 2.0, sz: 2.0 },
    mesh: { meshId: 2 },
    material: {
      metallic: 1.0,
      roughness: 0.0,
      color: '#ffffff'
    }
  });

  // Set environment: HDRI ON, Sun ON
  await callRpc('engine.setEnvironment', {
    hdriEnabled: true,
    skyboxEnabled: true,
    iblEnabled: true,
    ambientIntensity: 1.0,
    sunEnabled: true,
    sunIntensity: 3.0
  });

  // Position camera to look directly at the box
  await callRpc('engine.setCamera', {
    x: 0, y: 2.5, z: 5,
    yaw: -90, pitch: -15, fov: 60
  });

  // Step 15 frames
  for (let f = 0; f < 15; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  // Capture frame
  const cap = await callRpc('engine.captureFrame', { width: 1920, height: 1080 });
  if (cap && cap.frame) {
    const b64 = cap.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('isolated_box.jpg', Buffer.from(b64, 'base64'));
    console.log(`Saved isolated_box.jpg (${b64.length} bytes)`);
  }

} catch (err) {
  console.error('Error:', err);
} finally {
  ws.close();
  engine.kill();
}
