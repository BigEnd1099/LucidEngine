import WebSocket from 'ws';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { createServer } from 'node:net';

const ROOT = process.cwd();
const EXE = path.resolve(process.argv[2] || 'engine/build/Release/engine.exe');
const PORT = Number(process.env.LUCID_BENCH_PORT || 9011);
const WIDTH = Number(process.env.LUCID_BENCH_WIDTH || 2560);
const HEIGHT = Number(process.env.LUCID_BENCH_HEIGHT || 1440);
const WARMUP_MS = Number(process.env.LUCID_BENCH_WARMUP_MS || 1500);
const CAPTURE_MS = Number(process.env.LUCID_BENCH_CAPTURE_MS || 2000);
const RT_AB_REPEATS = Math.max(1, Math.min(5, Number(process.env.LUCID_RT_AB_REPEATS || 3)));
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

async function setBenchmarkEnvironment(enabled) {
  await callRpc('engine.setEnvironment', {
    hdriEnabled: enabled,
    skyboxEnabled: enabled,
    iblEnabled: enabled,
    ambientColor: enabled ? '#263449' : '#000000',
    ambientIntensity: enabled ? 0.7 : 0.0,
    sunEnabled: enabled,
    sunIntensity: enabled ? 2.2 : 0.0,
    fogEnabled: false
  });
}

async function createAtriumBenchmarkScene() {
  console.log('[SCENE] Building Atrium Benchmark scene...');
  // Use a truly empty registry. The engine's 'empty' preset intentionally creates a ground plane and sun.
  await callRpc('engine.resetScene', { preset: 'clean' });

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

  await setBenchmarkEnvironment(true);
  await callRpc('engine.setCamera', {
    x: 0, y: 8.0, z: 34, yaw: -90, pitch: -11, fov: 62
  });

  const meshEntityCount = spawnJobs.filter(item => item.mesh).length;
  const lightEntityCount = spawnJobs.filter(item => item.light).length;
  console.log('[SCENE] Created ' + ids.length + ' benchmark entities (' + meshEntityCount + ' meshes, ' + lightEntityCount + ' lights).');
  return { entityCount: ids.length, meshEntityCount, lightEntityCount };
}

async function setFeatures(features, quality = null) {
  return await callRpc('engine.setRenderFeatures', {
    gtao: !!features.gtao,
    ssgi: !!features.ssgi,
    rtEnabled: true,
    rtGi: !!features.rtGi,
    rtAo: !!features.rtAo,
    rtReflections: !!features.rtReflections,
    ...(quality ? { quality } : {})
  });
}

const RT_PROFILES = [
  {
    id: 'default',
    label: 'Engine defaults',
    settings: {
      giSamples: 1, giBounces: 2, giHalfRes: true, giCheckerboard: true,
      giIntensity: 1.0, reflectionSamples: 1, reflectionBounces: 2,
      reflectionInfiniteBounces: false, reflectionMaxRoughness: 0.60,
      reflectionMinThroughput: 0.06, maxHistory: 32, spatialStrength: 1.0,
      denoiseEnabled: true, svgfEnabled: true, reflectionCheckerboard: true,
      reflectionStochasticLights: true, reflectionPrimaryStochasticLights: false, reflectionLodBias: true,
      reflectionHalfRes: false, debugMode: 0
    }
  },
  {
    id: 'editor_quality',
    label: 'Editor Quality (reported configuration)',
    settings: {
      giSamples: 16, giBounces: 3, giHalfRes: true, giCheckerboard: true,
      giIntensity: 1.0, reflectionSamples: 16, reflectionBounces: 2,
      reflectionInfiniteBounces: false, reflectionMaxRoughness: 1.0,
      reflectionMinThroughput: 0.06, maxHistory: 256, spatialStrength: 1.0,
      denoiseEnabled: true, svgfEnabled: true, reflectionCheckerboard: true,
      reflectionStochasticLights: true, reflectionPrimaryStochasticLights: false, reflectionLodBias: true,
      reflectionHalfRes: false, debugMode: 0
    }
  }
];

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

