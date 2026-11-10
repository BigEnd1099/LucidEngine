import WebSocket from 'ws';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { createServer } from 'node:net';

const ROOT = process.cwd();
const EXE = path.resolve(process.argv[2] || 'engine/build/Release/engine.exe');
const PORT = Number(process.env.LUCID_BENCH_PORT || 9011);
const WIDTH = Number(process.env.LUCID_BENCH_WIDTH || 2404);
const HEIGHT = Number(process.env.LUCID_BENCH_HEIGHT || 1866);
const WARMUP_MS = Number(process.env.LUCID_BENCH_WARMUP_MS || 1500);
const CAPTURE_MS = Number(process.env.LUCID_BENCH_CAPTURE_MS || 2000);
const SAMPLE_INTERVAL_MS = 100;

if (!fs.existsSync(EXE)) {
  console.error('[BENCH] Engine executable not found: ' + EXE);
  console.error('[BENCH] Build Release first: .\\build_engine.bat release');
  process.exit(2);
}

const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
const outputDir = path.join(ROOT, 'benchmark-results', 'run-' + timestamp);
fs.mkdirSync(outputDir, { recursive: true });

let engineProcess;
let ws;
let rpcId = 1;
let childExited = false;
let stderrTail = '';

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function assertPortFree() {
  const probe = createServer();
  await new Promise((resolve, reject) => {
    probe.once('error', reject);
    probe.listen(PORT, '127.0.0.1', resolve);
  });
  await new Promise((resolve, reject) => probe.close(err => err ? reject(err) : resolve()));
}

function startEngine() {
  console.log('[BENCH] Starting windowed Vulkan engine on port ' + PORT + '...');
  engineProcess = spawn(EXE, ['--benchmark', '--port', String(PORT)], {
    cwd: ROOT,
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'pipe']
  });
  engineProcess.on('error', err => {
    childExited = true;
    stderrTail = (stderrTail + err.message).slice(-12000);
  });
  engineProcess.stdout.on('data', chunk => process.stdout.write('[ENGINE] ' + chunk));
  engineProcess.stderr.on('data', chunk => {
    stderrTail = (stderrTail + chunk.toString()).slice(-12000);
    process.stderr.write('[ENGINE] ' + chunk);
  });
  engineProcess.on('exit', (code, signal) => {
    childExited = true;
    console.log('[BENCH] Engine exited (code=' + code + ', signal=' + signal + ').');
  });
}

async function connectWebSocket() {
  const url = 'ws://127.0.0.1:' + PORT;
  const deadline = Date.now() + 25000;
  let lastError;
  while (Date.now() < deadline) {
    if (childExited) throw new Error('Engine exited before WebSocket became ready.\n' + stderrTail);
    try {
      const candidate = new WebSocket(url);
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('WebSocket connection timeout')), 1200);
        candidate.once('open', () => { clearTimeout(timer); resolve(); });
        candidate.once('error', err => { clearTimeout(timer); reject(err); });
      });
      ws = candidate;
      ws.on('error', err => console.error('[BENCH] WebSocket error:', err.message));
      return;
    } catch (err) {
      lastError = err;
      await sleep(350);
    }
  }
  throw new Error('Could not connect to ' + url + ': ' + (lastError?.message || 'timeout') + '\n' + stderrTail);
}

function callRpc(method, params = {}, timeoutMs = 10000) {
  return new Promise((resolve, reject) => {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
      reject(new Error('WebSocket is not open (RPC ' + method + ')'));
      return;
    }
    const id = rpcId++;
    const timer = setTimeout(() => {
      ws.off('message', onMessage);
      reject(new Error('RPC timeout: ' + method));
    }, timeoutMs);

    const onMessage = raw => {
      let message;
      try { message = JSON.parse(raw.toString()); } catch { return; }
      if (message.id !== id) return;
      clearTimeout(timer);
      ws.off('message', onMessage);
      if (message.error) reject(new Error(message.error.message || JSON.stringify(message.error)));
      else resolve(message.result);
    };

    ws.on('message', onMessage);
    ws.send(JSON.stringify({ jsonrpc: '2.0', id, method, params }));
  });
}

function entity(tag, position, scale, meshId, material) {
  return {
    tag,
    transform: {
      x: position[0], y: position[1], z: position[2],
      sx: scale[0], sy: scale[1], sz: scale[2]
    },
    mesh: { meshId },
    material
  };
}

