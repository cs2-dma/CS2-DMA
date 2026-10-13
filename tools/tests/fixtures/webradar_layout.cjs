"use strict";

const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, "../../..");
const assets = path.join(root, "src/Features/WebRadar/Assets");
const viewer = fs.readFileSync(path.join(assets, "viewer.js"), "utf8");
const names = ["normalizeDisplayText", "setMaskedIcon", "weaponUsesAmmo", "resolveIconName",
  "createPlayerCard", "updatePlayerCard", "formatWeaponName", "weaponNameFromId"];
const renderer = names.map(name => {
  const start = viewer.indexOf(`function ${name}(`);
  const end = viewer.indexOf("\n}", start);
  if (start < 0 || end < start) throw new Error(`Missing function ${name}`);
  return viewer.slice(start, end + 2);
}).join("\n");
const labels = [["KNOWN_ICONS", "\n]);"], ["GRENADE_ICONS", "\n]);"],
  ["ICON_ALIASES", "\n});"], ["WEAPON_ID_TO_ICON", "\n};"], ["WEAPON_LABELS", "\n};"]].map(([name, ending]) => {
  const start = viewer.indexOf(`const ${name} =`);
  const end = viewer.indexOf(ending, start);
  if (start < 0 || end < start) throw new Error(`Missing catalog ${name}`);
  return viewer.slice(start, end + ending.length);
}).join("\n");
const manifest = JSON.parse(fs.readFileSync(path.join(root, "src/assets/weapon_icons/manifest.json"), "utf8"));
const html = `<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>WebRadar card regression</title><link rel="stylesheet" href="/viewer.css">
<style>html,body{height:auto;overflow:auto}body{padding:24px}body::before,body::after{display:none}
main{display:flex;flex-wrap:wrap;gap:20px}section{display:grid;gap:10px;align-content:start}
h1{font-size:20px}h2{font-size:16px}#result{white-space:pre-wrap}#icon{width:48px;height:48px}
#catalog{display:grid;grid-template-columns:repeat(auto-fill,minmax(160px,1fr));gap:12px;margin-top:16px}
.sample{display:flex;align-items:center;gap:10px;min-height:30px;padding:8px;background:#091723;border-radius:8px}
.sample label{font-size:12px}</style>
</head><body><h1>WebRadar card regression</h1><pre id="result">Running</pre><main id="cards"></main>
<h2>All equipment: consistent silhouettes</h2><div id="catalog"></div>
<h2>CT incendiary: original proportions</h2><div id="icon"></div><script>
const tr = text => text;
const S = {language: 'en'};
const getRosterAccent = p => p.m_team === 3 ? '#84c8ed' : '#df7d29';
const resolveModelName = p => p.m_team === 3 ? 'ctm_sas' : 'tm_phoenix';
${labels}
${renderer}
const manifest = ${JSON.stringify(manifest.icons)};
(async () => {
  const checks = [];
  const cards = [];
  const grenadeCards = [];
  const examples = ['VeryLongNicknameWithoutAnySpaces123456789', 'ОченьДлинныйНикИгрокаБезПробелов',
    '全世界最長的玩家名字測試測試測試測試', '😎🎮Игрок😎🎮LongName', 'Short'];
  for (const width of [260, 320, 400]) for (const right of [false, true]) {
    const section = document.createElement('section');
    section.style.width = width + 'px';
    const title = document.createElement('h2');
    title.textContent = (right ? 'CT' : 'T') + ' / ' + width + 'px';
    section.append(title);
    for (const name of examples) {
      const card = createPlayerCard(right);
      updatePlayerCard(card, {m_name: name, m_money: 16000, m_health: 100, m_armor: 100,
        m_team: right ? 3 : 2, m_weapon: right ? 'incgrenade' : 'molotov',
        m_weapon_id: right ? 48 : 46, m_grenades: ['incgrenade', 'molotov', 'flashbang', 'smokegrenade'],
        m_has_defuser: right}, 3);
      section.append(card.root);
      cards.push({card, width, right, name});
    }
    document.querySelector('#cards').append(section);
  }
  await document.fonts.ready;
  for (const {card, width, right, name} of cards) {
    for (const dead of [false, true, false]) {
      updatePlayerCard(card, {m_name: name, m_team: right ? 3 : 2, m_health: dead ? 0 : 100,
        m_money: 16000, m_armor: dead ? 0 : 100, m_is_dead: dead, m_weapon_id: 0}, 3);
      checks.push({ok: card.weaponName.textContent === '' && card.weaponIcon.hidden &&
        card.name.textContent === name, test: 'unknown weapon/death/' + dead + '/' + width});
    }
    updatePlayerCard(card, {m_name: name, m_team: right ? 3 : 2, m_health: 100, m_armor: 100,
      m_money: 16000, m_weapon_id: 7, m_ammo_clip: 30,
      m_grenades: ['incgrenade', 'molotov', 'flashbang', 'smokegrenade']}, 3);
    checks.push({ok: card.weaponName.textContent === 'AK-47 (30)' && !card.weaponIcon.hidden,
      test: 'weapon recovered/' + width});
  }
  for (const {card, width, right, name} of cards) {
    const top = card.root.querySelector('.player-card__top').getBoundingClientRect();
    const n = card.name.getBoundingClientRect();
    const money = card.money.getBoundingClientRect();
    const root = card.root.getBoundingClientRect();
    const edgeGap = right ? money.left - root.left : root.right - money.right;
    const ok = n.left >= top.left - 1 && n.right <= top.right + 1 &&
      money.left >= top.left - 1 && money.right <= top.right + 1 &&
      (right ? money.right <= n.left : n.right <= money.left) &&
      card.money.clientWidth >= card.money.scrollWidth && edgeGap >= 17;
    checks.push({ok, test: (right ? 'CT' : 'T') + '/' + width + '/' + name});
    for (const util of card.util.querySelectorAll('.player-card__util-icon')) {
      const bounds = util.getBoundingClientRect();
      checks.push({ok: bounds.width === 16 && bounds.height === 16,
        test: 'utility frame/' + util.title + '/' + width});
    }
  }
  for (const item of manifest) {
    const sample = document.createElement('div');
    sample.className = 'sample';
    const icon = document.createElement('span');
    const square = GRENADE_ICONS.has(item.key) || item.key === 'c4' || item.key === 'health';
    icon.className = 'player-card__weapon-icon' + (square ? ' player-card__weapon-icon--square' : '');
    setMaskedIcon(icon, item.key, '#c8ddef', true);
    const label = document.createElement('label');
    label.textContent = formatWeaponName(item.key);
    sample.append(icon, label);
    document.querySelector('#catalog').append(sample);
    const image = new Image();
    image.src = '/assets/icons/' + item.key + '.png';
    await image.decode();
    const bounds = icon.getBoundingClientRect();
    checks.push({ok: image.naturalWidth > 0 && bounds.height === 16 &&
      bounds.width === (square ? 16 : 34) && weaponNameFromId(item.item_id) === item.key,
      test: 'weapon frame/asset/id/' + item.key});
    if (GRENADE_ICONS.has(item.key)) {
      checks.push({ok: image.naturalHeight === 50 && image.naturalWidth <= 50,
        test: 'grenade proportions/' + item.key});
      const card = createPlayerCard(false);
      updatePlayerCard(card, {m_name: item.key, m_team: 2, m_weapon: item.key,
        m_weapon_id: item.item_id, m_grenades: [item.key]}, 2);
      sample.append(card.root);
      grenadeCards.push(card.root);
      const util = card.util.querySelector('.player-card__util-icon');
      checks.push({ok: card.weaponIcon.style.mask === util.style.mask &&
        card.weaponIcon.getBoundingClientRect().height === util.getBoundingClientRect().height &&
        card.weaponIcon.title === util.title && card.weaponName.textContent === util.title,
        test: 'held/inventory asset/size/label/' + item.key});
    }
  }
  for (const card of grenadeCards) card.remove();
  document.querySelector('#icon').innerHTML = await (await fetch('/assets/icons/incgrenade.svg')).text();
  const group = document.querySelector('#icon svg g');
  const matrix = group.transform.baseVal.consolidate().matrix;
  const bounds = group.getBBox();
  checks.push({ok: matrix.a === matrix.d && bounds.width / bounds.height < 0.5,
    test: 'incendiary aspect ratio'});
  const failures = checks.filter(check => !check.ok);
  document.querySelector('#result').textContent = failures.length
    ? 'FAIL: ' + JSON.stringify(failures, null, 2)
    : 'PASS: ' + checks.length + ' layout / icon checks';
  fetch('/result', {method: 'POST', body: JSON.stringify({checks: checks.length, failures})});
})().catch(error => { document.querySelector('#result').textContent = 'ERROR: ' + error; });
</script></body></html>`;

