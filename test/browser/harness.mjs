/**
 * Responsibility: shared helpers for real getUserMedia + Chromium fake-device Y4M demos.
 *
 * Playwright's default --remote-debugging-pipe launch is unreliable with
 * --use-file-for-fake-video-capture on Windows. We spawn Chromium with a CDP port
 * and attach via connectOverCDP so the OS fake-device path stays real.
 */

import { spawn } from 'node:child_process';
import { copyFileSync, existsSync, mkdirSync, readdirSync, rmSync } from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from '@playwright/test';

const REPOSITORY_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const DEFAULT_DEMO_PORT = 8765;
const DEFAULT_WARM_UP_MILLISECONDS = 10000;
const DETECTOR_READY_TIMEOUT_MILLISECONDS = 60000;
const CDP_READY_TIMEOUT_MILLISECONDS = 20000;
const CDP_POLL_INTERVAL_MILLISECONDS = 150;

function resolvePlaywrightChromiumExecutablePath() {
  const localAppData = process.env.LOCALAPPDATA;
  if (!localAppData) {
    throw new Error('LOCALAPPDATA is required to locate Playwright Chromium');
  }

  const playwrightRoot = path.join(localAppData, 'ms-playwright');
  if (!existsSync(playwrightRoot)) {
    throw new Error(`Playwright browser cache missing at ${playwrightRoot}`);
  }

  const chromiumDirectories = readdirSync(playwrightRoot)
    .filter((entryName) => entryName.startsWith('chromium-') && !entryName.includes('headless_shell'))
    .sort()
    .reverse();

  for (const directoryName of chromiumDirectories) {
    const candidates = [
      path.join(playwrightRoot, directoryName, 'chrome-win64', 'chrome.exe'),
      path.join(playwrightRoot, directoryName, 'chrome-win', 'chrome.exe'),
    ];
    for (const candidate of candidates) {
      if (existsSync(candidate)) {
        return candidate;
      }
    }
  }

  throw new Error('No full Playwright Chromium executable found; run npx playwright install chromium');
}

async function allocateEphemeralPort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.listen(0, '127.0.0.1', () => {
      const address = server.address();
      if (!address || typeof address === 'string') {
        reject(new Error('Failed to allocate ephemeral port'));
        server.close();
        return;
      }
      const port = address.port;
      server.close((closeError) => {
        if (closeError) {
          reject(closeError);
          return;
        }
        resolve(port);
      });
    });
    server.on('error', reject);
  });
}

async function waitForCdpReady(cdpPort, timeoutMilliseconds = CDP_READY_TIMEOUT_MILLISECONDS) {
  const deadline = Date.now() + timeoutMilliseconds;
  while (Date.now() < deadline) {
    try {
      const versionJson = await new Promise((resolve, reject) => {
        const request = http.get(`http://127.0.0.1:${cdpPort}/json/version`, (response) => {
          let body = '';
          response.setEncoding('utf8');
          response.on('data', (chunk) => {
            body += chunk;
          });
          response.on('end', () => resolve(body));
        });
        request.on('error', reject);
      });
      if (versionJson.includes('webSocketDebuggerUrl')) {
        return;
      }
    } catch {
      // Guard: CDP endpoint is not ready yet.
    }
    await new Promise((resolve) => setTimeout(resolve, CDP_POLL_INTERVAL_MILLISECONDS));
  }
  throw new Error(`CDP endpoint on port ${cdpPort} did not become ready`);
}

export function resolveFixtureY4mPath(relativeFixturePath) {
  return path.resolve(REPOSITORY_ROOT, relativeFixturePath);
}