async function captureScenario(name, features, meta = {}, profile = RT_PROFILES[0]) {
  const actual = await setFeatures(features, profile.settings);
  const readback = actual?.quality || {};
  const matches = (actualValue, expected) => typeof expected === 'number' && typeof actualValue === 'number'
    ? Math.abs(actualValue - expected) < 1e-5
    : actualValue === expected;
  const mismatches = Object.entries(profile.settings).filter(([key, expected]) => !matches(readback[key], expected));
  if (mismatches.length) {
    throw new Error('Renderer quality readback mismatch for profile ' + profile.id + ': ' +
      mismatches.map(([key, expected]) => key + ' expected=' + expected + ' actual=' + readback[key]).join(', '));
  }
  console.log('\n[TEST] ' + name + ' | GTAO=' + actual.gtao + ' SSGI=' + actual.ssgi +
    ' RTGI=' + actual.rtGi + ' RTAO=' + actual.rtAo + ' RTRefl=' + actual.rtReflections);
  await sleep(WARMUP_MS);

  const samples = [];
  const deadline = Date.now() + CAPTURE_MS;
  while (Date.now() < deadline) {
    const stats = await callRpc('engine.getProfilerStats');
    const metrics = stats?.metrics || {};
    const sample = {
      profilerSamples: stats?.samples || {},
      timestamp: new Date().toISOString(),
      frameTimeMs: Number(stats?.frameTimeMs || 0),
      profilerSmoothedFps: Number(stats?.fps || 0),
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
      rtComputeTotalMs: Number(metrics.rtComputeTotalMs || 0),
      rtGiTraceExecMs: Number(metrics.rtGiTraceExecMs || 0),
      rtGiTraceSyncMs: Number(metrics.rtGiTraceSyncMs || 0),
      rtGiTemporalExecMs: Number(metrics.rtGiTemporalExecMs || 0),
      rtGiTemporalSyncMs: Number(metrics.rtGiTemporalSyncMs || 0),
      rtGiAtrous0ExecMs: Number(metrics.rtGiAtrous0ExecMs || 0),
      rtGiAtrous0SyncMs: Number(metrics.rtGiAtrous0SyncMs || 0),
      rtReflectionStartSyncMs: Number(metrics.rtReflectionStartSyncMs || 0),
      rtReflectionTraceExecMs: Number(metrics.rtReflectionTraceExecMs || 0),
      rtReflectionTraceSyncMs: Number(metrics.rtReflectionTraceSyncMs || 0),
      rtReflectionTemporalExecMs: Number(metrics.rtReflectionTemporalExecMs || 0),
      rtReflectionTemporalSyncMs: Number(metrics.rtReflectionTemporalSyncMs || 0),
      rtGiSamples: Number(metrics.rtGiSamples || 0),
      rtGiBounces: Number(metrics.rtGiBounces || 0),
      rtGiWidth: Number(metrics.rtGiWidth || 0),
      rtGiHeight: Number(metrics.rtGiHeight || 0),
      rtMaxHistory: Number(metrics.rtMaxHistory || 0),
      rtSpatialStrength: Number(metrics.rtSpatialStrength || 0),
      rtSvgfEnabled: Number(metrics.rtSvgfEnabled || 0),
      rtGiCheckerboard: Number(metrics.rtGiCheckerboard || 0),
      rtGiHistoryValid: Number(metrics.rtGiHistoryValid || 0),
      rtReflectionHistoryValid: Number(metrics.rtReflectionHistoryValid || 0),
      rtHistoryInvalidatedByLightChange: Number(metrics.rtHistoryInvalidatedByLightChange || 0),
      rtReflectionSamples: Number(metrics.rtReflectionSamples || 0),
      rtReflectionBounces: Number(metrics.rtReflectionBounces || 0),
      rtReflectionInfiniteBounces: Number(metrics.rtReflectionInfiniteBounces || 0),
      rtReflectionMaxRoughness: Number(metrics.rtReflectionMaxRoughness || 0),
      rtReflectionCheckerboard: Number(metrics.rtReflectionCheckerboard || 0),
      rtReflectionStochasticLights: Number(metrics.rtReflectionStochasticLights || 0),
      rtReflectionPrimaryStochasticLights: Number(metrics.rtReflectionPrimaryStochasticLights || 0),
      rtReflectionLodBias: Number(metrics.rtReflectionLodBias || 0),
      rtReflectionHalfRes: Number(metrics.rtReflectionHalfRes || 0),
      rtDebugMode: Number(metrics.rtDebugMode || 0),
      ...Object.fromEntries(Object.entries(metrics).filter(([, value]) => typeof value === 'number'))
    };
    const reflectionAgeBinKeys = Array.from({ length: 16 }, (_, i) =>
      'rtReflectionHistoryAgeBin' + String(i).padStart(2, '0'));
    const reflectionAgeBins = reflectionAgeBinKeys.map(key => Number(sample[key] || 0));
    const reflectionAgeHistogramTotal = reflectionAgeBins.reduce((sum, value) => sum + value, 0);
    const reflectionPixels = Number(sample.rtReflectionPixels || 0);
    sample.rtReflectionHistoryAgeHistogramTotal = reflectionAgeHistogramTotal;
    sample.rtReflectionHistoryAgeNonZeroPixels = reflectionAgeBins.slice(1).reduce((sum, value) => sum + value, 0);
    sample.rtReflectionHistoryAgeBin00Share = reflectionAgeHistogramTotal > 0
      ? reflectionAgeBins[0] / reflectionAgeHistogramTotal : 0;
    sample.rtReflectionHistoryAgeHistogramCoverage = reflectionPixels > 0
      ? reflectionAgeHistogramTotal / reflectionPixels : (reflectionAgeHistogramTotal === 0 ? 1 : 0);
    sample.rtReflectionHistoryAgeHistogramMismatch =
      reflectionPixels > 0 && reflectionAgeHistogramTotal !== reflectionPixels ? 1 : 0;
    samples.push(sample);
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

  // Profiler::fps is an exponential moving average and can lag scenario transitions.
  // Report throughput from the same measured frame-time window; retain the smoothed value separately.
  averages.fps = averages.frameTimeMs > 0 ? round(1000.0 / averages.frameTimeMs) : 0;

  // FPS is inverse frame time: the useful tail metric is throughput at p95 frame time
  // (equivalent to a lower-tail FPS value), not the 95th percentile of FPS itself.
  p95.fpsAtP95FrameTime = p95.frameTimeMs > 0
    ? round(1000.0 / p95.frameTimeMs)
    : 0;

  // CPU-side profiler scopes are captured independently from GPU timestamp queries.
  // Keep the last-sample distribution separate from rolling averages to expose stalls.
  const cpuStageNames = [...new Set(samples.flatMap(sample =>
    Object.keys(sample.profilerSamples || {})))].sort();
  const cpuStageStats = {};
  for (const stage of cpuStageNames) {
    const values = samples
      .map(sample => Number(sample.profilerSamples?.[stage]?.lastMs))
      .filter(Number.isFinite);
    if (!values.length) continue;
    cpuStageStats[stage] = {
      averageMs: round(mean(values)),
      p95Ms: round(percentile(values, 0.95)),
      maxMs: round(Math.max(...values))
    };
  }

  const result = {
    name, profile: { id: profile.id, label: profile.label }, settings: actual,
    features: {
      gtao: actual.gtao, ssgi: actual.ssgi, rtEnabled: actual.rtEnabled,
      rtGi: actual.rtGi, rtAo: actual.rtAo, rtReflections: actual.rtReflections
    },
    sampleCount: samples.length, captureMs: CAPTURE_MS, warmupMs: WARMUP_MS,
    averages, p95, cpuStageStats, rawSamples: samples, ...meta
  };
  fs.writeFileSync(path.join(outputDir, name + '.json'), JSON.stringify(result, null, 2));
  console.log('[RESULT] ' + name + ': ' + averages.fps.toFixed(1) + ' FPS | frame ' +
    averages.frameTimeMs.toFixed(3) + ' ms | GPU ' + averages.gpuTimeMs.toFixed(3) +
    ' ms | AO+RT ' + averages.gpuAoRtMs.toFixed(3) + ' ms');
  return result;
}

async function saveSceneCapture(fileName = 'atrium-benchmark.jpg') {
  const result = await callRpc('engine.captureFrame', {}, 20000);
  if (!result?.frame) return null;
  const base64 = result.frame.replace(/^data:image\/[^;]+;base64,/, '');
  const output = path.join(outputDir, fileName);
  fs.writeFileSync(output, Buffer.from(base64, 'base64'));
  console.log('[SCENE] Screenshot saved: ' + output);
  return output;
}

function writeSummary(results, sceneMeta) {
  const fields = [
    'fps', 'profilerSmoothedFps', 'frameTimeMs', 'gpuTimeMs', 'drawItems', 'rtInstances',
    'gpuAoRtMs', 'gpuScreenSpaceAoGiMs', 'gpuScreenSpaceBlurMs',
    'rtGiTraceMs', 'rtGiTemporalMs', 'rtGiAtrous0Ms', 'rtGiAtrous1Ms',
    'rtReflectionTraceMs', 'rtReflectionTemporalMs', 'rtComputeTotalMs'
  ];
  const rtHistoryAgeFields = Array.from({ length: 16 }, (_, i) =>
    'rtReflectionHistoryAgeBin' + String(i).padStart(2, '0'));
  const rtDiagnosticFields = [
    'rtGiTraceExecMs', 'rtGiTraceSyncMs', 'rtGiTemporalExecMs', 'rtGiTemporalSyncMs',
    'rtGiAtrous0ExecMs', 'rtGiAtrous0SyncMs', 'rtReflectionStartSyncMs',
    'rtReflectionTraceExecMs', 'rtReflectionTraceSyncMs',
    'rtReflectionTemporalExecMs', 'rtReflectionTemporalSyncMs',
    'rtGiSamples', 'rtGiBounces', 'rtGiWidth', 'rtGiHeight', 'rtMaxHistory',
    'rtSpatialStrength', 'rtSvgfEnabled', 'rtGiCheckerboard', 'rtGiHistoryValid',
    'rtReflectionHistoryValid', 'rtHistoryInvalidatedByLightChange',
    'rtReflectionPrimaryStochasticLights', 'rtReflectionSamples', 'rtReflectionBounces', 'rtReflectionInfiniteBounces',
    'rtReflectionMaxRoughness', 'rtReflectionCheckerboard', 'rtReflectionStochasticLights',
    'rtReflectionLodBias', 'rtReflectionHalfRes', 'rtDebugMode',
    'rtReflectionHistoryAgeHistogramTotal', 'rtReflectionHistoryAgeNonZeroPixels',
    'rtReflectionHistoryAgeBin00Share', 'rtReflectionHistoryAgeHistogramCoverage',
    'rtReflectionHistoryAgeHistogramMismatch', ...rtHistoryAgeFields
  ];
  const lines = [
    '# Lucid Engine Render Benchmark', '',
    '- Date: ' + new Date().toISOString(),
    '- Resolution: ' + WIDTH + ' × ' + HEIGHT,
    '- Scene entities: ' + sceneMeta.entityCount,
    '- Samples per case: ' + (results[0]?.sampleCount || 0),
    '- RT quality profiles: ' + [...new Set(results.map(r => r.profile?.id).filter(Boolean))].join(', '),
    '- Profile settings are applied over JSON-RPC and captured from renderer readback; numeric profiler metrics are also copied dynamically into raw samples.',
    '- Warmup: ' + WARMUP_MS + ' ms; capture: ' + CAPTURE_MS + ' ms',
    '- Primary-hit stochastic-light A/B repeats: ' + RT_AB_REPEATS + ' (editor_quality; OFF vs ON, same binary/scene/camera).',
    '- GPU subpass values are nested inside AO + RT and must not be summed into the parent.',
    '',
    '| Test | ' + fields.join(' | ') + ' |',
    '|---|' + fields.map(() => '---:').join('|') + '|'
  ];
  for (const result of results) {
    lines.push('| ' + result.name + ' | ' + fields.map(key => result.averages[key] ?? 0).join(' | ') + ' |');
  }
  const p95MetricKey = key => key === 'fps' ? 'fpsAtP95FrameTime' : key;
  const p95MetricLabel = key => key === 'fps' ? 'fpsAtP95FrameTime' : key;
  lines.push('', '## Sampled profiler snapshot p95 values', '',
    '| Test | ' + fields.map(p95MetricLabel).join(' | ') + ' |',
    '|---|' + fields.map(() => '---:').join('|') + '|');
  for (const result of results) {
    lines.push('| ' + result.name + ' | ' +
      fields.map(key => result.p95[p95MetricKey(key)] ?? 0).join(' | ') + ' |');
  }
  const cpuStageNames = [...new Set(results.flatMap(r => Object.keys(r.cpuStageStats || {})))].sort();
  lines.push('', '## CPU profiler scopes (sampled)', '',
    '| Test | Scope | Average ms | P95 ms | Max ms |',
    '|---|---|---:|---:|---:|');
  for (const result of results) {
    for (const stage of cpuStageNames) {
      const stats = result.cpuStageStats?.[stage];
      if (!stats) continue;
      lines.push('| ' + result.name + ' | ' + stage + ' | ' +
        stats.averageMs + ' | ' + stats.p95Ms + ' | ' + stats.maxMs + ' |');
    }
  }
  lines.push('', 'CPU scopes are polled snapshots of the latest completed scope, not per-frame percentiles.',
    'Vulkan Fence Wait, Vulkan Acquire Image and Vulkan Submit + Present isolate host-side synchronization/presentation costs.',
    '', '## Screen-space quality settings', '',
    '| Test | GTAO radius | GTAO intensity | SSGI intensity | Ground reflectance | Multi-bounce | Ground color (linear RGB) |',
    '|---|---:|---:|---:|---:|---:|---|');
  for (const result of results) {
    const a = result.averages;
    lines.push('| ' + result.name + ' | ' + [
      a.gtaoRadius ?? 0, a.gtaoIntensity ?? 0, a.ssgiIntensity ?? 0,
      a.ssgiGroundReflectance ?? 0, a.ssgiMultiBounce ?? 0,
      [a.ssgiGroundColorR ?? 0, a.ssgiGroundColorG ?? 0, a.ssgiGroundColorB ?? 0].join(', ')
    ].join(' | ') + ' |');
  }
  lines.push('', '## RT pass execution and dependency gaps', '',
    '| Test | GI trace exec | GI trace gap | Temporal exec | Temporal gap | A-trous 0 exec | A-trous 0 gap | GI→reflection gap | Reflection trace exec | Reflection trace gap | Reflection temporal exec | Reflection temporal gap |',
    '|---|' + Array(11).fill('---:').join('|') + '|');
  for (const result of results) {
    lines.push('| ' + result.name + ' | ' + rtDiagnosticFields.slice(0, 11).map(key => result.averages[key] ?? 0).join(' | ') + ' |');
  }
  lines.push('', '## RT quality and temporal-history telemetry', '',
    '| Test | GI samples | GI bounces | GI internal resolution | History capacity | Spatial strength | SVGF | Checkerboard | GI history valid | Reflection history valid | History reset by light/camera | Reflection samples |',
    '|---|' + Array(11).fill('---:').join('|') + '|');
  for (const result of results) {
    const a = result.averages;
    lines.push('| ' + result.name + ' | ' + [
      a.rtGiSamples ?? 0, a.rtGiBounces ?? 0,
      String(a.rtGiWidth ?? 0) + ' × ' + String(a.rtGiHeight ?? 0),
      a.rtMaxHistory ?? 0, a.rtSpatialStrength ?? 0,
      a.rtSvgfEnabled ?? 0, a.rtGiCheckerboard ?? 0, a.rtGiHistoryValid ?? 0,
      a.rtReflectionHistoryValid ?? 0, a.rtHistoryInvalidatedByLightChange ?? 0,
      a.rtReflectionSamples ?? 0
    ].join(' | ') + ' |');
  }
  lines.push('', '## RT reflection quality settings', '',
    '| Test | Reflection samples | Bounces | Infinite bounces | Max roughness | Checkerboard | Stochastic lights | LOD bias | Half resolution | Debug mode ID |',
    '|---|' + Array(9).fill('---:').join('|') + '|');
  for (const result of results) {
    const a = result.averages;
    lines.push('| ' + result.name + ' | ' + [
      a.rtReflectionSamples ?? 0, a.rtReflectionBounces ?? 0,
      a.rtReflectionInfiniteBounces ?? 0, a.rtReflectionMaxRoughness ?? 0,
      a.rtReflectionCheckerboard ?? 0, a.rtReflectionStochasticLights ?? 0,
      a.rtReflectionLodBias ?? 0, a.rtReflectionHalfRes ?? 0, a.rtDebugMode ?? 0
    ].join(' | ') + ' |');
  }
  lines.push('', '## Reflection-history age histogram', '',
    'Bin 00 includes zero-age pixels and ages below the first linear threshold (maxHistory / 16). The histogram is a per-frame diagnostic counter distribution; compare it with rtReflectionPixels. A coverage value of 1.0 means the 16 bins account for every in-bounds reflection pixel.', '',
    '| Test | Reflection pixels | Histogram total | Bin 00 share | Non-zero age pixels | Coverage | Mismatch |',
    '|---|---:|---:|---:|---:|---:|---:|');
  for (const result of results) {
    const a = result.averages;
    lines.push('| ' + result.name + ' | ' + [
      a.rtReflectionPixels ?? 0, a.rtReflectionHistoryAgeHistogramTotal ?? 0,
      a.rtReflectionHistoryAgeBin00Share ?? 0, a.rtReflectionHistoryAgeNonZeroPixels ?? 0,
      a.rtReflectionHistoryAgeHistogramCoverage ?? 0, a.rtReflectionHistoryAgeHistogramMismatch ?? 0
    ].join(' | ') + ' |');
  }
  lines.push('', '## Primary-hit stochastic-light A/B', '',
    '| Pair | Variant | Repeat | FPS | Frame ms | GPU ms | AO+RT ms | Reflection trace exec ms | Reflection trace incl. gap ms | Reflection temporal ms | History accepted | Bin 00 share |',
    '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|');
  for (const result of results.filter(r => r.abComparison)) {
    const a = result.averages;
    lines.push('| ' + [
      result.abComparison.featureSet, result.abComparison.primaryStochasticLights ? 'ON' : 'OFF',
      result.abComparison.repeat, a.fps ?? 0, a.frameTimeMs ?? 0, a.gpuTimeMs ?? 0, a.gpuAoRtMs ?? 0,
      a.rtReflectionTraceExecMs ?? 0, a.rtReflectionTraceMs ?? 0, a.rtReflectionTemporalMs ?? 0,
      a.rtReflectionHistoryAccepted ?? 0, a.rtReflectionHistoryAgeBin00Share ?? 0
    ].join(' | ') + ' |');
  }
  lines.push('', '## Notes', '',
    '- Each case resets RT temporal history before warmup.',
    '- Samples are polled over JSON-RPC from a dedicated maximized windowed Vulkan renderer (swapchain + presentation enabled).',
    '- Benchmark mode hides editor UI and disables viewport-driven resolution changes; the offscreen render target stays fixed to the configured benchmark resolution.',
    '- Compare runs only with the same GPU driver, render resolution, build configuration and scene.',
    '- p95 values are percentiles across the approximately 20 profiler snapshots polled at 100 ms intervals; they are not per-frame GPU/frame-time percentiles.',
    '- fpsAtP95FrameTime is calculated as 1000 / p95(frameTimeMs) across those snapshots; it is a sampled slow-snapshot throughput indicator, not a per-frame 1% low.',
    '- RT stage timings retain inclusive values and additionally expose dispatch-only execution and dependency-gap intervals.',
    '- Dependency-gap intervals are GPU scheduling gaps around synchronization barriers, not direct measurements of barrier instruction cost.',
    '- RT quality telemetry (samples, internal GI resolution, temporal history and SVGF state) is observational; the harness does not change RT quality defaults or reflection algorithms.',
    '- The A/B variant changes only reflectionPrimaryStochasticLights; reflectionStochasticLights remains enabled for secondary bounces in both variants.',
    '- Reflection-history bins are exported by the existing diagnostic SSBO; the report checks histogram coverage against rtReflectionPixels and retains every bin in raw samples/CSV.', '');
  fs.writeFileSync(path.join(outputDir, 'REPORT.md'), lines.join('\n'));
  const csvFields = [...new Set([...fields, ...rtDiagnosticFields, ...rtHistoryAgeFields,
    'gtaoRadius', 'gtaoIntensity', 'ssgiIntensity', 'ssgiGroundReflectance', 'ssgiMultiBounce',
    'ssgiGroundColorR', 'ssgiGroundColorG', 'ssgiGroundColorB',
    'rtEnabled', 'rtAvailable', 'rtShadowsEnabled', 'rtGiHalfRes', 'rtGiIntensity', 'rtDenoiseEnabled',
    'rtReflectionMinThroughput'])];
  const p95Fields = csvFields.map(key => 'p95_' + (key === 'fps' ? 'fpsAtP95FrameTime' : key));
  const csvEscape = value => '"' + String(value ?? '').replace(/"/g, '""') + '"';
  const csvHeader = ['test', 'profile', 'settings_json',
    ...csvFields.map(key => 'avg_' + key), ...p95Fields];
  fs.writeFileSync(path.join(outputDir, 'REPORT.csv'),
    [csvHeader.join(','), ...results.map(r => [
      csvEscape(r.name), csvEscape(r.profile?.id),
      csvEscape(JSON.stringify(r.settings || {})),
      ...csvFields.map(key => r.averages[key] ?? 0),
      ...csvFields.map(key => r.p95[key === 'fps' ? 'fpsAtP95FrameTime' : key] ?? 0)
    ].join(','))].join('\n') + '\n');
  fs.writeFileSync(path.join(outputDir, 'REPORT.json'), JSON.stringify({
    generatedAt: new Date().toISOString(), executable: EXE, port: PORT,
    resolution: { width: WIDTH, height: HEIGHT }, scene: sceneMeta,
    settings: { warmupMs: WARMUP_MS, captureMs: CAPTURE_MS, sampleIntervalMs: SAMPLE_INTERVAL_MS, rtAbRepeats: RT_AB_REPEATS },
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
  await callRpc('engine.resetScene', { preset: 'clean' });
  // True-empty baseline: no demo scene, fallback sun, HDRI/IBL or ambient fill.
  await setBenchmarkEnvironment(false);

  const results = [];
  const emptyOff = await captureScenario('empty_all_off',
    { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false });
  if (emptyOff.averages.drawItems !== 0 || emptyOff.averages.rtInstances !== 0) {
    throw new Error('Empty-scene baseline is contaminated: expected 0 draw items and 0 RT instances, got ' +
      emptyOff.averages.drawItems + ' draw items / ' + emptyOff.averages.rtInstances + ' RT instances.');
  }
  results.push(emptyOff);

  const emptyFlagsOn = await captureScenario('empty_all_flags_on',
    { gtao: true, ssgi: true, rtGi: true, rtAo: true, rtReflections: true });
  if (emptyFlagsOn.averages.drawItems !== 0 || emptyFlagsOn.averages.rtInstances !== 0) {
    throw new Error('Empty-scene all-flags-on baseline is contaminated: expected 0 geometry instances.');
  }
  results.push(emptyFlagsOn);

  // Environment-only isolates HDRI/IBL and sun cost without any scene geometry.
  await setBenchmarkEnvironment(true);
  const environmentOnly = await captureScenario('empty_environment_only',
    { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false });
  if (environmentOnly.averages.drawItems !== 0 || environmentOnly.averages.rtInstances !== 0) {
    throw new Error('Environment-only baseline is contaminated: expected 0 draw items and 0 RT instances.');
  }
  results.push(environmentOnly);

  const sceneMeta = await createAtriumBenchmarkScene();
  await setFeatures({ gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false });
  await sleep(500);
  const atriumValidation = await callRpc('engine.getProfilerStats');
  const atriumMetrics = atriumValidation?.metrics || {};
  const actualDrawItems = Number(atriumMetrics.drawItems || 0);
  const actualRtInstances = Number(atriumMetrics.rtInstances || 0);
  if (actualDrawItems !== sceneMeta.meshEntityCount || actualRtInstances !== sceneMeta.meshEntityCount) {
    throw new Error('Atrium scene validation failed: expected ' + sceneMeta.meshEntityCount +
      ' mesh draw items / RT instances, got ' + actualDrawItems + ' / ' + actualRtInstances +
      '. Check for unintended preset geometry or missing RT instances.');
  }
  const savedProjectPath = path.join(outputDir, 'AtriumBenchmark');
  const savedProject = await callRpc('engine.saveCurrentProject', { path: savedProjectPath }, 20000);
  if (!savedProject?.success) throw new Error('Could not save the generated Atrium Benchmark project.');
  sceneMeta.savedProjectPath = savedProject.path;
  console.log('[SCENE] Saved editable benchmark project: ' + savedProject.path);
  await setFeatures({ gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false });
  await sleep(1000);
  await saveSceneCapture();

  const profileCases = [
    ['all_off', { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: false }],
    ['gtao_only', { gtao: true, ssgi: false, rtGi: false, rtAo: false, rtReflections: false }],
    ['ssgi_only', { gtao: false, ssgi: true, rtGi: false, rtAo: false, rtReflections: false }],
    ['gtao_ssgi', { gtao: true, ssgi: true, rtGi: false, rtAo: false, rtReflections: false }],
    ['rt_gi_only', { gtao: false, ssgi: false, rtGi: true, rtAo: false, rtReflections: false }],
    ['rt_ao_only', { gtao: false, ssgi: false, rtGi: false, rtAo: true, rtReflections: false }],
    ['rt_gi_ao', { gtao: false, ssgi: false, rtGi: true, rtAo: true, rtReflections: false }],
    ['reflections_only', { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: true }],
    ['all_effects', { gtao: true, ssgi: true, rtGi: true, rtAo: true, rtReflections: true }]
  ];
  for (const profile of RT_PROFILES) {
    console.log('\\n[PROFILE] ' + profile.label);
    for (const [caseName, features] of profileCases) {
      results.push(await captureScenario('atrium_' + profile.id + '_' + caseName, features, sceneMeta, profile));
    }
  }

  // Controlled same-build A/B for PR #49's primary-hit local-light estimator.
  // Only the primary-hit toggle changes; all RT quality values, scene, camera and secondary-bounce
  // stochastic-light setting remain identical. Alternate order on each repeat to reduce drift bias.
  const editorQuality = RT_PROFILES.find(profile => profile.id === 'editor_quality');
  const abFeatureSets = [
    { id: 'reflections_only', flags: { gtao: false, ssgi: false, rtGi: false, rtAo: false, rtReflections: true } },
    { id: 'all_effects', flags: { gtao: true, ssgi: true, rtGi: true, rtAo: true, rtReflections: true } }
  ];
  for (let repeat = 1; repeat <= RT_AB_REPEATS; repeat++) {
    for (const featureSet of abFeatureSets) {
      const order = (repeat % 2 === 1) ? [false, true] : [true, false];
      for (const primaryStochasticLights of order) {
        const abProfile = {
          ...editorQuality,
          settings: { ...editorQuality.settings, reflectionPrimaryStochasticLights: primaryStochasticLights }
        };
        const variant = primaryStochasticLights ? 'on' : 'off';
        const name = 'ab_editor_quality_' + featureSet.id + '_primary_' + variant + '_r' + repeat;
        const result = await captureScenario(name, featureSet.flags, {
          ...sceneMeta,
          abComparison: {
            featureSet: featureSet.id,
            primaryStochasticLights,
            repeat,
            pairId: 'editor_quality_' + featureSet.id + '_r' + repeat
          }
        }, abProfile);
        results.push(result);
        if (repeat === 1) await saveSceneCapture(name + '.jpg');
      }
    }
  }

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
