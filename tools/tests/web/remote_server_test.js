"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const {EventEmitter} = require("node:events");
const sockets = [];
const intervals = [];
let now = 10000;
const server = new EventEmitter();
server.listen = () => {};
const routes = new Map();
const app = {disable() {}, use() {}, get(route,handler) {routes.set(route,handler);}};
const express = () => app;
express.static = () => {};
class Socket extends EventEmitter {
  constructor() {super(); this.readyState=1; this.bufferedAmount=0; this.sent=[]; this.pings=0;}
  send(value) {this.sent.push(value);}
  ping() {++this.pings;}
  close(code) {this.code=code; this.readyState=3; this.emit("close");}
  terminate() {this.terminated=true; this.close();}
}
class Wss extends EventEmitter {
  constructor(options) {super(); this.options=options; sockets.push(this);}
  handleUpgrade(req,socket,head,callback) {callback(socket);}
}
class Clock extends Date {static now() {return now;}}
const context = vm.createContext({URL,Buffer,console,Date:Clock,
  process:{env:{},pid:1}, __dirname:path.join(__dirname,"fixture"),
  setInterval(callback,ms) {intervals.push({callback,ms}); return {unref() {}};},
  require(name) {
    if (name==="http") return {createServer:()=>server};
    if (name==="express") return express;
    if (name==="ws") return {WebSocketServer:Wss,WebSocket:{OPEN:1}};
    return require(name);
  }});
vm.runInContext(fs.readFileSync(path.join(__dirname,
  "../../../src/Features/WebRadar/Assets/remote/server.js"),"utf8"),context);
assert.ok(routes.has("/{*path}"),"Express 5 requires a named wildcard, including the root path");
assert.ok(!routes.has("*"),"Express 4 wildcard must not remain");
assert.equal(sockets[0].options.maxPayload,262144);
assert.equal(sockets[1].options.maxPayload,1024);
const getLive = () => {
  let data;
  routes.get("/api/live")({}, {set() {},json(value) {data=value;}});
  return data;
};
const frame = (seq=100,ts=1000,sid="source-A") => ({v:2,seq,ts,sid,map:"rush_001",lt:3,
  p:[[1,"Long Unicode 名前",3,0,90,[1,2,3],100,100,16000,20,7,30,[],[0,0],ts]],
  b:[0,null,0,40,0,10],w:[[1,[10,20,30],18]]});
const send = (socket,value) => socket.emit("message",Buffer.from(JSON.stringify(value)));
const publisher = new Socket();
server.emit("upgrade",{url:"/api/ingest",headers:{}},publisher,Buffer.alloc(0));
send(publisher,frame());
assert.equal(getLive().map,"rush_001");
assert.equal(getLive().source_state,"live");
for (const invalid of [frame(99,999),frame(100,1000),frame(101,900),
    {...frame(101,1001),map:{}},{...frame(101,1001),p:"players"},
    {...frame(101,1001),seq:-1},{...frame(101,1001),b:[0,[null,1,2],0,40,0,10]},
    {...frame(101,1001),p:[frame().p[0],frame().p[0]]},
    {...frame(101,1001),w:[[1,[1,2,3],null]]},
    {...frame(101,1001),p:Array(66).fill(frame().p[0])}]) {
  send(publisher,invalid);
  assert.equal(getLive().seq,100,"Invalid/out-of-order frame changed live state");
}
publisher.emit("message",Buffer.from("{invalid json"));
assert.equal(getLive().seq,100);
const viewer = new Socket();
sockets[1].emit("connection",viewer);
const broadcast = intervals.find(item=>item.ms<100).callback;
viewer.bufferedAmount=70000;
send(publisher,frame(101,1001));
broadcast();
assert.equal(viewer.sent.length,1);
viewer.bufferedAmount=0;
broadcast();
assert.equal(JSON.parse(viewer.sent.at(-1)).seq,101,"Skipped viewer never received latest state");
assert.equal(viewer.sent.length,2);
broadcast();
assert.equal(viewer.sent.length,2,"Unchanged frames must not be resent");

const next = new Socket();
sockets[0].emit("connection",next);
send(next,{...frame(1,2000,"source-B"),map:"aim_botz"});
assert.equal(getLive().map,"aim_botz");
send(publisher,{...frame(102,1002),map:"old_source"});
assert.equal(getLive().map,"aim_botz","Retired publisher overwrote active stream");
next.close();
now += 2999;
assert.equal(getLive().source_state,"live","Reconnect grace period lost valid data");
now += 2;
broadcast();
assert.equal(getLive().source_state,"stale");
assert.equal(getLive().p.length,0);
assert.equal(getLive().w.length,0);
assert.equal(getLive().b[1],null);
const reconnect = new Socket();
sockets[0].emit("connection",reconnect);
send(reconnect,frame(1,2000,"source-B"));
assert.equal(getLive().source_state,"stale","Replayed frame refreshed expired source");
send(reconnect,frame(2,2001,"source-B"));
assert.equal(getLive().source_state,"live");
assert.equal(getLive().p.length,1);
send(reconnect,{...frame(1,2002,"source-C"),map:"same_socket_restart"});
assert.equal(getLive().map,"same_socket_restart");
send(reconnect,frame(200,3000,"source-B"));
assert.equal(getLive().map,"same_socket_restart","Retired stream session returned on the same socket");

const request = new EventEmitter();
const response = new EventEmitter();
response.writes=[]; response.writeHead=()=>{};
response.write=value=>{response.writes.push(value); return true;};
response.destroy=()=>{response.destroyed=true;};
routes.get("/api/stream")(request,response);
response.writableNeedDrain=true;
send(reconnect,frame(2,2003,"source-C"));
broadcast();
assert.equal(response.writes.length,1);
response.writableNeedDrain=false;
broadcast();
assert.equal(response.writes.length,2);
const heartbeat=intervals.find(item=>item.ms===15000).callback;
heartbeat();
assert.equal(reconnect.pings,0,"Send-only C++ publisher must not require receiving ping frames");
send(reconnect,frame(3,2004,"source-C"));
heartbeat();
assert.equal(reconnect.terminated,undefined);
assert.equal(viewer.terminated,true,"Missing pong must release broken sockets");
assert.ok(response.writes.at(-1).startsWith(": heartbeat"));
publisher.emit("message",Buffer.alloc(262145));
assert.equal(publisher.code,1009);
assert.doesNotThrow(()=>publisher.emit("error",new Error("invalid UTF8/frame")));
let destroyed=false;
server.emit("upgrade",{url:"http://["},{destroy() {destroyed=true;}},Buffer.alloc(0));
assert.equal(destroyed,true);
console.log("Remote server regressions passed: schema, sequence, source restart/expiry, WS/SSE backpressure, heartbeat; ingest access unchanged.");
