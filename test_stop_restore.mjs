import WebSocket from 'ws';

function delay(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }

async function runTest() {
    const ws = new WebSocket('ws://127.0.0.1:9000');
    await new Promise((resolve, reject) => {
        ws.on('open', resolve);
        ws.on('error', reject);
    });

    let msgId = 1;
    function rpc(method, params = {}) {
        return new Promise((resolve, reject) => {
            const id = msgId++;
            const handler = (data) => {
                try {
                    const msg = JSON.parse(data.toString());
                    if (msg.id === id) {
                        ws.off('message', handler);
                        if (msg.error) reject(new Error(msg.error.message));
                        else resolve(msg.result);
                    }
                } catch (e) {}
            };
            ws.on('message', handler);
            ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
        });
    }

    console.log('--- TEST: STOP NON-DESTRUCTIVE EDIT RESTORE ---');
    
    // 1. Reset to empty scene
    await rpc('engine.resetScene', { preset: 'empty' });
    
    // 2. Spawn a custom user entity at Y = 15.0
    const spawnRes = await rpc('engine.spawnEntity', {
        tag: 'UserCustomActor',
        transform: { x: 2.5, y: 15.0, z: -1.0, sx: 1, sy: 1, sz: 1 },
        velocity: { vx: 0, vy: 0, vz: 0 },
        mesh: { meshId: 2 },
        rigidBody: { mass: 1.0, shapeType: 0 }
    });
    const entId = spawnRes.entityId;
    console.log(`1. Spawned UserCustomActor with ID ${entId} at Y = 15.0`);

    // Verify initial pos
    let snap = await rpc('engine.getSnapshot');
    let actor = snap.find(e => e.id === entId);
    console.log(`Initial Y: ${actor.transform.y}`);
    if (Math.abs(actor.transform.y - 15.0) > 0.01) {
        throw new Error('Initial position did not match 15.0!');
    }

    // 3. Start simulation (Play)
    console.log('2. Starting simulation (Play)...');
    await rpc('engine.playSimulation');

    // 4. Step physics for 30 ticks (0.5s of gravity fall)
    console.log('3. Running physics simulation for 30 ticks...');
    await rpc('engine.stepSimulation', { steps: 30, dt: 0.016 });
    snap = await rpc('engine.getSnapshot');
    actor = snap.find(e => e.id === entId);
    console.log(`Simulated falling Y: ${actor.transform.y} (Linear Velocity Y: ${actor.velocity.vy})`);
    if (actor.transform.y >= 14.5) {
        throw new Error('Object did not fall during simulation!');
    }

    // 5. Stop simulation (Stop)
    console.log('4. Stopping simulation (Stop)...');
    await rpc('engine.stopSimulation');
    snap = await rpc('engine.getSnapshot');
    actor = snap.find(e => e.id === entId);
    console.log(`Restored edit pose Y: ${actor.transform.y} (Linear Velocity Y: ${actor.velocity.vy})`);

    // Verify it restored to 15.0 and did NOT reset to breakout or wipe the actor!
    if (Math.abs(actor.transform.y - 15.0) > 0.01) {
        throw new Error(`Stop failed to restore original Y=15.0! Got: ${actor.transform.y}`);
    }
    if (Math.abs(actor.velocity.vy) > 0.001) {
        throw new Error(`Stop failed to zero velocity! Got: ${actor.velocity.vy}`);
    }
    if (snap.length > 5 && snap.some(e => e.tag === 'Paddle' || e.tag === 'Brick')) {
        throw new Error('Stop wiped level and reset to breakout!');
    }

    console.log('✅ TEST PASSED: STOP restored pre-simulation snapshot cleanly without resetting scene!');
    ws.close();
}

runTest().catch(err => {
    console.error('❌ TEST FAILED:', err);
    process.exit(1);
});
