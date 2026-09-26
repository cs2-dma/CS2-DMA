"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const root = path.join(__dirname,"../../src/Features/WebRadar/Assets");
const manifest = JSON.parse(fs.readFileSync(path.join(root,"maps.json"),"utf8"));
const sources = JSON.parse(fs.readFileSync(path.join(root,"map-sources.json"),"utf8"));
const rc = fs.readFileSync(path.join(root,"webradar_resources.rc"),"utf8");
assert.doesNotMatch(rc,/^\s*\/\S+\s+RCDATA/m);
assert.match(sources.commit,/^[a-f0-9]{40}$/);
const resourceNames = new Set([...rc.matchAll(/^"([^"\r\n]+)"\s+RCDATA/gm)].map(match=>match[1]));
for (const name of resourceNames) assert.equal(name,name.toLowerCase(),name);
const css = fs.readFileSync(path.join(root,"viewer.css"),"utf8");
for (const match of css.matchAll(/url\("\.([^"\r\n]+)"\)/g)) assert.ok(resourceNames.has(match[1]),match[1]);
const names = new Set();
for (const map of manifest.maps) {
  assert.ok(!names.has(map.name)); names.add(map.name);
  assert.ok(Number.isFinite(map.scale) && map.scale>0);
  assert.ok(fs.existsSync(path.join(root,map.images.radar)));
  if (map.dynamic) continue;
  const dataPath = `/data/${map.name}/data.json`;
  const data = JSON.parse(fs.readFileSync(path.join(root,dataPath),"utf8"));
  assert.equal(data.x,map.origin.x); assert.equal(data.y,map.origin.y); assert.equal(data.scale,map.scale);
  assert.ok(rc.includes('"'+dataPath+'" RCDATA'), map.name);
  assert.ok(rc.includes('"'+map.images.radar+'" RCDATA'),map.name);
}
assert.equal(manifest.maps.filter(map=>map.parent==="rush_001").length,20);
const rush = manifest.maps.find(map=>map.name==="rush_001");
assert.equal(rush.origin.x,-11240); assert.equal(rush.origin.y,9944);
assert.equal(manifest.maps.find(map=>map.name==="aim_botz").dynamic,true);
assert.deepEqual(sources.missing_overviews,["ar_pool_day"]);
for (const name of sources.pool_maps) assert.ok(names.has(name) || sources.missing_overviews.includes(name),name);
for (const name of ["cs_shelter","de_boulder","de_debris","de_eldorado","de_fachwerk","de_poseidon"]) assert.ok(names.has(name),name);
for (const [name,x,y,scale] of [["de_mirage",-3230,1713,5],["de_vertigo",-3168,1762,4]]) {
  const map = manifest.maps.find(m=>m.name===name);
  assert.deepEqual([map.origin.x,map.origin.y,map.scale],[x,y,scale]);
}
let floors = 0;
for (const map of manifest.maps.filter(m=>m.section)) {
  assert.ok(Number.isFinite(map.altitude.min) && map.altitude.min<map.altitude.max);
  if (!map.parent) continue;
  ++floors;
  const parent = manifest.maps.find(m=>m.name===map.parent);
  assert.ok(parent && parent.section);
  assert.deepEqual(map.origin,parent.origin); assert.equal(map.scale,parent.scale);
  assert.ok(sources.maps[map.name]);
}
assert.equal(floors,5);
assert.ok(rc.includes('WEAPON_ICON_ATLAS_PNG RCDATA'));
assert.ok(!rc.includes('weapon_icon_atlas.rgba'));
for (const name of ['tm_phoenix','ctm_sas']) assert.ok(rc.includes(`"/assets/characters/${name}.webp" RCDATA`));
console.log(`Map resources validated: ${names.size} definitions, paired overviews/images, Rush root and 20 zones.`);
