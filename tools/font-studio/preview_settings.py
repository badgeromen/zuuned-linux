#!/usr/bin/env python3
"""Draw SETTINGS from the actual generated font. No reference image is read."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import numpy as np
root=Path(__file__).resolve().parent/'output'
f=ImageFont.truetype(str(root/'Specimen-Regular.ttf'),180)
mask=Image.new('L',(1250,390)); d=ImageDraw.Draw(mask)
d.text((65,20),'SETTINGS',font=f,fill=255)
mask=mask.crop(mask.getbbox()); mask=mask.rotate(9,resample=Image.Resampling.BICUBIC,expand=True)
w,h=1100,440
x=np.linspace(0,1,w)[None,:,None]; y=np.linspace(0,1,h)[:,None,None]
bg=np.broadcast_to(np.array([54.,51.,42.])*(1-.28*x)+np.zeros((h,1,1)),(h,w,3)).copy()
im=Image.fromarray(bg.clip(0,255).astype('uint8'))
paint=Image.new('L',(w,h)); paint.paste(mask,(65,55))
colors=np.array([[255,156,4],[241,86,46],[214,37,106]])
xs=np.linspace(0,1,w); rgb=np.stack([np.interp(xs,[0,.5,1],colors[:,c]) for c in range(3)],axis=-1)
grad=Image.fromarray(np.broadcast_to(rgb,(h,w,3)).astype('uint8'))
im.paste(grad,(0,0),paint)
im.save(root/'settings-study04.png')