export async function launchDemoWithFakeCamera({
  y4mAbsolutePath,
  baseURL = `http://127.0.0.1:${DEFAULT_DEMO_PORT}`,
  headless = true,
  schedulingMode = 'decoupled',
} = {}) {
  const cdpPort = await allocateEphemeralPort();
  // Prefer short ASCII paths; long TEMP profiles and long capture paths have crashed Chromium here.
  const sessionRootDirectory = path.join('C:\\temp', `kibo-chrome-${process.pid}-${cdpPort}`);
  const userDataDirectory = path.join(sessionRootDirectory, 'profile');
  const shortY4mPath = path.join(sessionRootDirectory, 'capture.y4m');
  mkdirSync(userDataDirectory, { recursive: true });
  copyFileSync(y4mAbsolutePath, shortY4mPath);

  const chromeExecutablePath = resolvePlaywrightChromiumExecutablePath();
  // Guard: on this Windows Chromium build, combining --use-fake-ui-for-media-stream with
  // --auto-accept-camera-and-microphone-capture crashes when a Y4M capture file is set.
  const chromeArguments = [
    `--remote-debugging-port=${cdpPort}`,
    `--user-data-dir=${userDataDirectory}`,
    '--use-fake-ui-for-media-stream',
    '--use-fake-device-for-media-stream',
    `--use-file-for-fake-video-capture=${shortY4mPath}`,
    '--disable-popup-blocking',
    '--no-first-run',
    '--no-default-browser-check',
  ];
  if (headless) {
    chromeArguments.push('--headless=new', '--disable-gpu');
  }

  let chromeExitCode = null;
  const chromeProcess = spawn(chromeExecutablePath, chromeArguments, {
    stdio: 'ignore',
    windowsHide: true,
  });
  chromeProcess.on('exit', (code) => {
    chromeExitCode = code;
  });

  const closeSession = async (browser) => {
    try {
      if (browser) {
        await browser.close();
      }
    } catch {
      // Guard: browser may already be disconnected.
    }
    if (!chromeProcess.killed) {
      chromeProcess.kill();
    }
    try {
      rmSync(sessionRootDirectory, { recursive: true, force: true });
    } catch {
      // Guard: Windows may keep the profile directory locked briefly.
    }
  };

  try {
    await waitForCdpReady(cdpPort);
    if (chromeExitCode !== null) {
      throw new Error(`Chromium exited early with code ${chromeExitCode}`);
    }
    const browser = await chromium.connectOverCDP(`http://127.0.0.1:${cdpPort}`);
    const context = browser.contexts()[0] || await browser.newContext();
    await context.grantPermissions(['camera'], { origin: baseURL });
    const page = context.pages()[0] || await context.newPage();

    page.on('console', (message) => {
      if (message.type() === 'error') {
        console.log(`[browser:${message.type()}] ${message.text()}`);
      }
    });

    await page.addInitScript((mode) => {
      window.__kiboPipelineSchedulingMode = mode;
      window.__kiboEnablePipelineMetrics = true;
    }, schedulingMode);

    await page.goto(baseURL, { waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => typeof window.apriltag !== 'undefined', null, {
      timeout: DETECTOR_READY_TIMEOUT_MILLISECONDS,
    });
    await page.waitForFunction(() => {
      const videoElement = document.getElementById('webcam_canvas');
      return videoElement && videoElement.videoWidth > 0 && videoElement.readyState >= 2;
    }, null, {
      timeout: DETECTOR_READY_TIMEOUT_MILLISECONDS,
    });

    return {
      browser,
      context,
      page,
      close: async () => closeSession(browser),
    };
  } catch (launchError) {
    await closeSession(null);
    const details = chromeExitCode === null
      ? launchError
      : new Error(`${launchError.message}; chromiumExitCode=${chromeExitCode}`);
    throw details;
  }
}

export async function waitForWarmUp(page, warmUpMilliseconds = DEFAULT_WARM_UP_MILLISECONDS) {
  await page.waitForTimeout(warmUpMilliseconds);
}

/**
 * Apply Chromium Emulation.setCPUThrottlingRate through the attached CDP session.
 * rate=1 means no throttle; rate=4 roughly quarters CPU for the page process.
 */
export async function applyCpuThrottling(page, rate) {
  const cdpSession = await page.context().newCDPSession(page);
  await cdpSession.send('Emulation.setCPUThrottlingRate', { rate });
  return cdpSession;
}

export async function readPipelineSummary(page, warmUpMilliseconds = DEFAULT_WARM_UP_MILLISECONDS) {
  return page.evaluate((warmUp) => {
    if (!window.__kiboPipelineMetrics) {
      return null;
    }
    return window.__kiboPipelineMetrics.summarize({ warmUpMilliseconds: warmUp });
  }, warmUpMilliseconds);
}

export { REPOSITORY_ROOT, DEFAULT_WARM_UP_MILLISECONDS };
