import { spawn, execSync } from 'child_process';
import readline from 'readline';

console.log('========================================================');
console.log('🧪 LUCID ENGINE COMPLETE TEST SUITE');
console.log('========================================================\n');

// 0. Ensure no conflicting previous engine instances are locking the mutex/ports
try {
  if (process.platform === 'win32') {
    execSync('powershell -Command "Stop-Process -Name engine -Force -ErrorAction SilentlyContinue"');
  }
} catch (e) {}

// Test 0: Binary Layout & Seqlock Parity Verification (Dual-Transport UI Shell)
console.log('[0/13] Testowanie parzystości układu binarnego i Seqlock (Dual-Transport UI Shell)...');
execSync('node tests/test_binary_layout.mjs', { stdio: 'inherit' });
console.log('  ✅ Parzystość struktur C++ i TypeScript zweryfikowana pomyślnie.\n');

console.log('[0b/13] Testowanie serwera pojedynczego portu (Dual-Transport Unified Server)...');
execSync('node tests/test_dual_transport.mjs', { stdio: 'inherit' });
console.log('  ✅ Dual-Transport Unified Server zweryfikowany pomyślnie.\n');

console.log('[0c/13] Testowanie trybu czysto lokalnego bez otwierania portów (--no-remote)...');
execSync('node tests/test_local_mode.mjs', { stdio: 'inherit' });
console.log('  ✅ Tryb czysto lokalny zweryfikowany pomyślnie.\n');

// 1. Start engine instance
console.log('[1/4] Uruchamianie instancji Lucid Engine (Headless)...');
const engine = spawn('./engine/build/Release/engine.exe', ['--headless'], {
  stdio: ['pipe', 'pipe', 'inherit']
});

let msgId = 1;
function sendMCP(method, params = {}) {
  const req = { jsonrpc: '2.0', id: msgId++, method, params };
  engine.stdin.write(JSON.stringify(req) + '\n');
}

let mcpResponses = [];
const rl = readline.createInterface({ input: engine.stdout, crlfDelay: Infinity });
rl.on('line', (line) => {
  if (!line.trim().startsWith('{')) return;
  try {
    const parsed = JSON.parse(line);
    mcpResponses.push(parsed);
  } catch (e) {}
});

await new Promise(r => setTimeout(r, 1500));

// 2. Test MCP stdio
console.log('[2/4] Testowanie protokołu Model Context Protocol (MCP stdio)...');
sendMCP('initialize');
sendMCP('tools/list');
sendMCP('tools/call', { name: 'get_scene_snapshot', arguments: {} });
sendMCP('tools/call', { 
  name: 'spawn_entity', 
  arguments: { 
    tag: 'MCP_Cube', 
    transform: { x: 0, y: 12, z: 0 },
    rigidBody: { mass: 1.5, shapeType: 0 }
  } 
});
sendMCP('tools/call', { name: 'step_simulation', arguments: { steps: 5, dt: 0.016 } });

await new Promise(r => setTimeout(r, 1500));
if (mcpResponses.length >= 5) {
  console.log(`  ✅ MCP Server: Odebrano ${mcpResponses.length} poprawnych odpowiedzi JSON-RPC.`);
} else {
  console.error(`  ❌ MCP Server: Otrzymano tylko ${mcpResponses.length}/5 odpowiedzi.`);
  engine.kill();
  process.exit(1);
}

// 3. Test WebSocket RPC (client & view sync)
console.log('\n[3/4] Testowanie połączenia WebSocket JSON-RPC (port 9000)...');
const { runClientTest } = await import('./test_client.mjs');
await runClientTest();

console.log('\n[4/5] Testowanie synchronizacji widoku i zrzutu klatek (Capture Frame)...');
const { runViewSyncTest } = await import('./test_view_sync.mjs');
await runViewSyncTest();

console.log('\n[5/6] Testowanie sprężystości fizyki (Bounciness) i API skryptów rozgrywki...');
const { runPhysicsTest } = await import('./test_physics_and_gameplay_sub.mjs');
await runPhysicsTest();

console.log('\n[6/8] Testowanie kształtu Kapsuły, Triggerów Sensorycznych i Audio SFX...');
const { runCapsuleAudioTest } = await import('./test_gameplay_capsule_and_audio.mjs');
await runCapsuleAudioTest();

console.log('\n[7/8] Testowanie 4 szablonów gier i kontrolerów rozgrywki QuickJS...');
const { runTemplatesTest } = await import('./test_game_templates_e2e.mjs');
await runTemplatesTest();

console.log('\n[8/9] Testowanie przycisku STOP (przywracanie migawki edycyjnej bez resetu)...');
const { runStopRestoreTest } = await import('./test_stop_restore.mjs');
await runStopRestoreTest();

console.log('\n[9/10] Testowanie kompilacji i pakowania pełnej gry PC (.exe + Vulkan 1.3 + Jolt)...');
const { runPackageGameTest } = await import('./test_package_game.mjs');
await runPackageGameTest();

console.log('\n[10/11] Testowanie skalowania UI oraz dynamicznej rozdzielczości renderowania (Vulkan 1.3)...');
const { runResolutionScalingTest } = await import('./test_resolution_scaling.mjs');
await runResolutionScalingTest();

console.log('\n[11/12] Testowanie przywracania obiektów na miejsce po zmianie sceny (Stop & Re-spawn)...');
const { runSceneChangeStopTest } = await import('./test_scene_change_stop.mjs');
await runSceneChangeStopTest();

console.log('\n[12/12] Testowanie korony technologii AAA (Profiler, Zelda TotK Chemia, Voronoi Destrukcja, A* NavMesh, PCG, 64-bit World Partition, Sub-Tick Netcode, Hot-Reload)...');
const { runAaaFeaturesTest } = await import('./test_aaa_features.mjs');
await runAaaFeaturesTest(engine);

console.log('\n========================================================');
console.log('🎉 WSZYSTKIE 12 PAKIETÓW TESTÓW ZAKOŃCZONE SUKCESEM (0 BŁĘDÓW)');
console.log('========================================================');

engine.kill();
process.exit(0);
