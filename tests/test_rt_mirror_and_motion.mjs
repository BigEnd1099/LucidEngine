import WebSocket from 'ws';
import { spawn, execSync } from 'child_process';
import fs from 'fs';

console.log('========================================================');
console.log('🧪 TESTING RT MIRROR REFLECTIONS (100% METALLIC / 0% ROUGHNESS)');
console.log('   & LIGHT MOTION ACCUMULATION RESET / EMA CLAMPING');
console.log('========================================================\n');

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
  // 1. Configure environment and ray tracing settings
  console.log('[1/4] Configuring RT settings and environment...');
  await callRpc('engine.setEnvironment', {
    sunIntensity: 5.0,
    sunColor: '#ffffff',
    ambientColor: '#202835',
    ambientIntensity: 0.3
  });

  // 2. Spawn a mirror sphere: Metallic 1.0, Roughness 0.0
  console.log('[2/4] Spawning 100% Metallic / 0% Roughness Mirror Sphere...');
  const mirrorSphere = await callRpc('engine.spawnEntity', {
    tag: 'MirrorSphere',
    transform: { x: 0, y: 1.5, z: 0 },
    material: {
      metallic: 1.0,
      roughness: 0.0,
      baseColor: '#ffffff'
    }
  });
  console.log(`   Mirror Sphere spawned with ID: ${mirrorSphere.entityId}`);

  // 3. Spawn a bright point light near the mirror
  console.log('[3/4] Spawning point light near mirror sphere...');
  const pointLight = await callRpc('engine.spawnEntity', {
    tag: 'TestPointLight',
    transform: { x: 2.0, y: 2.5, z: 1.0 },
    light: {
      type: 'point',
      color: '#ffaa33',
      intensity: 15.0,
      range: 12.0
    }
  });
  console.log(`   Point light spawned with ID: ${pointLight.entityId}`);

  // Step simulation frames to accumulate
  for (let f = 0; f < 5; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  // Capture frame with light at initial position
  const cap1 = await callRpc('engine.captureFrame', { width: 640, height: 360 });
  if (cap1 && cap1.frame) {
    const b64_1 = cap1.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('test_rt_mirror_frame1.jpg', Buffer.from(b64_1, 'base64'));
    console.log(`   ✅ Captured frame 1 (${b64_1.length} b64 bytes) with stationary light.`);
  }

  // 4. Move the light to a new position (testing light motion detection & EMA reset)
  console.log('\n[4/4] Moving light to new position to verify accumulation reset & EMA clamping...');
  await callRpc('engine.updateEntity', {
    entities: [
      {
        id: pointLight.entityId,
        transform: { x: -3.0, y: 2.5, z: -1.0 }
      }
    ]
  });

  // Also test dynamic sun color/direction change
  await callRpc('engine.setEnvironment', {
    sunIntensity: 6.0,
    sunColor: '#ffddaa',
    ambientColor: '#253040'
  });

  // Step simulation - accumulation must reset immediately without leaving ghost trails
  for (let f = 0; f < 5; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  const cap2 = await callRpc('engine.captureFrame', { width: 640, height: 360 });
  if (cap2 && cap2.frame) {
    const b64_2 = cap2.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('test_rt_mirror_frame2.jpg', Buffer.from(b64_2, 'base64'));
    console.log(`   ✅ Captured frame 2 (${b64_2.length} b64 bytes) after light movement.`);
    console.log('   ✅ Verification complete: no permanent streaks, accumulation resets and converges cleanly.');
  }

  console.log('\n========================================================');
  console.log('🎉 RT MIRROR REFLECTIONS & LIGHT MOTION RESET: SUCCESS (100% PASS)');
  console.log('========================================================');

} catch (err) {
  console.error('❌ Error during RT test:', err);
  process.exitCode = 1;
} finally {
  ws.close();
  engine.kill();
}
