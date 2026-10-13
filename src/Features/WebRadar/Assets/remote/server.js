const http = require("http");
const path = require("path");
const express = require("express");
const { WebSocketServer, WebSocket } = require("ws");

const PORT = Number(process.env.PORT || 8080);
const HOST = process.env.HOST || "0.0.0.0";
const PUBLIC_DIR = path.join(__dirname, "public");
const app = express();
app.disable("x-powered-by");
const server = http.createServer(app);
const viewers = new Set();
const sources = new Set();
const relaySession = Date.now().toString(36) + "-" + process.pid;
let generation = 0;
let activeSource = null;
let accepted = null;
let latest = emptySnapshot("waiting");
let latestText = JSON.stringify(latest);
let revision = 1;
let lastUpdateMs = 0;
let sourcePackets = 0;
let rejectedPackets = 0;
let broadcastPackets = 0;
const requestedHz = Number(process.env.BROADCAST_HZ || 60);
const BROADCAST_HZ = Number.isFinite(requestedHz) ? Math.max(1, Math.min(60, requestedHz)) : 60;
const BROADCAST_INTERVAL_MS = Math.max(16, Math.round(1000 / BROADCAST_HZ));
const MAX_VIEWER_BUFFER_BYTES = 64 * 1024;
const SOURCE_TTL_MS = 3000;
const MAX_BACKPRESSURE_MS = 10000;

function emptySnapshot(state) {
  return {v:2, seq:accepted ? accepted.seq : 0, ts:accepted ? accepted.ts + 1 : Date.now(),
    sid:accepted ? accepted.sid : relaySession, source_state:state,
    map:"unknown", lt:0, p:[], b:[0,null,0,40,0,10], w:[]};
}

function finite(value, max = 1000000000) {
  return typeof value === "number" && Number.isFinite(value) && Math.abs(value) <= max;
}

function vector(value, length, nullable = false) {
  return (nullable && value === null) ||
    (Array.isArray(value) && value.length === length && value.every(n => finite(n)));
}

function validSnapshot(value) {
  if (!value || Array.isArray(value) || value.v !== 2 ||
      !Number.isSafeInteger(value.seq) || value.seq < 0 ||
      !Number.isSafeInteger(value.ts) || value.ts <= 0 ||
      typeof value.map !== "string" || value.map.length < 1 || value.map.length > 128 ||
      /[\x00-\x1f]/.test(value.map) ||
      !Number.isInteger(value.lt) || value.lt < 0 || value.lt > 3 ||
      !Array.isArray(value.p) || value.p.length > 65 ||
      !Array.isArray(value.w) || value.w.length > 64 ||
      !Array.isArray(value.b) || value.b.length !== 6 ||
      (value.sid !== undefined && (typeof value.sid !== "string" || value.sid.length > 128))) return false;
  const ids = new Set();
  for (const row of value.p) {
    if (!Array.isArray(row) || row.length < 12 || row.length > 15 ||
        !Number.isInteger(row[0]) || row[0] < 0 || row[0] > 64 || ids.has(row[0]) ||
        typeof row[1] !== "string" || row[1].length > 512 ||
        !Number.isInteger(row[2]) || row[2] < 0 || row[2] > 3 ||
        !Number.isInteger(row[3]) || row[3] < 0 || row[3] > 127 ||
        !vector(row[5],3,true) || ![4,6,7,8,9,10,11].every(i => finite(row[i])) ||
        (row[12] !== undefined && (!Array.isArray(row[12]) || row[12].length > 16 ||
          !row[12].every(n => Number.isInteger(n) && n >= 0 && n <= 65535))) ||
        (row[13] !== undefined && !vector(row[13],2,true)) ||
        (row[14] !== undefined && (!Number.isSafeInteger(row[14]) || row[14] < 0))) return false;
    ids.add(row[0]);
  }
  if (!Number.isInteger(value.b[0]) || value.b[0] < 0 || value.b[0] > 31 ||
      !vector(value.b[1],3,true) || ![2,3,4,5].every(i => finite(value.b[i]))) return false;
  if (!value.w.every(row => Array.isArray(row) && row.length === 3 &&
      Number.isInteger(row[0]) && row[0] >= 1 && row[0] <= 4 &&
      vector(row[1],3) && finite(row[2]))) return false;
  if (value.ov !== undefined && (!value.ov || Array.isArray(value.ov) ||
      !finite(value.ov.x) || !finite(value.ov.y) ||
      !finite(value.ov.scale,100) || value.ov.scale <= 0.01)) return false;
  return value.lang === undefined || (typeof value.lang === "string" && value.lang.length <= 16);
}

