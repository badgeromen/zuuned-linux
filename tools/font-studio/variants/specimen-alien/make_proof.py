#!/usr/bin/env python3
"""Offline, interactive proof of the exported font, not an outline editor."""
from pathlib import Path
import json,base64
root=Path(__file__).resolve().parent/'output'
coverage=json.loads((root/'coverage.json').read_text())
html='''<!doctype html><html lang="en"><meta charset="utf-8"><title>Specimen Alien · Font Studio proof</title>
<style>
@font-face{font-family:SpecimenAlien;src:url('./SpecimenAlien-Regular.ttf') format('truetype')}
*{box-sizing:border-box}body{margin:0;background:#22231f;color:#eee9df;font:16px system-ui;padding:32px;max-width:1500px;margin:auto}h1{font-size:24px}label{display:inline-flex;gap:12px;margin:12px 24px 12px 0;align-items:center}textarea{display:block;width:100%;padding:14px;background:#151613;color:inherit;border:1px solid #6c6c5b;border-radius:6px;font:inherit}#sample{font:100px SpecimenAlien;overflow-wrap:anywhere;line-height:1.5;padding:25px 0;color:#ef6797}#grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(105px,1fr));gap:8px}.cell{border:1px solid #595b4c;min-height:135px;padding:8px;overflow:hidden}.glyph{font:55px SpecimenAlien;display:block;text-align:center;line-height:1.6}.code{font:11px system-ui;color:#bcbeb0}.note{color:#bcbeb0;max-width:900px}button{padding:9px 16px;background:#c8d691;color:#191c14;border:0;border-radius:5px;cursor:pointer}body.light{background:#eeeee6;color:#20231e}body.light #sample{color:#22251f}body.light .note,body.light .code{color:#565b50}
</style>
<h1>Specimen Alien · Font Studio proof</h1><p class="note">Actual local TTF. Edit the text and inspect all declared characters. This is a font proof, not yet a glyph editor. Lowercase intentionally uses capital-shaped forms. Original Specimen is preserved separately.</p>
<textarea id="text" rows="2">SETTINGS — WE COME IN PEACE
Café · Łódź · Æsir · Straße · 0123456789</textarea>
<label>Size <input id="size" type="range" min="18" max="180" value="100"><output id="value">100 px</output></label><label>Tracking <input id="track" type="range" min="-3" max="12" value="0"></label><button id="bg">Toggle background</button><div id="sample"></div>
<h2>Declared character set</h2><p class="note">Combining marks are shown after A. The soft hyphen and spacing characters are intentionally blank. Unsupported scripts use the browser fallback and are outside this release's Latin scope.</p><div id="grid"></div>
<script>
const data=REPLACE_DATA;
const text=document.getElementById('text'),sample=document.getElementById('sample'),size=document.getElementById('size'),track=document.getElementById('track');
function update(){sample.textContent=text.value;sample.style.whiteSpace='pre-wrap';sample.style.fontSize=size.value+'px';sample.style.letterSpacing=track.value+'px';document.getElementById('value').textContent=size.value+' px'}
text.oninput=size.oninput=track.oninput=update;document.getElementById('bg').onclick=()=>document.body.classList.toggle('light');update();
for(const c of data.characters){let e=document.createElement('div');e.className='cell';e.title=c.name;let a=document.createElement('span');a.className='code';a.textContent=c.code;let b=document.createElement('span');b.className='glyph';b.textContent=c.name.startsWith('COMBINING')?'A'+c.character:c.character;e.append(a,b);document.getElementById('grid').append(e)}
</script></html>'''
encoded=base64.b64encode((root/'SpecimenAlien-Regular.ttf').read_bytes()).decode('ascii')
html=html.replace("./SpecimenAlien-Regular.ttf",'data:font/ttf;base64,'+encoded)
(root/'proof.html').write_text(html.replace('REPLACE_DATA',json.dumps(coverage,ensure_ascii=True)))
print(root/'proof.html')
