import { TestRpcClient } from './test_utils.mjs';

export async function runPhysicsTest() {
  const client = new TestRpcClient('ws://127.0.0.1:9000');
  try {
    await client.connect();

    // 1. Test Bounciness
    await client.send('engine.resetScene', { preset: 'empty' });
    await client.send('engine.spawnEntity', {
      tag: 'Floor',
      transform: { x: 0, y: 0, z: 0, sx: 20, sy: 1, sz: 20 },
      rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.8, friction: 0.2 }
    });
    const ball = await client.send('engine.spawnEntity', {
      tag: 'BouncyBall',
      transform: { x: 0, y: 5.0, z: 0, sx: 0.8, sy: 0.8, sz: 0.8 },
      velocity: { vx: 0, vy: -5.0, vz: 0 },
      rigidBody: { mass: 1.0, shapeType: 1, restitution: 0.9, friction: 0.0 }
    });

    let bounced = false;
    for (let i = 0; i < 40; ++i) {
      await client.send('engine.stepSimulation', { steps: 1, dt: 0.016 });
      const snap = await client.send('engine.getSnapshot');
      const b = snap.find(e => e.id === ball.entityId);
      if (b && b.velocity && b.velocity.vy > 1.0) {
        bounced = true;
        break;
      }
    }

    if (bounced) {
      console.log('  ✅ Bounciness (Restitution): Piłka poprawnie odbija się od podłoża w Jolt Physics.');
    } else {
      console.error('  ❌ BŁĄD: Piłka nie odbiła się!');
      throw new Error('Piłka nie odbiła się w symulacji Jolt Physics.');
    }

    // 2. Test QuickJS Gameplay & Score
    await client.send('engine.loadScript', {
      code: `export function update(entities, dt) {
        if (typeof engine !== 'undefined' && engine.setScore) engine.setScore(420);
      }`
    });
    await client.send('engine.stepSimulation', { steps: 1, dt: 0.016 });
    const gs = await client.send('engine.getGameState');
    if (gs && gs.score === 420) {
      console.log('  ✅ Gameplay API: QuickJS poprawnie zarządza punktacją i stanem gry (Score: 420).');
    } else {
      console.error('  ❌ BŁĄD: getGameState nie zwróciło poprawnego wyniku.');
      throw new Error('getGameState nie zwróciło poprawnego wyniku.');
    }

    client.close();
    return true;
  } catch (err) {
    client.close();
    throw err;
  }
}

if (process.argv[1] && process.argv[1].endsWith('test_physics_and_gameplay_sub.mjs')) {
  runPhysicsTest().then(() => process.exit(0)).catch(() => process.exit(1));
}