async function spawnEntity(data) {
  const result = await callRpc('engine.spawnEntity', data);
  if (!result || !Number.isInteger(result.entityId)) {
    throw new Error('engine.spawnEntity returned an invalid result for ' + (data.tag || 'unnamed entity'));
  }
  return result.entityId;
}

async function createAtriumBenchmarkScene() {
  console.log('[SCENE] Building Atrium Benchmark scene...');
  await callRpc('engine.resetScene', { preset: 'empty' });

  const spawnJobs = [];
  const add = data => spawnJobs.push(data);

  // Broad architectural surfaces expose AO, contact shadows and GI leakage.
  add(entity('BENCH_Floor', [0, -0.25, 0], [42, 0.5, 28], 1,
    { color: '#777B82', metallic: 0.08, roughness: 0.82 }));
  add(entity('BENCH_BackWall', [0, 6, -14], [42, 12, 0.45], 2,
    { color: '#343A46', metallic: 0.12, roughness: 0.68 }));
  add(entity('BENCH_LeftWall', [-21, 6, 0], [0.45, 12, 28], 2,
    { color: '#283B50', metallic: 0.18, roughness: 0.62 }));
  add(entity('BENCH_RightWall', [21, 6, 0], [0.45, 12, 28], 2,
    { color: '#4A3035', metallic: 0.18, roughness: 0.62 }));
  add(entity('BENCH_Ceiling', [0, 12.2, 0], [42, 0.35, 28], 2,
    { color: '#20252D', metallic: 0.22, roughness: 0.55 }));

  // Structural columns and cross beams.
  for (const x of [-18, 18]) {
    for (const z of [-9, 0, 9]) {
      add(entity('BENCH_Column_' + x + '_' + z, [x, 5.8, z], [0.72, 5.8, 0.72], 2,
        { color: '#69717D', metallic: 0.72, roughness: 0.24 }));
    }
  }
  for (const z of [-9, 0, 9]) {
    add(entity('BENCH_CrossBeam_' + z, [0, 11.25, z], [40, 0.32, 0.42], 2,
      { color: '#8D929B', metallic: 0.86, roughness: 0.2 }));
  }

  // Raised plinths and a regular PBR material test gallery.
  const palette = ['#D7E0EA', '#C7A46A', '#6C9BCB', '#B86C72', '#73A88B', '#A58BC9'];
  let index = 0;
  for (let row = 0; row < 3; row++) {
    for (let col = 0; col < 7; col++) {
      const x = -12 + col * 4;
      const z = -5 + row * 5;
      const color = palette[index % palette.length];
      add(entity('BENCH_Plinth_' + index, [x, 0.35, z], [1.45, 0.35, 1.45], 2,
        { color: '#424A55', metallic: 0.35, roughness: 0.48 }));
      const metallic = (col % 4) / 3;
      const roughness = 0.06 + (row * 0.42);
      add(entity('BENCH_PBR_Sphere_' + index, [x, 1.55, z], [1.0, 1.0, 1.0], 3,
        { color, metallic, roughness }));
      index++;
    }
  }

  // Dense modular prop field; varied dimensions create depth discontinuities.
  for (let row = 0; row < 5; row++) {
    for (let col = 0; col < 12; col++) {
      const x = -16.5 + col * 3;
      const z = 7.5 + row * 1.3;
      const h = 0.55 + ((row * 7 + col * 3) % 5) * 0.18;
      const color = palette[(row * 12 + col) % palette.length];
      add(entity('BENCH_Prop_' + row + '_' + col, [x, h, z],
        [0.55 + (col % 3) * 0.18, h, 0.55 + (row % 2) * 0.2], 2,
        { color, metallic: ((row + col) % 5) / 5, roughness: 0.22 + ((row * col) % 6) * 0.11 }));
    }
  }

  // Reflective spheres and emissive panels give RT reflections/GI visible targets.
  for (let i = 0; i < 18; i++) {
    const x = -17 + (i % 9) * 4.25;
    const z = -11 + Math.floor(i / 9) * 3.2;
    add(entity('BENCH_ChromeSphere_' + i, [x, 2.1, z], [0.82, 0.82, 0.82], 3,
      { color: i % 2 ? '#D5E8FF' : '#FFD9A8', metallic: 1.0, roughness: 0.035 + (i % 4) * 0.035 }));
  }
  for (let i = 0; i < 12; i++) {
    const x = -16 + (i % 6) * 6.4;
    const z = -13.35 + Math.floor(i / 6) * 26.7;
    add(entity('BENCH_EmissivePanel_' + i, [x, 7.2, z], [1.5, 1.2, 0.12], 2,
      { color: '#FFFFFF', metallic: 0.05, roughness: 0.3,
        emissive: i % 2 ? '#38A8FF' : '#FF8A38', emissiveIntensity: 5.0 }));
  }

  // Local point lights exercise light upload and shading.
  const lightColors = ['#FFFFFF', '#7AB8FF', '#FFB36A', '#B58CFF', '#8DFFD4', '#FFFFFF'];
  for (let i = 0; i < 12; i++) {
    const x = -15 + (i % 6) * 6;
    const z = i < 6 ? -7 : 7;
    spawnJobs.push({
      tag: 'BENCH_Light_' + i,
      transform: { x, y: 9.6, z, sx: 1, sy: 1, sz: 1 },
      light: { type: 'point', color: lightColors[i % lightColors.length], intensity: 24.0, range: 15.0 }
    });
  }

  const ids = [];
  for (let i = 0; i < spawnJobs.length; i++) {
    ids.push(await spawnEntity(spawnJobs[i]));
    if ((i + 1) % 25 === 0) console.log('[SCENE] Spawned ' + (i + 1) + '/' + spawnJobs.length + ' entities...');
  }

  await callRpc('engine.setEnvironment', {
    hdriEnabled: true, skyboxEnabled: true, iblEnabled: true,
    ambientIntensity: 0.7, sunEnabled: true, sunIntensity: 2.2, fogEnabled: false
  });
  await callRpc('engine.setCamera', {
    x: 0, y: 8.0, z: 34, yaw: -90, pitch: -11, fov: 62
  });

  console.log('[SCENE] Created ' + ids.length + ' benchmark entities.');
  return { entityCount: ids.length };
}

