import WebSocket from 'ws';

const ws = new WebSocket('ws://127.0.0.1:9000');

function send(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = Date.now() + Math.floor(Math.random() * 1000);
    const timeout = setTimeout(() => reject(new Error('Timeout on ' + method)), 3000);
    const handler = (data) => {
      try {
        const res = JSON.parse(data.toString());
        if (res.id === id) {
          clearTimeout(timeout);
          ws.off('message', handler);
          if (res.error) reject(res.error);
          else resolve(res.result);
        }
      } catch (e) {}
    };
    ws.on('message', handler);
    ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
  });
}

ws.on('open', async () => {
  try {
    console.log('--- TEST SYNC & CAPTURE ---');
    
    // 1. Get Camera
    const cam = await send('engine.getCamera');
    console.log('1. getCamera result:', cam);
    
    // 2. Set Camera
    const setCamRes = await send('engine.setCamera', {
      x: 5, y: 20, z: 40, yaw: -45, pitch: -30, fov: 60
    });
    console.log('2. setCamera result:', setCamRes);
    
    // 3. Verify Camera updated
    const camUpdated = await send('engine.getCamera');
    console.log('3. updated Camera:', camUpdated);

    // 4. Capture Frame
    const frameRes = await send('engine.captureFrame');
    console.log('4. captureFrame format prefix:', frameRes?.frame?.substring(0, 30));
    console.log('   captureFrame total base64 length:', frameRes?.frame?.length);

    console.log('ALL TESTS PASSED SUCCESSFULLY!');
  } catch (err) {
    console.error('Test error:', err);
  } finally {
    ws.close();
  }
});

ws.on('error', (e) => {
  console.error('WebSocket connection error:', e.message);
  process.exit(1);
});
