/* DMA telemetry adapter. Presentation targets and actual measurements never share a label. */
(() => {
  'use strict';
  const site=window.KevqSite,panel=document.getElementById('telemetry');if(!site||!panel)return;
  const config=window.KEVQ_DMA_CONFIG||{},targets={readLatencyMs:2.3,dataHz:300,cameraHz:300,renderFps:240,snapshotSlots:8,...config.targets};
  const get=id=>document.getElementById(id),tr=(el,key)=>{el.dataset.t=key;el.textContent=site.t(key);};
  const interval=Math.max(1000,Math.min(60000,Number(config.intervalMs)||2000));
  const staleAfter=Math.max(5000,Number(config.staleAfterMs)||10000);
  let sample=null,received=0,measured=0,failed=false,visible=false,timer=0,staleTimer=0,controller=null,active=false;
  const history=[];
  let endpoint='';
  try{if(config.endpoint){const u=new URL(config.endpoint,location.href);if(['http:','https:'].includes(u.protocol))endpoint=u.href;}}catch{/* Invalid URLs never generate requests. */}
  function valid(value,max){return typeof value==='number'&&Number.isFinite(value)&&value>=0&&value<=max;}
  function render(){
    const live=Boolean(sample),stale=live&&(Date.now()-measured>staleAfter||failed);
    panel.dataset.state=stale?'stale':live?'live':endpoint?'waiting':'profile';
    tr(get('dmaHeading'),live||endpoint?'telemetry.live':'telemetry.label');
    tr(get('dmaStatus'),stale?'telemetry.stale':live?'telemetry.connected':endpoint?'telemetry.offline':'telemetry.profile');
    tr(get('dmaNote'),stale?'telemetry.staleNote':live?'telemetry.liveNote':endpoint?'telemetry.empty':'telemetry.profileNote');
    tr(get('dmaLatencyLabel'),live||endpoint?'telemetry.latency':'telemetry.target');
    const n=new Intl.NumberFormat(site.locale,{maximumFractionDigits:0}),decimal=new Intl.NumberFormat(site.locale,{minimumFractionDigits:2,maximumFractionDigits:2});
    const data=sample||(!endpoint?targets:null);
    get('dmaLatency').textContent=data?(live?'':'< ')+decimal.format(data.readLatencyMs):'\u00b7\u00b7\u00b7';
    [['dmaData','dataHz'],['dmaCamera','cameraHz'],['dmaRender','renderFps'],['dmaSlots','snapshotSlots']].forEach(([id,key])=>get(id).textContent=data?n.format(data[key]):'\u00b7\u00b7\u00b7');
    const chart=get('dmaReadChart');
    if(history.length>1){
      const max=Math.max(1,targets.readLatencyMs,...history)*1.12;
      chart.setAttribute('d',history.map((value,index)=>`${index?'L':'M'}${(index*180/(history.length-1)).toFixed(2)} ${(22-value/max*20).toFixed(2)}`).join(' '));
      chart.removeAttribute('stroke-dasharray');
    }else{chart.setAttribute('d','M0 19H180');chart.setAttribute('stroke-dasharray','2 6');}
  }
  function scheduleStaleness(){
    clearTimeout(staleTimer);
    if(sample&&!document.hidden&&visible){staleTimer=setTimeout(()=>{render();},Math.max(100,staleAfter-(Date.now()-measured)+20));}
  }
  function update(data){
    if(!data||!['readLatencyMs','dataHz','cameraHz','renderFps'].every(key=>valid(data[key],100000)))return false;
    if(data.snapshotSlots!==undefined&&(!valid(data.snapshotSlots,1024)||data.snapshotSlots<1||!Number.isInteger(data.snapshotSlots)))return false;
    const stamp=data.timestamp===undefined?Date.now():Date.parse(data.timestamp);
    if(!Number.isFinite(stamp)||stamp>Date.now()+5000)return false;
    sample={readLatencyMs:data.readLatencyMs,dataHz:data.dataHz,cameraHz:data.cameraHz,renderFps:data.renderFps,snapshotSlots:data.snapshotSlots??8};
    received=Date.now();measured=stamp;failed=false;history.push(sample.readLatencyMs);if(history.length>40)history.shift();
    render();scheduleStaleness();return true;
  }
  const shouldPoll=()=>endpoint&&visible&&!document.hidden&&!site.dialogOpen;
  async function poll(){
    clearTimeout(timer);if(!shouldPoll()||active)return;
    active=true;controller=new AbortController();const current=controller;
    const timeout=setTimeout(()=>current.abort(),4000);
    try{
      const response=await fetch(endpoint,{signal:current.signal,cache:'no-store',credentials:'omit',headers:{Accept:'application/json'}});
      if(!response.ok)throw Error('HTTP '+response.status);
      if(!update(await response.json()))throw Error('Invalid telemetry');
    }catch{if(shouldPoll()){failed=true;render();}}
    finally{clearTimeout(timeout);active=false;if(controller===current)controller=null;if(shouldPoll())timer=setTimeout(poll,interval);}
  }
  function refresh(){
    clearTimeout(timer);clearTimeout(staleTimer);
    if(shouldPoll()){render();poll();scheduleStaleness();}else{controller?.abort();if(sample){render();scheduleStaleness();}}
  }
  if('IntersectionObserver' in window){new IntersectionObserver(entries=>{visible=entries[0].isIntersecting;refresh();},{threshold:.01}).observe(panel);}else{visible=true;}
  document.addEventListener('kevq:language',render);
  ['kevq:visibility','kevq:dialog'].forEach(name=>document.addEventListener(name,refresh));
  document.addEventListener('kevq:dma-sample',event=>update(event.detail));
  window.KevqDMA=Object.freeze({updateTelemetry:update,get state(){return panel.dataset.state;},get lastReceived(){return received;}});
  render();refresh();
})();
