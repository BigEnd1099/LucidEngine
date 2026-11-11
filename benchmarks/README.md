# Lucid Engine render benchmark

The benchmark runner starts a separate windowed engine process on port **9011** (default), generates the deterministic **Atrium Benchmark** scene, saves it as an editable project inside the run folder, toggles renderer features over JSON-RPC, and writes sampling windows to disk.

## Run

1. Build the engine in Release:
   `.uild_engine.bat release`
2. From the repository root, run:
   `npm run benchmark:render`

Or pass a custom engine executable path:

`node tools/run_render_benchmark.mjs engine/build/Release/engine.exe`

The runner disables the FPS limiter and VSync for uncapped measurements. The render target remains fixed to `LUCID_BENCH_WIDTH` × `LUCID_BENCH_HEIGHT`; maximizing the window does not change those dimensions. It does not attach to or terminate an already-open editor instance. It launches its own normal windowed Vulkan process using `--benchmark --port 9011` (no `--headless` flag), so the engine creates a maximized GLFW window, swapchain and presents frames to the GPU. Benchmark mode shows only the scene viewport; editor panels and viewport auto-resize are disabled. If the port is occupied, close the process using it or set `LUCID_BENCH_PORT` to another free port.

## Optional environment variables

- `LUCID_BENCH_PORT` — remote port, default `9011`
- `LUCID_BENCH_WIDTH` / `LUCID_BENCH_HEIGHT` — render resolution, defaults `2560 × 1440` (1440p, 16:9)
- `LUCID_BENCH_WARMUP_MS` — warm-up per case, default `1500`
- `LUCID_BENCH_CAPTURE_MS` — sampling period per case, default `2000`

## Scene design

Atrium Benchmark is generated from native ECS entities and built-in meshes. It contains an architectural shell, 6 perimeter columns, beams, a 21-object PBR gallery, a dense modular prop field, polished metal spheres, emissive panels, and 12 local point lights. The current scene generator creates approximately 160 entities, depending on future changes.

## Test matrix

- True-empty scene (no demo geometry, fallback sun, HDRI or ambient fill): all effects off / all effect flags on
- Environment-only scene: HDRI/IBL and sun enabled, with no geometry
- Atrium: all effects off
- GTAO only / SSGI only / GTAO + SSGI
- RT GI only / RT AO only / RT GI + AO
- RT reflections only
- All effects on

Each case resets RT temporal history, warms up, then samples profiler telemetry. The report includes mean and p95 values, raw samples, and a CSV summary.

## Quality profiles and diagnostics

The Atrium feature matrix runs twice with identical feature combinations:

- `default` — the engine's current RT defaults (1 GI sample, 2 GI bounces, 1 reflection sample, 2 reflection bounces, 32-frame history capacity and 0.60 reflection roughness limit).
- `editor_quality` — the configuration from the supplied Editor Performance Profiler capture (16 GI samples, 3 GI bounces, 16 reflection samples, 2 reflection bounces, 256-frame history capacity and 1.0 reflection roughness limit). Other RT controls are explicitly set to the same known defaults and are included in readback.

The benchmark configures settings through `engine.setRenderFeatures` and stores the renderer's actual readback in every case JSON. It does not assume that requested settings were accepted. The JSON-RPC endpoint supports RT sample counts, bounce counts, GI resolution/checkerboard/intensity, reflection roughness/throughput and optimization flags, denoiser/SVGF/history controls, debug mode, plus GTAO radius/intensity and SSGI intensity/ground controls.

Every numeric profiler metric is copied into each raw sample dynamically, so newly added numeric diagnostics are retained without another benchmark-script edit. Current settings telemetry includes GTAO/SSGI parameters and RT enable flags, quality settings, internal GI dimensions, pass timings and dependency intervals. Reports retain per-case mean/p95 and raw samples.

The editor-quality profile reproduces the RT values visible in the supplied capture; it does not claim to reproduce unreported editor settings. GTAO/SSGI values are recorded from the running renderer and are not silently inferred.

The shader debug modes include history-length and variance visualizations. The benchmark deliberately does not read back per-pixel history-age distributions or rejection counts in its normal performance pass: that requires an additional GPU readback path and would perturb the timings. Those measurements should be added as an explicitly opt-in diagnostic capture, separate from clean performance numbers.

## Output

Every run creates `benchmark-results/run-<timestamp>/` containing:

- `REPORT.md` — comparison table
- `REPORT.csv` — spreadsheet-friendly summary
- `REPORT.json` — machine-readable summary
- one JSON file per test case with raw samples
- `atrium-benchmark.jpg` — scene capture
- `AtriumBenchmark/` — saved editable project (`.uproject`, `project.json`, `game.json` and project folders)

GPU subpass values are nested inside the AO + RT aggregate; do not add them to the parent stage.

RT pass timings retain inclusive stage values and also report dispatch-only execution plus GPU dependency-gap intervals around barriers. Dependency gaps are scheduling intervals, not direct measurements of barrier instruction cost.

Reports include observational RT quality telemetry: GI sample count, internal GI resolution, GI and reflection bounce counts, temporal-history capacity/validity, history invalidation by camera/light changes, SVGF state, checkerboard state, reflection roughness threshold, stochastic-light/LOD options and reflection resolution.

RT reflection algorithms, shaders and quality defaults are not modified by the benchmark harness.

## Renderer algorithm regression tests

The optional C++ regression target covers CPU-side renderer algorithms without launching the editor or requiring a GPU device:

```powershell
cmake -S engine -B engine/build/algorithm-tests -DLUCID_BUILD_ALGORITHM_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build engine/build/algorithm-tests --config Release --target lucid_renderer_algorithm_tests
ctest --test-dir engine/build/algorithm-tests -C Release --output-on-failure
```

The suite currently checks ReSTIR reservoir candidate-count normalization, invalid target-PDF handling, non-normalized frustum planes, conservative handling of invalid normal-cone metadata, CPU batch culling, and profiler sample aggregation. It is opt-in so normal engine builds do not gain another executable.

Profiler JSON semantics:
- `samples.*.avgMs` is the arithmetic mean over all recorded samples.
- `samples.*.rollingAvgMs` is the exponential moving average.
- `samples.*.sampleCount`, `minMs`, and `maxMs` expose the sample population and range.
- `frameTimeAvgMs` is the arithmetic mean over completed frames; `frameTimeP95Ms` is the 95th percentile of the latest 180 frame-time samples.
