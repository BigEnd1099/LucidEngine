import WebSocket from 'ws';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const PORT = Number(process.env.LUCID_RT_PORT || process.argv[2] || 9000);
const CAPTURE_MS = Number(process.env.LUCID_RT_CAPTURE_MS || process.argv[3] || 10000);
const INTERVAL_MS = Number(process.env.LUCID_RT_SAMPLE_INTERVAL_MS || 100);
const WARMUP_MS = Number(process.env.LUCID_RT_WARMUP_MS || 1200);
const MODE = String(process.env.LUCID_RT_DEBUG_MODE || process.argv[4] || 'none').toLowerCase();
const OUTPUT_ROOT = path.resolve(process.env.LUCID_RT_OUTPUT || 'rt-debug-results');

const DEBUG_MODES = {
  none: 0, raw: 1, history: 2, normals: 3, depth: 4,
  ao: 5, reflections: 6, variance: 7
};
if (!Object.hasOwn(DEBUG_MODES, MODE)) {
  console.error('[RTDEBUG] Invalid mode. Use: ' + Object.keys(DEBUG_MODES).join(', '));
  process.exit(2);
}

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const round = n => Number.isFinite(n) ? Number(n.toFixed(4)) : 0;
const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
const outputDir = path.join(OUTPUT_ROOT, 'run-' + timestamp);
fs.mkdirSync(outputDir, { recursive: true });

let ws;
let rpcId = 1;
function callRpc(method, params = {}, timeoutMs = 8000) {
  return new Promise((resolve, reject) => {
    if (!ws || ws.readyState !== WebSocket.OPEN) return reject(new Error('WebSocket is not connected'));
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

function summarize(samples) {
  const keys = [...new Set(samples.flatMap(s => Object.keys(s.metrics || {})))].sort();
  const result = {};
  for (const key of keys) {
    const values = samples.map(s => Number(s.metrics?.[key])).filter(Number.isFinite);
    if (!values.length) continue;
    const sorted = [...values].sort((a,b) => a-b);
    result[key] = {
      average: round(values.reduce((a,b) => a+b, 0) / values.length),
      min: round(sorted[0]),
      max: round(sorted.at(-1)),
      p95: round(sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * 0.95))])
    };
  }
  for (const key of ['frameTimeMs', 'gpuTimeMs', 'fps']) {
    const values = samples.map(s => Number(s[key])).filter(v => Number.isFinite(v) && v > 0);
    if (!values.length) continue;
    const sorted = [...values].sort((a,b) => a-b);
    result[key] = {
      average: round(values.reduce((a,b) => a+b, 0) / values.length),
      min: round(sorted[0]),
      max: round(sorted.at(-1)),
      p95: round(sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * 0.95))])
    };
  }
  const stageNames = [...new Set(samples.flatMap(s => Object.keys(s.profilerSamples || {})))].sort();
  result.cpuStages = {};
  for (const stage of stageNames) {
    const values = samples.map(s => Number(s.profilerSamples?.[stage]?.lastMs)).filter(Number.isFinite);
    if (!values.length) continue;
    const sorted = [...values].sort((a,b) => a-b);
    result.cpuStages[stage] = {
      averageMs: round(values.reduce((a,b) => a+b, 0) / values.length),
      p95Ms: round(sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * 0.95))]),
      maxMs: round(sorted.at(-1))
    };
  }
  return result;
}

async function captureCase(name, flags, profile) {
  const quality = {
    giSamples: profile.giSamples, giBounces: profile.giBounces,
    giHalfRes: true, giCheckerboard: true, giIntensity: 1.0,
    reflectionSamples: profile.reflectionSamples, reflectionBounces: 2,
    reflectionInfiniteBounces: false, reflectionMaxRoughness: profile.reflectionMaxRoughness,
    reflectionMinThroughput: 0.06, maxHistory: profile.maxHistory,
    spatialStrength: 1.0, denoiseEnabled: true, svgfEnabled: true,
    reflectionCheckerboard: true, reflectionStochasticLights: true,
    reflectionLodBias: true, reflectionHalfRes: false,
    debugMode: DEBUG_MODES[MODE]
  };
  const actual = await callRpc('engine.setRenderFeatures', {
    gtao: false, ssgi: false, rtEnabled: true,
    rtGi: !!flags.gi, rtAo: !!flags.ao, rtReflections: !!flags.reflections,
    quality
  });
  const rb = actual?.quality || {};
  const mismatches = Object.entries(quality).filter(([k,v]) =>
    typeof v === 'number' && typeof rb[k] === 'number' ? Math.abs(rb[k]-v) > 1e-4 : rb[k] !== v);
  if (mismatches.length) {
    throw new Error('Quality readback mismatch: ' + mismatches.map(([k,v]) => k + ' expected=' + v + ' got=' + rb[k]).join('; '));
  }

  console.log('[RTDEBUG] ' + name + ' | mode=' + MODE + ' | GI=' + !!flags.gi +
    ' AO=' + !!flags.ao + ' reflections=' + !!flags.reflections + ' | warming up');
  await sleep(WARMUP_MS);
  const samples = [];
  const deadline = Date.now() + CAPTURE_MS;
  while (Date.now() < deadline) {
    const stats = await callRpc('engine.getProfilerStats');
    samples.push({
      timestamp: new Date().toISOString(),
      frameTimeMs: Number(stats?.frameTimeMs || 0),
      fps: Number(stats?.fps || 0),
      profilerSamples: stats?.samples || {},
      gpuTimeMs: Number(stats?.gpuTimeMs || 0),
      metrics: stats?.metrics || {}
    });
    await sleep(INTERVAL_MS);
  }
  return {
    name, profile: profile.id, debugMode: MODE, flags, quality: rb,
    sampleCount: samples.length, summary: summarize(samples), samples
  };
}

