# Performance Decision Record and Measurement Contract

## Decision boundary (Phase 0 — approved by remediation plan)

| Decision | Choice |
|---|---|
| Supported browsers | Modern Chromium (Chrome / Edge) |
| Public JS API / detection JSON / families / pose / overlay / save-restore | Preserve |
| Distribution | Static files; no required bundler; no unpinned CDN runtime deps for production detection path |
| Playwright | Dev dependency only, locked via package-lock |
| Performance acceptance | Relative comparison on same host / Chromium / fixture — not fixed absolute FPS |
| Implementation techniques | Undecided until Phase 4 evidence (TrackProcessor, VideoFrame, transferable, direct video, Comlink removal, C API changes, `-O3`, WASM threads are candidates only) |
| Stop for ADR | Public API changes, distribution model changes, COOP/COEP / cross-origin isolation |

## Classification

### In Scope

- Diagnostic harness and metric schema
- Characterization / browser E2E / soak / blur non-regression
- Evidence-driven minimal FPS remediation
- Compatibility-preserving refactors

### Conditional (require Phase 4 evidence)

- MediaStreamTrackProcessor / VideoFrame / transferable
- Direct `<video>` preview
- Comlink removal
- C/WASM buffer API changes
- `-O3` or WASM threads

### ADR Required

- Public API contract changes
- Bundler / packaging requirements
- Cross-origin isolation

## Baseline identity

| Field | Value |
|---|---|
| Clean baseline SHA | `4715c7cb82c9e7cd0fe12f3f1ec234cc8d87d416` |
| Apriltag submodule SHA | `0e16a12dd380fd607e4afd54712ee9b1ffb9ec8f` |
| Quarantined unproven FPS WIP | `artifacts/fps-wip-quarantine/` (gitignored; local only) |
| Known structural fact | Committed baseline schedules `requestAnimationFrame` only after `await detect` completes (present since `801a2c5`) |

## Metric schema v1 (names)

Do **not** use the label `paint FPS`.

| Metric | Meaning |
|---|---|
| `canvasPresentedFps` | User-visible canvas cadence |
| `cameraPresentedFps` | Schema v1 alias of `canvasPresentedFps` (not rVFC) |
| `sourceVideoPresentedFps` | New video frames via `requestVideoFrameCallback` / media time |
| `detectThroughputFps` | Completed detections per second |
| `p50LatencyMs` / `p95LatencyMs` | Detect-wall latency percentiles after warm-up (source→result plumbing deferred) |
| `framesSubmitted` / `framesCompleted` / `framesDropped` / `framesCoalesced` / `detectInFlightSkips` | Pipeline accounting |
| `mainThreadLongTaskMilliseconds` | Aggregated long tasks during sample |
| Stage timers | readback, grayscale, dispatch, detect wall, overlay |

## Primary symptom (locked — contract v1)

- **Name:** `detectGatedPresentationUnderLoad`
- **Fixture:** `static-tag36h11-0` @ 1280×720 / 30 fps fake-device Y4M
- **Load profile:** Chromium `Emulation.setCPUThrottlingRate` rate=`8` (calibrated so detect becomes rate-limiting on the remediation host)
- **Definition:** After warm-up, user-visible `canvasPresentedFps` (alias `cameraPresentedFps` in schema v1) stays coupled to `detectThroughputFps` and both are detect/main-thread limited:
  - `presentationDetectRatio = canvasPresentedFps / detectThroughputFps` ∈ `[0.85, 1.15]`
  - `canvasPresentedFps < 25`
  - `detectThroughputFps < 25`
- **Observed side effect:** under the same load, `sourceVideoPresentedFps` also drops because the gated loop occupies the main thread (draw/readback/grayscale dominate stage totals).
- **Mechanism (structural):** committed baseline schedules the next `requestAnimationFrame` only after `await detect` completes.
- **Artifact:** `artifacts/symptom-reproduction/contract-v1-runs.json`

## Reproduction contract (Phase 1)