function publish(value) {
  latest = value;
  latestText = JSON.stringify(value);
  revision += 1;
}

function expireSource(now) {
  if (lastUpdateMs && latest.source_state === "live" && now - lastUpdateMs > SOURCE_TTL_MS)
    publish(emptySnapshot("stale"));
}

app.use(express.static(PUBLIC_DIR, {etag:true, maxAge:"1h", index:false}));
app.get("/api/status", (req,res) => {
  expireSource(Date.now());
  res.set("Cache-Control","no-store");
  res.json({ok:true, active_map:latest.map, sent_packets:sourcePackets,
    rejected_packets:rejectedPackets, broadcast_packets:broadcastPackets,
    last_update_ms:lastUpdateMs, source_age_ms:lastUpdateMs ? Date.now()-lastUpdateMs : -1,
    source_state:latest.source_state, connected_clients:viewers.size});
});
app.get("/api/ping", (req,res) => {
  res.set("Cache-Control","no-store");
  res.json({ok:true});
});
app.get("/api/live", (req,res) => {
  expireSource(Date.now());
  res.set("Cache-Control","no-store");
  res.json(latest);
});
app.get("/api/stream", (req,res) => {
  expireSource(Date.now());
  res.writeHead(200, {"Content-Type":"text/event-stream","Cache-Control":"no-store",
    "Connection":"keep-alive","Access-Control-Allow-Origin":"*","X-Accel-Buffering":"no"});
  res.write(`event: snapshot\ndata: ${latestText}\n\n`);
  const client = {sse:true, revision, pressureAt:0,
    send(text) {
      if (res.destroyed || res.writableEnded || res.writableNeedDrain) return false;
      res.write(`event: snapshot\ndata: ${text}\n\n`);
      return true;
    },
    heartbeat() {
      if (!res.destroyed && !res.writableEnded && !res.writableNeedDrain)
        res.write(": heartbeat\n\n");
    },
    terminate() { res.destroy(); viewers.delete(client); }};
  viewers.add(client);
  req.on("close", () => viewers.delete(client));
  res.on("error", () => viewers.delete(client));
});
app.use("/api", (req,res) => {
  res.set("Cache-Control","no-store");
  res.status(404).json({ok:false,error:"not_found"});
});
app.get("/{*path}", (req,res,next) => {
  if (path.extname(req.path) || !req.accepts("html")) return next();
  res.set("Cache-Control","no-cache");
  res.sendFile(path.join(PUBLIC_DIR,"index.html"));
});
app.use((req,res) => res.status(404).json({ok:false,error:"not_found"}));
app.use((error,req,res,next) => {
  if (res.headersSent) return next(error);
  const status = Number.isInteger(error.status) && error.status >= 400 && error.status <= 599
    ? error.status : 500;
  res.set("Cache-Control","no-store");
  res.status(status).json({ok:false,error:status >= 500 ? "server_error" : "request_error"});
});

const sourceWss = new WebSocketServer({noServer:true, maxPayload:262144, perMessageDeflate:false});
const viewerWss = new WebSocketServer({noServer:true, maxPayload:1024, perMessageDeflate:false});

