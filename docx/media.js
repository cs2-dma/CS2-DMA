/* Progressive media gallery. Images are lazy; video bytes load only in the viewer. */
(() => {
  'use strict';
  const site=window.KevqSite;if(!site)return;
  const $=(s,r=document)=>r.querySelector(s),$$=(s,r=document)=>[...r.querySelectorAll(s)];
  const config=window.KEVQ_MEDIA||{},viewer=$('#mediaViewer'),stage=$('#mediaViewerStage');
  const cards=$$('[data-media-id]'),keys=cards.map(card=>card.dataset.mediaId);
  let filter='all',expanded=false,selected=null,loadGeneration=0,zoom=false;
  const tr=(el,key)=>{el.dataset.t=key;el.textContent=site.t(key);};
  function asset(value){
    if(typeof value!=='string'||!value.trim())return '';
    try{const u=new URL(value,document.baseURI);return ['http:','https:','file:'].includes(u.protocol)?u.href:'';}catch{return '';}
  }
  function entry(key){const c=config[key]||{};return {src:asset(c.src),poster:asset(c.poster),kind:cards.find(card=>card.dataset.mediaId===key).dataset.kind};}
  function applyFilter(){
    cards.forEach(card=>{card.hidden=(filter!=='all'&&card.dataset.kind!==filter)||(filter==='all'&&!expanded&&card.dataset.extra==='true');card.dataset.pending='false';});
    $$('[data-media-filter]').forEach(button=>button.setAttribute('aria-pressed',String(button.dataset.mediaFilter===filter)));
    const more=$('#mediaMore');more.hidden=filter!=='all';more.setAttribute('aria-expanded',String(expanded));tr(more,expanded?'media.less':'media.more');
    document.dispatchEvent(new CustomEvent('kevq:layout'));
  }
  $$('[data-media-filter]').forEach(button=>button.addEventListener('click',()=>{filter=button.dataset.mediaFilter;applyFilter();}));
  $('#mediaMore').addEventListener('click',()=>{expanded=!expanded;applyFilter();});
  function prepareCard(card){
    const key=card.dataset.mediaId,e=entry(key),status=$('[data-media-status]',card),label=$('.media-open-label',card);
    if(!e.src)return;
    card.dataset.ready='true';tr(status,e.kind==='video'?'media.video':'media.photo');tr(label,'media.open');
    const preview=e.kind==='photo'?e.src:e.poster;
    if(!preview)return;
    const img=new Image();img.loading='lazy';img.decoding='async';img.width=800;img.height=500;img.alt='';img.className='media-asset';
    img.addEventListener('load',()=>card.dataset.poster='true');
    img.addEventListener('error',()=>{img.remove();card.dataset.poster='false';});
    img.src=preview;$('.media-poster',card).append(img);
  }
  cards.forEach(prepareCard);
  function clearStage(){
    ++loadGeneration;$$('video',stage).forEach(video=>{video.pause();video.removeAttribute('src');video.load();});
    stage.replaceChildren();stage.classList.remove('is-zoomed');zoom=false;$('#mediaZoom').hidden=true;
  }
  function emptyState(failed=false){
    const box=document.createElement('div');box.className='media-empty';
    const icon=document.createElement('span');icon.className='media-empty-icon';icon.textContent=entry(selected).kind==='video'?'\u25b7':'\u25a1';icon.setAttribute('aria-hidden','true');
    const tag=document.createElement('p');tag.className='eyebrow';tr(tag,failed?'media.failed':'media.pending');
    const copy=document.createElement('p');tr(copy,failed?'media.failedDesc':`media.${selected}.shot`);
    box.append(icon,tag,copy);stage.replaceChildren(box);
  }
  function updateText(){
    if(!selected)return;
    $('#mediaViewerTitle').textContent=site.t(`media.${selected}.title`);
    $('#mediaViewerDescription').textContent=site.t(`media.${selected}.desc`);
    const e=entry(selected);$('#mediaViewerType').textContent=site.t(`media.${e.kind}`)+' / '+String(keys.indexOf(selected)+1).padStart(2,'0')+' / 09';
    tr($('#mediaZoom'),zoom?'media.fit':'media.zoom');
    const img=$('img',stage);if(img)img.alt=site.t(`media.${selected}.title`);
    const video=$('video',stage);if(video)video.setAttribute('aria-label',site.t(`media.${selected}.title`));
  }
  function show(key){
    if(!keys.includes(key))return;
    clearStage();selected=key;updateText();
    const e=entry(key),generation=loadGeneration;
    if(!e.src){emptyState();return;}
    if(e.kind==='video'){
      const video=document.createElement('video');video.controls=true;video.playsInline=true;video.preload='metadata';
      video.setAttribute('aria-label',site.t(`media.${key}.title`));if(e.poster)video.poster=e.poster;
      video.addEventListener('error',()=>{if(generation===loadGeneration){video.pause();video.removeAttribute('src');emptyState(true);}});
      // No autoplay, background video, loop or preloading of unopened clips.
      video.src=e.src;stage.append(video);
    }else{
      const img=new Image();img.decoding='async';img.alt=site.t(`media.${key}.title`);
      img.addEventListener('load',()=>{if(generation===loadGeneration)$('#mediaZoom').hidden=false;});
      img.addEventListener('error',()=>{if(generation===loadGeneration){$('#mediaZoom').hidden=true;emptyState(true);}});
      img.src=e.src;stage.append(img);
    }
  }
  $$('[data-media-open]').forEach(button=>button.addEventListener('click',()=>{show(button.dataset.mediaOpen);site.openDialog('mediaViewer',button);}));
  function navigate(delta){
    const visible=cards.filter(card=>!card.hidden).map(card=>card.dataset.mediaId);if(!visible.length)return;
    show(visible[(Math.max(0,visible.indexOf(selected))+delta+visible.length)%visible.length]);
  }
  $('#mediaPrev').addEventListener('click',()=>navigate(-1));$('#mediaNext').addEventListener('click',()=>navigate(1));
  $('#mediaZoom').addEventListener('click',()=>{zoom=!zoom;stage.classList.toggle('is-zoomed',zoom);updateText();});
  viewer.addEventListener('click',()=>{if(!viewer.open)clearStage();});
  viewer.addEventListener('cancel',clearStage);
  viewer.addEventListener('close',()=>{if(viewer.open)return;clearStage();selected=null;});
  document.addEventListener('kevq:visibility',()=>{if(document.hidden)$$('video',stage).forEach(video=>video.pause());});
  document.addEventListener('kevq:language',()=>{applyFilter();updateText();});
  applyFilter();
})();
