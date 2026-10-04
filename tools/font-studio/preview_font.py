#!/usr/bin/env python3
"""Render the actual TTF, not a conceptual mockup."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parent/'output'
fontpath=str(root/'Specimen-Regular.ttf')
def font(size):return ImageFont.truetype(fontpath,size)
im=Image.new('RGB',(1600,1180),'#141313'); d=ImageDraw.Draw(im)
ui=ImageFont.truetype('/usr/share/fonts/liberation/LiberationSans-Regular.ttf',24)
d.text((55,32),'SPECIMEN / ORIGINAL TYPE STUDY 04',font=ui,fill='#bbb4b0')
d.text((45,83),'ZUUNED',font=font(200),fill='#ee4086')
d.text((55,306),'MUSIC   VIDEOS   PHOTOS',font=font(77),fill='#f09739')
d.text((55,409),'PLAYLISTS   MAKE IT YOURS.',font=font(64),fill='#e6e0d7')
d.line((55,520,1540,520),fill='#403936',width=2)
d.text((55,542),'ACTUAL FONT / LETTERS, NUMBERS & PUNCTUATION',font=ui,fill='#aaa19a')
for y,txt in [(590,'ABCDEFGHIJKLM'),(684,'NOPQRSTUVWXYZ'),(780,'0123456789  & @ # %'),(876,'!?  / + - = : ; ( ) [ ]'),(970,'CAFÉ · PIÑATA · DÉJÀ VU')]:
 d.text((55,y),txt,font=font(68),fill='#eee7df')
d.text((55,1105),'24 PX: MUSIC / VIDEOS / PHOTOS / PLAYLISTS',font=font(24),fill='#eee7df')
im.save(root/'specimen.png')
