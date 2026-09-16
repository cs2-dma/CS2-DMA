/* Set src/poster after adding your own files to assets/media/.
   Empty slots make NO file requests. No upload service or external account is needed.
   Example: menu: {kind:'photo', src:'assets/media/menu.webp'}
   Video: aim: {kind:'video', src:'assets/media/aim.mp4', poster:'assets/media/aim-poster.webp'} */
window.KEVQ_MEDIA={
  menu:     {kind:'photo',src:'',poster:''},
  aim:      {kind:'video',src:'',poster:''},
  webradar: {kind:'video',src:'',poster:''},
  bomb:     {kind:'photo',src:'',poster:''},
  metrics:  {kind:'photo',src:'',poster:''},
  esp:      {kind:'photo',src:'',poster:''},
  grenades: {kind:'video',src:'',poster:''},
  profiles: {kind:'photo',src:'',poster:''},
  hardware: {kind:'photo',src:'',poster:''}
};
/* These are a presentation/target profile, NOT Captain factory specifications or
   measured performance. An empty endpoint never polls and never invents samples.
   Connect a trusted JSON exporter only when actual measurements are available. */
window.KEVQ_DMA_CONFIG={
  endpoint:'',intervalMs:2000,staleAfterMs:10000,
  targets:{readLatencyMs:2.3,dataHz:300,cameraHz:300,renderFps:240,snapshotSlots:8}
};
