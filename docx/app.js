/* KevqDMA presentation layer. This site does not connect to the native application. */
(() => {
  'use strict';
  const $ = (s, root=document) => root.querySelector(s);
  const $$ = (s, root=document) => [...root.querySelectorAll(s)];
  const codes = {ru:'ru-RU',en:'en-US',de:'de-DE',es:'es-ES',vi:'vi-VN',zh:'zh-CN'};
  const storage = {
    get(key){try{return localStorage.getItem(key);}catch{return null;}},
    set(key,value){try{localStorage.setItem(key,value);}catch{/* Private browsing may deny storage. */}}
  };
  const baseline=JSON.parse($('#runtimeStrings').textContent);
  $$('[data-t]').forEach(el=>{baseline[el.dataset.t]=el.textContent;});
  $$('[data-label]').forEach(el=>{baseline[el.dataset.label]=el.getAttribute('aria-label');});
  $$('[data-meta]').forEach(el=>{baseline[el.dataset.meta]=el.content;});
  window.KEVQ_LOCALES=Object.assign(window.KEVQ_LOCALES||{},{ru:baseline});
  let language='ru', languageRequest=0, toastTimer=0, currentDialog=null, dialogTrigger=null;
  const requests=new Map();
  const t=key=>window.KEVQ_LOCALES[language][key]??baseline[key]??key;
  const systemMotion=matchMedia('(prefers-reduced-motion:reduce)');
  let savedMotion=storage.get('kevq.v15.motion');
  let paused=savedMotion==='paused'||(!savedMotion&&(systemMotion.matches||navigator.connection?.saveData===true));
  const visibleArts=new Map();
  let activeNode='snapshot';
  const event=(name,detail)=>document.dispatchEvent(new CustomEvent(name,{detail}));
  function notice(message){const el=$('#toast');el.textContent=message;el.hidden=false;clearTimeout(toastTimer);toastTimer=setTimeout(()=>{el.hidden=true;},3800);}
  function updateMotion(){
    document.documentElement.dataset.motion=paused?'paused':'full';
    const b=$('#motionToggle'),name=t(paused?'scene.play':'scene.pause');
    b.setAttribute('aria-pressed',String(paused));b.setAttribute('aria-label',name);b.title=name;
    $('path',b).setAttribute('d',paused?'m8 5 11 7-11 7Z':'M9 5v14M15 5v14');
    event('kevq:motion',{paused});
  }
  function setMotion(value,persist=true){paused=Boolean(value);if(persist){savedMotion=paused?'paused':'full';storage.set('kevq.v15.motion',savedMotion);}updateMotion();}
  $('#motionToggle').addEventListener('click',()=>setMotion(!paused));
  systemMotion.addEventListener('change',()=>{if(!savedMotion)setMotion(systemMotion.matches||navigator.connection?.saveData===true,false);});
  function updateArtActivity(){
    visibleArts.forEach((visible,el)=>{el.dataset.active=String(visible&&(!currentDialog||currentDialog.contains(el)));});
    event('kevq:artactivity',{});
  }
  function updateNode(){
    const keys=['data','camera','snapshot','render','web','recovery'];
    const prefix=activeNode==='recovery'?'runtime':'arch';
    $('#nodeTitle').textContent=t(`${prefix}.${activeNode}`);
    $('#nodeDescription').textContent=t(`${prefix}.${activeNode}.desc`);
    $('#nodeIndex').textContent=`0${keys.indexOf(activeNode)+1} / 06`;
    $$('[data-node]').forEach(el=>el.setAttribute('aria-pressed',String(el.dataset.node===activeNode)));
  }
  function getLocale(code){
    if(window.KEVQ_LOCALES[code])return Promise.resolve();
    if(requests.has(code))return requests.get(code);
    const promise=new Promise((resolve,reject)=>{
      const script=document.createElement('script');script.src=new URL(`locales/${code}.js`,document.baseURI).href;
      script.onload=()=>{if(window.KEVQ_LOCALES[code])resolve();else{requests.delete(code);reject(Error('Missing locale'));}};
      script.onerror=()=>{requests.delete(code);script.remove();reject(Error('Locale unavailable'));};
      document.head.append(script);
    });
    requests.set(code,promise);return promise;
  }
  async function changeLanguage(code,persist=true){
    if(!Object.hasOwn(codes,code))return false;
    const request=++languageRequest;$('#languagePicker').setAttribute('aria-busy','true');
    try{
      await getLocale(code);if(request!==languageRequest)return false;
      const dictionary=window.KEVQ_LOCALES[code];
      if(!Object.keys(baseline).every(k=>typeof dictionary[k]==='string'&&dictionary[k].length))throw Error('Incomplete dictionary');
      language=code;document.documentElement.lang=code==='zh'?'zh-CN':code;
      $$('[data-t]').forEach(el=>{el.textContent=t(el.dataset.t);});
      $$('[data-label]').forEach(el=>el.setAttribute('aria-label',t(el.dataset.label)));
      $$('[data-meta]').forEach(el=>el.content=t(el.dataset.meta));
      $('#currentLanguage').textContent=code.toUpperCase();
      $$('[data-lang]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.lang===code)));
      if(persist)storage.set('kevq.language',code);
      try{const u=new URL(location.href);u.searchParams.set('lang',code);history.replaceState(null,'',u);}catch{/* file:// may restrict history. */}
      updateMotion();updateNode();event('kevq:language',{language});return true;
    }catch{if(request===languageRequest)notice(t('error.language'));return false;}
    finally{if(request===languageRequest)$('#languagePicker').removeAttribute('aria-busy');}
  }
  $$('[data-lang]').forEach(button=>button.addEventListener('click',()=>{
    $('#languagePicker').open=false;changeLanguage(button.dataset.lang);$('#languagePicker summary').focus({preventScroll:true});
  }));
  const nav=$('#mobileNav'),menu=$('#menuToggle');
  function closeNav(focus=false){const wasOpen=!nav.hidden;nav.hidden=true;menu.setAttribute('aria-expanded','false');if(focus&&wasOpen)menu.focus();}
  menu.addEventListener('click',()=>{nav.hidden=!nav.hidden;menu.setAttribute('aria-expanded',String(!nav.hidden));$('#languagePicker').open=false;});
  $$('a',nav).forEach(el=>el.addEventListener('click',()=>closeNav()));
  document.addEventListener('click',e=>{
    if(!$('#languagePicker').contains(e.target))$('#languagePicker').open=false;
    if(!nav.contains(e.target)&&!menu.contains(e.target))closeNav();
  });
  document.addEventListener('keydown',e=>{
    // Only native menu/dialog dismissal. No website hotkeys or command search.
    if(e.key!=='Escape')return;
    closeNav(true);
    if($('#languagePicker').open){$('#languagePicker').open=false;$('#languagePicker summary').focus();}
  });
  matchMedia('(min-width:1001px)').addEventListener('change',e=>{if(e.matches)closeNav();});
  $$('[data-filter]').forEach(button=>button.addEventListener('click',()=>{
    const filter=button.dataset.filter;
    $$('[data-filter]').forEach(el=>el.setAttribute('aria-pressed',String(el===button)));
    $$('.feature-card').forEach(card=>{card.hidden=filter!=='all'&&card.dataset.module!==filter;card.dataset.pending='false';});
    event('kevq:layout',{});
  }));
  function openDialog(id,trigger=null){
    const dialog=document.getElementById(id);
    if(!dialog||typeof dialog.showModal!=='function')return;
    if(currentDialog)currentDialog.close();
    currentDialog=dialog;dialogTrigger=trigger;
    document.documentElement.classList.add('dialog-open');
    dialog.showModal();dialog.scrollTop=0;
    $('.close-dialog',dialog).focus({preventScroll:true});
    updateArtActivity();event('kevq:dialog',{open:true});
  }
  $$('[data-open]').forEach(el=>el.addEventListener('click',e=>{e.preventDefault();openDialog(el.dataset.open,el);}));
  $$('.feature-dialog').forEach(dialog=>{
    $('.close-dialog',dialog).addEventListener('click',()=>dialog.close());
    dialog.addEventListener('click',e=>{
      if(e.target!==dialog)return;const r=dialog.getBoundingClientRect();
      if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)dialog.close();
    });
    dialog.addEventListener('close',()=>{
      if(currentDialog!==dialog||dialog.open)return;currentDialog=null;document.documentElement.classList.remove('dialog-open');
      updateArtActivity();event('kevq:dialog',{open:false});dialogTrigger?.focus({preventScroll:true});dialogTrigger=null;
    });
  });
  $$('[data-layer]').forEach(button=>button.addEventListener('click',()=>{
    const on=button.getAttribute('aria-pressed')!=='true';button.setAttribute('aria-pressed',String(on));
    const selector={box:'.box-layer',skeleton:'.skeleton-layer',health:'.health-fill'}[button.dataset.layer];
    $$(selector,$('#detail-visuals')).forEach(el=>el.style.opacity=on?'':'0');
  }));
  $$('[data-node]').forEach(b=>b.addEventListener('click',()=>{activeNode=b.dataset.node;updateNode();}));
  $('#copyContact').addEventListener('click',async()=>{
    try{
      if(navigator.clipboard?.writeText){await navigator.clipboard.writeText('@ne_sravnim');}
      else{
        const a=document.createElement('textarea');a.value='@ne_sravnim';a.style.cssText='position:fixed;left:-9999px';document.body.append(a);a.select();
        const success=document.execCommand('copy');a.remove();if(!success)throw Error('Copy denied');
      }
      notice(t('contact.copied'));
    }catch{notice(t('error.copy'));}
  });
  if('IntersectionObserver' in window){
    const artObserver=new IntersectionObserver(entries=>{entries.forEach(e=>visibleArts.set(e.target,e.isIntersecting));updateArtActivity();},{threshold:0.02});
    $$('.animated').forEach(el=>{visibleArts.set(el,false);artObserver.observe(el);});
    const revealObserver=new IntersectionObserver(entries=>entries.forEach(e=>{if(e.isIntersecting){e.target.dataset.pending='false';revealObserver.unobserve(e.target);}}),{threshold:.045,rootMargin:'0px 0px -10px 0px'});
    $$('.reveal').forEach(el=>{if(!paused&&el.getBoundingClientRect().top>innerHeight)el.dataset.pending='true';revealObserver.observe(el);});
    const sections=new IntersectionObserver(entries=>{entries.forEach(e=>{if(e.isIntersecting)$$('.desktop-nav a').forEach(a=>a.classList.toggle('is-current',a.hash==='#'+e.target.id));});},{rootMargin:'-15% 0px -55% 0px',threshold:0});
    ['features','showcase','architecture','quickstart'].forEach(id=>sections.observe(document.getElementById(id)));
  }else{$$('.animated').forEach(el=>el.dataset.active='true');}
  let pageHeight=0,progressFrame=0;
  function measure(){pageHeight=Math.max(1,document.documentElement.scrollHeight-innerHeight);}
  function progress(){progressFrame=0;$('#pageProgress').style.transform=`scaleX(${Math.min(1,Math.max(0,scrollY/pageHeight))})`;}
  window.addEventListener('scroll',()=>{if(!progressFrame)progressFrame=requestAnimationFrame(progress);},{passive:true});
  window.addEventListener('resize',()=>{measure();progress();},{passive:true});
  new ResizeObserver(()=>{measure();progress();}).observe(document.body);
  document.addEventListener('visibilitychange',()=>{
    document.documentElement.dataset.visibility=document.hidden?'hidden':'visible';
    event('kevq:visibility',{hidden:document.hidden});
  });
  window.KevqSite=Object.freeze({t,changeLanguage,openDialog,setMotion,
    get language(){return language;},get locale(){return codes[language];},get paused(){return paused;},get dialogOpen(){return Boolean(currentDialog);}
  });
  document.documentElement.dataset.enhanced='true';updateMotion();updateNode();measure();progress();
  const query=new URL(location.href).searchParams.get('lang');
  const browser=(navigator.languages?.[0]||navigator.language||'ru').toLowerCase().split('-')[0];
  const preferred=[query,storage.get('kevq.language'),browser,'ru'].find(k=>Object.hasOwn(codes,k));
  changeLanguage(preferred,false);
})();
