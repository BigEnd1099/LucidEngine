// ==============================================================================
// Lucid Engine - Dual-Transport Unified Server End-to-End Test
// Verifies HTTP serving on port 9000, Binary WebSocket streaming & MPSC commands
// ==============================================================================

import assert from 'node:assert';
import http from 'node:http';
import WebSocket from 'ws';
import { spawn, execSync } from 'node:child_process';
import {
  PROTOCOL_MAGIC,
  PROTOCOL_VERSION,
  NET_HEADER_SIZE,
  SNAPSHOT_SIZE,
  BinaryFrameType,
  OFF_MAGIC,
  OFF_VERSION,
  OFF_SEQ_HEAD,
  OFF_SEQ_TAIL,
  OFF_CPU_FRAME_MS,
  OFF_ACTIVE_ENTITIES
} from '../src/transport/TelemetrySnapshot.ts';

console.log('========================================================');
console.log('🧪 DUAL-TRANSPORT UNIFIED SERVER E2E TEST (PORT 9000)');
console.log('========================================================\n');

// 0. Kill any existing engine instances
try {
  if (process.platform === 'win32') {
    execSync('powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue"');
  }
} catch {}

// 1. Launch engine in headless mode
console.log('[1/5] Uruchamianie instancji Lucid Engine (--headless)...');
const engineProcess = spawn('.\\engine\\build\\Release\\engine.exe', ['--headless'], {
  stdio: ['ignore', 'pipe', 'inherit']
});

await new Promise(r => setTimeout(r, 2000));

