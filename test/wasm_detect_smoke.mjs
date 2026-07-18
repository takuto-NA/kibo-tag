/**
 * Responsibility: load the built AprilTag WASM module in Node and smoke-test detect().
 */

import assert from 'node:assert/strict';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const repositoryRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const wasmLoaderPath = path.join(repositoryRoot, 'html', 'apriltag_wasm.js');

const AprilTagWasm = require(wasmLoaderPath);
const Module = await AprilTagWasm();

const atagjs_init = Module.cwrap('atagjs_init', 'number', []);
const atagjs_destroy = Module.cwrap('atagjs_destroy', 'number', []);
const atagjs_set_img_buffer = Module.cwrap('atagjs_set_img_buffer', 'number', ['number', 'number', 'number']);
const atagjs_detect = Module.cwrap('atagjs_detect', 'number', []);
const atagjs_set_default_tag_size = Module.cwrap('atagjs_set_default_tag_size', 'number', ['number']);

assert.equal(atagjs_init(), 0);
assert.equal(atagjs_set_default_tag_size(0.15), 0);

const imageWidth = 64;
const imageHeight = 64;
const imageBufferPointer = atagjs_set_img_buffer(imageWidth, imageHeight, imageWidth);
assert.ok(imageBufferPointer);

const whiteFrame = new Uint8Array(imageWidth * imageHeight);
whiteFrame.fill(255);
Module.HEAPU8.set(whiteFrame, imageBufferPointer);

const detectionJsonPointer = atagjs_detect();
const jsonLength = Module.getValue(detectionJsonPointer, 'i32');
const jsonStringPointer = Module.getValue(detectionJsonPointer + 4, 'i32');
assert.ok(jsonLength > 0);
assert.ok(jsonStringPointer !== 0);

const detectionJson = Module.UTF8ToString(jsonStringPointer, jsonLength);
const detections = JSON.parse(detectionJson);
assert.ok(Array.isArray(detections));
assert.equal(detections.length, 0);

assert.equal(atagjs_destroy(), 0);
console.log('wasm_detect_smoke.mjs: PASS');
