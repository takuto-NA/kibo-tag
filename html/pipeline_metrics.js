/**
 * Responsibility: collect stage timings for the demo pipeline without changing scheduling behavior.
 */

const METRIC_SCHEMA_VERSION = 1;
const MILLISECONDS_PER_SECOND = 1000;
const MAX_TIMESTAMP_SAMPLES = 4000;
const MAX_LATENCY_SAMPLES = 4000;

function createEmptyCounters() {
  return {
    sourceVideoFramesPresented: 0,
    canvasFramesPresented: 0,
    framesSubmitted: 0,
    framesCompleted: 0,
    framesDropped: 0,
    framesCoalesced: 0,
    detectInFlightSkips: 0,
    detectionsWithTags: 0,
    mainThreadLongTaskMilliseconds: 0,
  };
}

function createEmptyStageTotals() {
  return {
    drawReadbackMilliseconds: 0,
    grayscaleMilliseconds: 0,
    dispatchMilliseconds: 0,
    detectWallMilliseconds: 0,
    overlayMilliseconds: 0,
  };
}

function pushBounded(samples, value, maximumLength) {
  samples.push(value);
  if (samples.length > maximumLength) {
    samples.splice(0, samples.length - maximumLength);
  }
}

export function createPipelineMetricsCollector() {
  const counters = createEmptyCounters();
  const stageTotals = createEmptyStageTotals();
  const latencySamplesMilliseconds = [];
  const detectCompletionTimestampsMilliseconds = [];
  const canvasPresentedTimestampsMilliseconds = [];
  const sourceVideoPresentedTimestampsMilliseconds = [];
  let collectionStartedAtMilliseconds = null;
  let lastSourceVideoMediaTimeSeconds = null;
  // Default off so production cadence is not taxied by always-on sample buffers.
  // Tests/harnesses opt in via setMetricsRecordingEnabled(true) or ?diag=1.
  let metricsRecordingEnabled = false;

  function ensureStarted(nowMilliseconds) {
    if (collectionStartedAtMilliseconds === null) {
      collectionStartedAtMilliseconds = nowMilliseconds;
    }
  }

  function setMetricsRecordingEnabled(isEnabled) {
    metricsRecordingEnabled = isEnabled;
  }

  function markSourceVideoFramePresented(mediaTimeSeconds, nowMilliseconds = performance.now()) {
    // Guard: overhead A/B disables recording while keeping the collector installed.
    if (!metricsRecordingEnabled) {
      return;
    }
    ensureStarted(nowMilliseconds);
    // Guard: Chromium may redeliver the same mediaTime while a frame is held.
    if (lastSourceVideoMediaTimeSeconds !== null && mediaTimeSeconds <= lastSourceVideoMediaTimeSeconds) {
      counters.framesCoalesced += 1;
      return;
    }
    lastSourceVideoMediaTimeSeconds = mediaTimeSeconds;
    counters.sourceVideoFramesPresented += 1;
    pushBounded(sourceVideoPresentedTimestampsMilliseconds, nowMilliseconds, MAX_TIMESTAMP_SAMPLES);
  }

  function markCanvasFramePresented(nowMilliseconds = performance.now()) {
    if (!metricsRecordingEnabled) {
      return;
    }
    ensureStarted(nowMilliseconds);
    counters.canvasFramesPresented += 1;
    pushBounded(canvasPresentedTimestampsMilliseconds, nowMilliseconds, MAX_TIMESTAMP_SAMPLES);
  }

  function markFrameSubmitted(nowMilliseconds = performance.now()) {
    if (!metricsRecordingEnabled) {
      return;
    }
    ensureStarted(nowMilliseconds);
    counters.framesSubmitted += 1;
  }

  function markFrameDropped() {
    if (!metricsRecordingEnabled) {
      return;
    }
    counters.framesDropped += 1;
  }

  function markFrameCoalesced() {
    if (!metricsRecordingEnabled) {
      return;
    }
    counters.framesCoalesced += 1;
  }

  function markDetectInFlightSkip() {
    if (!metricsRecordingEnabled) {
      return;
    }
    counters.detectInFlightSkips += 1;
  }

  function markStageDuration(stageName, durationMilliseconds) {
    if (!metricsRecordingEnabled) {
      return;
    }
    if (!(stageName in stageTotals)) {
      return;
    }
    stageTotals[stageName] += durationMilliseconds;
  }

  function markDetectionCompleted({
    tagCount,
    detectWallMilliseconds,
    sourceToResultLatencyMilliseconds,
    nowMilliseconds = performance.now(),
  }) {
    if (!metricsRecordingEnabled) {
      return;
    }
    ensureStarted(nowMilliseconds);
    counters.framesCompleted += 1;
    if (tagCount > 0) {
      counters.detectionsWithTags += 1;
    }
    pushBounded(detectCompletionTimestampsMilliseconds, nowMilliseconds, MAX_TIMESTAMP_SAMPLES);
    const latencyMilliseconds = Number.isFinite(sourceToResultLatencyMilliseconds)
      ? sourceToResultLatencyMilliseconds
      : detectWallMilliseconds;
    if (Number.isFinite(latencyMilliseconds)) {
      // Schema note: until true source-media→result timestamps are plumbed, this stores detect wall time.
      pushBounded(latencySamplesMilliseconds, latencyMilliseconds, MAX_LATENCY_SAMPLES);
    }
  }

  function markLongTask(durationMilliseconds) {
    if (!metricsRecordingEnabled) {
      return;
    }
    counters.mainThreadLongTaskMilliseconds += durationMilliseconds;
  }

  function percentile(sortedValues, percentileFraction) {
    if (sortedValues.length === 0) {
      return null;
    }
    const rankIndex = Math.min(
      sortedValues.length - 1,
      Math.max(0, Math.ceil(percentileFraction * sortedValues.length) - 1));
    return sortedValues[rankIndex];
  }

  function rateFromTimestamps(timestampsMilliseconds, windowStartMilliseconds, windowEndMilliseconds) {
    const samplesInWindow = timestampsMilliseconds.filter((timestamp) => (
      timestamp >= windowStartMilliseconds && timestamp <= windowEndMilliseconds
    ));
    const windowSeconds = (windowEndMilliseconds - windowStartMilliseconds) / MILLISECONDS_PER_SECOND;
    if (windowSeconds <= 0) {
      return 0;
    }
    return samplesInWindow.length / windowSeconds;
  }

  function summarize({
    warmUpMilliseconds = 10000,
    nowMilliseconds = performance.now(),
  } = {}) {
    const startedAt = collectionStartedAtMilliseconds ?? nowMilliseconds;
    const measurementStartMilliseconds = startedAt + warmUpMilliseconds;
    const sortedLatencies = latencySamplesMilliseconds
      .filter((_, index) => {
        const completionTime = detectCompletionTimestampsMilliseconds[index];
        return completionTime >= measurementStartMilliseconds;
      })
      .slice()
      .sort((left, right) => left - right);

    const canvasPresentedFps = rateFromTimestamps(
      canvasPresentedTimestampsMilliseconds,
      measurementStartMilliseconds,
      nowMilliseconds);
    const detectThroughputFps = rateFromTimestamps(
      detectCompletionTimestampsMilliseconds,
      measurementStartMilliseconds,
      nowMilliseconds);
    const sourceVideoPresentedFps = rateFromTimestamps(
      sourceVideoPresentedTimestampsMilliseconds,
      measurementStartMilliseconds,
      nowMilliseconds);

    return {
      schemaVersion: METRIC_SCHEMA_VERSION,
      elapsedMilliseconds: nowMilliseconds - startedAt,
      measurementWindowMilliseconds: Math.max(0, nowMilliseconds - measurementStartMilliseconds),
      counters: { ...counters },
      stageTotalsMilliseconds: { ...stageTotals },
      // Schema v1: cameraPresentedFps aliases user-visible canvas cadence (not rVFC).
      cameraPresentedFps: canvasPresentedFps,
      canvasPresentedFps,
      sourceVideoPresentedFps,
      detectThroughputFps,
      p50LatencyMs: percentile(sortedLatencies, 0.50),
      p95LatencyMs: percentile(sortedLatencies, 0.95),
      presentationDetectRatio: detectThroughputFps <= 0
        ? null
        : canvasPresentedFps / detectThroughputFps,
    };
  }

  function reset() {
    Object.assign(counters, createEmptyCounters());
    Object.assign(stageTotals, createEmptyStageTotals());
    latencySamplesMilliseconds.length = 0;
    detectCompletionTimestampsMilliseconds.length = 0;
    canvasPresentedTimestampsMilliseconds.length = 0;
    sourceVideoPresentedTimestampsMilliseconds.length = 0;
    collectionStartedAtMilliseconds = null;
    lastSourceVideoMediaTimeSeconds = null;
  }

  return {
    markSourceVideoFramePresented,
    markSourceFramePresented: markSourceVideoFramePresented,
    markCanvasFramePresented,
    markFrameSubmitted,
    markFrameDropped,
    markFrameCoalesced,
    markDetectInFlightSkip,
    markStageDuration,
    markDetectionCompleted,
    markLongTask,
    setMetricsRecordingEnabled,
    summarize,
    reset,
  };
}

export function installPipelineMetricsOnWindow(windowObject = window) {
  const collector = createPipelineMetricsCollector();
  windowObject.__kiboPipelineMetrics = collector;
  windowObject.__kiboPresentationLoopCount = 0;

  const diagnosticQueryEnabled = (() => {
    try {
      return new URL(windowObject.location.href).searchParams.get('diag') === '1';
    } catch {
      return false;
    }
  })();
  if (diagnosticQueryEnabled || windowObject.__kiboEnablePipelineMetrics === true) {
    collector.setMetricsRecordingEnabled(true);
  }

  if (typeof PerformanceObserver === 'function') {
    try {
      const longTaskObserver = new PerformanceObserver((entryList) => {
        for (const entry of entryList.getEntries()) {
          collector.markLongTask(entry.duration);
        }
      });
      longTaskObserver.observe({ type: 'longtask', buffered: true });
    } catch (observerError) {
      // Guard: longtask observation is optional and unavailable in some Chromium builds.
      console.log('Long task observer unavailable', observerError);
    }
  }

  return collector;
}
