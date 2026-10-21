#!/usr/bin/env python3
"""Validate declared coverage and actual Qt font loading/shaping, isolated."""
import os, json
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen';os.environ['QT_QPA_PLATFORMTHEME']=''
from PySide6.QtGui import QGuiApplication,QFontDatabase,QFont,QRawFont,QTextLayout
from fontTools.ttLib import TTFont
ROOT=Path(__file__).resolve().parent
font=TTFont(ROOT/'output/ZuunedSans-Regular.ttf');cmap=font.getBestCmap()
manifest=json.loads((ROOT/'output/coverage.json').read_text())
required={int(row['code'][2:],16) for row in manifest['characters']}
assert required==set(cmap)
assert set(range(32,127))|set(range(160,384))<=required
assert len(required)==350
assert font['OS/2'].fsType==0
assert 'Open Font License' in font['name'].getDebugName(13)
assert font['name'].getDebugName(14)=='https://openfontlicense.org/open-font-license-official-text/'
for cp,name in cmap.items():
    g=font['glyf'][name];advance=font['hmtx'][name][0]
    if cp not in (32,160,173):assert g.numberOfContours>0,(hex(cp),name)
    if g.numberOfContours:
        assert g.yMin>=-260 and g.yMax<=960,(hex(cp),g.yMin,g.yMax)
        assert all(-1000<=x<=2000 and -260<=y<=960 for x,y in g.coordinates)
        assert all(end>=0 for end in g.endPtsOfContours)
    assert advance>=0
for lower in 'abcdefghijklmnopqrstuvwxyz':
    assert cmap[ord(lower)]!=cmap[ord(lower.upper())]
    assert font['glyf'][cmap[ord(lower)]].compile(font['glyf'])!=font['glyf'][cmap[ord(lower.upper())]].compile(font['glyf'])
app=QGuiApplication([]);loaded=QFontDatabase.addApplicationFont(str(ROOT/'output/ZuunedSans-Regular.ttf'))
assert loaded>=0 and QFontDatabase.applicationFontFamilies(loaded)==['Zuuned Sans']
qfont=QFont('Zuuned Sans',42);raw=QRawFont.fromFont(qfont)
assert all(raw.supportsCharacter(cp) for cp in required)
def layout(text):
    item=QTextLayout(text,qfont);item.beginLayout();line=item.createLine();line.setLineWidth(20000);item.endLayout()
    assert all(run.rawFont().familyName()=='Zuuned Sans' and 0 not in run.glyphIndexes() for run in item.glyphRuns()),text
    return line.naturalTextWidth()
for text in ['music videos photos podcasts audiobooks','ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz','Björk Beyoncé Zweihänder Łódź Œuvre Æsir Straße Þing Dvořák','0123456789 !? @#$%&* () [] {} £€¥ ©®™','¼ ½ ¾ ± × ÷ — … “quoted”']:
    layout(text)
for pre,decomposed in [('é','e\u0301'),('ñ','n\u0303'),('Å','A\u030a'),('Ç','C\u0327')]:
    assert abs(layout(pre)-layout(decomposed))<.1
assert abs(layout('x')-layout('x\u0301'))<.1
assert all(font['hmtx'][cmap[cp]][0]==0 for cp in required if 0x300<=cp<=0x36f)
print('PASS: 350 characters; distinct lowercase; contours/metrics; Qt loading/shaping; accents; OFL metadata.')
