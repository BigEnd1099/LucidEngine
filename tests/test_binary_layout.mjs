// ==============================================================================
// Lucid Engine - Binary Layout & Seqlock Parity Test Suite
// Verifies exact byte offsets, structure sizes, and seqlock reader consistency
// ==============================================================================

import assert from 'node:assert';
import {
  PROTOCOL_MAGIC,
  PROTOCOL_VERSION,
  RING_SLOT_COUNT,
  MAX_BUFFERED_AMOUNT,
  BinaryFrameType,
  TelemetryStateFlags,
  OFF_MAGIC,
  OFF_VERSION,
  OFF_SEQ_HEAD,
  OFF_CPU_FRAME_MS,
  OFF_OUTPUT_LEVEL_DB,
  OFF_STATE_FLAGS,
  OFF_ACTIVE_ENTITIES,
  OFF_SEQ_TAIL,
  SNAPSHOT_SIZE,
  RING_HEADER_SIZE,
  SHARED_RING_TOTAL_SIZE,
  NET_HEADER_SIZE,
  NET_OFF_TYPE,
  NET_OFF_FLAGS,
  NET_OFF_STREAM_ID,
  NET_OFF_PAYLOAD_LEN,
  TelemetrySnapshotReader
} from '../src/transport/TelemetrySnapshot.ts';

console.log('========================================================');
console.log('🧪 DUAL-TRANSPORT UI SHELL: BINARY LAYOUT PARITY TEST');
console.log('========================================================\n');

// 1. Structure Size and Alignment Parity Checks
console.log('[1/4] Weryfikacja stalych rozmiarow i offsetow struktur...');
assert.strictEqual(SNAPSHOT_SIZE, 40, 'Rozmiar TelemetrySnapshot musi wynosic dokladnie 40 bajtow');
assert.strictEqual(RING_HEADER_SIZE, 64, 'Naglowek SharedStateRing musi wynosic 64 bajty (izolacja cache-line)');
assert.strictEqual(RING_SLOT_COUNT, 32, 'Liczba slotow pierscienia musi wynosic 32');
assert.strictEqual(SHARED_RING_TOTAL_SIZE, 64 + 32 * 40, 'Calkowity rozmiar SharedStateRing musi wynosic 1344 bajty (64 + 32*40)');
assert.strictEqual(NET_HEADER_SIZE, 8, 'Naglowek ramki sieciowej musi wynosic dokladnie 8 bajtow');
assert.strictEqual(MAX_BUFFERED_AMOUNT, 512, 'Prog dlawienia WebSocket musi wynosic 512 bajtow');

// Verify exact field offsets match C++ offsetof
assert.strictEqual(OFF_MAGIC, 0);
assert.strictEqual(OFF_VERSION, 4);
assert.strictEqual(OFF_SEQ_HEAD, 8);
assert.strictEqual(OFF_CPU_FRAME_MS, 16);
assert.strictEqual(OFF_OUTPUT_LEVEL_DB, 20);
assert.strictEqual(OFF_STATE_FLAGS, 24);
assert.strictEqual(OFF_ACTIVE_ENTITIES, 28);
assert.strictEqual(OFF_SEQ_TAIL, 32);

// Network header offsets
assert.strictEqual(NET_OFF_TYPE, 0);
assert.strictEqual(NET_OFF_FLAGS, 1);
assert.strictEqual(NET_OFF_STREAM_ID, 2);
assert.strictEqual(NET_OFF_PAYLOAD_LEN, 4);

console.log('  ✅ Wymiary i przesuniecia bajtowe w pelni zgodne ze specyfikacja C++ alignas(8) / alignas(64).\n');

// 2. Seqlock Consistency Test: Normal consistent frame
console.log('[2/4] Test odczytu spojnej klatki (Seqlock tail -> copy -> head)...');
const buffer = new ArrayBuffer(SHARED_RING_TOTAL_SIZE);
const view = new DataView(buffer);

// Simulate C++ writer filling slot 0
const slot0Offset = RING_HEADER_SIZE;
const seq = 1001n;
view.setUint32(slot0Offset + OFF_MAGIC, PROTOCOL_MAGIC, true);
view.setUint32(slot0Offset + OFF_VERSION, PROTOCOL_VERSION, true);
view.setBigUint64(slot0Offset + OFF_SEQ_HEAD, seq, true);
view.setFloat32(slot0Offset + OFF_CPU_FRAME_MS, 16.32, true);
view.setFloat32(slot0Offset + OFF_OUTPUT_LEVEL_DB, -3.5, true);
view.setUint32(slot0Offset + OFF_STATE_FLAGS, TelemetryStateFlags.SIMULATING | TelemetryStateFlags.NET_ACTIVE, true);
view.setUint32(slot0Offset + OFF_ACTIVE_ENTITIES, 42, true);
view.setBigUint64(slot0Offset + OFF_SEQ_TAIL, seq, true);