- Input: Chromium fake-device Y4M at 30 fps, 1280×720
- Warm-up: 10 s; sample: 60 s for P0 comparisons; 5-run symptom check uses shorter fixed windows documented in the artifact manifest
- No background load unless the CPU profile says otherwise
- Pass for reproduction RED: baseline hits the single primary symptom in **4/5** runs; replay within ±10 percentage points (rates) or ±15% (latency) of the artifact
- Stop: if not reproducible, do not change production code

## P0 vs candidate predicates

| Predicate | Applies to | Purpose |
|---|---|---|
| Source-normalized symptom predicate | Baseline / P0 | Must FAIL on P0 (problem still present) |
| Candidate-vs-P0 predicate | Candidates only | Non-regression / improvement vs P0 |

Noise band = paired-bootstrap 95% CI. Full sample protocol: 10 s warm-up + 60 s measurement, ≥7 paired blocks, ABBA/BAAB ordering, ±5% equivalence. Abbreviated local P0 overhead (`scripts/run-p0-overhead.mjs`) uses shorter windows and ±15% unless `KIBO_PERF_EQUIV_BOUND=0.05` is set.

## Root-cause log

Host experiment (`scripts/run-root-cause-ab.mjs`, CPU throttle rate=8, 5s warm / 15s sample):

| Experiment | canvasPresentedFps | Notes |
|---|---:|---|
| baseline-full-pipeline (await-gated) | ~20.0 | Primary symptom present |
| skip-detect-after-grayscale | ~30.9 | +10.9 FPS → await-detect coupling is a major limiter |
| skip-grayscale-and-detect | ~39.9 | +19.9 FPS → grayscale also material; draw/readback remains |

Stage totals on gated baseline under throttle: drawReadback > grayscale > detectWall.

**Dominant mechanism for the locked symptom:** `requestAnimationFrame` scheduled only after `await detect`, so canvas cadence collapses with detect/main-thread work. Largest single reversible lever: decouple presentation from detect and keep in-flight ≤ 1.

## Adopted fixes

1. **Decoupled latest-frame scheduling** in `html/video_process.js`
   - Schedule the next animation frame before detect completion
   - At most one detect in flight; additional frames take a lightweight present-only path (`drawImage` + overlay, no `getImageData`/grayscale)
   - Keep colored camera canvas; grayscale buffer is detector-only (no `putImageData` gray paint)
2. **Behavior-neutral metrics** in `html/pipeline_metrics.js` (P0 instrumentation)

### Dual acceptance artifacts

| Artifact | Meaning |
|---|---|
| `artifacts/symptom-reproduction/contract-v1-runs.json` | P0 gated mode: symptom present ≥4/5 |
| `artifacts/symptom-reproduction/contract-v1-candidate-runs.json` | Decoupled candidate: cleared majority ≥3/5 vs same-host gated reference |
| `test/browser/performance-acceptance.spec.mjs` | P0 fails candidate predicate; candidate passes skips + canvas > gated × 1.1 |

### Candidate clearance predicate (locked)

A candidate run **clears** when both hold against a same-host gated reference measured in the same campaign:

1. `detectInFlightSkips > 0` (structural proof of latest-frame present-only path)
2. `canvasPresentedFps > gatedReferenceCanvasPresentedFps × 1.1`

Campaign pass: cleared runs ≥3/5 **and** median canvas improvement ratio > 1.1.

Absolute `presentationDetectRatio > 1.15` alone is **not** the clearance gate: when detect keeps up under throttle, ratio can stay near 1.0 even though presentation is no longer await-gated.

### Candidate-vs-P0 result (CPU throttle rate=8)

| Metric | P0 (`schedulingMode=gated`) | Candidate (`decoupled`) |
|---|---:|---:|
| canvasPresentedFps | ~9–11 | ~12–14 (host-load dependent; still > gated × 1.1) |
| presentationDetectRatio | ~1.0 | often >1.15 when detect is slow; may stay ~1.0 if detect keeps up |
| detectInFlightSkips | 0 | > 0 |
| primary symptom | present ≥4/5 | cleared ≥3/5 (majority) via skips + canvas lift |

Scheduling mode for P0 re-capture: `window.__kiboPipelineSchedulingMode = 'gated'` (test-only). Production default remains `decoupled`.