async function setFeatures(features) {
  return await callRpc('engine.setRenderFeatures', {
    gtao: !!features.gtao,
    ssgi: !!features.ssgi,
    rtEnabled: true,
    rtGi: !!features.rtGi,
    rtAo: !!features.rtAo,
    rtReflections: !!features.rtReflections
  });
}

function percentile(values, p) {
  if (!values.length) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * p))];
}
function mean(values) {
  return values.length ? values.reduce((sum, value) => sum + value, 0) / values.length : 0;
}
function round(value, digits = 3) {
  return Number.isFinite(value) ? Number(value.toFixed(digits)) : 0;
}

async function captureScenario(name, features, meta = {}) {
  const actual = await setFeatures(features);
  console.log('\n[TEST] ' + name + ' | GTAO=' + actual.gtao + ' SSGI=' + actual.ssgi +
    ' RTGI=' + actual.rtGi + ' RTAO=' + actual.rtAo + ' RTRefl=' + actual.rtReflections);
  await sleep(WARMUP_MS);

  const samples = [];
  const deadline = Date.now() + CAPTURE_MS;
  while (Date.now() < deadline) {
    const stats = await callRpc('engine.getProfilerStats');
    const metrics = stats?.metrics || {};
    samples.push({
      timestamp: new Date().toISOString(),
      fps: Number(stats?.fps || 0),
      frameTimeMs: Number(stats?.frameTimeMs || 0),
      gpuTimeMs: Number(stats?.gpuTimeMs || 0),
      registryEntities: Number(stats?.registryEntities || 0),
      meshEntities: Number(stats?.meshEntities || 0),
      drawItems: Number(metrics.drawItems || 0),
      rtInstances: Number(metrics.rtInstances || 0),
      gpuScenePrepMs: Number(metrics.gpuScenePrepMs || 0),
      gpuShadowMs: Number(metrics.gpuShadowMs || 0),
      gpuDepthMs: Number(metrics.gpuDepthMs || 0),
      gpuGBufferMs: Number(metrics.gpuGBufferMs || 0),
      gpuAoRtMs: Number(metrics.gpuAoRtMs || 0),
      gpuHdrMs: Number(metrics.gpuHdrMs || 0),
      gpuPostMs: Number(metrics.gpuPostMs || 0),
      gpuUiMs: Number(metrics.gpuUiMs || 0),
      gpuScreenSpaceAoGiMs: Number(metrics.gpuScreenSpaceAoGiMs || 0),
      gpuScreenSpaceBlurMs: Number(metrics.gpuScreenSpaceBlurMs || 0),
      rtGiTraceMs: Number(metrics.rtGiTraceMs || 0),
      rtGiTemporalMs: Number(metrics.rtGiTemporalMs || 0),
      rtGiAtrous0Ms: Number(metrics.rtGiAtrous0Ms || 0),
      rtGiAtrous1Ms: Number(metrics.rtGiAtrous1Ms || 0),
      rtReflectionTraceMs: Number(metrics.rtReflectionTraceMs || 0),
      rtReflectionTemporalMs: Number(metrics.rtReflectionTemporalMs || 0),
      rtComputeTotalMs: Number(metrics.rtComputeTotalMs || 0)
    });
    await sleep(SAMPLE_INTERVAL_MS);
  }

  const numericKeys = Object.keys(samples[0] || {}).filter(key =>
    key !== 'timestamp' && samples.some(sample => typeof sample[key] === 'number'));
  const averages = {};
  const p95 = {};
  for (const key of numericKeys) {
    const values = samples.map(sample => sample[key]);
    averages[key] = round(mean(values));
    p95[key] = round(percentile(values, 0.95));
  }

  const result = {
    name, features: actual, sampleCount: samples.length,
    captureMs: CAPTURE_MS, warmupMs: WARMUP_MS,
    averages, p95, rawSamples: samples, ...meta
  };
  fs.writeFileSync(path.join(outputDir, name + '.json'), JSON.stringify(result, null, 2));
  console.log('[RESULT] ' + name + ': ' + averages.fps.toFixed(1) + ' FPS | frame ' +
    averages.frameTimeMs.toFixed(3) + ' ms | GPU ' + averages.gpuTimeMs.toFixed(3) +
    ' ms | AO+RT ' + averages.gpuAoRtMs.toFixed(3) + ' ms');
  return result;
}