async function main() {
  ws = new WebSocket('ws://127.0.0.1:' + PORT);
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Connection timeout. Start Lucid Engine first.')), 5000);
    ws.once('open', () => { clearTimeout(timer); resolve(); });
    ws.once('error', err => { clearTimeout(timer); reject(err); });
  });
  console.log('[RTDEBUG] Connected to ws://127.0.0.1:' + PORT);
  const initialStats = await callRpc('engine.getProfilerStats');
  if (!initialStats || Number(initialStats.gpuTimeMs || 0) <= 0) {
    throw new Error('GPU timestamps are unavailable. Refusing to create misleading RT timing reports.');
  }
  if (Number(initialStats?.metrics?.drawItems || 0) === 0) {
    console.warn('[RTDEBUG] Warning: no draw items are currently visible. Load AtriumBenchmark for meaningful RT workload measurements.');
  }
  console.log('[RTDEBUG] Uses the currently loaded scene. Load AtriumBenchmark for comparable RT measurements.');
  console.log('[RTDEBUG] Debug view is applied to the live renderer; close this tool to stop sampling.');

  const profiles = [
    { id: 'default', giSamples: 1, giBounces: 2, reflectionSamples: 1, reflectionMaxRoughness: 0.60, maxHistory: 32 },
    { id: 'editor_quality', giSamples: 16, giBounces: 3, reflectionSamples: 16, reflectionMaxRoughness: 1.0, maxHistory: 256 }
  ];
  const cases = [
    ['gi_only', { gi: true, ao: false, reflections: false }],
    ['ao_only', { gi: false, ao: true, reflections: false }],
    ['reflections_only', { gi: false, ao: false, reflections: true }],
    ['all_rt', { gi: true, ao: true, reflections: true }]
  ];
  const results = [];
  for (const profile of profiles) {
    for (const [name, flags] of cases) {
      results.push(await captureCase(profile.id + '_' + name, flags, profile));
    }
  }

  const report = {
    generatedAt: new Date().toISOString(),
    endpoint: 'ws://127.0.0.1:' + PORT,
    scene: 'currently loaded scene (not modified by this tool)',
    debugMode: MODE,
    settings: { warmupMs: WARMUP_MS, captureMsPerCase: CAPTURE_MS, sampleIntervalMs: INTERVAL_MS },
    results: results.map(({ samples, ...summary }) => summary),
    rawSamples: results.map(r => ({ name: r.name, samples: r.samples }))
  };
  fs.writeFileSync(path.join(outputDir, 'RT_DEBUG_REPORT.json'), JSON.stringify(report, null, 2));
  const lines = [
    '# Lucid Engine RT Debugger',
    '',
    '- Generated: ' + report.generatedAt,
    '- Endpoint: ' + report.endpoint,
    '- Scene: currently loaded scene',
    '- Debug view: ' + MODE + ' (ID ' + DEBUG_MODES[MODE] + ')',
    '- Samples per case: approximately ' + Math.ceil(CAPTURE_MS / INTERVAL_MS),
    '',
    '| Case | Samples | Frame ms avg | GPU ms avg | RT GI trace ms | RT reflections ms | RT total ms |',
    '|---|---:|---:|---:|---:|---:|---:|'
  ];
  for (const r of results) {
    const s = r.summary;
    const m = r.summary;
    lines.push('| ' + r.name + ' | ' + r.sampleCount + ' | ' +
      (s.frameTimeMs?.average ?? 0) + ' | ' + (s.gpuTimeMs?.average ?? 0) + ' | ' +
      (m.rtGiTraceMs?.average ?? 0) + ' | ' + (m.rtReflectionTraceMs?.average ?? 0) + ' | ' +
      (m.rtComputeTotalMs?.average ?? 0) + ' |');
  }
  const cpuStageNames = [...new Set(results.flatMap(result =>
    Object.keys(result.summary.cpuStages || {})))].sort();
  lines.push('', '## CPU profiler scopes (sampled)', '',
    '| Case | Scope | Average ms | P95 ms | Max ms |',
    '|---|---|---:|---:|---:|');
  for (const result of results) {
    for (const stage of cpuStageNames) {
      const stats = result.summary.cpuStages?.[stage];
      if (!stats) continue;
      lines.push('| ' + result.name + ' | ' + stage + ' | ' +
        stats.averageMs + ' | ' + stats.p95Ms + ' | ' + stats.maxMs + ' |');
    }
  }
  lines.push('', 'CPU scopes are sampled snapshots, not per-frame percentiles.',
    '', '## Methodology', '',
    '- Reads existing renderer profiler snapshots through JSON-RPC; does not perform GPU readback.',
    '- The selected debug view is rendered live and may alter the visual output.',
    '- Percentiles are across polled snapshots, not per-frame percentiles.',
    '- The tool changes RT feature/quality settings and leaves the last selected configuration active.',
    '- Use the same scene, resolution, build and GPU driver when comparing runs.',
    '- This tool does not report per-pixel history-age histograms or temporal rejection counters; those require shader instrumentation and a separate GPU diagnostic readback path.',
    '');
  fs.writeFileSync(path.join(outputDir, 'RT_DEBUG_REPORT.md'), lines.join('\n'));
  console.log('[RTDEBUG] Completed ' + results.length + ' cases.');
  console.log('[RTDEBUG] Reports: ' + outputDir);
}

main().catch(err => {
  console.error('[RTDEBUG] FAILED:', err?.stack || err);
  process.exitCode = 1;
}).finally(() => {
  if (ws && ws.readyState === WebSocket.OPEN) ws.close();
});
