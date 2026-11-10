# Lucid Engine render benchmark

The benchmark runner starts a separate headless engine process on port **9011** (default), generates the deterministic **Atrium Benchmark** scene, toggles renderer features over JSON-RPC, and writes one-second-plus sampling windows to disk.

## Run

1. Build the engine in Release:
   `.uild_engine.bat release`
2. From the repository root, run:
   `npm run benchmark:render`

Or pass a custom engine executable path:

`node tools/run_render_benchmark.mjs engine/build/Release/engine.exe`

The runner does not attach to or terminate an already-open editor instance. It launches its own process using `--headless --port 9011`. If the port is occupied, close the process using it or set `LUCID_BENCH_PORT` to another free port.

## Optional environment variables

- `LUCID_BENCH_PORT` — remote port, default `9011`
- `LUCID_BENCH_WIDTH` / `LUCID_BENCH_HEIGHT` — render resolution, defaults `2404 × 1866`
- `LUCID_BENCH_WARMUP_MS` — warm-up per case, default `1500`
- `LUCID_BENCH_CAPTURE_MS` — sampling period per case, default `2000`

## Scene design

Atrium Benchmark is generated from native ECS entities and built-in meshes. It contains an architectural shell, 15 structural columns, beams, a 21-object PBR gallery, a dense modular prop field, polished metal spheres, emissive panels, and 12 local point lights. The current scene generator creates approximately 170 entities, depending on future changes.

## Test matrix

- Empty scene: all effects off / all effect flags on
- Atrium: all effects off
- GTAO only / SSGI only / GTAO + SSGI
- RT GI only / RT AO only / RT GI + AO
- RT reflections only
- All effects on

Each case resets RT temporal history, warms up, then samples profiler telemetry. The report includes mean and p95 values, raw samples, and a CSV summary.

## Output

Every run creates `benchmark-results/run-<timestamp>/` containing:

- `REPORT.md` — comparison table
- `REPORT.csv` — spreadsheet-friendly summary
- `REPORT.json` — machine-readable summary
- one JSON file per test case with raw samples
- `atrium-benchmark.jpg` — scene capture

GPU subpass values are nested inside the AO + RT aggregate; do not add them to the parent stage. RT reflection algorithms and quality defaults are not modified by the benchmark harness.
