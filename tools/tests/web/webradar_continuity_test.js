"use strict";

// Test the actual viewer functions without starting the game, DMA or a server.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname,
  "../../../src/Features/WebRadar/Assets/viewer.js"), "utf8");
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
  ingestSnapshot: () => { ++ingested; return true; },
  getSnapshotSampleTime: p => p.m_ts || 0,
  resetSnapshotTimeline: () => { ++resets; state.renderPayload = null; }
});
state.mapDataCache = new Map([["test_map", {}]]);
state.payload = {m_seq:1,m_ts:1000,m_session:"A", m_map: "test_map", m_players: [{ m_team: 2 }, { m_team: 3 }] };
state.lastEntityPayloadAt = now;
loadFunction("payloadHasLiveEntities");
loadFunction("isSnapshotNewer");
loadFunction("flushPendingPayload");
const collapsed = {m_seq:2,m_ts:1001,m_session:"A", m_map: "test_map", m_players: [{ m_team: 2 }], m_bomb: planted };
state.pendingPayload = collapsed;
context.flushPendingPayload();
assert.equal(ingested, 1, "Team collapse must not suppress the next bomb/frame update");
assert.equal(state.payload, collapsed);
state.pendingPayload = {...collapsed,m_seq:1,m_ts:1000};
context.flushPendingPayload();
assert.equal(state.payload,collapsed,"Rejected frame must not update visible payload");
assert.equal(ingested,1);
assert.equal(context.isSnapshotNewer({...collapsed,m_seq:1,m_session:"B"},collapsed),true);
assert.equal(context.isSnapshotNewer({...collapsed,m_seq:3,m_ts:900},collapsed),false);
const empty = {m_seq:3,m_ts:1002,m_session:"A", m_map: "test_map", m_players: [], m_bomb: null };
state.pendingPayload = empty;
context.flushPendingPayload();
assert.equal(state.payload, empty, "An authoritative empty frame must not be rejected");
assert.equal(resets, 1);
context.schedulePayloadRender = () => {};
loadFunction("queuePayload");
const newestPending = {v:2,seq:100,ts:2000,sid:"C"};
context.queuePayload(newestPending);
context.queuePayload({v:2,seq:99,ts:1999,sid:"C"});
assert.equal(state.pendingPayload,newestPending,"Coalescing replaced newest frame with old polling response");
context.queuePayload({v:2,seq:1,ts:2001,sid:"D"});
assert.equal(state.pendingPayload.sid,"D");

Object.assign(context, {
  tr: text => text,
  getRosterAccent: () => "#84c8ed",
  resolveModelName: () => "ctm_sas"
});
for (const [name, ending] of [["KNOWN_ICONS", "\n]);"], ["GRENADE_ICONS", "\n]);"],
  ["ICON_ALIASES", "\n});"], ["WEAPON_ID_TO_ICON", "\n};"], ["WEAPON_LABELS", "\n};"]]) {
  const start = source.indexOf(`const ${name} =`);
  assert.ok(start >= 0, `Missing icon catalog: ${name}`);
  const end = source.indexOf(ending, start) + ending.length;
  vm.runInContext(source.slice(start, end), context);
}
for (const name of ["normalizeDisplayText", "resolveIconName", "formatWeaponName",
  "weaponNameFromId", "weaponUsesAmmo", "setMaskedIcon", "updatePlayerCard", "buildRosterSignature"])
  loadFunction(name);
const icon = {style: {}};
context.setMaskedIcon(icon, "incgrenade");
assert.match(icon.style.mask, /incgrenade\.png.*center \/ contain/);
const manifest = JSON.parse(fs.readFileSync(path.join(__dirname,
  "../../../src/assets/weapon_icons/manifest.json"), "utf8"));
