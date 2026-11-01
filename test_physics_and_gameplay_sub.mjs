import WebSocket from 'ws';

const ws = new WebSocket('ws://localhost:9000');
let msgId = 100;
const pending = new Map();

function send(method, params = {}) {
  return new Promise((resolve, reject) => {
    const id = msgId++;
    pending.set(id, { resolve, reject });
    ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
  });
}

ws.on('message', (data) => {
  try {
    const res = JSON.parse(data.toString());
    if (res.id && pending.has(res.id)) {
      const { resolve } = pending.get(res.id);
      pending.delete(res.id);
      resolve(res.result);
    }
  } catch (e) {}
});

await new Promise((resolve) => ws.on('open', resolve));

// 1. Test Bounciness
await send('engine.resetScene', { preset: 'empty' });
await send('engine.spawnEntity', {
  tag: 'Floor',
  transform: { x: 0, y: 0, z: 0, sx: 20, sy: 1, sz: 20 },
  rigidBody: { mass: 0.0, shapeType: 0, restitution: 0.8, friction: 0.2 }
});
const ball = await send('engine.spawnEntity', {
  tag: 'BouncyBall',
  transform: { x: 0, y: 5.0, z: 0, sx: 0.8, sy: 0.8, sz: 0.8 },
  velocity: { vx: 0, vy: -5.0, vz: 0 },
  rigidBody: { mass: 1.0, shapeType: 1, restitution: 0.9, friction: 0.0 }
});

let bounced = false;
for (let i = 0; i < 40; ++i) {
  await send('engine.stepSimulation', { steps: 1, dt: 0.016 });
  const snap = await send('engine.getSnapshot');
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
}

// 2. Test QuickJS Gameplay & Score
await send('engine.loadScript', {
  code: `export function update(entities, dt) {
    if (typeof engine !== 'undefined' && engine.setScore) engine.setScore(420);
  }`
});
await send('engine.stepSimulation', { steps: 1, dt: 0.016 });
const gs = await send('engine.getGameState');
if (gs && gs.score === 420) {
  console.log('  ✅ Gameplay API: QuickJS poprawnie zarządza punktacją i stanem gry (Score: 420).');
} else {
  console.error('  ❌ BŁĄD: getGameState nie zwróciło poprawnego wyniku.');
}

ws.close();
