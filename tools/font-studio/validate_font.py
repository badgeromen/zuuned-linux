#!/usr/bin/env python3
"""Load generated outlines through Qt and check coverage/metric invariants."""
import os
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
os.environ['QT_QPA_PLATFORMTHEME']=''
os.environ['QT_STYLE_OVERRIDE']='Fusion'
from PySide6.QtGui import QGuiApplication,QFontDatabase,QFont,QRawFont
from fontTools.ttLib import TTFont
p=Path(__file__).resolve().parent/'output'/'Specimen-Regular.ttf'
app=QGuiApplication([]); i=QFontDatabase.addApplicationFont(str(p))
assert i>=0
assert QFontDatabase.applicationFontFamilies(i)==['Specimen']
raw=QRawFont.fromFont(QFont('Specimen',30)); assert raw.isValid()
assert all(raw.supportsCharacter(ord(c)) for c in 'SETTINGS ZUUNED abc 0123456789 !? Café · Piñata — €')
f=TTFont(p); cm=f.getBestCmap()
assert all(c in cm for c in range(32,127))
for code,name in cm.items():
    if code not in (32,160): assert f['glyf'][name].numberOfContours>0
    assert f['hmtx'][name][0]>0
for name in f.getGlyphOrder():
    g=f['glyf'][name]
    if g.numberOfContours:
        assert g.yMax<=f['hhea'].ascent and g.yMin>=f['hhea'].descent,(name,g.yMin,g.yMax)
print('PASS: Qt loading, Unicode/ASCII coverage, nonempty outlines, positive advances and vertical metric bounds.')
