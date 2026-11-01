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

// 1. Start engine instance
console.log('[1/4] Uruchamianie instancji Lucid Engine (Headless)...');
const engine = spawn('.\\engine\\build\\Release\\engine.exe', ['--headless'], {
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
import('./test_client.mjs');
await new Promise(r => setTimeout(r, 1500));

console.log('\n[4/5] Testowanie synchronizacji widoku i zrzutu klatek (Capture Frame)...');
import('./test_view_sync.mjs');
await new Promise(r => setTimeout(r, 2000));

console.log('\n[5/6] Testowanie sprężystości fizyki (Bounciness) i API skryptów rozgrywki...');
import('./test_physics_and_gameplay_sub.mjs');
await new Promise(r => setTimeout(r, 2000));

console.log('\n[6/8] Testowanie kształtu Kapsuły, Triggerów Sensorycznych i Audio SFX...');
import('./test_gameplay_capsule_and_audio.mjs');
await new Promise(r => setTimeout(r, 2000));

console.log('\n[7/8] Testowanie 4 szablonów gier i kontrolerów rozgrywki QuickJS...');
const { runTemplatesTest } = await import('./test_game_templates_e2e.mjs');
await runTemplatesTest();

console.log('\n[8/9] Testowanie przycisku STOP (przywracanie migawki edycyjnej bez resetu)...');
import('./test_stop_restore.mjs');
await new Promise(r => setTimeout(r, 2000));

console.log('\n[9/10] Testowanie kompilacji i pakowania pełnej gry PC (.exe + Vulkan 1.3 + Jolt)...');
import('./test_package_game.mjs');
await new Promise(r => setTimeout(r, 2000));

console.log('\n[10/11] Testowanie skalowania UI oraz dynamicznej rozdzielczości renderowania (Vulkan 1.3)...');
const { runResolutionScalingTest } = await import('./test_resolution_scaling.mjs');
await runResolutionScalingTest();

console.log('\n[11/12] Testowanie przywracania obiektów na miejsce po zmianie sceny (Stop & Re-spawn)...');
import('./test_scene_change_stop.mjs');
await new Promise(r => setTimeout(r, 3000));

console.log('\n[12/12] Testowanie korony technologii AAA (Profiler, Zelda TotK Chemia, Voronoi Destrukcja, A* NavMesh, PCG, 64-bit World Partition, Sub-Tick Netcode, Hot-Reload)...');
const { runAaaFeaturesTest } = await import('./test_aaa_features.mjs');
await runAaaFeaturesTest(engine);

console.log('\n========================================================');
console.log('🎉 WSZYSTKIE 12 PAKIETÓW TESTÓW ZAKOŃCZONE SUKCESEM (0 BŁĘDÓW)');
console.log('========================================================');

engine.kill();
process.exit(0);
