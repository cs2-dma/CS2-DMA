const http = require("http");
const fs = require("fs");
const path = require("path");
const express = require("express");
const { WebSocketServer, WebSocket } = require("ws");

const PORT = Number(process.env.PORT || 8080);
const PUBLIC_DIR = path.join(__dirname, "public");
const app = express();
const server = http.createServer(app);
const viewers = new Set();
let latest = { v: 2, seq: 0, ts: Date.now(), map: "unknown", lt: 0, p: [], b: [0, [0, 0, 0], 0, 40, 0, 10], w: [] };
let latestText = JSON.stringify(latest);
let lastUpdateMs = 0;
let sourcePackets = 0;
let broadcastPackets = 0;
let dirty = false;
const BROADCAST_HZ = Number(process.env.BROADCAST_HZ || 60);
const BROADCAST_INTERVAL_MS = Math.max(16, Math.round(1000 / Math.max(1, Math.min(60, BROADCAST_HZ))));
const MAX_VIEWER_BUFFER_BYTES = 64 * 1024;

app.use(express.static(PUBLIC_DIR, { etag: true, maxAge: "1h" }));
app.get("/api/status", (req, res) => {
  res.set("Cache-Control", "no-store");
  res.json({
    ok: true,
    active_map: latest.map || "unknown",
    sent_packets: sourcePackets,
    broadcast_packets: broadcastPackets,
    last_update_ms: lastUpdateMs,
    connected_clients: viewers.size
  });
});
app.get("/api/live", (req, res) => {
  res.set("Cache-Control", "no-store");
  res.json(latest);
});
app.get("/api/stream", (req, res) => {
  res.writeHead(200, {
    "Content-Type": "text/event-stream",
    "Cache-Control": "no-store",
    "Connection": "keep-alive",
    "Access-Control-Allow-Origin": "*"
  });
  res.write(`event: snapshot\ndata: ${JSON.stringify(latest)}\n\n`);
  const client = {
    sse: true,
    send: text => {
      if (res.destroyed || res.writableEnded || res.writableNeedDrain) return false;
      return res.write(`event: snapshot\ndata: ${text}\n\n`);
    }
  };
  viewers.add(client);
  req.on("close", () => viewers.delete(client));
});
app.get("*", (req, res) => res.sendFile(path.join(PUBLIC_DIR, "index.html")));

const sourceWss = new WebSocketServer({ noServer: true, maxPayload: 262144, perMessageDeflate: false });
const viewerWss = new WebSocketServer({ noServer: true, maxPayload: 1024, perMessageDeflate: false });

function broadcastLatest() {
  if (!dirty) return;
  dirty = false;
  const text = latestText;
  for (const client of viewers) {
    try {
      if (client.sse === true) {
        client.send(text);
      } else if (client.readyState === WebSocket.OPEN) {
        if (client.bufferedAmount > MAX_VIEWER_BUFFER_BYTES) continue;
        client.send(text);
      }
    } catch {}
  }
  broadcastPackets += 1;
}
setInterval(broadcastLatest, BROADCAST_INTERVAL_MS).unref();

sourceWss.on("connection", ws => {
  ws.on("error", () => ws.terminate());
  ws.on("message", raw => {
    if (raw.byteLength > 262144) { ws.close(1009); return; }
    const text = raw.toString();
    if (text.length < 8 || text.length > 262144) return;
    try {
      const parsed = JSON.parse(text);
      if (!parsed || Number(parsed.v || 0) !== 2) return;
      latest = parsed;
      latestText = text;
      lastUpdateMs = Date.now();
      sourcePackets += 1;
      dirty = true;
    } catch {}
  });
});

viewerWss.on("connection", ws => {
  viewers.add(ws);
  ws.send(latestText);
  ws.on("close", () => viewers.delete(ws));
  ws.on("error", () => viewers.delete(ws));
});

server.on("upgrade", (req, socket, head) => {
  let url;
  try { url = new URL(req.url, "http://127.0.0.1"); }
  catch { socket.destroy(); return; }
  if (url.pathname === "/api/ingest") {
    sourceWss.handleUpgrade(req, socket, head, ws => sourceWss.emit("connection", ws, req));
    return;
  }
  if (url.pathname === "/api/ws") {
    viewerWss.handleUpgrade(req, socket, head, ws => viewerWss.emit("connection", ws, req));
    return;
  }
  socket.destroy();
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`KevqDMA WebRadar server listening on 0.0.0.0:${PORT}`);
});