for (const entry of manifest.icons) {
  assert.equal(context.weaponNameFromId(entry.item_id), entry.key, `Wrong item mapping: ${entry.key}`);
  assert.equal(context.resolveIconName(entry.key, ""), entry.key);
  assert.ok(context.formatWeaponName(entry.key));
}
for (const [input, output] of [["weapon_ak47", "ak47"], ["gauge", "sawedoff"],
  ["Smoke", "smokegrenade"], ["weapon_incgrenade", "incgrenade"], ["HE", "hegrenade"],
  ["healthshot", "health"], ["Flash", "flashbang"], ["incendiary", "incgrenade"]]) {
  assert.equal(context.resolveIconName(input, "knife"), output);
  assert.equal(context.formatWeaponName(input), context.formatWeaponName(output));
}
assert.equal(context.resolveIconName("../fake", "knife"), "knife");
assert.equal(context.formatWeaponName("molotov"), "Molotov");
assert.equal(context.formatWeaponName("incgrenade"), "Incendiary");
for (const entry of manifest.icons) {
  const ammoExpected = ![41, 42, 43, 44, 45, 46, 47, 48, 49, 57, 59].includes(entry.item_id) && entry.item_id < 500;
  assert.equal(context.weaponUsesAmmo(entry.item_id, entry.key), ammoExpected, entry.key);
}
assert.equal(context.weaponUsesAmmo(Infinity, "ak47"), false);
assert.equal(context.weaponUsesAmmo(999, "ak47"), false);
assert.notEqual(context.buildRosterSignature([{m_weapon: "ak47", m_weapon_id: 7}]),
  context.buildRosterSignature([{m_weapon: "ak47", m_weapon_id: 0}]));
const makeElement = () => ({style: {setProperty() {}}, classList: {toggle() {}}});
context.document = {
  createElement: () => makeElement(),
  createDocumentFragment: () => ({appendChild() {}})
};
const card = {
  root: makeElement(), name: {}, money: {}, healthValue: {}, armorValue: {}, model: {},
  healthIcon: makeElement(), armorIcon: makeElement(), weaponIcon: makeElement(), weaponName: {},
  modelKey: "", weaponKey: "", utilKey: "0:0:0::en", util: {replaceChildren() {}}
};
for (const name of ["ExtremelyLongUnbrokenNickname0123456789", "ОченьДлинныйНикИмяИгрока😎🎮", "玩家名稱測試測試測試測試測試測試"]) {
  context.updatePlayerCard(card, {m_name: name, m_money: 16000, m_health: 100,
    m_armor: 100, m_weapon: "incgrenade", m_weapon_id: 48}, 3);
  assert.equal(card.name.textContent, name);
  assert.equal(card.name.title, name);
  assert.equal(card.money.textContent, "$16000");
  assert.match(card.weaponIcon.style.mask, /incgrenade\.png.*center \/ contain/);
}
context.updatePlayerCard(card, {m_name: "Test", m_weapon_id: 7, m_ammo_clip: 30}, 3);
assert.equal(card.weaponName.textContent, "AK-47 (30)");
assert.equal(card.weaponIcon.hidden, false);
context.updatePlayerCard(card, {m_name: "Test", m_weapon: "invalid", m_weapon_id: 0}, 3);
assert.equal(card.weaponName.textContent, "");
assert.equal(card.weaponIcon.hidden, true);
assert.equal(card.name.textContent, "Test");
context.updatePlayerCard(card, {m_name: "Test", m_is_dead: true, m_weapon_id: 0}, 3);
assert.equal(card.weaponName.textContent, "");
assert.equal(card.weaponIcon.hidden, true);
const svg = fs.readFileSync(path.join(__dirname,
  "../../../src/Features/WebRadar/Assets/assets/icons/incgrenade.svg"), "utf8");
assert.match(svg, /transform="translate\(8\.85 0\)"/);
assert.doesNotMatch(svg, /scale\(2\.133/);
const css = fs.readFileSync(path.join(__dirname,
  "../../../src/Features/WebRadar/Assets/viewer.css"), "utf8");
assert.match(css, /\.player-card__top\s*\{[^}]*width:\s*100%/);
assert.match(css, /\.player-card__name\s*\{[^}]*flex:\s*1 1 auto/);
assert.match(css, /\.player-card__money\s*\{[^}]*flex:\s*0 0 auto/);

console.log("WebRadar continuity/card tests passed: coordinates, TTL, map/team transitions, full Unicode names, money, icon assets and aspect ratio.");