try {
  // 2. Test HTTP GET on port 9000 (Serving embedded index.html)
  console.log('[2/5] Testowanie serwowania zasobow HTTP na porcie 9000 (Single Port)...');
  const httpRes = await new Promise((resolve, reject) => {
    http.get('http://127.0.0.1:9000/', (res) => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => resolve({ status: res.statusCode, headers: res.headers, body: data }));
    }).on('error', reject);
  });

  assert.strictEqual(httpRes.status, 200, 'HTTP status dla / powinien wynosic 200 OK');
  assert(httpRes.headers['content-type']?.includes('text/html'), 'Content-Type powinien byc text/html');
  assert(httpRes.body.includes('<div id="root">') || httpRes.body.includes('Lucid Engine') || httpRes.body.includes('vite'), 'Odpowiedz HTTP powinna zawierac glowny szablon HTML aplikacji');
  console.log(`  ✅ HTTP GET / zwrocil kod 200 OK (${httpRes.body.length} bajtow wbudowanego HTML).\n`);

  // 3. Test Binary WebSocket on the same port 9000
  console.log('[3/5] Testowanie strumienia binarnego WebSocket na tym samym porcie (ws://127.0.0.1:9000/ws?transport=binary)...');
  const ws = new WebSocket('ws://127.0.0.1:9000/ws?transport=binary');

  const binarySnapshots = [];
  const ackReceived = [];
  let jsonRpcResponse = null;

  await new Promise((resolve, reject) => {
    ws.on('open', resolve);
    ws.on('error', reject);
  });

  ws.on('message', (data, isBinary) => {
    if (isBinary && Buffer.isBuffer(data)) {
      if (data.length >= NET_HEADER_SIZE) {
        const frameType = data.readUInt8(0);
        const payloadLen = data.readUInt32LE(4);

        if (frameType === BinaryFrameType.FULL_SNAPSHOT && payloadLen >= SNAPSHOT_SIZE) {
          const payload = data.subarray(NET_HEADER_SIZE, NET_HEADER_SIZE + SNAPSHOT_SIZE);
          const magic = payload.readUInt32LE(OFF_MAGIC);
          const version = payload.readUInt32LE(OFF_VERSION);
          const seqHead = payload.readBigUInt64LE(OFF_SEQ_HEAD);
          const seqTail = payload.readBigUInt64LE(OFF_SEQ_TAIL);
          const cpuMs = payload.readFloatLE(OFF_CPU_FRAME_MS);
          const entities = payload.readUInt32LE(OFF_ACTIVE_ENTITIES);

          binarySnapshots.push({ magic, version, seqHead, seqTail, cpuMs, entities });
        } else if (frameType === BinaryFrameType.RPC_ACK) {
          const cmdId = data.readUInt32LE(NET_HEADER_SIZE);
          const reqId = data.readUInt32LE(NET_HEADER_SIZE + 4);
          ackReceived.push({ cmdId, reqId });
        }
      }
    } else {
      try {
        const parsed = JSON.parse(data.toString());
        if (parsed.id === 42) {
          jsonRpcResponse = parsed;
        }
      } catch {}
    }
  });

  // Wait for 60 Hz binary snapshots to arrive
  await new Promise(r => setTimeout(r, 500));

  assert(binarySnapshots.length >= 3, `Powinno naplynac co najmniej kilka klatek telemetrii (odebrano ${binarySnapshots.length})`);
  const first = binarySnapshots[0];
  assert.strictEqual(first.magic, PROTOCOL_MAGIC, 'Bledny Protocol Magic');
  assert.strictEqual(first.version, PROTOCOL_VERSION, 'Bledna wersja protokolu');
  assert.strictEqual(first.seqHead, first.seqTail, 'Sekwencja seq_head i seq_tail musi byc identyczna (brak rozdarcia Seqlock)');
  assert(first.entities > 0, 'Liczba encji powinna byc wieksza od 0');
  console.log(`  ✅ Odebrano ${binarySnapshots.length} binarnych klatek telemetrii (CPU: ${first.cpuMs.toFixed(2)}ms, Encje: ${first.entities}, Magic: 0x${first.magic.toString(16)}).\n`);

  // 4. Test Binary RPC Command (0x10) over MPSC queue
  console.log('[4/5] Testowanie binarnej komendy RPC (0x10) i potwierdzenia ACK (0x11)...');
  const reqId = 12345;
  const cmdId = 1; // Set Paused
  const cmdBuf = Buffer.alloc(NET_HEADER_SIZE + 8 + 1);

  // 8B Header
  cmdBuf.writeUInt8(BinaryFrameType.RPC_COMMAND, 0);
  cmdBuf.writeUInt8(0, 1);
  cmdBuf.writeUInt16LE(0, 2);
  cmdBuf.writeUInt32LE(9, 4); // Payload: 4B cmdId + 4B reqId + 1B flag = 9B

  // Payload
  cmdBuf.writeUInt32LE(cmdId, NET_HEADER_SIZE);
  cmdBuf.writeUInt32LE(reqId, NET_HEADER_SIZE + 4);
  cmdBuf.writeUInt8(1, NET_HEADER_SIZE + 8); // paused = true

  ws.send(cmdBuf);
  await new Promise(r => setTimeout(r, 300));

  assert(ackReceived.some(a => a.cmdId === cmdId && a.reqId === reqId), 'Nie odebrano binarnego potwierdzenia ACK dla komendy RPC');
  console.log(`  ✅ Komenda RPC (ID: ${cmdId}, Req: ${reqId}) przetworzona przez MPSC i potwierdzona pakietem ACK 0x11.\n`);

  // 5. Test Backward-Compatible JSON-RPC on the same port
  console.log('[5/5] Testowanie wstecznej kompatybilnosci z tekstowym JSON-RPC na tym samym porcie...');
  ws.send(JSON.stringify({ jsonrpc: '2.0', id: 42, method: 'engine.getSnapshot', params: {} }));
  await new Promise(r => setTimeout(r, 500));

  assert(jsonRpcResponse !== null, 'Powinna nadejsc odpowiedz JSON-RPC o id 42');
  assert(Array.isArray(jsonRpcResponse.result), 'Wynik engine.getSnapshot powinien byc tablica encji');
  console.log(`  ✅ Tekstowy JSON-RPC dziala wspolbieznie na tym samym porcie 9000 (odebrano ${jsonRpcResponse.result.length} encji).\n`);

  ws.close();
  console.log('========================================================');
  console.log('🎉 WSZYSTKIE TESTY DUAL-TRANSPORT UNIFIED SERVER ZALICZONE!');
  console.log('========================================================\n');

} finally {
  engineProcess.kill();
  try {
    if (process.platform === 'win32') {
      execSync('powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue"');
    }
  } catch {}
}
