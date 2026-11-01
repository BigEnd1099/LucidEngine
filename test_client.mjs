import WebSocket from 'ws';

const ws = new WebSocket('ws://127.0.0.1:9000'); // Note: Engine starts on port 9000

function send(method, params = {}) {
  return new Promise((resolve) => {
    const id = Date.now() + Math.floor(Math.random() * 1000);
    const handler = (data) => {
      const res = JSON.parse(data.toString());
      if (res.id === id) {
        ws.off('message', handler);
        resolve(res.result);
      }
    };
    ws.on('message', handler);
    ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
  });
}

ws.on('open', async () => {
  console.log('Połączono z silnikiem!');

  // 1. Pobranie definicji typów dla agenta
  const types = await send('engine.getTypeDefinitions');
  console.log('Typy engine.d.ts:', types ? 'Pobrane pomyślnie' : 'Błąd');

  // 2. Utworzenie encji testowej
  const entity = await send('engine.spawnEntity', {
    transform: { x: 0, y: 10, z: 0 },
    velocity: { vx: 0, vy: -9.81, vz: 0 }
  });
  console.log('Utworzono encję:', entity);

  // 3. Wstrzyknięcie prostej logiki ruchu (JS)
  await send('engine.loadScript', {
    systemName: 'GravitySystem',
    code: `
      export function update(entities, dt) {
        for (const e of entities) {
          if (e.velocity && e.transform) {
            e.transform.y += e.velocity.vy * dt;
          }
        }
      }
    `
  });

  // 4. Krok symulacji o 1 sekundę
  await send('engine.stepSimulation', { steps: 1, dt: 1.0 });

  // 5. Zrzut stanu świata i weryfikacja pozycji
  const snapshot = await send('engine.getSnapshot');
  const spawned = snapshot.find(e => e.id === entity.entityId);
  console.log('Stan utworzonej encji po 1s symulacji QuickJS:', JSON.stringify(spawned, null, 2));

  ws.close();
});
