#!/usr/bin/env python3
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
root=Path(__file__).resolve().parent/'output'
f=ImageFont.truetype(str(root/'SpecimenAlien-Regular.ttf'),125)
ui=ImageFont.truetype('/usr/share/fonts/liberation/LiberationSans-Regular.ttf',20)
im=Image.new('RGB',(1200,1260),'#e7e5dc');d=ImageDraw.Draw(im)
d.text((30,18),'SPECIMEN / ALIEN STUDY 01 / ALIEN STREET STENCIL',font=ui,fill='#383b36')
for i,c in enumerate('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'):
 x=(i%6)*200; y=60+(i//6)*200
 d.rectangle((x+8,y+8,x+192,y+192),outline='#b6b9af')
 d.text((x+20,y+17),c,font=ui,fill='#85877f')
 d.text((x+100,y+111),c,font=f,anchor='mm',fill='#191e19')
im.save(root/'glyphs-alien01.png')