const types = {".css": "text/css", ".svg": "image/svg+xml", ".png": "image/png",
  ".webp": "image/webp", ".woff2": "font/woff2"};
const server = http.createServer((request, response) => {
  const url = new URL(request.url, "http://127.0.0.1");
  if (url.pathname === "/result" && request.method === "POST") {
    let body = "";
    request.on("data", data => { body += data; if (body.length > 8192) request.destroy(); });
    request.on("end", () => { console.log(body); response.end("OK"); });
    return;
  }
  if (url.pathname === "/") {
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    response.end(html);
    return;
  }
  const file = /^\/assets\/icons\/[a-z0-9_]+\.png$/.test(url.pathname)
    ? path.join(root, "src/assets/weapon_icons", path.basename(url.pathname) === 'health.png' ? 'healthshot.png' : path.basename(url.pathname))
    : path.resolve(assets, "." + decodeURIComponent(url.pathname));
  if (!file.startsWith(assets + path.sep) && !file.startsWith(path.join(root, "src/assets/weapon_icons") + path.sep)) {
    response.writeHead(403).end();
    return;
  }
  if (!fs.existsSync(file) || !fs.statSync(file).isFile()) {
    response.writeHead(404).end();
    return;
  }
  response.setHeader("Content-Type", types[path.extname(file)] || "application/octet-stream");
  response.setHeader("Cache-Control", "no-store");
  fs.createReadStream(file).pipe(response);
});
server.listen(8768, "127.0.0.1", () => console.log("WebRadar fixture: http://127.0.0.1:8768/"));