const outData = {
  magic: 0,
  version: 0,
  seqHead: 0n,
  cpuFrameMs: 0,
  outputLevelDb: 0,
  stateFlags: 0,
  activeEntities: 0,
  seqTail: 0n
};

let success = TelemetrySnapshotReader.tryReadConsistent(view, slot0Offset, outData, 0n);
assert.strictEqual(success, true, 'Odczyt spojnej klatki powinien zwrocic true');
assert.strictEqual(outData.magic, PROTOCOL_MAGIC);
assert.strictEqual(outData.version, PROTOCOL_VERSION);
assert.strictEqual(outData.seqHead, seq);
assert.strictEqual(outData.seqTail, seq);
assert.strictEqual(Math.round(outData.cpuFrameMs * 100) / 100, 16.32);
assert.strictEqual(Math.round(outData.outputLevelDb * 10) / 10, -3.5);
assert.strictEqual(outData.stateFlags, TelemetryStateFlags.SIMULATING | TelemetryStateFlags.NET_ACTIVE);
assert.strictEqual(outData.activeEntities, 42);
console.log('  ✅ Poprawnie odczytano i zdekodowano wartosci metryk bez alokacji pamieci.\n');

// 3. Seqlock Tearing Test: Writer is actively modifying buffer during read
console.log('[3/4] Test wykrywania rozdarcia (Tearing: seq_tail !== seq_head)...');
// Simulate writer in the middle of writing: seq_head updated to 1002, but seq_tail is still 1001
view.setBigUint64(slot0Offset + OFF_SEQ_HEAD, 1002n, true);
view.setBigUint64(slot0Offset + OFF_SEQ_TAIL, 1001n, true);

success = TelemetrySnapshotReader.tryReadConsistent(view, slot0Offset, outData, 1001n);
assert.strictEqual(success, false, 'Odczyt rozdarcia powinien zwrocic false i odrzucic klatke');

// Stale frame test (seq <= lastSeq)
view.setBigUint64(slot0Offset + OFF_SEQ_HEAD, 1001n, true);
view.setBigUint64(slot0Offset + OFF_SEQ_TAIL, 1001n, true);
success = TelemetrySnapshotReader.tryReadConsistent(view, slot0Offset, outData, 1001n);
assert.strictEqual(success, false, 'Przestarzala klatka powinna zostac zignorowana');
console.log('  ✅ Ochrona Seqlock poprawnie odrzuca rozdarcia pamieci i klatki przeterminowane.\n');

// 4. Network 8-byte Frame Header & Payload Alignment Test
console.log('[4/4] Test 8-bajtowego naglowka ramki sieciowej i wyrownania ładunku...');
const netBuf = new ArrayBuffer(NET_HEADER_SIZE + SNAPSHOT_SIZE);
const netView = new DataView(netBuf);

// Encode 8-byte network header
netView.setUint8(NET_OFF_TYPE, BinaryFrameType.FULL_SNAPSHOT);
netView.setUint8(NET_OFF_FLAGS, 0);
netView.setUint16(NET_OFF_STREAM_ID, 1, true);
netView.setUint32(NET_OFF_PAYLOAD_LEN, SNAPSHOT_SIZE, true);

// Encode TelemetrySnapshot at offset 8 (alignas 8)
const payloadOffset = NET_HEADER_SIZE;
const netSeq = 2005n;
netView.setUint32(payloadOffset + OFF_MAGIC, PROTOCOL_MAGIC, true);
netView.setUint32(payloadOffset + OFF_VERSION, PROTOCOL_VERSION, true);
netView.setBigUint64(payloadOffset + OFF_SEQ_HEAD, netSeq, true);
netView.setFloat32(payloadOffset + OFF_CPU_FRAME_MS, 11.45, true);
netView.setFloat32(payloadOffset + OFF_OUTPUT_LEVEL_DB, -1.2, true);
netView.setUint32(payloadOffset + OFF_STATE_FLAGS, TelemetryStateFlags.SIMULATING, true);
netView.setUint32(payloadOffset + OFF_ACTIVE_ENTITIES, 128, true);
netView.setBigUint64(payloadOffset + OFF_SEQ_TAIL, netSeq, true);

// Verify header
assert.strictEqual(netView.getUint8(NET_OFF_TYPE), BinaryFrameType.FULL_SNAPSHOT);
assert.strictEqual(netView.getUint32(NET_OFF_PAYLOAD_LEN, true), 40);

// Verify reading payload directly with 8-byte offset (zero-copy DataView slice)
success = TelemetrySnapshotReader.tryReadConsistent(netView, payloadOffset, outData, 0n);
assert.strictEqual(success, true);
assert.strictEqual(outData.seqHead, netSeq);
assert.strictEqual(outData.activeEntities, 128);
console.log('  ✅ 8-bajtowy naglowek ramki zachowuje pelne 8-bajtowe wyrownanie ladunku TelemetrySnapshot.\n');

console.log('========================================================');
console.log('🎉 WSZYSTKIE TESTY PARZYSTOSCI UKLADU ZAKONCZONE SUKCESEM');
console.log('========================================================');
