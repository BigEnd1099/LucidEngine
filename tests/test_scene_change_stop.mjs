import WebSocket from 'ws';

export async function runSceneChangeStopTest() {
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
                    const text = data.toString();
                    if (!text.startsWith('{')) return;
                    const msg = JSON.parse(text);
                    if (msg.id === id) {
                        ws.off('message', handler);
                        if (msg.error) reject(new Error(msg.error.message));
                        else resolve(msg.result);
                    }
                } catch {}
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
    let snapPlat = await rpc('engine.getSnapshot');
    const platPlayer = snapPlat.find(e => e.tag === 'Player' || e.tag === 'Player_Platformer');
    if (!platPlayer) {
        throw new Error('Platformer Player entity not found in scene snapshot!');
    }
    console.log(`Platformer Player initial position: Y=${platPlayer.transform.y}, Z=${platPlayer.transform.z}, entities: ${snapPlat.length}`);

    // 3. Play Platformer and simulate physics
    console.log('3. Playing Platformer simulation...');
    await rpc('engine.playSimulation');
    await rpc('engine.stepSimulation', { steps: 20, dt: 0.016 });
    let snapPlatSim = await rpc('engine.getSnapshot');
    const platPlayerSim = snapPlatSim.find(e => e.id === platPlayer.id);
    console.log(`Platformer Player simulated to: Y=${platPlayerSim.transform.y}`);

    // Destroy a coin during gameplay
    const firstCoin = snapPlatSim.find(e => e.tag === 'Coin');
    if (firstCoin) {
        console.log(`Destroying coin ${firstCoin.id} during simulation...`);
        await rpc('engine.destroyEntity', { entityId: firstCoin.id });
    }

    // 4. STOP Platformer simulation!
    console.log('4. Stopping Platformer simulation...');
    await rpc('engine.stopSimulation');
    let snapPlatRestored = await rpc('engine.getSnapshot');
    const platPlayerRestored = snapPlatRestored.find(e => e.id === platPlayer.id);
    console.log(`Platformer Player restored to: Y=${platPlayerRestored.transform.y}, Z=${platPlayerRestored.transform.z}`);

    if (Math.abs(platPlayerRestored.transform.y - platPlayer.transform.y) > 0.01) {
        throw new Error(`Stop failed to restore platformer player Y! Got: ${platPlayerRestored.transform.y}`);
    }
    const coinsRestored = snapPlatRestored.filter(e => e.tag === 'Coin');
    console.log(`Coins in restored scene: ${coinsRestored.length} (Initial was 8)`);
    if (coinsRestored.length !== 8) {
        throw new Error(`Stop failed to restore destroyed coin! Got: ${coinsRestored.length}`);
    }

    // 5. SWITCH TO RACER PRESET
    console.log('5. Switching scene to RACER...');
    await rpc('engine.resetScene', { preset: 'racer' });
    let snapRacer = await rpc('engine.getSnapshot');
    const car = snapRacer.find(e => e.tag === 'PlayerCar' || e.tag === 'SportsCar');
    if (!car) {
        throw new Error('Racer Car entity (PlayerCar) not found in scene snapshot!');
    }
    console.log(`Racer Car initial position: X=${car.transform.x}, Y=${car.transform.y}, Z=${car.transform.z}`);

    await rpc('engine.playSimulation');
    await rpc('engine.stepSimulation', { steps: 15, dt: 0.016 });
    await rpc('engine.stopSimulation');

    let snapRacerRestored = await rpc('engine.getSnapshot');
    const carRestored = snapRacerRestored.find(e => e.id === car.id);
    console.log(`Racer Car restored position: X=${carRestored.transform.x}, Y=${carRestored.transform.y}, Z=${carRestored.transform.z}`);
    if (Math.abs(carRestored.transform.x - car.transform.x) > 0.01) {
        throw new Error('Stop failed to restore car position!');
    }

    console.log('✅ ALL SCENE CHANGE & NON-DESTRUCTIVE STOP RESTORATION TESTS PASSED!');
    ws.close();
    return true;
}

if (process.argv[1] && process.argv[1].endsWith('test_scene_change_stop.mjs')) {
    runSceneChangeStopTest().then(() => process.exit(0)).catch(err => {
        console.error('❌ TEST FAILED:', err);
        process.exit(1);
    });
}
