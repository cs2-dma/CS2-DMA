"use strict";

// Test the actual viewer functions without starting the game, DMA or a server.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname,
  "../../src/Features/WebRadar/Assets/viewer.js"), "utf8");
let now = 1000;
const marker = { state: {} };
const state = {};
const context = vm.createContext({
  S: state,
  performance: { now: () => now },
  ensureBombMarker: () => marker,
  getBombMarkerSize: () => 10
});
function loadFunction(name) {
  const start = source.indexOf(`function ${name}(`);
  assert.ok(start >= 0, `Missing viewer function: ${name}`);
  // Top-level functions in viewer.js close at column zero.
  const end = source.indexOf("\n}", start);
  assert.ok(end > start, `Missing closing brace: ${name}`);
  vm.runInContext(source.slice(start, end + 2), context, { filename: `viewer.js:${name}` });
}
for (const name of ["clamp", "compactVec3", "hasFiniteVec3", "cloneVec3",
  "lerp", "deriveVelocity", "addVelocity", "sampleBombState", "getRadarPosition",
  "updateBombMarker"]) loadFunction(name);

for (const invalid of [null, undefined, [], [1, 2], [null, 2, 3], [NaN, 2, 3], [Infinity, 2, 3]]) {
  assert.equal(context.compactVec3(invalid), null);
}
assert.equal(context.hasFiniteVec3({ x: null, y: 1, z: 2 }), false);
assert.equal(context.hasFiniteVec3(context.compactVec3([0, 0, 0])), true);

// Decode the exact compact wire shape emitted when bomb state is known but
// its coordinate is not. Use the viewer's actual flag definitions/normalizer.
const flagsStart = source.indexOf("const BOMB_FLAGS_V2 =");
const flagsEnd = source.indexOf("\n};", flagsStart);
assert.ok(flagsStart >= 0 && flagsEnd > flagsStart);
vm.runInContext(source.slice(flagsStart, flagsEnd + 3), context);
context.normalizeLanguageCode = value => value;
loadFunction("normalizePayloadV2");
const wireBomb = context.normalizePayloadV2({ v: 2, seq: 1, ts: 1000,
  map: "test_map", p: [], b: [5, null, 30, 40, 0, 10], w: [] }).m_bomb;
assert.equal(wireBomb.m_is_planted, true);
assert.equal(wireBomb.m_is_ticking, true);
assert.equal(wireBomb.m_position, null);
assert.equal(wireBomb.m_blow_time, 30);

const planted = { m_is_planted: true, m_is_ticking: true, m_blow_time: 30,
  m_position: { x: 100, y: -100, z: 300 } };
const missing = { ...planted, m_position: null };
const sampled = context.sampleBombState(planted, missing, 1, 8, 0);
assert.equal(sampled.m_position, null, "Missing position must not become old coordinates/origin");
assert.equal(sampled.m_is_planted, true);
assert.equal(sampled.m_blow_time, 30, "A missing coordinate must not hide a valid timer");
assert.equal(context.sampleBombState(missing, missing, 1, 8, 0).m_position, null);

const dropped = { m_is_dropped: true, m_position: { x: 300, y: -100, z: 0 } };
const transition = context.sampleBombState(dropped, planted, 0.25, 8, 0);
assert.equal(transition.m_is_dropped, true);
assert.equal(transition.m_position.x, 300, "Do not combine old mode with new position");

const map = { x: 0, y: 0, scale: 1 };
function update(bomb, mapName = "test_map") {
  context.updateBombMarker(bomb, map, 1024, 1024, mapName);
}
update(planted);
assert.equal(marker.state.active, true);
const positionStamp = state.lastKnownBombPositionAt;
now += 300;
update(missing);
assert.equal(marker.state.active, true, "Brief coordinate gap retains planted marker");
assert.equal(state.lastKnownBombPositionAt, positionStamp, "Fallback must not renew its own TTL");
now += 351;
update(missing);
assert.equal(marker.state.active, false, "Repeated missing samples eventually expire");
now += 1;
update(planted);
update(missing, "different_map");
assert.equal(marker.state.active, false, "Never reuse a position from a different map");
update({ m_is_planted: false, m_is_dropped: false, m_position: null });
assert.equal(state.lastKnownBombPosition, null, "Terminal/inactive state clears marker cache");
update({ ...dropped, m_position: null });
assert.equal(marker.state.active, false, "No planted-to-dropped cache leakage");

let ingested = 0;
let resets = 0;
Object.assign(context, {
  sanitizePayload: p => p,
  requestLanguage: () => {},
  updateConnection: () => {},
  scheduleUiRender: () => {},
  updateMapAssets: () => {},
  ingestSnapshot: () => { ++ingested; },
  resetSnapshotTimeline: () => { ++resets; state.renderPayload = null; }
});
state.mapDataCache = new Map([["test_map", {}]]);
state.payload = { m_map: "test_map", m_players: [{ m_team: 2 }, { m_team: 3 }] };
state.lastEntityPayloadAt = now;
loadFunction("payloadHasLiveEntities");
loadFunction("flushPendingPayload");
const collapsed = { m_map: "test_map", m_players: [{ m_team: 2 }], m_bomb: planted };
state.pendingPayload = collapsed;
context.flushPendingPayload();
assert.equal(ingested, 1, "Team collapse must not suppress the next bomb/frame update");
assert.equal(state.payload, collapsed);
const empty = { m_map: "test_map", m_players: [], m_bomb: null };
state.pendingPayload = empty;
context.flushPendingPayload();
assert.equal(state.payload, empty, "An authoritative empty frame must not be rejected");
assert.equal(resets, 1);

console.log("WebRadar continuity tests passed (viewer functions: coordinates, timers, TTL, mode/map transitions, team/empty frame delivery).");
