#!/usr/bin/env python3
"""Render the actual generated TTF, without a substituted design mockup."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parent;FONT=ROOT/'output/ZuunedSans-Regular.ttf'
img=Image.new('RGB',(1500,1160),'#111018');d=ImageDraw.Draw(img)
def text(x,y,t,size,color='#dedbe5'):
    d.text((x,y),t,font=ImageFont.truetype(str(FONT),size),fill=color)
text(70,45,'Zuuned Sans',100,'#f05a9a')
text(72,165,'An original, quieter third voice for Zuuned.',34,'#b9b4c4')
text(70,260,'music  videos  photos',100)
text(70,375,'podcasts  audiobooks',100)
text(72,525,'ABCDEFGHIJKLMNOPQRSTUVWXYZ',53)
text(72,610,'abcdefghijklmnopqrstuvwxyz',58)
text(72,700,'0123456789   !? @ # $ % & * ( ) [ ] { }',48)
text(72,790,'Björk · Beyoncé · Zweihänder · Łódź · Œuvre',49)
text(72,880,'Café, déjà vu. Æsir / Straße / Þing / Dvořák',45)
text(72,985,'Listen. Discover. Make it yours.',43,'#ff9759')
text(72,1065,'Regular  |  350 declared characters  |  Actual TTF rendering',26,'#a5a1b0')
img.save(ROOT/'output/specimen.png')
print(ROOT/'output/specimen.png')
