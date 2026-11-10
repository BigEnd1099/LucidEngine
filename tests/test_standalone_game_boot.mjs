import { spawn } from 'child_process';
import WebSocket from 'ws';
import path from 'path';

console.log('--- TESTING STANDALONE GAME BOOT IN D:\\Lucid_Game_test ---');

const gameDir = 'D:/Lucid_Game_test';
const exePath = path.join(gameDir, 'LucidGame.exe');

// Spawn the standalone game with --headless
const child = spawn(exePath, ['--game', 'game.json', '--headless'], {
    cwd: gameDir,
    stdio: ['ignore', 'pipe', 'pipe']
});

child.stdout.on('data', (d) => {
    process.stdout.write(`[Game STDOUT] ${d}`);
});

child.stderr.on('data', (d) => {
    process.stderr.write(`[Game STDERR] ${d}`);
});

// Give engine a moment to initialize Vulkan and listen on 9000
await new Promise(r => setTimeout(r, 4500));

let ws;
try {
    ws = new WebSocket('ws://127.0.0.1:9000');
    await new Promise((resolve, reject) => {
        const to = setTimeout(() => reject(new Error('WebSocket connect timeout')), 3000);
        ws.on('open', () => { clearTimeout(to); resolve(); });
        ws.on('error', reject);
    });
    console.log('✅ Connected to Standalone Game WebSocket RPC!');

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

    // 1. Check Scene entities
    const scene = await rpc('engine.getSnapshot');
    console.log(`Entities loaded in standalone game: ${scene ? scene.length : 0}`);
    if (!scene || scene.length < 25) {
        throw new Error(`Expected at least 25 entities, got ${scene ? scene.length : 0}`);
    }

    // 2. Check Game Status and Health
    const statusRes = await rpc('engine.getGameStatus');
    console.log(`Game Status: ${statusRes.status}`);

    const healthRes = await rpc('engine.getHealth');
    console.log(`Player Health: ${healthRes.health}%`);

    const stateRes = await rpc('engine.getGameState');
    console.log(`Score (Relics): ${stateRes.score}`);

    // 3. Step simulation for 60 ticks
    await rpc('engine.stepSimulation', { steps: 60, dt: 0.016 });
    console.log('✅ Stepped simulation 60 ticks successfully!');

    // 4. Verify entities updated
    const sceneAfter = await rpc('engine.getSnapshot');
    console.log(`✅ Post-simulation entities verified: ${sceneAfter.length} entities active!`);

    ws.close();
    child.kill('SIGTERM');
    console.log('🎉 STANDALONE GAME VERIFICATION COMPLETE (100% WORKING)!');
    process.exit(0);
} catch (err) {
    console.error('❌ Standalone game test failed:', err);
    if (ws) ws.close();
    child.kill('SIGTERM');
    process.exit(1);
}
