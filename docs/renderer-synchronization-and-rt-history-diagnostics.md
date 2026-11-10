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
- GPU counters for temporal rejection reasons (history disabled, reprojection out of bounds, sky, depth mismatch, normal mismatch, accepted history, checkerboard reuse, and spatial fallback).
- GPU histogram/readback for GI and reflection history age.

## Requirements before enabling multiple frames in flight

The current renderer uses shared images and buffers across the complete frame graph. A correct multi-frame implementation must make every resource that can be written by one frame while sampled/read by another either per-frame or explicitly synchronized. This includes the RT ping-pong images and their descriptor sets; merely duplicating the primary command buffer and fence is insufficient.

The RT rejection counters should be a dedicated storage buffer (not packed into GI geometry alpha or the public reflection output). Reset and write operations need explicit transfer/compute barriers. Readback must use a fence-safe staging ring and consume only completed slots; it must not introduce `vkQueueWaitIdle` or synchronous GPU readback in the normal render path.

## Validation

The headless fence change requires a Vulkan build and runtime validation in both headless and windowed configurations. The reflection-history alpha change requires shader compilation and a scene test with reflections enabled, including checkerboard on/off and camera/light history invalidation. No GPU runtime result is claimed by this document.
