/* A small, single-batch WebGL renderer. No engine, textures, shadows, or post-processing.
   The GPU transforms and shades the fragments. Animation diagnostics describe this web scene only. */
(() => {
  'use strict';
  const site=window.KevqSite,canvas=document.getElementById('cubeCanvas'),stage=document.getElementById('cubeStage');
  if(!site||!canvas)return;
  const small=matchMedia('(max-width:740px)');
  const clamp=(n,a=0,b=1)=>Math.max(a,Math.min(b,n));
  const gl=canvas.getContext('webgl',{alpha:true,antialias:!small.matches,depth:true,stencil:false,preserveDrawingBuffer:false,premultipliedAlpha:false,powerPreference:'low-power'});
  const stats={renderer:gl?'WebGL':'static SVG',fragments:0,drawCallsPerFrame:1,frames:0,cpuSamples:[],width:0,height:0,dpr:1,running:false};
  window.KevqScene={stats};
  if(!gl){stats.drawCallsPerFrame=0;document.querySelector('.scene-modes').hidden=true;document.getElementById('resetScene').hidden=true;return;}
  const vertexSource=`
  precision highp float;
  attribute vec3 aPosition;
  attribute vec3 aNormal;
  attribute vec3 aCenter;
  attribute vec2 aParam;
  uniform vec2 uAngles;
  uniform vec2 uPointer;
  uniform float uHover;
  uniform float uSpread;
  uniform float uOrbit;
  uniform float uTime;
  uniform float uAspect;
  uniform float uSize;
  varying vec3 vNormal;
  varying vec3 vWorld;
  varying vec3 vLocal;
  varying float vSeed;
  varying float vCore;
  vec3 rotateY(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);}
  vec3 rotateX(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(p.x,c*p.y-s*p.z,s*p.y+c*p.z);}
  void main(){
    float seed=aParam.x;
    float core=aParam.y;
    float radius=max(length(aCenter),.01);
    vec3 outward=aCenter/radius;
    float wave=sin(seed*31.0+uTime*.8)*.10*uSpread;
    vec3 center=aCenter*(1.0+uSpread*.82)+outward*(uSpread*.23+wave);
    float theta=seed*237.9+uTime*.14;
    float yy=1.0-2.0*seed;
    float rr=sqrt(max(0.0,1.0-yy*yy));
    vec3 sphere=vec3(cos(theta)*rr,yy,sin(theta)*rr)*2.16;
    center=mix(center,sphere,uOrbit*(1.0-core));
    vec2 delta=center.xy-uPointer;
    float repel=(1.0-smoothstep(.0,1.25,length(delta)))*uHover*(1.0-core);
    center.xy+=normalize(delta+vec2(.001))*repel*.20;
    center.z+=repel*.16;
    center*=1.0-core;
    float turn=uSpread*.21*sin(seed*50.0)+uOrbit*(seed-.5)*.4;
    vec3 local=rotateY(rotateX(aPosition,turn*.7),turn);
    vec3 normal=rotateY(rotateX(aNormal,turn*.7),turn);
    float scale=uSize*(1.0-uOrbit*.2)*(1.0-core*.17);
    vec3 world=rotateX(rotateY(center+local*scale,uAngles.x),uAngles.y);
    normal=rotateX(rotateY(normal,uAngles.x),uAngles.y);
    float camera=(7.9+uSpread*4.8+uOrbit*1.6)*max(1.0,1.0/uAspect);
    float zz=world.z-camera;
    float focal=3.15;
    gl_Position=vec4(world.x*focal/uAspect,world.y*focal-.035*zz,-1.0050125*zz-.20050125,-zz);
    vNormal=normal;vWorld=world;vLocal=aPosition;vSeed=seed;vCore=core;
  }`;
  const fragmentSource=`
  precision mediump float;
  varying vec3 vNormal;
  varying vec3 vWorld;
  varying vec3 vLocal;
  varying float vSeed;
  varying float vCore;
  void main(){
    vec3 n=normalize(vNormal);
    vec3 view=normalize(vec3(0.0,0.0,8.0)-vWorld);
    vec3 light=normalize(vec3(-.55,.95,1.4));
    float diffuse=max(0.0,dot(n,light));
    float spec=pow(max(0.0,dot(n,normalize(light+view))),44.0);
    vec3 reflected=reflect(-view,n);
    float env=pow(max(0.0,dot(reflected,normalize(vec3(.7,.8,.4)))),5.0);
    float rim=pow(1.0-max(0.0,dot(n,view)),3.0);
    float brush=sin(vWorld.y*24.0+vWorld.x*.7)*.013;
    float shade=.11+diffuse*.49+spec*.48+env*.16+rim*.11+vSeed*.04+brush;
    vec3 edges=abs(vLocal);
    float second=max(min(edges.x,edges.y),min(max(edges.x,edges.y),edges.z));
    float edge=smoothstep(.477,.498,second);
    shade=mix(shade,min(.93,shade+.20),edge);
    shade=mix(shade,.88,vCore);
    gl_FragColor=vec4(vec3(clamp(shade,.04,1.0)),1.0);
  }`;
  let program,buffer,uniforms={},count=0,n=0,contextLost=false;
  let quality=1,intervalEMA=16,lastQualityChange=0;
  let width=0,height=0,docTop=0,left=0,visible=false,raf=0,last=0,lastDraw=0;
  let yaw=-.61,pitch=.46,dragYaw=0,dragPitch=0,rotation=0;
  let spread=0,target=0,orbit=1,orbitTarget=1,manual=null,manualScroll=0;
  let mouseX=0,mouseY=0,hover=0,hoverTarget=0,drag=null;
  let introStart=performance.now(),lastActivity=introStart,clock=0;
  function compile(type,source){
    const shader=gl.createShader(type);gl.shaderSource(shader,source);gl.compileShader(shader);
    if(!gl.getShaderParameter(shader,gl.COMPILE_STATUS)){const err=gl.getShaderInfoLog(shader);gl.deleteShader(shader);throw Error(err);}return shader;
  }
  function setup(){
    const vertex=compile(gl.VERTEX_SHADER,vertexSource),fragment=compile(gl.FRAGMENT_SHADER,fragmentSource);
    program=gl.createProgram();gl.attachShader(program,vertex);gl.attachShader(program,fragment);gl.linkProgram(program);
    gl.deleteShader(vertex);gl.deleteShader(fragment);
    if(!gl.getProgramParameter(program,gl.LINK_STATUS))throw Error(gl.getProgramInfoLog(program));
    gl.useProgram(program);
    for(const name of ['uAngles','uPointer','uHover','uSpread','uOrbit','uTime','uAspect','uSize'])uniforms[name]=gl.getUniformLocation(program,name);
    buffer=gl.createBuffer();
    gl.enable(gl.DEPTH_TEST);gl.depthFunc(gl.LEQUAL);gl.enable(gl.CULL_FACE);gl.cullFace(gl.BACK);
    gl.clearColor(0,0,0,0);n=0;geometry();
  }
  function geometry(){
    const size=small.matches?4:5;if(size===n)return;n=size;
    const faces=[
      [[1,0,0],[[.5,-.5,-.5],[.5,.5,-.5],[.5,.5,.5],[.5,-.5,.5]]],
      [[-1,0,0],[[-.5,-.5,.5],[-.5,.5,.5],[-.5,.5,-.5],[-.5,-.5,-.5]]],
      [[0,1,0],[[-.5,.5,-.5],[-.5,.5,.5],[.5,.5,.5],[.5,.5,-.5]]],
      [[0,-1,0],[[-.5,-.5,.5],[-.5,-.5,-.5],[.5,-.5,-.5],[.5,-.5,.5]]],
      [[0,0,1],[[-.5,-.5,.5],[.5,-.5,.5],[.5,.5,.5],[-.5,.5,.5]]],
      [[0,0,-1],[[.5,-.5,-.5],[-.5,-.5,-.5],[-.5,.5,-.5],[.5,.5,-.5]]]
    ];
    const centers=[],step=2.58/n;
    for(let x=0;x<n;x++)for(let y=0;y<n;y++)for(let z=0;z<n;z++){
      if(x>0&&x<n-1&&y>0&&y<n-1&&z>0&&z<n-1)continue;
      centers.push([(x-(n-1)/2)*step,(y-(n-1)/2)*step,(z-(n-1)/2)*step,0]);
    }
    centers.push([0,0,0,1]);
    const total=centers.length,data=new Float32Array(total*36*11);let offset=0;
    centers.forEach((center,id)=>faces.forEach(([normal,vertices])=>{
      for(const index of [0,1,2,0,2,3]){
        for(const f of vertices[index])data[offset++]=f;
        for(const f of normal)data[offset++]=f;
        data[offset++]=center[0];data[offset++]=center[1];data[offset++]=center[2];
        data[offset++]=id/(total-1);data[offset++]=center[3];
      }
    }));
    gl.bindBuffer(gl.ARRAY_BUFFER,buffer);gl.bufferData(gl.ARRAY_BUFFER,data,gl.STATIC_DRAW);
    let base=0;for(const [name,size] of [['aPosition',3],['aNormal',3],['aCenter',3],['aParam',2]]){
      const attr=gl.getAttribLocation(program,name);gl.enableVertexAttribArray(attr);gl.vertexAttribPointer(attr,size,gl.FLOAT,false,44,base*4);base+=size;
    }
    count=total*36;stats.fragments=total;
  }
  function resize(){
    if(contextLost)return;
    const r=stage.getBoundingClientRect();width=r.width;height=r.height;docTop=r.top+scrollY;left=r.left;
    if(!width||!height)return;
    const ratio=Math.min(devicePixelRatio||1,small.matches?1.25:1.4)*quality;
    const w=Math.round(width*ratio),h=Math.round(height*ratio);
    if(canvas.width!==w||canvas.height!==h){canvas.width=w;canvas.height=h;gl.viewport(0,0,w,h);}
    stats.width=w;stats.height=h;stats.dpr=ratio;stats.quality=quality;geometry();
    if(site.paused){spread=target;orbit=orbitTarget;render(0);}
    else wake();
  }
  function select(mode){
    manual=mode;manualScroll=scrollY;lastActivity=performance.now();
    target=mode==='scatter'?1:0;orbitTarget=mode==='orbit'?1:0;
    document.querySelectorAll('[data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===mode)));
    if(site.paused){spread=target;orbit=orbitTarget;render(0);}else wake();
  }
  function onScroll(){
    if(manual&&Math.abs(scrollY-manualScroll)>100)manual=null;
    if(!manual){target=Math.pow(clamp((scrollY-8)/280),.84);orbitTarget=1-target;
      document.querySelectorAll('[data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===(target>.45?'scatter':'orbit'))));}
    if(site.paused){spread=target;orbit=orbitTarget;if(visible)render(0);}else wake();
  }
  function render(dt){
    if(contextLost||!width||!height)return;
    const cpuStart=performance.now();
    const blend=dt>0?1-Math.exp(-dt/170):1;
    spread+=(target-spread)*blend;orbit+=(orbitTarget-orbit)*blend;hover+=(hoverTarget-hover)*blend;
    if(Math.abs(spread-target)<.0001)spread=target;
    if(Math.abs(orbit-orbitTarget)<.0001)orbit=orbitTarget;
    if(dt&&!site.paused&&!drag){rotation+=dt*.000044;clock+=dt*.001;}
    const wobble=site.paused?0:Math.sin(clock*.37)*.032;
    gl.useProgram(program);gl.clear(gl.COLOR_BUFFER_BIT|gl.DEPTH_BUFFER_BIT);
    gl.uniform2f(uniforms.uAngles,yaw+dragYaw+rotation,pitch+dragPitch+wobble);
    gl.uniform2f(uniforms.uPointer,mouseX,mouseY);gl.uniform1f(uniforms.uHover,hover);
    gl.uniform1f(uniforms.uSpread,spread);gl.uniform1f(uniforms.uOrbit,orbit);gl.uniform1f(uniforms.uTime,clock);
    gl.uniform1f(uniforms.uAspect,width/height);gl.uniform1f(uniforms.uSize,(2.58/n)*.92);
    gl.drawArrays(gl.TRIANGLES,0,count);
    stage.classList.add('is-ready');stats.frames++;stats.spread=spread;stats.orbit=orbit;
    const now=performance.now();stats.cpuSamples.push(now-cpuStart);if(stats.cpuSamples.length>240)stats.cpuSamples.shift();
  }
  function shouldRun(){return visible&&!document.hidden&&!site.paused&&!site.dialogOpen&&!contextLost;}
  function tick(now){
    raf=0;if(!shouldRun()){stats.running=false;return;}
    const active=now-lastActivity<6500;
    // 60 Hz while interacting on desktop; a calm 30 Hz cadence when idle or on mobile.
    const rate=small.matches?30:(active?60:30);
    if(now-lastDraw>=1000/rate-.8){
      const raw=last?now-last:1000/rate;
      intervalEMA=intervalEMA*.9+raw*.1;
      const dt=Math.min(raw,65);last=now;lastDraw=now;render(dt);
      if(stats.frames>20&&intervalEMA>48&&now-lastQualityChange>2500&&quality>.55){
        quality=Math.max(.55,quality*.8);lastQualityChange=now;resize();
      }
    }
    stats.running=true;if(!raf)raf=requestAnimationFrame(tick);
  }
  function wake(){if(!shouldRun()||raf)return;last=0;raf=requestAnimationFrame(tick);}
  function stop(){if(raf)cancelAnimationFrame(raf);raf=0;last=0;stats.running=false;}
  function point(e){mouseX=((e.clientX-left)/width-.5)*4.8;mouseY=(.5-(e.clientY-(docTop-scrollY))/height)*4.8;}
  stage.addEventListener('pointerenter',e=>{if(e.pointerType==='mouse'){hoverTarget=1;point(e);lastActivity=performance.now();wake();}});
  stage.addEventListener('pointerleave',()=>{hoverTarget=0;wake();});
  stage.addEventListener('pointerdown',e=>{
    if(e.button!==0)return;
    drag={x:e.clientX,y:e.clientY,yaw:dragYaw,pitch:dragPitch,moved:false,pointerType:e.pointerType};
    // Preserve vertical touch scrolling; desktop dragging receives pointer capture.
    if(e.pointerType==='mouse')stage.setPointerCapture(e.pointerId);
    stage.classList.add('is-dragging');lastActivity=performance.now();wake();
  });
  stage.addEventListener('pointermove',e=>{
    if(e.pointerType==='mouse')point(e);
    if(drag){const dx=e.clientX-drag.x,dy=e.clientY-drag.y;
      if(e.pointerType==='touch'&&Math.abs(dy)>Math.abs(dx)*1.25){drag=null;stage.classList.remove('is-dragging');return;}
      if(Math.abs(dx)+Math.abs(dy)>5)drag.moved=true;
      dragYaw=drag.yaw+dx*.007;dragPitch=clamp(drag.pitch+dy*.004,-.8,.8);
      if(site.paused)render(0);
    }
    lastActivity=performance.now();wake();
  },{passive:true});
  function endDrag(e){if(drag){drag=null;stage.classList.remove('is-dragging');try{if(stage.hasPointerCapture(e.pointerId))stage.releasePointerCapture(e.pointerId);}catch{}}}
  stage.addEventListener('pointerup',endDrag);stage.addEventListener('pointercancel',endDrag);
  document.querySelectorAll('[data-mode]').forEach(b=>b.addEventListener('click',()=>select(b.dataset.mode)));
  document.getElementById('resetScene').addEventListener('click',()=>{dragYaw=0;dragPitch=0;rotation=0;clock=0;select('orbit');});
  let scrollFrame=0;
  window.addEventListener('scroll',()=>{if(!scrollFrame)scrollFrame=requestAnimationFrame(()=>{scrollFrame=0;onScroll();});},{passive:true});
  document.addEventListener('kevq:motion',()=>{if(site.paused){stop();spread=target;orbit=orbitTarget;render(0);}else{lastActivity=performance.now();wake();}});
  document.addEventListener('kevq:visibility',()=>document.hidden?stop():wake());
  document.addEventListener('kevq:dialog',e=>e.detail.open?stop():wake());
  canvas.addEventListener('webglcontextlost',e=>{e.preventDefault();contextLost=true;stop();stage.classList.remove('is-ready');});
  canvas.addEventListener('webglcontextrestored',()=>{contextLost=false;try{setup();resize();wake();}catch{stage.classList.remove('is-ready');}});
  try{setup();resize();render(0);}catch(error){
    stats.renderer='static SVG';stats.error=error.message;stage.classList.remove('is-ready');
    document.querySelector('.scene-modes').hidden=true;document.getElementById('resetScene').hidden=true;return;
  }
  new ResizeObserver(resize).observe(stage);small.addEventListener('change',resize);
  if('IntersectionObserver' in window){
    new IntersectionObserver(entries=>{visible=entries[0].isIntersecting;if(visible)wake();else stop();},{threshold:.01}).observe(stage);
  }else{visible=true;wake();}
  window.KevqScene.select=select;
})();
