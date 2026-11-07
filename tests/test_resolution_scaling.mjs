import WebSocket from 'ws';
import { spawn } from 'child_process';
import fs from 'fs';
import path from 'path';

export async function runResolutionScalingTest() {
  console.log('🚀 Starting Resolution & UI Scaling Test...');

  // Start engine if not already running
  let engineProcess = null;
  let ws = null;

  async function connectWs(retries = 25) {
    for (let i = 0; i < retries; i++) {
      try {
        const socket = new WebSocket('ws://127.0.0.1:9000');
        await new Promise((resolve, reject) => {
          socket.once('open', resolve);
          socket.once('error', reject);
        });
        return socket;
      } catch (err) {
        await new Promise(r => setTimeout(r, 400));
      }
    }
    throw new Error('Could not connect to WebSocket on ws://127.0.0.1:9000');
  }

  try {
    ws = await connectWs(2);
    console.log('Connected to existing engine instance.');
  } catch (e) {
    console.log('Spawning engine/build/Release/engine.exe --headless...');
    engineProcess = spawn('engine/build/Release/engine.exe', ['--headless'], {
      cwd: process.cwd(),
      stdio: 'ignore',
      detached: false
    });
    ws = await connectWs(25);
    console.log('Connected to newly spawned engine instance.');
  }

  let rpcId = 1;
  function sendRpc(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++rpcId;
      const timeout = setTimeout(() => {
        reject(new Error(`Timeout waiting for RPC ${method}`));
      }, 8000);

      const handler = (data) => {
        try {
          const msg = JSON.parse(data.toString());
          if (msg.id === id) {
            clearTimeout(timeout);
            ws.removeListener('message', handler);
            if (msg.error) reject(new Error(msg.error.message || JSON.stringify(msg.error)));
            else resolve(msg.result);
          }
        } catch {}
      };

      ws.on('message', handler);
      ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    });
  }

  try {
    // 1. Initial State Check
    console.log('\n--- 1. Testing Initial State ---');
    const initRes = await sendRpc('engine.getRenderResolution');
    console.log('Initial Render Resolution:', initRes);
    if (!initRes || typeof initRes.width !== 'number' || typeof initRes.height !== 'number') {
      throw new Error(`Invalid getRenderResolution response: ${JSON.stringify(initRes)}`);
    }

    const initResScale = await sendRpc('engine.getResolutionScale');
    console.log('Initial Resolution Scale:', initResScale);

    const initUiScale = await sendRpc('engine.getUiScale');
    console.log('Initial UI Scale:', initUiScale);

    // 2. Test engine.setUiScale
    console.log('\n--- 2. Testing engine.setUiScale ---');
    const uiScaleSetRes = await sendRpc('engine.setUiScale', { scale: 1.25 });
    console.log('engine.setUiScale(1.25) response:', uiScaleSetRes);
    const updatedUiScale = await sendRpc('engine.getUiScale');
    console.log('Updated UI Scale:', updatedUiScale);
    if (Math.abs(updatedUiScale.scale - 1.25) > 0.01) {
      throw new Error(`Expected UI Scale ~1.25, got ${updatedUiScale.scale}`);
    }

    // 3. Test engine.setResolutionScale (50% - 540p)
    console.log('\n--- 3. Testing Resolution Scale 50% (960x540) ---');
    const scale50Res = await sendRpc('engine.setResolutionScale', { scale: 0.5 });
    console.log('engine.setResolutionScale(0.5) response:', scale50Res);
    const res50 = await sendRpc('engine.getRenderResolution');
    console.log('Render Resolution at 50%:', res50);
    if (res50.width !== 960 || res50.height !== 540) {
      throw new Error(`Expected 960x540 at 50% scale, got ${res50.width}x${res50.height}`);
    }

    // Capture frame at 50% scale
    const frame50 = await sendRpc('engine.captureFrame');
    const frameData50 = frame50?.frame || frame50?.data;
    if (!frameData50 || frameData50.length < 500) {
      throw new Error(`Failed to capture frame at 50% scale: ${JSON.stringify(frame50)}`);
    }
    console.log(`Captured frame at 50% scale: ${frameData50.length} base64 chars`);

    // 4. Test engine.setResolutionScale (75% - 1440x810)
    console.log('\n--- 4. Testing Resolution Scale 75% (1440x810) ---');
    const scale75Res = await sendRpc('engine.setResolutionScale', { scale: 0.75 });
    console.log('engine.setResolutionScale(0.75) response:', scale75Res);
    const res75 = await sendRpc('engine.getRenderResolution');
    console.log('Render Resolution at 75%:', res75);
    if (res75.width !== 1440 || res75.height !== 810) {
      throw new Error(`Expected 1440x810 at 75% scale, got ${res75.width}x${res75.height}`);
    }

    // Capture frame at 75% scale
    const frame75 = await sendRpc('engine.captureFrame');
    const frameData75 = frame75?.frame || frame75?.data;
    if (!frameData75 || frameData75.length < 500) {
      throw new Error('Failed to capture frame at 75% scale');
    }
    console.log(`Captured frame at 75% scale: ${frameData75.length} base64 chars`);

    // 5. Test engine.setRenderResolution (Custom 1280x720)
    console.log('\n--- 5. Testing Custom Render Resolution (1280x720) ---');
    const customRes = await sendRpc('engine.setRenderResolution', { width: 1280, height: 720 });
    console.log('engine.setRenderResolution(1280, 720) response:', customRes);
    const resCustom = await sendRpc('engine.getRenderResolution');
    console.log('Current Render Resolution:', resCustom);
    if (resCustom.width !== 1280 || resCustom.height !== 720) {
      throw new Error(`Expected 1280x720, got ${resCustom.width}x${resCustom.height}`);
    }

    // Capture frame at 1280x720
    const frameCustom = await sendRpc('engine.captureFrame');
    const frameDataCustom = frameCustom?.frame || frameCustom?.data;
    if (!frameDataCustom || frameDataCustom.length < 500) {
      throw new Error('Failed to capture frame at 1280x720');
    }
    console.log(`Captured frame at 1280x720: ${frameDataCustom.length} base64 chars`);

    // 6. Test Restoring to 100% Native (1920x1080) and UI Scale 1.0
    console.log('\n--- 6. Restoring to 100% Native (1920x1080) and UI Scale 1.0 ---');
    await sendRpc('engine.setResolutionScale', { scale: 1.0 });
    await sendRpc('engine.setUiScale', { scale: 1.0 });
    const finalRes = await sendRpc('engine.getRenderResolution');
    const finalUi = await sendRpc('engine.getUiScale');
    console.log('Final Render Resolution:', finalRes);
    console.log('Final UI Scale:', finalUi);
    if (finalRes.width !== 1920 || finalRes.height !== 1080) {
      throw new Error(`Expected 1920x1080, got ${finalRes.width}x${finalRes.height}`);
    }

    // 7. Verify Environment serialization
    console.log('\n--- 7. Testing Environment Serialization with Scaling ---');
    const env = await sendRpc('engine.getEnvironment');
    console.log('Environment settings:', {
      uiScale: env.uiScale,
      resolutionScale: env.resolutionScale,
      renderResolution: env.renderResolution
    });

    console.log('\n✅ ALL RESOLUTION & UI SCALING TESTS PASSED SUCCESSFULLY!');
  } finally {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.close();
    }
    if (engineProcess) {
      engineProcess.kill('SIGTERM');
    }
  }
}

if (process.argv[1] && process.argv[1].endsWith('test_resolution_scaling.mjs')) {
  runResolutionScalingTest().catch(err => {
    console.error('❌ Test failed:', err);
    process.exit(1);
  });
}
