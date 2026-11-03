import WebSocket from 'ws';

export async function runPackageGameTest() {
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

    console.log('--- TEST: PACKAGE STANDALONE GAME (.EXE) ---');
    
    // Call packageGame
    const res = await rpc('engine.packageGame', { outDir: 'Build/Windows', name: 'MyLucidGame' });
    console.log('Package result:', res);

    if (!res || !res.success) {
        throw new Error('Packaging standalone game failed!');
    }

    console.log('✅ TEST PASSED: Standalone PC game successfully packaged!');
    ws.close();
    return true;
}

if (process.argv[1] && process.argv[1].endsWith('test_package_game.mjs')) {
    runPackageGameTest().then(() => process.exit(0)).catch(err => {
        console.error('❌ TEST FAILED:', err);
        process.exit(1);
    });
}