async function saveSceneCapture() {
  const result = await callRpc('engine.captureFrame', {}, 20000);
  if (!result?.frame) return null;
  const base64 = result.frame.replace(/^data:image\/[^;]+;base64,/, '');
  const output = path.join(outputDir, 'atrium-benchmark.jpg');
  fs.writeFileSync(output, Buffer.from(base64, 'base64'));
  console.log('[SCENE] Screenshot saved: ' + output);
  return output;
}

function writeSummary(results, sceneMeta) {
  const fields = [
    'fps', 'frameTimeMs', 'gpuTimeMs', 'drawItems', 'rtInstances',
    'gpuAoRtMs', 'gpuScreenSpaceAoGiMs', 'gpuScreenSpaceBlurMs',
    'rtGiTraceMs', 'rtGiTemporalMs', 'rtGiAtrous0Ms', 'rtGiAtrous1Ms',
    'rtReflectionTraceMs', 'rtReflectionTemporalMs', 'rtComputeTotalMs'
  ];
  const lines = [
    '# Lucid Engine Render Benchmark', '',
    '- Date: ' + new Date().toISOString(),
    '- Resolution: ' + WIDTH + ' × ' + HEIGHT,
    '- Scene entities: ' + sceneMeta.entityCount,
    '- Samples per case: ' + (results[0]?.sampleCount || 0),
    '- Warmup: ' + WARMUP_MS + ' ms; capture: ' + CAPTURE_MS + ' ms',
    '- GPU subpass values are nested inside AO + RT and must not be summed into the parent.',
    '',
    '| Test | ' + fields.join(' | ') + ' |',
    '|---|' + fields.map(() => '---:').join('|') + '|'
  ];
  for (const result of results) {
    lines.push('| ' + result.name + ' | ' + fields.map(key => result.averages[key] ?? 0).join(' | ') + ' |');
  }
  lines.push('', '## Notes', '',
    '- Each case resets RT temporal history before warmup.',
    '- Samples are polled over JSON-RPC from a dedicated maximized windowed Vulkan renderer (swapchain + presentation enabled).',
    '- Benchmark mode hides editor UI and disables viewport-driven resolution changes; the offscreen render target stays fixed to the configured benchmark resolution.',
    '- Compare runs only with the same GPU driver, render resolution, build configuration and scene.',
    '- The harness does not alter RT reflection algorithms or quality defaults.', '');
  fs.writeFileSync(path.join(outputDir, 'REPORT.md'), lines.join('\n'));
  fs.writeFileSync(path.join(outputDir, 'REPORT.csv'),
    ['test,' + fields.join(','), ...results.map(r => [r.name, ...fields.map(k => r.averages[k] ?? 0)].join(','))].join('\n') + '\n');
  fs.writeFileSync(path.join(outputDir, 'REPORT.json'), JSON.stringify({
    generatedAt: new Date().toISOString(), executable: EXE, port: PORT,
    resolution: { width: WIDTH, height: HEIGHT }, scene: sceneMeta,
    settings: { warmupMs: WARMUP_MS, captureMs: CAPTURE_MS, sampleIntervalMs: SAMPLE_INTERVAL_MS },
    results: results.map(({ rawSamples, ...summary }) => summary)
  }, null, 2));
}

