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

    console.log('--- TEST: PACKAGE STANDALONE GAME (.EXE) ---');
    
    // Call packageGame
    const res = await rpc('engine.packageGame', { outDir: 'Build/Windows', name: 'MyLucidGame' });
    console.log('Package result:', res);

    if (!res || !res.success) {
        throw new Error('Packaging standalone game failed!');
    }

    console.log('✅ TEST PASSED: Standalone PC game successfully packaged!');
    ws.close();
}

runTest().catch(err => {
    console.error('❌ TEST FAILED:', err);
    process.exit(1);
});
