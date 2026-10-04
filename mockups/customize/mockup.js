/* Standalone design study. All media, results, and edits are local demo data.
 * No network calls, application settings, library DB, or device access.
 * Each specimen owns a saved copy and a draft; style changes preserve drafts.
 */
(() => {
  'use strict';
  const $ = s => document.querySelector(s);
  const $$ = s => [...document.querySelectorAll(s)];
  const copy = value => structuredClone(value);
  const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const albumArt = 'assets/after-the-last-train.png';
  const artistArt = 'assets/artist.png';
  const seriesArt = 'assets/series.png';
  const directions = {
    sleeve: ['01 / SLEEVE', 'Open the sleeve. Make it yours.', 'Big artwork. Quiet liner notes. A little ink on the edges.', 'An open record sleeve, floating above your collection.', 'Art lifts into place. The tools hold still.'],
    afterhours: ['02 / AFTER HOURS', 'Let the artwork set the mood.', 'A darker room. An immersive backdrop. The collection becomes the atmosphere.', 'A late-night listening room, lit by the artwork itself.', 'A still backdrop and soft fades keep the edit unhurried.'],
    bootleg: ['03 / BOOTLEG', 'Leave your fingerprints on it.', 'Taped sleeves. Offset print. A little more beautiful disorder.', 'A personal mixtape, a gig flyer, the record you wore out.', 'Loose artwork, anchored controls. The mess has a purpose.']
  };
  const originals = {
    album: {title:'After the Last Train',artist:'Soft Static',year:'2008',genre:'Shoegaze / alternative',overview:'Eleven songs for the way home. Recorded after midnight, played a little too loud.',art:albumArt,filter:'none',artName:'original sleeve',manual:false,custom:false,matchId:0},
    artist: {title:'Alex Mercer',artist:'Composer / producer',year:'1987',genre:'Ambient / electronic',overview:'Small rooms. Old synthesizers. Music for places that only exist in your head.',art:artistArt,filter:'none',artName:'original portrait',manual:false,custom:false,matchId:0},
    movie: {title:'The Last Broadcast',artist:'A film by Robin Hale',year:'2009',genre:'Drama / science fiction',overview:'One voice is still on the air. Somewhere across the sleeping city, someone is listening.',art:albumArt,filter:'hue-rotate(315deg) saturate(.65)',artName:'theatrical poster',manual:false,custom:false,matchId:0},
    series: {title:'Midnight Signal: Abridged',artist:'A fan edit by Night Shift',year:'2016',genre:'Anime / science fiction',season:'1',episode:'3',episodeTitle:'A perfectly ordinary night',overview:'The city, the signal, and a very questionable plan. Our own cut of the story.',art:seriesArt,filter:'none',artName:'original poster',manual:true,custom:false,matchId:null}
  };
  const fields = {
    album: [['title','album title','wide'],['artist','album artist','wide'],['year','year'],['genre','genre'],['overview','liner notes','wide','textarea']],
    artist: [['title','artist name','wide'],['artist','known for','wide'],['year','born'],['genre','genre'],['overview','about the artist','wide','textarea']],
    movie: [['title','movie title','wide'],['artist','director','wide'],['year','year'],['genre','genre'],['overview','the story','wide','textarea']],
    series: [['title','series title','wide'],['year','year'],['genre','genre'],['season','season'],['episode','episode'],['episodeTitle','episode title','wide'],['overview','the story','wide','textarea']]
  };
  const sources = {
    album:[['covers','cover archive'],['fanart','fanart.tv'],['file','your computer']],
    artist:[['portraits','deezer'],['fanart','fanart.tv'],['file','your computer']],
    movie:[['posters','tmdb'],['fanart','fanart.tv'],['frames','frame grab'],['file','your computer']],
    series:[['posters','tmdb'],['fanart','fanart.tv'],['frames','frame grab'],['file','your computer']]
  };
  const providers = {album:['MusicBrainz','Discogs'],artist:['MusicBrainz','Deezer'],movie:['TMDB'],series:['TMDB']};
  const automatic = copy(originals);
  Object.assign(automatic.series,{title:'Midnight Signal',artist:'Original television series',manual:false,matchId:0});
  const specimens = Object.fromEntries(Object.entries(originals).map(([kind,item]) => [kind,{saved:copy(item),draft:copy(item),source:sources[kind][0][0],tab:'artwork',mode:'manual',provider:providers[kind][0],artSearch:'',matchSearch:''}]));
  let kind = 'album';
  let direction = 'sleeve';
  let toastTimer;
  let fileRequest = 0;
  let open = true;
  const state = () => specimens[kind];
  const draft = () => state().draft;
  const dirty = () => JSON.stringify(state().saved) !== JSON.stringify(draft());
  function toast(message) {
    clearTimeout(toastTimer);
    $('#toast').textContent = message;
    $('#toast').classList.add('shown');
    toastTimer = setTimeout(() => $('#toast').classList.remove('shown'), 3300);
  }
  function updateHash() {
    const params = new URLSearchParams({direction,kind,tab:state().tab});
    history.replaceState(null,'','#'+params);
  }
  function setDirection(next) {
    if (!directions[next]) return;
    direction = next;
    document.body.dataset.direction = next;
    $$('.directions button').forEach(b => b.setAttribute('aria-pressed',String(b.dataset.direction===next)));
    ['direction-label','direction-title','direction-description','note-feel','note-motion'].forEach((id,i) => $('#'+id).textContent=directions[next][i]);
    updateHash();
  }
  function setKind(next) {
    if (!specimens[next]) return;
    fileRequest++;
    kind = next;
    document.body.dataset.kind = next;
    $$('.specimens button').forEach(b => b.setAttribute('aria-pressed',String(b.dataset.kind===next)));
    $('#editor').hidden = false;
    $('#reopen').hidden = true;
    open = true;
    $('#item-kind').textContent = {album:'ALBUM / 001',artist:'ARTIST / 002',movie:'MOVIE / 003',series:'SERIES / 004'}[kind];
    $('#art-query').value = kind==='album' ? draft().artist+' — '+draft().title : draft().title.replace(': Abridged','');
    $('#match-query').value = draft().title;
    $('.pane-heading p').innerHTML = {album:'Something that feels like <em>this</em> record.',artist:'A face for the name. A photo that feels right.',movie:'The poster you would put on your wall.',series:'Borrow a poster. Keep your own story.'}[kind];
    $('#identity-description').textContent = {album:'The right name. The right artist. Your liner notes.',artist:'The person behind the music, in their own words.',movie:'Your cut. Your title. Your version of the story.',series:'A fan edit belongs in your collection, too.'}[kind];
    renderFields();renderSources();renderProviders();renderMatches();setMode(state().mode);setTab(state().tab);renderPreview();
  }
  function setTab(tab) {
    state().tab = tab;
    $('#identity-pane').hidden = tab!=='identity';
    $('#artwork-pane').hidden = tab!=='artwork';
    $$('.pivots button').forEach(b => {const selected=b.dataset.tab===tab;b.setAttribute('aria-selected',String(selected));b.tabIndex=selected?0:-1;});
    updateHash();
  }
  function setMode(mode) {
    state().mode = mode;
    $$('[data-mode]').forEach(b => b.setAttribute('aria-pressed',String(b.dataset.mode===mode)));
    $('#identity-form').hidden = mode!=='manual';
    $('#identity-match').hidden = mode!=='match';
    if(mode==='match') renderMatches();
  }
  function renderFields() {
    $('#identity-fields').innerHTML = fields[kind].map(([key,label,wide,type]) => `<div class="field ${wide==='wide'?'wide':''}"><label for="field-${key}">${esc(label)}</label>${type==='textarea'?`<textarea id="field-${key}" data-field="${key}" rows="2" maxlength="1000">${esc(draft()[key])}</textarea>`:`<input id="field-${key}" data-field="${key}" value="${esc(draft()[key])}" maxlength="140"${['year','season','episode'].includes(key)?' inputmode="numeric"':''}>`}</div>`).join('');
    $$('[data-field]').forEach(input => input.addEventListener('input',() => {
      draft()[input.dataset.field]=input.value;
      draft().manual=true;draft().matchId=null;
      renderPreview();
    }));
  }
  function renderPreview() {
    const d = draft();
    document.documentElement.style.setProperty('--art',`url("${d.art}")`);
    document.documentElement.style.setProperty('--art-filter',d.filter);
    $('.hero-image').style.backgroundImage=`url("${d.art}")`;
    $('.hero-image').style.filter=d.filter;
    $('.hero-image').style.backgroundPosition=d.position || 'center';
    $('#preview-title').textContent=d.title || 'Untitled';
    $('#preview-subtitle').textContent = [d.artist,d.year,{album:'11 tracks',artist:'6 albums',movie:'1h 48m',series:'12 episodes'}[kind]].filter(Boolean).join(' · ');
    $('.cover-name').textContent=d.title.toUpperCase();
    $('.cover-artist').textContent=d.artist.toUpperCase();
    const badge=d.manual?'manual identity':d.custom?'custom artwork':'original';
    $('#status-badge').textContent=badge;
    $('#status-badge').classList.toggle('modified',d.manual||d.custom);
    $('#save-note').textContent=dirty()?'Previewing your changes.':(d.manual||d.custom?'Your picks stay yours.':'Make a change. Make it yours.');
    $('.save-dot').style.background=dirty()?'var(--orange)':'var(--green)';
    $('#apply').disabled=!dirty();
    $('#reset').disabled=JSON.stringify(d)===JSON.stringify(automatic[kind]);
  }
  function candidates() {
    const d=originals[kind];
    const src=state().source;
    if (src==='frames') return [0,1,2,3].map((i) => ({id:`frames-${i}`,title:['00:42','08:16','17:35','24:09'][i],art:d.art,filter:['none','brightness(.75) saturate(.8)','hue-rotate(18deg)','brightness(1.15)'][i],position:['center','30% 60%','70% 20%','center bottom'][i]}));
    const labels = kind==='artist'?['press photo','the studio','on the road','live, 2008']:['original sleeve','night edition','washed out','the blue hour'];
    if (kind==='movie'||kind==='series') labels.splice(0,4,'original poster','international','alternate art','midnight edition');
    const filters=src==='fanart'?['sepia(.3) saturate(.7)','hue-rotate(290deg)','grayscale(1) contrast(1.2)','hue-rotate(115deg)']:['none','hue-rotate(325deg) saturate(1.25)','grayscale(1) contrast(.92)','hue-rotate(175deg) saturate(.72)'];
    return labels.map((title,i)=>({id:`${src}-${i}`,title,art:d.art,filter:filters[i],position:'center'}));
  }
  function renderSources() {
    $('#art-sources').innerHTML=sources[kind].map(([key,label])=>`<button data-source="${key}" aria-pressed="${state().source===key}">${label}</button>`).join('');
    $$('[data-source]').forEach(b=>b.addEventListener('click',()=>{const src=b.dataset.source;state().source=src;state().artSearch='';renderSources();$(`[data-source="${src}"]`).focus({preventScroll:true});}));
    $('#upload-area').hidden=state().source!=='file';
    $('#online-art').hidden=state().source==='file';
    $('#online-art .search-line').hidden=state().source==='frames';
    $('#gallery-label').textContent=state().source==='frames'?'stills from this file':kind==='artist'?'portrait studies':'alternate covers';
    renderGallery();
  }
  function renderGallery(query=state().artSearch) {
    state().artSearch=query;
    const rows=candidates();
    const search=query.trim().toLowerCase();
    const title=$('#art-query').value.toLowerCase();
    const matches=rows.filter(r=>!search||r.title.includes(search)||originals[kind].title.toLowerCase().includes(search)||originals[kind].artist.toLowerCase().includes(search)||title===search && search.includes(' — '));
    $('#gallery-count').textContent=String(matches.length).padStart(2,'0')+' found';
    $('#art-gallery').innerHTML=matches.map(r=>`<button class="art-option" data-art="${r.id}" aria-pressed="${draft().artName===r.title&&draft().filter===r.filter}" aria-label="Preview ${esc(r.title)}"><span class="candidate-picture"><span class="art-image" style="background-image:url('${r.art}');filter:${r.filter};background-position:${r.position}"></span><span class="art-check">✓</span></span><span class="candidate-title">${esc(r.title)}</span></button>`).join('');
    $$('.art-option').forEach(b=>b.addEventListener('click',()=>{
      const selected=rows.find(r=>r.id===b.dataset.art);
      Object.assign(draft(),{art:selected.art,filter:selected.filter,artName:selected.title,custom:true,position:selected.position});
      $('.hero-image').style.backgroundPosition=selected.position;
      renderPreview();renderGallery();
      $(`[data-art="${selected.id}"]`)?.focus({preventScroll:true});
      $('#selection-note').innerHTML=`<strong>${esc(selected.title)}</strong> on the sleeve. Apply when it feels right.`;
    }));
    if (!matches.length) $('#selection-note').textContent='No sample covers match. Try “night”, “original”, or clear the search.';
    else $('#selection-note').textContent=kind==='series'?'Artwork never changes your manual identity.':draft().custom?'Your pick is on the sleeve. Apply when it feels right.':'Pick a cover. See it on the sleeve.';
  }
  function renderProviders() {
    $('#match-providers').innerHTML=providers[kind].map(p=>`<button data-provider="${p}" aria-pressed="${state().provider===p}">${p}</button>`).join('');
    $$('[data-provider]').forEach(b=>b.addEventListener('click',()=>{const provider=b.dataset.provider;state().provider=provider;renderProviders();renderMatches();$(`[data-provider="${provider}"]`).focus({preventScroll:true});}));
  }
  function identityCandidates() {
    const original=copy(originals[kind]);
    const alt=copy(original);
    if(kind==='artist'){alt.artist='Actor / musician';alt.genre='Indie pop';alt.overview='A different artist with the same name. Known for film and television.';}
    else if(kind==='album'){alt.title='After the Last Train (Live)';alt.year='2010';alt.genre='Live / alternative';}
    else if(kind==='movie'){alt.title='The Last Broadcast: A Documentary';alt.year='2017';alt.genre='Documentary';}
    else {original.title='Midnight Signal';original.artist='Original television series';original.manual=false;alt.title='Midnight Signal: The Movie';alt.year='2018';}
    return [original,alt];
  }
  function renderMatches(query=state().matchSearch) {
    state().matchSearch=query;
    const queryText=query.toLowerCase().trim();
    const rows=identityCandidates().map((item,index)=>({item,index})).filter(({item})=>!queryText||[item.title,item.artist].join(' ').toLowerCase().includes(queryText));
    $('#match-results').innerHTML=rows.map(({item,index})=>`<button class="match-result" data-match="${index}" aria-pressed="${draft().matchId===index}"><span class="art-image" style="background-image:url('${originals[kind].art}');filter:${index?'grayscale(1)':'none'}"></span><span><b>${esc(item.title)}</b><small>${esc(item.artist)} · ${esc(item.year)}<br>${esc(item.genre)} · ${esc(state().provider)}</small></span><span>${draft().matchId===index?'selected':'use this'}</span></button>`).join('');
    if(!rows.length) $('#match-results').innerHTML='<p class="selection-note">No sample matches. You can always write the identity yourself.</p>';
    $$('[data-match]').forEach(b=>b.addEventListener('click',()=>{
      const selected=identityCandidates()[Number(b.dataset.match)];
      const keepArt={art:draft().art,filter:draft().filter,custom:draft().custom,artName:draft().artName,position:draft().position};
      Object.assign(draft(),selected,keepArt,{manual:false,matchId:Number(b.dataset.match)});
      renderFields();renderPreview();renderMatches();
      $(`[data-match="${b.dataset.match}"]`)?.focus({preventScroll:true});
    }));
  }
  function closeEditor(commit) {
    fileRequest++;
    if(commit) {
      if(!draft().title.trim()){setTab('identity');setMode('manual');$('#field-title').focus();toast('Give this one a name first.');return;}
      state().saved=copy(draft());
      toast('Saved in this preview. Your collection, your version.');
    } else {
      state().draft=copy(state().saved);
      toast('Unapplied changes discarded.');
    }
    $('#editor').hidden=true;$('#reopen').hidden=false;open=false;
    renderPreview();$('#reopen').focus();
  }
  async function readImage(file) {
    if(!file) return;
    if(!['image/jpeg','image/png','image/webp'].includes(file.type)){toast('Choose a JPEG, PNG, or WebP image.');return;}
    if(file.size>20*1024*1024){toast('Choose an image smaller than 20 MB.');return;}
    const request=++fileRequest;
    const targetKind=kind;
    try {
      const data=await new Promise((resolve,reject)=>{const r=new FileReader();r.onload=()=>resolve(r.result);r.onerror=reject;r.readAsDataURL(file);});
      await new Promise((resolve,reject)=>{const img=new Image();img.onload=resolve;img.onerror=reject;img.src=data;});
      if(request!==fileRequest||targetKind!==kind||!open) return;
      Object.assign(draft(),{art:data,filter:'none',artName:file.name,custom:true,position:'center'});
      $('.hero-image').style.backgroundPosition='center';
      renderPreview();toast('Your image is on the sleeve. Apply to keep it.');
    }catch{toast('That image could not be opened. Try another.');}
  }
  $$('.directions button').forEach(b=>b.addEventListener('click',()=>setDirection(b.dataset.direction)));
  $$('.specimens button').forEach(b=>b.addEventListener('click',()=>setKind(b.dataset.kind)));
  $$('.pivots button').forEach(b=>{
    b.addEventListener('click',()=>setTab(b.dataset.tab));
    b.addEventListener('keydown',e=>{if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key)){e.preventDefault();const next=e.key==='Home'?'identity':e.key==='End'?'artwork':state().tab==='artwork'?'identity':'artwork';setTab(next);$(`[data-tab="${next}"]`).focus();}});
  });
  $$('[data-mode]').forEach(b=>b.addEventListener('click',()=>setMode(b.dataset.mode)));
  $('#hero-art').addEventListener('click',()=>{setTab('artwork');$('#artwork-tab').focus();});
  $('#identity-form').addEventListener('submit',e=>e.preventDefault());
  $('#art-search').addEventListener('click',()=>renderGallery($('#art-query').value));
  $('#art-query').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();renderGallery(e.target.value);}});
  $('#match-search').addEventListener('click',()=>renderMatches($('#match-query').value));
  $('#match-query').addEventListener('keydown',e=>{if(e.key==='Enter'){e.preventDefault();renderMatches(e.target.value);}});
  $('#apply').addEventListener('click',()=>closeEditor(true));
  $('#cancel').addEventListener('click',()=>closeEditor(false));
  $('#close').addEventListener('click',()=>closeEditor(false));
  $('#reset').addEventListener('click',()=>{fileRequest++;state().draft=copy(automatic[kind]);renderFields();renderPreview();renderGallery();renderMatches();toast('Automatic identity and art restored in the preview. Apply to keep them.');});
  $('#reopen').addEventListener('click',()=>{setKind(kind);$('#close').focus();});
  $('#choose-file').addEventListener('click',()=>$('#file-input').click());
  $('#file-input').addEventListener('change',e=>{readImage(e.target.files[0]);e.target.value='';});
  const drop=$('#upload-area');
  for(const ev of ['dragenter','dragover']) drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.add('drag-over');});
  drop.addEventListener('dragleave',()=>drop.classList.remove('drag-over'));
  drop.addEventListener('drop',e=>{e.preventDefault();drop.classList.remove('drag-over');readImage(e.dataTransfer.files[0]);});
  document.addEventListener('dragover',e=>e.preventDefault());
  document.addEventListener('drop',e=>e.preventDefault());
  document.addEventListener('keydown',e=>{if(e.key==='Escape'&&open){e.preventDefault();closeEditor(false);}});
  $('#toggle-font').addEventListener('click',()=>{const on=document.body.classList.toggle('graffiti');$('#toggle-font').setAttribute('aria-pressed',String(on));$('#toggle-font').textContent=on?'back to permanent marker ↗':'try the graffiti face ↗';});
  $('.ambient-gallery').innerHTML='<div></div>'.repeat(12);
  const hash=new URLSearchParams(location.hash.slice(1));
  const initialKind=specimens[hash.get('kind')]?hash.get('kind'):'album';
  if(hash.get('tab')==='identity') specimens[initialKind].tab='identity';
  setKind(initialKind);setDirection(directions[hash.get('direction')]?hash.get('direction'):'sleeve');
})();
