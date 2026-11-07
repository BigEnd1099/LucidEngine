import WebSocket from 'ws';

export async function runTemplatesTest() {
  console.log('--- TEST: 4 GAME PRESETS & GAMEPLAY CONTROLLERS ---');
  const ws = new WebSocket('ws://127.0.0.1:9000');

  await new Promise((resolve, reject) => {
    ws.on('open', resolve);
    ws.on('error', reject);
  });

  let rpcId = 100;
  function sendRpc(method, params = {}) {
    return new Promise((resolve, reject) => {
      const id = ++rpcId;
      const handler = (data) => {
        try {
          const res = JSON.parse(data.toString());
          if (res.id === id) {
            ws.removeListener('message', handler);
            if (res.error) reject(new Error(res.error.message || JSON.stringify(res.error)));
            else resolve(res.result);
          }
        } catch {}
      };
      ws.on('message', handler);
      ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
    });
  }

  let scriptErrorCount = 0;
  ws.on('message', (data) => {
    try {
      const msg = JSON.parse(data.toString());
      if (msg.method === 'engine.onScriptError') {
        console.error('❌ Script Error caught:', msg.params);
        scriptErrorCount++;
      }
    } catch {}
  });

  const presets = ['breakout', 'platformer', 'shooter', 'racer'];

  for (const preset of presets) {
    console.log(`\nTesting Preset: ${preset.toUpperCase()}...`);
    await sendRpc('engine.resetScene', { preset });
    const snapshot = await sendRpc('engine.getSnapshot');
    console.log(`  Entities spawned: ${snapshot.length}`);
    if (snapshot.length === 0) {
      throw new Error(`Preset ${preset} has 0 entities!`);
    }

    // Step simulation for 10 frames to verify script updates & contact physics
    await sendRpc('engine.setPaused', { paused: false });
    for (let f = 0; f < 10; ++f) {
      await sendRpc('engine.stepSimulation', { steps: 1, dt: 0.016 });
    }
    await sendRpc('engine.setPaused', { paused: true });

    // Verify game status & health API
    const hp = await sendRpc('engine.getHealth');
    const status = await sendRpc('engine.getGameStatus');
    console.log(`  State: HP = ${hp.health}, Status = ${status.status}`);
  }

  if (scriptErrorCount > 0) {
    throw new Error(`Encountered ${scriptErrorCount} script errors during template evaluation!`);
  }

  console.log('  ✅ Wszystkie 4 szablony gier działają bezbłędnie w QuickJS + Jolt Physics!');
  ws.close();
}

if (process.argv[1]?.includes('test_game_templates_e2e.mjs')) {
  runTemplatesTest().catch(err => {
    console.error('❌ Błąd testu szablonów:', err);
    process.exit(1);
  });
}
