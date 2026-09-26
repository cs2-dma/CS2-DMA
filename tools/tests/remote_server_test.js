"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const { EventEmitter } = require("node:events");
const sockets = [];
const server = new EventEmitter();
server.listen = () => {};
const routes = new Map();
const app = { use() {}, get(route, handler) { routes.set(route, handler); } };
const express = () => app;
express.static = () => {};
class Socket extends EventEmitter {
  constructor() { super(); this.readyState = 1; this.bufferedAmount = 0; this.sent = []; }
  send(value) { this.sent.push(value); }
  close(code) { this.code = code; this.emit("close"); }
  terminate() { this.terminated = true; this.emit("close"); }
}
class Wss extends EventEmitter {
  constructor(options) { super(); this.options = options; sockets.push(this); }
  handleUpgrade(req, socket, head, callback) { callback(socket); }
}
const context = vm.createContext({ URL, Buffer, console,
  process: { env: {} }, __dirname: path.join(__dirname, "fixture"),
  setInterval: () => ({unref() {}}),
  require(name) {
    if (name === "http") return {createServer: () => server};
    if (name === "express") return express;
    if (name === "ws") return {WebSocketServer: Wss, WebSocket: {OPEN:1}};
    return require(name);
  }
});
vm.runInContext(fs.readFileSync(path.join(__dirname,
  "../../src/Features/WebRadar/Assets/remote/server.js"), "utf8"), context);
assert.equal(sockets[0].options.maxPayload,262144);
assert.equal(sockets[1].options.maxPayload,1024);
const publisher = new Socket();
server.emit("upgrade",{url:"/api/ingest",headers:{}},publisher,Buffer.alloc(0));
assert.equal(publisher.listenerCount("error"),1);
const getLive = () => {
  let data;
  routes.get("/api/live")({}, {set() {}, json(value) {data=value;}});
  return data;
};
publisher.emit("message",Buffer.from(JSON.stringify({v:2,map:"rush_001",p:[]})));
assert.equal(getLive().map,"rush_001");
publisher.emit("message",Buffer.from("{invalid json"));
assert.equal(getLive().map,"rush_001");
publisher.emit("message",Buffer.alloc(262145));
assert.equal(publisher.code,1009);
assert.equal(getLive().map,"rush_001");
assert.doesNotThrow(() => publisher.emit("error",new Error("invalid UTF8/frame")));
assert.equal(publisher.terminated,true);
const next = new Socket();
sockets[0].emit("connection",next);
next.emit("message",Buffer.from(JSON.stringify({v:2,map:"aim_botz",p:[]})));
assert.equal(getLive().map,"aim_botz");
const viewer = new Socket();
sockets[1].emit("connection",viewer);
assert.doesNotThrow(() => viewer.emit("error",new Error("viewer disconnected")));
let destroyed = false;
server.emit("upgrade",{url:"http://["},{destroy() {destroyed=true;}},Buffer.alloc(0));
assert.equal(destroyed,true);
console.log("Remote server regressions passed: limits, malformed data, socket errors, recovery; ingest access unchanged.");
