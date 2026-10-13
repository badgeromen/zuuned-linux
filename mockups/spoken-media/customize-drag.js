// Extension of the visual study only. User files stay in browser memory.
// A cover draft owns its object URL until Apply transfers ownership to the map.
const customCovers=new Map();
let coverDraft=null,coverRevision=0,activeDrag=null;
const coverNames={moonbound:'Moonbound',frequency:'Off Frequency',night:'Night Shift',field:'Field Notes',signal:'The Last Signal',orchard:'The Glass Orchard',city:'A City Without Sleep',atlas:'Atlas of Elsewhere'};
const defaultCover=id=>'assets/'+id+(id==='moonbound'?'.jpg':'.svg');
const isBookCover=id=>books.some(b=>b.cover===id);
function applyCoverSurfaces(){
 document.querySelectorAll('[data-art-id]').forEach(el=>{
  const id=el.dataset.artId,src=customCovers.get(id)||defaultCover(id);
  if(el.id==='player-cover')el.style.backgroundImage=`url("${src}")`;
  else {const img=el.querySelector('img');if(img)img.src=src;el.classList.toggle('has-custom-cover',customCovers.has(id));}
 });
 document.querySelectorAll('img[data-follow-cover]').forEach(img=>img.src=customCovers.get(img.dataset.followCover)||defaultCover(img.dataset.followCover));
}
function decorateSpoken(){
 $('#main .continue>img')?.setAttribute('data-follow-cover','moonbound');
 const player=$('#player-cover');if(!player.dataset.artId)player.dataset.artId='moonbound';
 $('#sheet-content .preview img')?.setAttribute('data-follow-cover','moonbound');
 document.querySelectorAll('.card .cover[data-art-id]').forEach(el=>{
  const card=el.closest('.card');if(!card.querySelector('.cover-edit')){
   const button=document.createElement('button');button.className='cover-edit';button.dataset.customize=el.dataset.artId;button.textContent='customize';button.setAttribute('aria-label','Customize '+coverNames[el.dataset.artId]+' cover');card.append(button);
  }
 });
 const hero=$('.detail-hero');if(hero&&!hero.querySelector('[data-customize],[data-action="book-edit"]')){
  const id=hero.querySelector('[data-art-id]').dataset.artId;
  const button=document.createElement('button');button.dataset.customize=id;button.textContent='customize cover';hero.querySelector('.buttons').append(button);
 }
 const bar=$('.select-bar');if(bar){const hint=bar.querySelector('span');if(!selected.size)hint.textContent=connected?'Select episodes, then drag them to the Zune or use the buttons.':'Select or download episodes. Connect a Zune to queue them.';}
 applyCoverSurfaces();
}
function discardCoverDraft(){
 coverRevision++;
 if(coverDraft?.ownedUrl)URL.revokeObjectURL(coverDraft.ownedUrl);
 coverDraft=null;$('#sheet').classList.remove('cover-sheet');
}
function coverPreview(){
 if(!coverDraft)return;
 $('#draft-cover').src=coverDraft.src;
 $('#cover-source').textContent=coverDraft.manual?'Your custom cover · stays yours on refresh':'Automatic cover · feed or file artwork';
 $('#cover-apply').disabled=coverDraft.loading;
 $('#cover-error').textContent='';
}
function openCoverEditor(id){
 if(!Object.hasOwn(coverNames,id))return;
 // One dialog at a time. Dismissal never applies another item's draft.
 if($('#sheet').open)$('#sheet').close();
 discardCoverDraft();
 coverDraft={id,src:customCovers.get(id)||defaultCover(id),manual:customCovers.has(id),ownedUrl:null,loading:false};
 $('#sheet').classList.add('cover-sheet');
 modal(`<div class="eyebrow">YOUR COLLECTION / YOUR CALL</div><h2>Make it yours.</h2><div class="cover-editor"><div><div class="cover-preview ${isBookCover(id)?'book-cover':''}"><img id="draft-cover" alt="Draft cover for ${esc(coverNames[id])}"></div><h3>${esc(coverNames[id])}</h3><small>${isBookCover(id)?'AUDIOBOOK':'PODCAST SHOW'}</small></div><div><div class="tabs"><button class="active" type="button">artwork</button></div><h3>Give it your cover.</h3><p>Use your own image. Your choice stays put when the feed refreshes or files are rescanned.</p><div id="cover-drop" tabindex="0" role="button" aria-label="Choose or drop a cover image"><strong>Drop a cover here</strong><span>or click to choose an image</span><small>JPEG, PNG or WebP · up to 10 MiB</small></div><input id="cover-file" type="file" accept="image/jpeg,image/png,image/webp" hidden><p id="cover-source"></p><p class="caption">${isBookCover(id)?'A book':'A show'} cover carries through the library and player. Device artwork transfer is a separate implementation step.</p><div id="cover-error" class="error" role="alert"></div></div></div><div class="cover-editor-footer"><button data-cover-action="reset">reset to automatic</button><span>Changes are staged until Apply.</span><button data-cover-action="cancel">cancel</button><button id="cover-apply" class="primary" data-cover-action="apply">apply changes ↗</button></div>`);
 coverPreview();
 $('#cover-file').addEventListener('change',e=>stageCoverFile(e.target.files[0]));
 $('#cover-drop').addEventListener('click',()=>$('#cover-file').click());
 $('#cover-drop').addEventListener('keydown',e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();$('#cover-file').click();}});
}
async function stageCoverFile(file){
 if(!coverDraft||!file)return;
 const draft=coverDraft,revision=++coverRevision;
 if(!['image/jpeg','image/png','image/webp'].includes(file.type)||file.size>10*1024*1024||!file.size){
  draft.loading=false;$('#cover-apply').disabled=false;$('#cover-error').textContent='Choose a JPEG, PNG or WebP image up to 10 MiB.';return;
 }
 draft.loading=true;$('#cover-apply').disabled=true;
 const url=URL.createObjectURL(file),img=new Image();img.src=url;
 try{
  await img.decode();
  if(coverDraft!==draft||revision!==coverRevision){URL.revokeObjectURL(url);return;}
  if(!img.naturalWidth||!img.naturalHeight||img.naturalWidth*img.naturalHeight>40000000)throw Error('Image dimensions exceed the preview limit.');
  if(draft.ownedUrl)URL.revokeObjectURL(draft.ownedUrl);
  draft.ownedUrl=url;draft.src=url;draft.manual=true;draft.loading=false;coverPreview();
 }catch{
  URL.revokeObjectURL(url);
  if(coverDraft===draft&&revision===coverRevision){draft.loading=false;$('#cover-apply').disabled=false;$('#cover-error').textContent='Could not open that image. Try another cover.';}
 }
}
function finishCover(action){
 if(!coverDraft)return;
 if(action==='cancel'){$('#sheet').close();discardCoverDraft();return;}
 if(action==='reset'){
  ++coverRevision;if(coverDraft.ownedUrl)URL.revokeObjectURL(coverDraft.ownedUrl);
  coverDraft.ownedUrl=null;coverDraft.src=defaultCover(coverDraft.id);coverDraft.manual=false;coverDraft.loading=false;coverPreview();return;
 }
 if(action==='apply'&&!coverDraft.loading){
  const d=coverDraft,old=customCovers.get(d.id);
  if(d.manual)customCovers.set(d.id,d.src);else customCovers.delete(d.id);
  if(old&&old!==customCovers.get(d.id))URL.revokeObjectURL(old);
  d.ownedUrl=null;$('#sheet').close();discardCoverDraft();render();toast('Cover applied to this preview. Your media files are unchanged.');
 }
}
$('#sheet').addEventListener('close',()=>{if(!$('#sheet').open)discardCoverDraft();});
$('#sheet').addEventListener('cancel',discardCoverDraft);
document.addEventListener('click',e=>{
 const custom=e.target.closest('[data-customize]');if(custom)openCoverEditor(custom.dataset.customize);
 const action=e.target.closest('[data-cover-action]');if(action)finishCover(action.dataset.coverAction);
});
document.addEventListener('contextmenu',e=>{
 const art=e.target.closest('[data-art-id]');if(art&&art.id!=='player-cover'&&!$('#sheet').open){e.preventDefault();openCoverEditor(art.dataset.artId);}
});
const dragMime='application/x-zuuned-preview-episodes';
function clearDrag(){activeDrag=null;$('.device').classList.remove('drop-ready','drop-over');$('#drag-caption')?.remove();$('#drag-chip')?.remove();}
function queueDragged(ids){
 if(!connected){toast('Zune disconnected. Nothing was added.');return;}
 let added=0,skipped=0;
 for(const id of ids){const ep=episodes.find(x=>x.id===id);if(!ep)continue;if(ep.device||queue.some(x=>x.id===id)){skipped++;continue;}queue.push(ep);added++;}
 render();toast(`${added} added to the preview queue${skipped?'; '+skipped+' already on Zune or queued':''}.`);
}
document.addEventListener('dragstart',e=>{
 const row=e.target.closest('[data-episode]');if(!row)return;
 if(!connected){e.preventDefault();toast('Connect a Zune to drag episodes.');return;}
 const id=Number(row.dataset.episode),ids=selected.has(id)?[...selected]:[id];
 const token=crypto.randomUUID();activeDrag={token,ids};
 e.dataTransfer.setData(dragMime,token);e.dataTransfer.effectAllowed='copy';
 const chip=document.createElement('div');chip.id='drag-chip';chip.textContent=`${ids.length} ${ids.length===1?'episode':'episodes'} → Zune`;document.body.append(chip);e.dataTransfer.setDragImage(chip,18,18);
 const caption=document.createElement('div');caption.id='drag-caption';caption.textContent=`Drop ${ids.length} ${ids.length===1?'episode':'episodes'} to add to Zune`;$('.device').append(caption);$('.device').classList.add('drop-ready');
});
document.addEventListener('dragend',clearDrag);
document.addEventListener('dragover',e=>{
 const zone=e.target.closest('#cover-drop');
 if(zone&&coverDraft&&[...e.dataTransfer.types].includes('Files')){e.preventDefault();e.dataTransfer.dropEffect='copy';zone.classList.add('hover');return;}
 const device=e.target.closest('.device');
 if(device&&activeDrag&&[...e.dataTransfer.types].includes(dragMime)){
  e.preventDefault();e.dataTransfer.dropEffect=connected?'copy':'none';device.classList.toggle('drop-over',connected);
 }
});
document.addEventListener('dragleave',e=>{const zone=e.target.closest('#cover-drop,.device');if(zone&&!zone.contains(e.relatedTarget))zone.classList.remove('hover','drop-over');});
document.addEventListener('drop',e=>{
 const zone=e.target.closest('#cover-drop');
 if(zone&&coverDraft){e.preventDefault();zone.classList.remove('hover');const files=e.dataTransfer.files;if(files.length!==1){$('#cover-error').textContent='Drop one cover image at a time.';return;}stageCoverFile(files[0]);return;}
 if(e.target.closest('.device')&&activeDrag){
  e.preventDefault();const payload=activeDrag;if(e.dataTransfer.getData(dragMime)===payload.token)queueDragged(payload.ids);clearDrag();return;
 }
 // External images only go to the editor's explicit target, never navigate away.
 if([...e.dataTransfer.types].includes('Files'))e.preventDefault();
});
window.addEventListener('beforeunload',()=>{discardCoverDraft();for(const url of customCovers.values())URL.revokeObjectURL(url);});
decorateSpoken();
