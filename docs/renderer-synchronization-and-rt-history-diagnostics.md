# Renderer synchronization and RT history diagnostics

## Implemented in this change

- Headless rendering now uses a signaled completion fence. The renderer waits for the previous headless submission at the start of the next render call and submits the current command buffer with that fence.
- The headless path no longer calls `vkQueueWaitIdle` after every frame. This moves the completion wait away from the render-call tail and lets the caller continue after queue submission.
- The renderer remains **single-frame-in-flight** in headless mode. The command buffer, offscreen render targets, descriptor sets, acceleration-structure scratch lifetime, and RT temporal images are shared. Reusing them concurrently would be unsafe.
- Reflection temporal history now stores its effective accumulated sample age in the alpha channel of the private `imgReflHistCur/Prev` images. The public `imgRefl` output retains alpha=1 as its PBR validity mask. Checkerboard reuse preserves age without pretending a new ray sample was traced; a newly traced pixel starts at the current sample count.
- Diffuse GI history age remains in `imgGeomCur/Prev.w`. It is independent from reflection history age.

## Not implemented yet

- Frames-in-flight ring (2–3 slots) for the windowed renderer.
- Per-frame copies of offscreen attachments, descriptor sets, command buffers, semaphores, scene upload buffers, timestamp query pools, and RT denoiser resources.

## Requirements before enabling multiple frames in flight

The current renderer uses shared images and buffers across the complete frame graph. A correct multi-frame implementation must make every resource that can be written by one frame while sampled/read by another either per-frame or explicitly synchronized. This includes the RT ping-pong images and their descriptor sets; merely duplicating the primary command buffer and fence is insufficient.

## Implemented RT diagnostics

A dedicated 48-counter storage buffer is bound at set 1 / binding 15. It is reset with `vkCmdFillBuffer`, followed by a transfer-to-compute barrier. GI and reflection temporal shaders update independent counters with atomics; the counters never reuse GI geometry alpha or the public reflection validity alpha.

The buffer is persistently mapped in host-visible memory. `RtDenoiser::record()` consumes the previous frame's values only after the renderer's frame fence has completed, invalidating non-coherent memory when required. A compute-to-host buffer barrier closes the producer side. This avoids a blocking queue-idle call and a separate synchronous GPU readback request. Each temporal 8×8 workgroup aggregates its diagnostic events in a 48-entry shared-memory counter array. All invocations, including out-of-range edge lanes, participate in the initialization and flush barriers; only non-zero workgroup totals are atomically added to the global SSBO. This removes per-pixel global SSBO atomics without changing the exported counter meanings. Counter names are exported as profiler metrics:

- GI: `rtGiPixels`, `rtGiHistoryAccepted`, `rtGiHistoryRejected`, `rtGiOutOfBounds`, `rtGiDepthRejected`, `rtGiNormalRejected`, `rtGiCheckerboardReuse`, `rtGiSpatialFallback`.
- Reflections: the matching `rtReflection*` metrics.
- History age: 16 normalized bins per effect (`rtGiHistoryAgeBin00..15` and `rtReflectionHistoryAgeBin00..15`). Bin 00 includes sky/no-sample pixels; remaining bins represent increasing fractions of configured maximum history age. Counts are per-pixel totals, not percentages.

The counters remain diagnostic instrumentation. Shared-memory atomics and two workgroup barriers still have a cost, but global updates are now bounded by the number of non-zero counters per workgroup rather than the number of pixels. Compare identical benchmark captures before and after this optimization. The current host-visible SSBO is a simple fence-safe first implementation, not an optimized device-local staging ring. A device-local counter buffer plus a multi-slot staging/readback ring remains a follow-up if profiling shows host-visible writes are costly.

## Reflection local-light sampling

When reflection stochastic-light sampling is enabled, each reflection hit now selects one local light uniformly and multiplies its contribution by the active-light count. This is an unbiased estimator of the sum of local-light contributions and is applied to the primary reflection hit as well as secondary bounces. Previously, the primary hit evaluated every active local light (up to eight) and could issue a separate shadow ray query for each one, while stochastic selection was restricted to later bounces. The change reduces primary-hit local-light shadow-ray work when multiple local lights are active. It trades per-sample variance for fewer shadow queries; the configured reflection sample count and temporal accumulation remain unchanged. Validate image stability and GPU timings in the same scene with stochastic lights enabled and disabled.

## Deliberately not enabled: multiple frames in flight

The renderer still has one shared command buffer, shared render targets, shared descriptor sets, shared upload/scratch resources and ping-pong RT histories. Therefore this branch does not pretend to enable 2–3 frames in flight. That requires a per-frame resource ring across the renderer and every pass, not merely more fences. Enabling it without that ownership split would introduce races and invalid image/descriptor reuse.

## Validation

The headless fence change requires a Vulkan build and runtime validation in both headless and windowed configurations. The reflection-history alpha change requires shader compilation and a scene test with reflections enabled, including checkerboard on/off and camera/light history invalidation. No GPU runtime result is claimed by this document.

## Performance Profiler: RT temporal diagnostics

The in-editor Performance Profiler now samples all 48 RT diagnostic metrics into the same rolling one-second window as CPU/GPU timings. It displays GI and reflection history acceptance, checkerboard reuse, depth/normal rejection, and two 16-bin history-age histograms. The **Copy 1-second average report** export includes every counter as an average count per rendered frame, the derived history acceptance percentages, and the exact values for all 32 histogram bins. The histogram values are not percentages; compare their distribution and totals against the corresponding pixel counters.

The profiler only reads the metrics already published by RtDenoiser; it does not add GPU work, alter RT settings, or change temporal accumulation. A zero value can mean the corresponding effect is disabled or that no diagnostic events were recorded in the capture window.
