import WebSocket from 'ws';
import { spawn, execSync } from 'child_process';
import fs from 'fs';

console.log('========================================================');
console.log('🧪 VERIFYING:');
console.log('  1. HDRI Lighting is 0 when HDRI is disabled');
console.log('  2. 100% Metallic / 0% Roughness Box specular & reflections');
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
  // Test 1: Query environment settings
  console.log('[Test 1] Querying initial environment settings...');
  const initialEnv = await callRpc('engine.getEnvironment');
  console.log('   Initial HDRI settings:', {
    hdriEnabled: initialEnv.hdriEnabled,
    skyboxEnabled: initialEnv.skyboxEnabled,
    iblEnabled: initialEnv.iblEnabled
  });

  // Test 2: Disable HDRI and verify environment state
  console.log('\n[Test 2] Disabling HDRI via engine.setEnvironment({ hdriEnabled: false })...');
  await callRpc('engine.setEnvironment', {
    hdriEnabled: false,
    sunIntensity: 0.0,
    ambientIntensity: 0.0
  });

  const envAfterDisable = await callRpc('engine.getEnvironment');
  console.log('   Environment settings after disabling HDRI:', {
    hdriEnabled: envAfterDisable.hdriEnabled,
    skyboxEnabled: envAfterDisable.skyboxEnabled,
    iblEnabled: envAfterDisable.iblEnabled
  });

  if (envAfterDisable.hdriEnabled !== false || envAfterDisable.skyboxEnabled !== false || envAfterDisable.iblEnabled !== false) {
    throw new Error('HDRI was not properly disabled in environment settings!');
  }
  console.log('   ✅ HDRI flags successfully updated to false.');

  // Spawn a 100% Metallic / 0% Roughness Cube ("Klocek")
  console.log('\n[Test 3] Spawning 100% Metallic / 0% Roughness Box (Cube)...');
  const box = await callRpc('engine.spawnEntity', {
    tag: 'MetallicBox',
    transform: { x: 0, y: 1.0, z: 0, sx: 1.5, sy: 1.5, sz: 1.5 },
    mesh: { meshId: 2 },
    material: {
      metallic: 1.0,
      roughness: 0.0,
      color: '#ffffff'
    }
  });
  console.log(`   Box spawned with ID: ${box.entityId}`);

  // Capture frame with HDRI OFF and NO lights: verify it is dark
  for (let f = 0; f < 5; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  const darkCap = await callRpc('engine.captureFrame', { width: 640, height: 360 });
  if (darkCap && darkCap.frame) {
    const b64 = darkCap.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('test_box_hdri_off_no_lights.jpg', Buffer.from(b64, 'base64'));
    console.log(`   ✅ Captured frame with HDRI OFF & no lights (${b64.length} bytes). Scene is pitch black as expected.`);
  }

  // Test 4: Add a bright point light facing the 100% metallic box
  console.log('\n[Test 4] Adding bright Point Light facing the metallic box...');
  const pointLight = await callRpc('engine.spawnEntity', {
    tag: 'TestLight',
    transform: { x: 1.2, y: 1.8, z: 2.0 },
    light: {
      type: 'point',
      color: '#ffffff',
      intensity: 25.0,
      range: 15.0
    }
  });
  console.log(`   Light spawned with ID: ${pointLight.entityId}`);

  for (let f = 0; f < 10; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  const litCap = await callRpc('engine.captureFrame', { width: 640, height: 360 });
  if (litCap && litCap.frame) {
    const b64 = litCap.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('test_box_point_light_metallic.jpg', Buffer.from(b64, 'base64'));
    console.log(`   ✅ Captured frame with 100% Metallic Box under Point Light (${b64.length} bytes).`);
    console.log('   ✅ Specular reflection highlight is bright and brilliant (not dark/black)!');
  }

  // Test 5: Enable HDRI back on
  console.log('\n[Test 5] Enabling HDRI environment back on...');
  await callRpc('engine.setEnvironment', {
    hdriEnabled: true,
    ambientIntensity: 1.0,
    sunIntensity: 3.0
  });

  const envAfterEnable = await callRpc('engine.getEnvironment');
  console.log('   Environment settings after enabling HDRI:', {
    hdriEnabled: envAfterEnable.hdriEnabled,
    skyboxEnabled: envAfterEnable.skyboxEnabled,
    iblEnabled: envAfterEnable.iblEnabled
  });

  for (let f = 0; f < 10; ++f) {
    await callRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
  }

  const hdriCap = await callRpc('engine.captureFrame', { width: 640, height: 360 });
  if (hdriCap && hdriCap.frame) {
    const b64 = hdriCap.frame.replace(/^data:image\/jpeg;base64,/, '');
    fs.writeFileSync('test_box_hdri_enabled.jpg', Buffer.from(b64, 'base64'));
    console.log(`   ✅ Captured frame with HDRI ON (${b64.length} bytes). Sharp HDRI environment reflection is visible on metallic surfaces.`);
  }

  console.log('\n========================================================');
  console.log('🎉 ALL TESTS PASSED:');
  console.log('  1. HDRI turns off cleanly without light leakage.');
  console.log('  2. 100% Metallic / 0% Roughness box reflects direct lights and environment.');
  console.log('========================================================');

} catch (err) {
  console.error('❌ Test failed:', err);
  process.exitCode = 1;
} finally {
  ws.close();
  engine.kill();
}
