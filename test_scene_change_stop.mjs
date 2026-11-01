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

    console.log('--- TEST: SCENE CHANGE & NON-DESTRUCTIVE STOP RESTORATION ---');

    // 1. Load BREAKOUT preset
    console.log('1. Loading Breakout preset...');
    await rpc('engine.resetScene', { preset: 'breakout' });
    let snapBreakout = await rpc('engine.getSnapshot');
    const ballInitial = snapBreakout.find(e => e.tag === 'Ball');
    console.log(`Breakout Ball initial position: Y=${ballInitial.transform.y}, total entities: ${snapBreakout.length}`);

    // Play Breakout and step physics so ball moves
    await rpc('engine.playSimulation');
    await rpc('engine.stepSimulation', { steps: 25, dt: 0.016 });
    let snapRunning = await rpc('engine.getSnapshot');
    const ballMoved = snapRunning.find(e => e.tag === 'Ball');
    console.log(`Breakout Ball moved to: Y=${ballMoved.transform.y}`);
    if (Math.abs(ballMoved.transform.y - ballInitial.transform.y) < 0.1) {
        throw new Error('Ball did not move during Breakout simulation!');
    }

    // 2. NOW SWITCH SCENE TO PLATFORMER while simulation was running or stopped!
    console.log('2. Switching scene to PLATFORMER...');
    await rpc('engine.resetScene', { preset: 'platformer' });
    let snapPlatformer = await rpc('engine.getSnapshot');
    const playerInitial = snapPlatformer.find(e => e.tag === 'Player');
    console.log(`Platformer Player initial position: Y=${playerInitial.transform.y}, Z=${playerInitial.transform.z}, entities: ${snapPlatformer.length}`);

    // Verify simulation is in edit mode (paused)
    // 3. Play Platformer and step physics
    console.log('3. Playing Platformer simulation...');
    await rpc('engine.playSimulation');
    await rpc('engine.stepSimulation', { steps: 30, dt: 0.016 });
    let snapPlatRunning = await rpc('engine.getSnapshot');
    const playerMoved = snapPlatRunning.find(e => e.tag === 'Player');
    console.log(`Platformer Player simulated to: Y=${playerMoved.transform.y}`);

    // Destroy a Coin during simulation to test re-spawn on STOP
    const coinToDestroy = snapPlatRunning.find(e => e.tag === 'Coin');
    if (coinToDestroy) {
        console.log(`Destroying coin ${coinToDestroy.id} during simulation...`);
        await rpc('engine.destroyEntity', { entityId: coinToDestroy.id });
    }

    // 4. STOP simulation in Platformer
    console.log('4. Stopping Platformer simulation...');
    await rpc('engine.stopSimulation');
    let snapPlatRestored = await rpc('engine.getSnapshot');
    const playerRestored = snapPlatRestored.find(e => e.tag === 'Player');
    console.log(`Platformer Player restored to: Y=${playerRestored.transform.y}, Z=${playerRestored.transform.z}`);

    if (Math.abs(playerRestored.transform.y - playerInitial.transform.y) > 0.02) {
        throw new Error(`Platformer Player failed to return to place! Expected Y=${playerInitial.transform.y}, got Y=${playerRestored.transform.y}`);
    }
    if (Math.abs(playerRestored.transform.z - playerInitial.transform.z) > 0.02) {
        throw new Error(`Platformer Player failed to return to place! Expected Z=${playerInitial.transform.z}, got Z=${playerRestored.transform.z}`);
    }

    // Verify destroyed coin was restored back to the scene!
    const coinCount = snapPlatRestored.filter(e => e.tag === 'Coin').length;
    console.log(`Coins in restored scene: ${coinCount} (Initial was 8)`);
    if (coinCount !== 8) {
        throw new Error(`Expected 8 coins restored, got ${coinCount}`);
    }

    // 5. SWITCH SCENE TO RACER
    console.log('5. Switching scene to RACER...');
    await rpc('engine.resetScene', { preset: 'racer' });
    let snapRacer = await rpc('engine.getSnapshot');
    const carInitial = snapRacer.find(e => e.tag === 'PlayerCar');
    console.log(`Racer Car initial position: X=${carInitial.transform.x}, Y=${carInitial.transform.y}, Z=${carInitial.transform.z}`);

    await rpc('engine.playSimulation');
    await rpc('engine.stepSimulation', { steps: 30, dt: 0.016 });
    await rpc('engine.stopSimulation');

    let snapRacerRestored = await rpc('engine.getSnapshot');
    const carRestored = snapRacerRestored.find(e => e.tag === 'PlayerCar');
    console.log(`Racer Car restored position: X=${carRestored.transform.x}, Y=${carRestored.transform.y}, Z=${carRestored.transform.z}`);

    if (Math.abs(carRestored.transform.y - carInitial.transform.y) > 0.02 || Math.abs(carRestored.transform.z - carInitial.transform.z) > 0.02) {
        throw new Error(`Racer Car failed to return to place! Got Y=${carRestored.transform.y}, Z=${carRestored.transform.z}`);
    }

    console.log('✅ ALL SCENE CHANGE & NON-DESTRUCTIVE STOP RESTORATION TESTS PASSED!');
    ws.close();
}

runTest().catch(err => {
    console.error('❌ TEST FAILED:', err);
    process.exit(1);
});
