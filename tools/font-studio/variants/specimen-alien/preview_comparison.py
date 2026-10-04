#!/usr/bin/env python3
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parent
base=root.parents[1]/'output'/'Specimen-Regular.ttf'
alien=root/'output'/'SpecimenAlien-Regular.ttf'
im=Image.new('RGB',(1400,820),'#22231f');d=ImageDraw.Draw(im)
ui=ImageFont.truetype('/usr/share/fonts/liberation/LiberationSans-Regular.ttf',24)
for i,(name,p) in enumerate([('SPECIMEN / PRESERVED',base),('SPECIMEN ALIEN / SEPARATE VARIANT',alien)]):
 y=25+i*400
 d.text((45,y),name,font=ui,fill='#c7c7ba')
 d.text((40,y+40),'SETTINGS',font=ImageFont.truetype(str(p),155),fill='#ef5889')
 d.text((45,y+242),'WE COME IN PEACE  0123456789',font=ImageFont.truetype(str(p),62),fill='#e5be71')
 d.line((45,y+373,1350,y+373),fill='#525246')
im.save(root/'output'/'comparison.png')