function broadcastLatest() {
  const now = Date.now();
  expireSource(now);
  let delivered = false;
  for (const client of viewers) {
    if (client.revision === revision) continue;
    try {
      const available = client.sse === true || client.readyState === WebSocket.OPEN;
      const blocked = client.sse !== true && client.bufferedAmount > MAX_VIEWER_BUFFER_BYTES;
      if (!available) continue;
      if (blocked || (client.sse === true && !client.send(latestText))) {
        if (!client.pressureAt) client.pressureAt = now;
        if (now-client.pressureAt > MAX_BACKPRESSURE_MS) client.terminate();
        continue;
      }
      if (client.sse !== true) client.send(latestText);
      client.revision = revision;
      client.pressureAt = 0;
      delivered = true;
    } catch { client.terminate(); }
  }
  if (delivered) broadcastPackets += 1;
}
setInterval(broadcastLatest,BROADCAST_INTERVAL_MS).unref();

function monitorSocket(ws, collection) {
  ws.alive = true;
  collection.add(ws);
  ws.on("pong", () => {ws.alive=true;});
  ws.on("close", () => collection.delete(ws));
  ws.on("error", () => {collection.delete(ws); ws.terminate();});
}

setInterval(() => {
  for (const ws of [...sources,...viewers]) {
    try {
      if (ws.sse === true) {ws.heartbeat(); continue;}
      if (!ws.alive) {ws.terminate(); continue;}
      ws.alive = false;
      if (!sources.has(ws)) ws.ping();
    } catch {ws.terminate();}
  }
},15000).unref();

sourceWss.on("connection",ws => {
  monitorSocket(ws,sources);
  ws.session = relaySession + "-" + (++generation);
  ws.lastSequence = -1;
  ws.lastTimestamp = 0;
  ws.lastSession = "";
  ws.retiredSessions = new Set();
  ws.retired = false;
  ws.on("message",raw => {
    if (raw.byteLength > 262144) {ws.close(1009); return;}
    const text = raw.toString();
    if (text.length < 8 || text.length > 262144) {rejectedPackets += 1; return;}
    try {
      const parsed = JSON.parse(text);
      if (!validSnapshot(parsed) || ws.retired) {rejectedPackets += 1; return;}
      const sid = parsed.sid || ws.session;
      if (ws.retiredSessions.has(sid) ||
          (sid === ws.lastSession && (parsed.seq <= ws.lastSequence || parsed.ts < ws.lastTimestamp)) ||
          (accepted && sid === accepted.sid &&
            (parsed.seq <= accepted.seq || parsed.ts < accepted.ts))) {
        rejectedPackets += 1;
        return;
      }
      if (activeSource && activeSource !== ws) activeSource.retired = true;
      activeSource = ws;
      if (ws.lastSession && ws.lastSession !== sid) {
        ws.retiredSessions.add(ws.lastSession);
        if (ws.retiredSessions.size > 16)
          ws.retiredSessions.delete(ws.retiredSessions.values().next().value);
      }
      ws.lastSession = sid;
      ws.lastSequence = parsed.seq;
      ws.lastTimestamp = parsed.ts;
      accepted = {sid,seq:parsed.seq,ts:parsed.ts};
      lastUpdateMs = Date.now();
      ws.alive = true;
      sourcePackets += 1;
      publish({...parsed,sid,source_state:"live"});
    } catch {rejectedPackets += 1;}
  });
});

viewerWss.on("connection",ws => {
  expireSource(Date.now());
  monitorSocket(ws,viewers);
  ws.revision = revision;
  ws.pressureAt = 0;
  try {ws.send(latestText);} catch {ws.terminate();}
});

server.on("upgrade",(req,socket,head) => {
  let url;
  try {url = new URL(req.url,"http://127.0.0.1");}
  catch {socket.destroy(); return;}
  if (url.pathname === "/api/ingest") {
    sourceWss.handleUpgrade(req,socket,head,ws => sourceWss.emit("connection",ws,req));
    return;
  }
  if (url.pathname === "/api/ws") {
    viewerWss.handleUpgrade(req,socket,head,ws => viewerWss.emit("connection",ws,req));
    return;
  }
  socket.destroy();
});

server.listen(PORT,HOST,() => {
  console.log(`KevqDMA WebRadar server listening on ${HOST}:${server.address().port}`);
});
