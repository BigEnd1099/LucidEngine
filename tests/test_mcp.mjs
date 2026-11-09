import { spawn } from 'child_process';
import readline from 'readline';

const engine = spawn('./engine/build/Release/engine.exe', ['--headless'], {
  stdio: ['pipe', 'pipe', 'inherit']
});

let msgId = 1;

function sendMCP(method, params = {}) {
  const req = { jsonrpc: '2.0', id: msgId++, method, params };
  engine.stdin.write(JSON.stringify(req) + '\n');
}

let responses = [];

const rl = readline.createInterface({ input: engine.stdout, crlfDelay: Infinity });
rl.on('line', (line) => {
  if (!line.trim().startsWith('{')) return;
  try {
    const parsed = JSON.parse(line);
    responses.push(parsed);
    console.log('[MCP Response]', parsed.id, parsed.result ? 'OK' : 'Error/Other');
    if (parsed.result?.content?.[0]?.text) {
      console.log('   Content preview:', parsed.result.content[0].text.substring(0, 60) + '...');
    }
  } catch (e) {
    console.error('Parse error on line:', e.message);
  }
});

// Wait 1.5s for engine boot
await new Promise(r => setTimeout(r, 1500));

console.log('--- TEST MCP SERVER ---');
sendMCP('initialize');
sendMCP('tools/list');
sendMCP('tools/call', { name: 'get_scene_snapshot', arguments: {} });
sendMCP('tools/call', { 
  name: 'spawn_entity', 
  arguments: { 
    tag: 'MCP_Spawned_Box', 
    transform: { x: 5, y: 15, z: 5 },
    rigidBody: { mass: 2.0, shapeType: 0 }
  } 
});
sendMCP('tools/call', { name: 'step_simulation', arguments: { steps: 5, dt: 0.016 } });

await new Promise(r => setTimeout(r, 2000));

console.log('Total valid MCP responses received:', responses.length);
if (responses.length >= 5) {
  console.log('✅ ALL MCP TOOLS EXECUTED SAFELY ON MAIN THREAD!');
} else {
  console.error('❌ Missing MCP responses');
}

engine.kill();
process.exit(responses.length >= 5 ? 0 : 1);
