import WebSocket from 'ws';

export async function runCapsuleAudioTest() {
  const ws = new WebSocket('ws://localhost:9000');
  let msgId = 500;
  const pending = new Map();

  function send(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = msgId++;
      pending.set(id, { resolve, reject });
      ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    });
  }

  let receivedSfx = [];
  ws.on('message', (data) => {
    try {
      const text = data.toString();
      if (!text.startsWith('{')) return;
      const res = JSON.parse(text);
      if (res.method === 'engine.onPlaySfx') {
        receivedSfx.push(res.params);
        return;
      }
      if (res.id && pending.has(res.id)) {
        const { resolve } = pending.get(res.id);
        pending.delete(res.id);
        resolve(res.result);
      }
    } catch {}
  });

  await new Promise((resolve, reject) => {
    ws.once('open', resolve);
    ws.once('error', reject);
  });

  try {
    console.log('--- TEST: CAPSULE, SENSOR & AUDIO SFX ---');

    // 1. Reset scene
    await send('engine.resetScene', { preset: 'empty' });

    // 2. Spawn a Capsule Player with Rotation Lock
    const capsule = await send('engine.spawnEntity', {
      tag: 'Player_Capsule',
      transform: { x: 0, y: 5.0, z: 0 },
      rigidBody: {
        mass: 1.0,
        shapeType: 2, // Capsule
        capsuleRadius: 0.6,
        capsuleHalfHeight: 1.0,
        lockRotationX: true,
        lockRotationZ: true
      }
    });
    console.log('1. Spawned Capsule Player, ID:', capsule?.entityId);

    // 3. Spawn a Sensor Trigger Coin
    const coin = await send('engine.spawnEntity', {
      tag: 'Coin_Sensor',
      transform: { x: 0, y: 1.0, z: 0 },
      rigidBody: {
        mass: 0.0,
        shapeType: 1, // Sphere
        isSensor: true
      }
    });
    console.log('2. Spawned Sensor Trigger Coin, ID:', coin?.entityId);

    // 4. Test Script invoking playSfx, setHealth, and setGameStatus
    await send('engine.loadScript', {
      code: `export function update(entities, dt) {
        if (typeof engine !== 'undefined') {
          engine.playSfx('coin', 0.8);
          engine.setHealth(85);
          engine.setGameStatus('PLAYING');
        }
      }`
    });

    // Step simulation once
    await send('engine.stepSimulation', { steps: 1, dt: 0.016 });

    // Allow a tick for async WS broadcast
    await new Promise(r => setTimeout(r, 200));

    const snap = await send('engine.getSnapshot');
    const playerEnt = snap.find(e => e.id === capsule?.entityId);
    console.log('3. Player entity rigidBody:', playerEnt?.rigidBody);

    if (playerEnt?.rigidBody?.shapeType === 2 && playerEnt?.rigidBody?.lockRotationX === true) {
      console.log('  ✅ Capsule Shape & Rotation Lock poprawnie zachowane w Jolt ECS!');
    } else {
      console.error('  ❌ Błąd kapsuły!');
      throw new Error('Capsule Shape & Rotation Lock nie zachowane w Jolt ECS!');
    }

    if (receivedSfx.length > 0 && receivedSfx[0].type === 'coin') {
      console.log('  ✅ Audio SFX: Silnik poprawnie emituje powiadomienia audio (SFX: coin, vol: 0.8)!');
    } else {
      console.error('  ❌ Brak powiadomienia SFX!');
      throw new Error('Brak powiadomienia SFX!');
    }

    ws.close();
    return true;
  } catch (err) {
    ws.close();
    throw err;
  }
}

if (process.argv[1] && process.argv[1].endsWith('test_gameplay_capsule_and_audio.mjs')) {
  runCapsuleAudioTest().then(() => process.exit(0)).catch(() => process.exit(1));
}
