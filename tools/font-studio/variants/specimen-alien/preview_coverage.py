#!/usr/bin/env python3
from pathlib import Path
import json,math
from PIL import Image,ImageDraw,ImageFont
root=Path(__file__).resolve().parent/'output'
rows=json.loads((root/'coverage.json').read_text())['characters']
f=ImageFont.truetype(str(root/'SpecimenAlien-Regular.ttf'),57)
ui=ImageFont.truetype('/usr/share/fonts/liberation/LiberationSans-Regular.ttf',15)
for page in range(math.ceil(len(rows)/80)):
 im=Image.new('RGB',(1400,1050),'#eeeae0');d=ImageDraw.Draw(im)
 d.text((25,15),f'SPECIMEN ALIEN / CHARACTER PROOF {page+1}',font=ui,fill='#35362f')
 for i,c in enumerate(rows[page*80:(page+1)*80]):
  x=20+(i%10)*138;y=50+(i//10)*124
  d.rectangle((x,y,x+130,y+117),outline='#c3c0b5')
  d.text((x+6,y+5),c['code'],font=ui,fill='#747467')
  text=('A' if c['name'].startswith('COMBINING') else '')+c['character']
  d.text((x+63,y+67),text,font=f,anchor='mm',fill='#171d16')
 im.save(root/f'coverage-{page+1}.png')
