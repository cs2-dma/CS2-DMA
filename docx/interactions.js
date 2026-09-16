/* One lightweight clock drives the target and its dotted path from the same coordinates. */
(() => {
  'use strict';
  const site=window.KevqSite;
  if(!site)return;
  const items=[...document.querySelectorAll('.target-sync')].map(group=>({
    group,path:group.previousElementSibling,svg:group.closest('svg'),dialog:group.closest('dialog'),
    origin:group.dataset.origin.split(',').map(Number),point:group.dataset.point.split(',').map(Number),
    mini:group.dataset.mini==='true',time:0,visible:false
  }));
  let raf=0,last=0,frames=0;
  const smooth=t=>t*t*(3-2*t);
  const canRun=item=>item.visible&&!document.hidden&&!site.paused&&(!site.dialogOpen||item.dialog?.open);
  function draw(item){
    const phase=(item.time/(item.mini?10000:8000))%1;
    const stops=[[0,0,0],[.35,-19,17],[.75,12,9],[1,0,0]];
    let index=0;while(index<2&&phase>stops[index+1][0])index++;
    const a=stops[index],b=stops[index+1],k=smooth((phase-a[0])/(b[0]-a[0])),scale=item.mini?.48:1;
    const dx=(a[1]+(b[1]-a[1])*k)*scale,dy=(a[2]+(b[2]-a[2])*k)*scale;
    const [ox,oy]=item.origin,x=item.point[0]+dx,y=item.point[1]+dy;
    item.group.setAttribute('transform',`translate(${dx.toFixed(4)} ${dy.toFixed(4)})`);
    item.path.setAttribute('d',`M${ox} ${oy} Q${((ox+x)/2).toFixed(4)} ${(y-8*scale).toFixed(4)} ${x.toFixed(4)} ${y.toFixed(4)}`);
    item.path.setAttribute('stroke-dashoffset',String(-item.time*.007));
  }
  function tick(now){
    raf=0;const active=items.filter(canRun);
    if(!active.length){last=0;return;}
    const dt=last?Math.min(50,now-last):0;last=now;
    active.forEach(item=>{item.time+=dt;draw(item);});frames++;
    raf=requestAnimationFrame(tick);
  }
  function refresh(){
    if(items.some(canRun)){if(!raf){last=0;raf=requestAnimationFrame(tick);}}
    else{cancelAnimationFrame(raf);raf=0;last=0;}
  }
  items.forEach(draw);
  if('IntersectionObserver' in window){
    const observer=new IntersectionObserver(entries=>{
      entries.forEach(entry=>{items.filter(item=>item.svg===entry.target).forEach(item=>item.visible=entry.isIntersecting);});refresh();
    },{threshold:.01});
    [...new Set(items.map(item=>item.svg))].forEach(svg=>observer.observe(svg));
  }else{items.forEach(item=>item.visible=true);}
  ['kevq:motion','kevq:visibility','kevq:dialog','kevq:artactivity','kevq:layout'].forEach(name=>document.addEventListener(name,refresh));
  window.KevqTargeting=Object.freeze({get frames(){return frames;},get running(){return Boolean(raf);},get count(){return items.length;}});
  refresh();
})();
