"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawn} = require("node:child_process");
const {setTimeout: delay} = require("node:timers/promises");
const WebSocket = require("ws");

async function waitFor(predicate, label, timeout = 5000) {
  const deadline = Date.now() + timeout;
  while (!await predicate()) {
    if (Date.now() > deadline) throw new Error("Timeout: " + label);
    await delay(10);
  }
}

async function main() {
  const root = path.resolve(__dirname,"../../..");
  const assets = path.join(root,"src/Features/WebRadar/Assets");
  const source = fs.readFileSync(path.join(root,"src/Features/WebRadar/Runtime/web_remote.cpp"),"utf8");
  const dependencies = JSON.parse(source.match(/"dependencies":(\{[^}]+\})/)[1]);
  for (const [name,version] of Object.entries(dependencies))
    assert.equal(require(name+"/package.json").version,version,"Runtime dependency mismatch: "+name);
  const fixture = fs.mkdtempSync(path.join(os.tmpdir(),"kevqdma-express5-"));
  const script = path.join(fixture,"server.js");
  fs.copyFileSync(path.join(assets,"remote/server.js"),script);
  const publicDir = path.join(fixture,"public");
  fs.mkdirSync(path.join(publicDir,"assets/icons"),{recursive:true});
  for (const file of ["index.html","viewer.js","viewer.css","maps.json","assets/icons/incgrenade.svg"])
    fs.copyFileSync(path.join(assets,file),path.join(publicDir,file));
  fs.copyFileSync(path.join(root,"src/assets/weapon_icons/weapon_icon_atlas.png"),path.join(publicDir,"atlas.png"));
  const child = spawn(process.execPath,[script],{
    env:{...process.env,HOST:"127.0.0.1",PORT:"0"},stdio:["ignore","pipe","pipe"]});
  let stdout = "", stderr = "", port = 0;
  const clients = [];
  const streams = [];
  child.stdout.on("data",data => {
    stdout += data;
    const match = stdout.match(/127\.0\.0\.1:(\d+)/);
    if (match) port = Number(match[1]);
  });
  child.stderr.on("data",data => {stderr += data;});
  const connect = async endpoint => {
    const socket = new WebSocket(`ws://127.0.0.1:${port}/api/${endpoint}`,{
      autoPong:endpoint!=="ingest"});
    socket.frames = [];
    socket.on("message",text => socket.frames.push(JSON.parse(text.toString())));
    socket.on("error",error => {socket.lastError=error;});
    clients.push(socket);
    await waitFor(()=>socket.readyState===WebSocket.OPEN,"connect "+endpoint);
    return socket;
  };
  const frame = (seq,sid="integration-A") => ({v:2,seq,ts:1000+seq,sid,
    map:"de_inferno",lt:3,p:[[1,"Player",3,0,90,[seq,20,30],100,100,800,10,7,30,[],[0,0],1000+seq]],
    b:[2,[40,50,60],0,40,0,10],w:[[2,[70,80,90],7]]});
  try {
    await waitFor(()=>port>0 || child.exitCode!==null,"server start");
    assert.ok(port>0,stderr);
    const base = `http://127.0.0.1:${port}`;
    for (const url of ["/","/radar/session"]) {
      const response = await fetch(base+url);
      assert.equal(response.status,200,url);
      assert.equal(await response.text(),fs.readFileSync(path.join(publicDir,"index.html"),"utf8"));
      assert.equal(response.headers.get("x-powered-by"),null);
    }
    for (const [url,type] of [["/viewer.js","text/javascript"],["/viewer.css","text/css"],
        ["/maps.json","application/json"],["/assets/icons/incgrenade.svg","image/svg+xml"],["/atlas.png","image/png"]]) {
      const response = await fetch(base+url);
      assert.equal(response.status,200,url);
      assert.ok(response.headers.get("content-type").startsWith(type),url);
      assert.match(response.headers.get("cache-control"),/max-age=3600/);
      assert.deepEqual(Buffer.from(await response.arrayBuffer()),fs.readFileSync(path.join(publicDir,url)));
      const cached = await fetch(base+url,{cache:"force-cache",headers:{"If-None-Match":response.headers.get("etag")}});
      assert.equal(cached.status,304,url);
      const head = await fetch(base+url,{method:"HEAD"});
      assert.equal(head.status,200,url);
      assert.equal((await head.arrayBuffer()).byteLength,0);
    }
    for (const url of ["/api/missing","/missing.js","/data/missing/data.json"]) {
      const response = await fetch(base+url);
      assert.equal(response.status,404,url);
      assert.deepEqual(await response.json(),{ok:false,error:"not_found"});
    }
    const bad = await fetch(base+"/%zz");
    assert.equal(bad.status,400);
    assert.deepEqual(await bad.json(),{ok:false,error:"request_error"});
    const controller = new AbortController();
    streams.push(controller);
    const timeout = setTimeout(()=>controller.abort(),5000);
    let stream, reader;
    try {
      stream = await fetch(base+"/api/stream",{signal:controller.signal});
      assert.equal(stream.status,200);
      assert.equal(stream.headers.get("content-type"),"text/event-stream");
      assert.equal(stream.headers.get("x-accel-buffering"),"no");
      reader = stream.body.getReader();
      assert.match(new TextDecoder().decode((await reader.read()).value),/^event: snapshot\ndata: /);
    } finally {clearTimeout(timeout);}
    const viewer = await connect("ws");
    const source = await connect("ingest");
    source.send(JSON.stringify(frame(10)));
    await waitFor(()=>viewer.frames.at(-1)?.seq===10,"first actual WS frame");
    const updateTimeout = setTimeout(()=>controller.abort(),5000);
    try {
      const update = new TextDecoder().decode((await reader.read()).value);
      assert.match(update,/"seq":10,/);
    } finally {
      clearTimeout(updateTimeout);
      await reader.cancel();
      controller.abort();
    }
    await waitFor(async()=>(await (await fetch(base+"/api/status")).json()).connected_clients===1,
      "SSE disconnect removes viewer");
    source.send(JSON.stringify(frame(5)));
    source.send(JSON.stringify({...frame(11),p:"invalid"}));
    await delay(60);
    assert.equal(viewer.frames.at(-1).seq,10);
    for (let seq=11;seq<=300;++seq) source.send(JSON.stringify(frame(seq)));
    await waitFor(()=>viewer.frames.at(-1)?.seq===300,"coalesced burst completion");
    assert.equal(viewer.frames.at(-1).p[0][5][0],300);
    const status=await (await fetch(`http://127.0.0.1:${port}/api/status`)).json();
    assert.equal(status.source_state,"live");
    assert.equal(status.rejected_packets,2);
    if (process.env.KEVQ_REMOTE_HEARTBEAT_SOAK === "1") {
      const soakDeadline = Date.now()+32000;
      let sequence = 301;
      while (Date.now()<soakDeadline) {
        assert.equal(source.readyState,WebSocket.OPEN,"Publisher without pong was disconnected");
        source.send(JSON.stringify(frame(sequence++)));
        await delay(100);
      }
      await waitFor(()=>viewer.frames.at(-1)?.seq===sequence-1,"send-only publisher after heartbeat soak");
    }
    source.close();
    const replacement=await connect("ingest");
    replacement.send(JSON.stringify(frame(1,"integration-B")));
    await waitFor(()=>viewer.frames.at(-1)?.sid==="integration-B","source restart");
    replacement.close();
    await waitFor(()=>viewer.frames.at(-1)?.source_state==="stale","source expiry",4500);
    const live=await (await fetch(`http://127.0.0.1:${port}/api/live`)).json();
    assert.equal(live.p.length,0);
    assert.equal(live.w.length,0);
    assert.equal(live.b[1],null);
    assert.equal(stderr,"");
    console.log("Remote loopback integration passed: pinned Express/ws, HTTP/static/HEAD/ETag/404, SSE, coalescing, rejection, reconnect, expiry; no external listener.");
  } finally {
    for (const controller of streams) controller.abort();
    for (const socket of clients) socket.terminate();
    child.kill();
    await waitFor(()=>child.exitCode!==null || child.signalCode!==null,"server shutdown");
    assert.equal(path.dirname(fixture),path.resolve(os.tmpdir()));
    assert.ok(path.basename(fixture).startsWith("kevqdma-express5-"));
    fs.rmSync(fixture,{recursive:true,force:true});
  }
}

main().catch(error=>{console.error(error);process.exitCode=1;});
