#!/usr/bin/env python3
"""Load generated outlines through Qt and check coverage/metric invariants."""
import os
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
os.environ['QT_QPA_PLATFORMTHEME']=''
os.environ['QT_STYLE_OVERRIDE']='Fusion'
from PySide6.QtGui import QGuiApplication,QFontDatabase,QFont,QRawFont
from fontTools.ttLib import TTFont
p=Path(__file__).resolve().parent/'output'/'SpecimenAlien-Regular.ttf'
app=QGuiApplication([]); i=QFontDatabase.addApplicationFont(str(p))
assert i>=0
assert QFontDatabase.applicationFontFamilies(i)==['Specimen Alien']
raw=QRawFont.fromFont(QFont('Specimen Alien',30)); assert raw.isValid()
assert all(raw.supportsCharacter(ord(c)) for c in 'SETTINGS ZUUNED abc 0123456789 !? Café · Piñata — €')
f=TTFont(p); cm=f.getBestCmap()
assert all(c in cm for c in range(32,127))
for code,name in cm.items():
    if code not in (32,160,173): assert f['glyf'][name].numberOfContours>0
    assert f['hmtx'][name][0]>=0
for name in f.getGlyphOrder():
    g=f['glyf'][name]
    if g.numberOfContours:
        assert g.yMax<=f['hhea'].ascent and g.yMin>=f['hhea'].descent,(name,g.yMin,g.yMax)
print('PASS: Qt loading, Unicode/ASCII coverage, nonempty outlines, positive advances and vertical metric bounds.')

from completion import REQUIRED,MARKS
assert REQUIRED<=set(cm)
assert all(raw.supportsCharacter(cp) for cp in REQUIRED)
assert 'GPOS' in f
assert all(f['hmtx'][cm[ord(c)]][0]==0 for c in MARKS)
print(f'PASS: all {len(REQUIRED)} declared codepoints available in Qt; zero-advance marks and GPOS present.')
from PySide6.QtGui import QTextLayout

def layout(text):
    q=QTextLayout(text,QFont('Specimen Alien',42))
    q.beginLayout();l=q.createLine();l.setLineWidth(10000);q.endLayout()
    runs=q.glyphRuns()
    assert all(r.rawFont().familyName()=='Specimen Alien' for r in runs),text
    assert all(0 not in r.glyphIndexes() for r in runs),text
    return l.naturalTextWidth()
for pre,decomposed in [('É','E\u0301'),('ñ','n\u0303'),('Å','A\u030a'),('Ç','C\u0327')]:
    assert abs(layout(pre)-layout(decomposed))<.1,(pre,decomposed)
assert abs(layout('X')-layout('X\u0301'))<.1
for sample in ['Æsir Œuvre Straße Łódź Þing Ðelta','£25 €30 ¥40 © ® ™','½ ¾ ± × ÷ ≠ ≤ ≥ ← → ✓','0123456789 !? () [] {}']:
    layout(sample)
print('PASS: Qt shapes accents and extended text without fallback or missing glyphs; combining accents preserve advances.')