async function main() {
  await assertPortFree();
  startEngine();
  await connectWebSocket();
  console.log('[BENCH] Connected. Configuring render resolution...');
  await callRpc('engine.setTargetFps', { fps: 0 });
  await callRpc('engine.setVsyncEnabled', { enabled: false });
  await callRpc('engine.setRenderResolution', { width: WIDTH, height: HEIGHT });
  await sleep(1500); // allow the windowed renderer to recreate its offscreen targets
  const initialStats = await callRpc('engine.getProfilerStats');
  if (!initialStats || Number(initialStats.gpuTimeMs || 0) <= 0) {
    throw new Error('GPU timestamps are not active after starting the windowed renderer. Refusing to produce an invalid benchmark.');
  }
  await callRpc('engine.resetScene', { preset: 'empty' });

  const results = [];
  results.push(await captureScenario('empty_all_off',
    { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false }));
  results.push(await captureScenario('empty_all_flags_on',
    { gtao: true, ssgi: true, rtGi: true, rtAo: true, rtReflections: true }));

  const sceneMeta = await createAtriumBenchmarkScene();
  const savedProjectPath = path.join(outputDir, 'AtriumBenchmark');
  const savedProject = await callRpc('engine.saveCurrentProject', { path: savedProjectPath }, 20000);
  if (!savedProject?.success) throw new Error('Could not save the generated Atrium Benchmark project.');
  sceneMeta.savedProjectPath = savedProject.path;
  console.log('[SCENE] Saved editable benchmark project: ' + savedProject.path);
  await setFeatures({ gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false });
  await sleep(1000);
  await saveSceneCapture();

  results.push(await captureScenario('atrium_all_off',
    { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false }, sceneMeta));
  results.push(await captureScenario('atrium_gtao_only',
    { gtao: true, ssgi: false, rtGi: false, rtAo: false, rtReflections: false }, sceneMeta));
  results.push(await captureScenario('atrium_ssgi_only',
    { gtao: false, ssgi: true, rtGi: false, rtAo: false, rtReflections: false }, sceneMeta));
  results.push(await captureScenario('atrium_gtao_ssgi',
    { gtao: true, ssgi: true, rtGi: false, rtAo: false, rtReflections: false }, sceneMeta));

  results.push(await captureScenario('atrium_rt_gi_only',
    { gtao: false, ssgi: false, rtGi: true, rtAo: false, rtReflections: false }, sceneMeta));
  results.push(await captureScenario('atrium_rt_ao_only',
    { gtao: false, ssgi: false, rtGi: false, rtAo: true, rtReflections: false }, sceneMeta));
  results.push(await captureScenario('atrium_rt_gi_ao',
    { gtao: false, ssgi: false, rtGi: true, rtAo: true, rtReflections: false }, sceneMeta));
  results.push(await captureScenario('atrium_reflections_only',
    { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: true }, sceneMeta));
  results.push(await captureScenario('atrium_all_effects',
    { gtao: true, ssgi: true, rtGi: true, rtAo: true, rtReflections: true }, sceneMeta));

  writeSummary(results, sceneMeta);
  console.log('\n[BENCH] Completed ' + results.length + ' cases.');
  console.log('[BENCH] Reports: ' + outputDir);
}

try {
  await main();
} catch (error) {
  console.error('\n[BENCH] FAILED:', error?.stack || error);
  if (stderrTail) console.error('\n[ENGINE STDERR TAIL]\n' + stderrTail);
  process.exitCode = 1;
} finally {
  if (ws && ws.readyState === WebSocket.OPEN) ws.close();
  if (engineProcess && !childExited) {
    engineProcess.kill();
    await Promise.race([new Promise(resolve => engineProcess.once('exit', resolve)), sleep(2500)]);
  }
}
