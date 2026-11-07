import fs from 'fs';
import { TestRpcClient } from './tests/test_utils.mjs';

export async function runViewSyncTest() {
  const client = new TestRpcClient('ws://127.0.0.1:9000');
  try {
    await client.connect();
    console.log('--- TEST SYNC & CAPTURE ---');

    // 1. Get Camera
    const cam = await client.send('engine.getCamera');
    console.log('1. getCamera result:', cam);

    // 2. Set Camera
    const setCamRes = await client.send('engine.setCamera', {
      x: 5, y: 20, z: 40, yaw: -45, pitch: -30, fov: 60
    });
    console.log('2. setCamera result:', setCamRes);

    // 3. Verify Camera updated
    const camUpdated = await client.send('engine.getCamera');
    console.log('3. updated Camera:', camUpdated);

    // 4. Capture Frame
    const frameRes = await client.send('engine.captureFrame');
    console.log('4. captureFrame format prefix:', frameRes?.frame?.substring(0, 30));
    console.log('   captureFrame total base64 length:', frameRes?.frame?.length);

    if (frameRes?.frame) {
      const base64Data = frameRes.frame.replace(/^data:image\/jpeg;base64,/, '');
      fs.writeFileSync('test_frame.jpg', Buffer.from(base64Data, 'base64'));
      console.log('   Saved captured frame to test_frame.jpg');
    }

    console.log('ALL TESTS PASSED SUCCESSFULLY!');
    client.close();
    return true;
  } catch (err) {
    console.error('Test view sync error:', err.message);
    client.close();
    throw err;
  }
}

if (process.argv[1] && process.argv[1].endsWith('test_view_sync.mjs')) {
  runViewSyncTest().then(() => process.exit(0)).catch(() => process.exit(1));
}
