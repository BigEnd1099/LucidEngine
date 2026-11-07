import { TestRpcClient } from './test_utils.mjs';

export async function runClientTest() {
  const client = new TestRpcClient('ws://127.0.0.1:9000');
  try {
    await client.connect();
    console.log('Połączono z silnikiem!');

    // 1. Pobranie definicji typów dla agenta
    const types = await client.send('engine.getTypeDefinitions');
    console.log('Typy engine.d.ts:', types ? 'Pobrane pomyślnie' : 'Błąd');

    // 2. Utworzenie encji testowej
    const entity = await client.send('engine.spawnEntity', {
      transform: { x: 0, y: 10, z: 0 },
      velocity: { vx: 0, vy: -9.81, vz: 0 }
    });
    console.log('Utworzono encję:', entity);

    // 3. Wstrzyknięcie prostej logiki ruchu (JS)
    await client.send('engine.loadScript', {
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
    await client.send('engine.stepSimulation', { steps: 1, dt: 1.0 });

    // 5. Zrzut stanu świata i weryfikacja pozycji
    const snapshot = await client.send('engine.getSnapshot');
    const spawned = snapshot.find(e => e.id === entity.entityId);
    console.log('Stan utworzonej encji po 1s symulacji QuickJS:', JSON.stringify(spawned, null, 2));

    client.close();
    return true;
  } catch (err) {
    console.error('Błąd testu client:', err.message);
    client.close();
    throw err;
  }
}

if (process.argv[1] && process.argv[1].endsWith('test_client.mjs')) {
  runClientTest().then(() => process.exit(0)).catch(() => process.exit(1));
}
