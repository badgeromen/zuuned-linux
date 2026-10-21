#!/usr/bin/env python3
"""Original Zuuned Sans: authored humanist outlines, never reads another font.
Generator: GPL-3.0-or-later. Generated font: SIL Open Font License 1.1.
Developer tools only: PySide6 and fontTools. No application dependency.
"""
from pathlib import Path
import json, unicodedata
from PySide6.QtCore import Qt, QPointF
from PySide6.QtGui import QPainterPath, QPainterPathStroker, QTransform
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
ROOT=Path(__file__).resolve().parent
OUT=ROOT/'output'; OUT.mkdir(exist_ok=True)
STROKE=48

def draw(*cmds, width=STROKE):
    p=QPainterPath()
    for c in cmds:
        if c[0]=='M': p.moveTo(*c[1:])
        elif c[0]=='L': p.lineTo(*c[1:])
        elif c[0]=='C': p.cubicTo(*c[1:])
        elif c[0]=='Q': p.quadTo(*c[1:])
        elif c[0]=='Z': p.closeSubpath()
    s=QPainterPathStroker();s.setWidth(width)
    s.setCapStyle(Qt.PenCapStyle.FlatCap);s.setJoinStyle(Qt.PenJoinStyle.RoundJoin)
    return s.createStroke(p)
def line(*xy,width=STROKE):return draw(('M',*xy[0]),*(('L',*a) for a in xy[1:]),width=width)
def ellipse(x,y,w,h):
    p=QPainterPath();p.addEllipse(x,y,w,h);s=QPainterPathStroker();s.setWidth(STROKE);return s.createStroke(p)
def dot(x,y,r=29):
    p=QPainterPath();p.addEllipse(x-r,y-r,2*r,2*r);return p
def rect(x,y,w,h):
    p=QPainterPath();p.addRect(x,y,w,h);return p
def merge(*parts):
    p=QPainterPath()
    for a in parts:p=p.united(a)
    return p
def move(p,x=0,y=0,sx=1,sy=1):return QTransform(sx,0,0,sy,x,y).map(p)
P={};W={}
def put(c,p,w):P[c]=p;W[c]=w
# Capitals: open counters, gently asymmetric bowls and moderate stroke contrast
# through authored proportions; no samples or existing outline data are used.
put('A',merge(line((45,0),(290,700),(535,0)),line((127,232),(453,232))),580)
put('B',merge(line((85,0),(85,700)),draw(('M',85,676),('L',300,676),('C',515,676,535,375,300,375),('L',85,375)),draw(('M',85,375),('L',316,375),('C',560,375,550,24,305,24),('L',85,24))),580)
put('C',draw(('M',510,595),('C',445,685,370,700,280,675),('C',40,625,40,80,270,26),('C',360,3,460,32,520,105)),590)
put('D',merge(line((85,0),(85,700)),draw(('M',85,676),('L',260,676),('C',645,676,645,24,260,24),('L',85,24))),650)
for c in 'EF':put(c,merge(line((85,0),(85,700)),line((85,676),(490,676)),line((85,365),(445,365)),line((85,24),(500,24)) if c=='E' else QPainterPath()),560)
put('G',merge(P['C'],line((520,110),(520,330),(330,330))),610)
put('H',merge(line((85,0),(85,700)),line((535,0),(535,700)),line((85,360),(535,360))),620)
put('I',line((115,0),(115,700)),230)
put('J',draw(('M',340,700),('L',340,180),('C',340,-25,105,-25,65,100)),430)
put('K',merge(line((85,0),(85,700)),line((535,700),(85,295)),line((283,474),(550,0))),620)
put('L',line((85,700),(85,24),(490,24)),550)
put('M',line((85,0),(85,700),(370,212),(655,700),(655,0)),740)
put('N',line((85,0),(85,700),(535,0),(535,700)),620)
put('O',ellipse(75,17,500,666),650)
put('P',merge(line((85,0),(85,700)),draw(('M',85,676),('L',300,676),('C',575,676,575,340,300,340),('L',85,340))),590)
put('Q',merge(P['O'],line((385,175),(620,-90))),660)
put('R',merge(P['P'],line((305,340),(555,0))),620)
put('S',draw(('M',490,597),('C',360,755,94,683,94,515),('C',94,325,496,409,496,190),('C',496,-8,200,-35,65,107)),570)
put('T',merge(line((30,676),(550,676)),line((290,676),(290,0))),580)
put('U',draw(('M',85,700),('L',85,235),('C',85,-55,535,-55,535,235),('L',535,700)),620)
put('V',line((40,700),(290,0),(540,700)),580)
put('W',line((45,700),(225,0),(430,584),(635,0),(815,700)),860)
put('X',merge(line((55,700),(525,0)),line((525,700),(55,0))),580)
put('Y',merge(line((40,700),(290,350),(540,700)),line((290,350),(290,0))),580)
put('Z',line((65,676),(510,676),(65,24),(530,24)),590)
# True lowercase with ascenders/descenders, separate from the capital drawings.
put('o',ellipse(65,17,385,476),515)
put('a',merge(draw(('M',98,435),('C',185,530,435,525,435,345),('L',435,0)),draw(('M',435,292),('C',325,345,70,300,70,142),('C',70,-13,325,-10,435,115))),515)
put('b',merge(line((85,0),(85,735)),draw(('M',85,410),('C',265,605,475,475,475,260),('C',475,20,250,-68,85,102))),545)
put('c',draw(('M',430,430),('C',235,625,65,444,65,255),('C',65,55,250,-65,440,90)),500)
put('d',merge(line((460,0),(460,735)),draw(('M',460,410),('C',280,605,70,475,70,260),('C',70,20,295,-68,460,102))),545)
put('e',draw(('M',70,275),('L',455,275),('C',455,570,65,565,65,260),('C',65,30,255,-65,455,92)),525)
put('f',merge(draw(('M',125,0),('L',125,565),('C',125,750,255,766,350,715)),line((35,486),(330,486))),360)
put('g',merge(move(P['o'],x=5),line((455,510),(455,-30)),draw(('M',455,-30),('C',455,-235,190,-245,85,-140))),535)
put('h',merge(line((85,0),(85,735)),draw(('M',85,360),('C',230,590,450,555,450,305),('L',450,0))),535)
put('i',merge(line((110,0),(110,510)),dot(110,660)),220)
put('j',merge(draw(('M',130,510),('L',130,-80),('C',130,-190,90,-225,-10,-193)),dot(130,660)),240)
put('k',merge(line((85,0),(85,735)),line((430,510),(85,185)),line((245,336),(460,0))),525)
put('l',draw(('M',100,735),('L',100,94),('C',100,32,122,18,175,24)),235)
put('m',merge(line((85,0),(85,510)),draw(('M',85,365),('C',195,570,355,555,355,310),('L',355,0)),draw(('M',355,365),('C',475,570,645,555,645,310),('L',645,0))),730)
put('n',merge(line((85,0),(85,510)),draw(('M',85,360),('C',230,590,450,555,450,305),('L',450,0))),535)
put('p',merge(line((85,-220),(85,510)),draw(('M',85,410),('C',265,605,475,475,475,260),('C',475,20,250,-68,85,102))),545)
put('q',merge(move(P['d']),line((460,0),(460,-220))).intersected(rect(-100,-240,800,774)),545)
put('r',merge(line((85,0),(85,510)),draw(('M',85,350),('C',155,490,250,525,340,470))),370)
put('s',draw(('M',395,433),('C',295,540,85,510,85,375),('C',85,222,400,300,400,133),('C',400,-5,185,-28,65,70)),465)
put('t',merge(draw(('M',150,650),('L',150,135),('C',150,3,265,7,345,53)),line((35,486),(340,486))),385)
put('u',merge(draw(('M',85,510),('L',85,185),('C',85,-85,345,5,450,145)),line((450,510),(450,0))),535)
put('v',line((35,510),(245,0),(455,510)),490)
put('w',line((35,510),(185,0),(350,440),(515,0),(665,510)),700)
put('x',merge(line((45,510),(445,0)),line((445,510),(45,0))),490)
put('y',merge(line((35,510),(250,0)),draw(('M',465,510),('L',250,0),('C',180,-190,130,-240,35,-187))),500)
put('z',line((55,486),(430,486),(55,24),(445,24)),500)
# Digits use a common 560-unit advance, with separate author-drawn outlines.
put('0',ellipse(75,17,410,666),560)
put('1',merge(line((110,530),(300,680),(300,24)),line((110,24),(470,24))),560)
put('2',draw(('M',80,555),('C',140,750,480,730,480,530),('C',480,375,180,240,85,24),('L',495,24)),560)
put('3',draw(('M',85,605),('C',210,750,475,685,475,525),('C',475,390,360,355,240,355),('C',500,385,560,47,320,24),('C',220,2,130,36,65,100)),560)
put('4',merge(line((395,700),(65,235),(515,235)),line((405,700),(405,0))),560)
put('5',draw(('M',485,676),('L',105,676),('L',85,360),('C',260,460,485,385,485,200),('C',485,-22,190,-33,70,100)),560)
put('6',merge(draw(('M',470,620),('C',210,835,40,510,80,210)),ellipse(80,17,405,362)),560)
put('7',line((55,676),(505,676),(220,0)),560)
put('8',merge(ellipse(100,365,360,318),ellipse(75,17,410,348)),560)
put('9',merge(ellipse(75,321,405,362),draw(('M',480,490),('C',525,150,355,-100,100,75))),560)
put(' ',QPainterPath(),270)
put('.',dot(105,30),210);put(',',merge(dot(105,37),draw(('M',122,38),('C',135,-40,101,-85,62,-118),width=36)),215)
put(':',merge(dot(110,30),dot(110,390)),220);put(';',merge(P[','],dot(105,390)),220)
put('!',merge(line((110,180),(110,700)),dot(110,30)),220)
put('?',merge(draw(('M',65,590),('C',170,760,430,695,430,520),('C',430,385,245,360,245,185)),dot(245,30)),490)
put('-',line((65,245),(345,245)),410);put('_',line((20,-100),(540,-100)),560)
put('/',line((50,-115),(390,735)),440);put('\\',line((50,735),(390,-115)),440)
put('|',line((105,-150),(105,760)),210)
for c,flip in [('(',False),(')',True)]:
    p=draw(('M',230,760),('C',30,570,30,40,230,-150))
    put(c,move(p,x=300,sx=-1) if flip else p,300)
for c,flip in [('[',False),(']',True)]:
    p=line((235,735),(90,735),(90,-125),(235,-125));put(c,move(p,x=320,sx=-1) if flip else p,320)
for c,flip in [('{',False),('}',True)]:
    p=draw(('M',275,735),('C',100,755,140,595,140,460),('C',140,350,70,325,45,305),('C',70,285,140,270,140,160),('C',140,20,100,-145,275,-125));put(c,move(p,x=350,sx=-1) if flip else p,350)
put("'",line((100,520),(115,700)),210);put('"',merge(P["'"],move(P["'"],x=180)),390)
put('`',line((85,730),(205,610)),280);put('^',line((60,480),(250,700),(440,480)),500)
put('~',draw(('M',45,265),('C',170,430,290,100,420,265)),470)
put('+',merge(line((70,305),(490,305)),line((280,95),(280,515))),560)
put('=',merge(line((70,205),(490,205)),line((70,405),(490,405))),560)
put('<',line((440,525),(95,305),(440,85)),530);put('>',move(P['<'],x=530,sx=-1),530)
put('*',merge(line((280,710),(280,340)),line((100,620),(460,430)),line((100,430),(460,620))),560)
put('#',merge(line((180,0),(270,700)),line((380,0),(470,700)),line((60,235),(540,235)),line((90,475),(570,475))),630)
put('$',merge(P['S'],line((285,-100),(285,790),width=36)),570)
put('%',merge(move(P['0'],sx=.4,sy=.4,y=410),move(P['0'],sx=.4,sy=.4,x=350),line((65,0),(565,700))),650)
put('&',draw(('M',555,0),('L',170,455),('C',20,650,315,815,370,600),('C',400,470,75,400,75,195),('C',75,-60,500,-50,530,365)),620)
put('@',merge(ellipse(65,0,660,690),move(P['a'],x=170,y=150,sx=.67,sy=.75),draw(('M',465,520),('L',440,245),('C',430,110,640,180,690,315))),790)
# Marks are positioned relative to their anchor, independently of cap/x height.
marks={
'\u0300':line((-75,130),(30,35),width=42),'\u0301':line((-30,35),(75,130),width=42),
'\u0302':line((-100,25),(0,125),(100,25),width=42),'\u0303':draw(('M',-115,45),('C',-50,160,45,-10,115,90),width=38),
'\u0304':line((-110,65),(110,65),width=42),'\u0306':draw(('M',-100,125),('C',-80,0,80,0,100,125),width=38),
'\u0307':dot(0,65,25),'\u0308':merge(dot(-75,65,25),dot(75,65,25)),
'\u030a':ellipse(-60,15,120,120),'\u030b':merge(line((-100,25),(-10,135),width=38),line((25,25),(115,135),width=38)),
'\u030c':line((-100,125),(0,25),(100,125),width=42),
'\u0327':draw(('M',15,0),('L',-20,-60),('C',115,-45,105,-155,-15,-155),width=36),
'\u0328':draw(('M',55,0),('C',-70,-85,-30,-170,75,-125),width=36)}
# Original ligatures and special Latin letters. Lowercase retains lowercase forms.
for a,b,w,scale in [('Æ','AE',790,.72),('æ','ae',760,.75),('Œ','OE',850,.72),('œ','oe',800,.75)]:
    put(a,merge(move(P[b[0]],sx=scale),move(P[b[1]],x=w-W[b[1]]*scale,sx=scale)),w)
for a,b in [('Ø','O'),('ø','o')]:put(a,merge(P[b],line((80,-25),(W[b]-80,735 if b.isupper() else 545),width=36)),W[b])
for a,b,y in [('Ð','D',350),('Đ','D',350),('đ','d',610),('Ħ','H',545),('ħ','h',610),('Ł','L',310),('ł','l',370),('Ŧ','T',400),('ŧ','t',320)]:put(a,merge(P[b],line((20,y-40),(min(W[b]-20,330),y+70),width=40)),W[b])
put('Þ',merge(line((85,0),(85,700)),move(P['P'],y=-155,sy=.82)).intersected(rect(0,-10,650,740)),590)
put('þ',merge(line((85,-220),(85,735)),P['p']),545)
put('ð',merge(P['o'],draw(('M',140,735),('C',410,660,450,470,450,260)),line((190,565),(380,690),width=38)),535)
put('ß',draw(('M',85,0),('L',85,530),('C',85,770,400,780,400,570),('C',400,465,260,400,245,340),('C',320,345,470,310,470,165),('C',470,12,315,-25,225,55)),535)
put('ı',line((110,0),(110,510)),220)
put('Ĳ',merge(P['I'],move(P['J'],x=230)),660);put('ĳ',merge(P['i'],move(P['j'],x=220)),460)
put('ĸ',merge(line((85,0),(85,510)),line((430,510),(85,185)),line((245,336),(460,0))),525)
put('Ŋ',merge(P['N'],draw(('M',535,0),('C',535,-180,415,-230,315,-175))),620)
put('ŋ',merge(P['n'],draw(('M',450,0),('C',450,-180,330,-230,230,-175))),535)
put('ſ',merge(line((125,0),(125,565)),draw(('M',125,565),('C',125,750,255,766,350,715))),360)
put('ŉ',merge(move(P["'"],x=-30,y=25),move(P['n'],x=160)),695)
put('Ŀ',merge(P['L'],dot(350,350)),550);put('ŀ',merge(P['l'],dot(285,350)),370)
# Latin-1 punctuation and symbols.
put('\u00a0',QPainterPath(),270);put('\u00ad',QPainterPath(),0)
put('¡',move(P['!'],x=220,y=510,sx=-1,sy=-1),220);put('¿',move(P['?'],x=490,y=510,sx=-1,sy=-1),490)
put('¢',merge(move(P['c'],y=80),line((250,0),(250,700),width=36)),500)
put('£',merge(draw(('M',470,610),('C',370,745,145,715,145,500),('L',165,160),('C',165,80,125,40,70,24),('L',500,24)),line((60,315),(365,315))),560)
put('¤',merge(ellipse(155,175,250,250),line((80,100),(165,185)),line((395,415),(480,500)),line((80,500),(165,415)),line((395,185),(480,100))),560)
put('¥',merge(P['Y'],line((90,215),(490,215),width=40),line((90,335),(490,335),width=40)),580)
put('¦',merge(line((105,-150),(105,230)),line((105,380),(105,760))),210)
put('§',merge(move(P['s'],y=205),move(P['s'],y=-130)),465)
put('¨',move(marks['\u0308'],x=170,y=540),340)
put('©',merge(ellipse(50,20,660,660),move(P['C'],x=160,y=150,sx=.55,sy=.58)),760)
put('®',merge(ellipse(50,20,660,660),move(P['R'],x=205,y=150,sx=.55,sy=.58)),760)
put('ª',move(P['a'],x=20,y=365,sx=.6,sy=.6),350);put('º',move(P['o'],x=20,y=365,sx=.6,sy=.6),350)
put('«',merge(move(P['<'],sx=.5,sy=.65,y=70),move(P['<'],sx=.5,sy=.65,x=220,y=70)),510);put('»',move(P['«'],x=510,sx=-1),510)
put('¬',line((70,360),(470,360),(470,160)),550)
put('¯',line((45,650),(395,650),width=42),440);put('°',ellipse(85,465,180,180),350)
put('±',merge(P['+'],line((70,-20),(490,-20))),560)
for c,b in [('¹','1'),('²','2'),('³','3')]:put(c,move(P[b],x=15,y=340,sx=.55,sy=.55),340)
put('´',move(marks['\u0301'],x=160,y=540),320)
put('µ',merge(P['u'],line((85,-200),(85,130))),535)
put('¶',merge(ellipse(75,345,260,330),rect(205,345,130,330),line((335,-100),(335,700)),line((450,-100),(450,700))),540)
put('·',dot(110,270,24),220);put('¸',move(marks['\u0327'],x=140),280)
for c,num,den in [('¼','1','4'),('½','1','2'),('¾','3','4')]:put(c,merge(move(P[num],x=-5,y=365,sx=.47,sy=.47),line((170,-15),(600,710),width=35),move(P[den],x=460,sx=.47,sy=.47)),750)
put('×',merge(line((85,105),(475,495)),line((85,495),(475,105))),560)
put('÷',merge(line((75,300),(485,300)),dot(280,110,27),dot(280,490,27)),560)
# Precomposed Latin accents. Base anchors differ for real lowercase forms.
for code in range(160,384):
    c=chr(code)
    if c in P:continue
    decomp=unicodedata.normalize('NFD',c)
    if len(decomp)>1 and decomp[0] in P and all(m in marks for m in decomp[1:]):
        base=decomp[0];bp=P[base]
        if base in 'ij' and any(m not in '\u0327\u0328' for m in decomp[1:]):bp=line((110 if base=='i' else 130,0),(110 if base=='i' else 130,510)) if base=='i' else draw(('M',130,510),('L',130,-80),('C',130,-190,90,-225,-10,-193))
        accent=merge(*(move(marks[m],x=W[base]/2,y=0 if m in '\u0327\u0328' else (740 if base.isupper() else 550)) for m in decomp[1:]))
        put(c,merge(bp,accent),W[base])
# Common typographic/currency marks beyond the declared Latin ranges.
for c,b in [('‐','-'),('‑','-'),('−','-')]:put(c,P[b],W[b])
put('–',line((45,245),(515,245)),560);put('—',line((45,245),(875,245)),920)
put('…',merge(P['.'],move(P['.'],x=210),move(P['.'],x=420)),630)
put('•',dot(190,300,60),380)
put('‘',move(P[','],x=215,y=690,sx=-1,sy=-1),215);put('’',move(P[','],y=630),215)
put('‚',P[','],215);put('“',merge(P['‘'],move(P['‘'],x=180)),395);put('”',merge(P['’'],move(P['’'],x=180)),395);put('„',merge(P[','],move(P[','],x=180)),395)
put('‹',move(P['<'],sx=.65),345);put('›',move(P['>'],sx=.65),345)
put('€',merge(P['C'],line((35,275),(410,275),width=36),line((35,425),(410,425),width=36)),590)
put('™',merge(move(P['T'],y=340,sx=.48,sy=.48),move(P['M'],x=285,y=340,sx=.48,sy=.48)),680)
put('ẞ',move(P['ß'],sy=1.03),535)
for mark,path in marks.items():put(mark,path,0)
REQUIRED=set(range(32,127))|set(range(160,384))|set(map(ord,'‐‑–—‘’‚“”„•…‹›€™−ẞ'))|set(map(ord,marks))
missing=REQUIRED-set(map(ord,P));assert not missing,[(hex(c),unicodedata.name(chr(c))) for c in sorted(missing)]
# Convert original paths to deterministic quadratic-compatible contours. Fine
# flattening avoids platform-specific Boolean curve representation in the TTF.
def glyph(path):
    pen=TTGlyphPen(None);polys=move(path,sx=4,sy=4).toSubpathPolygons()
    for poly in polys:
        pts=[]
        for q in poly:
            xy=(round(q.x()/4),round(q.y()/4))
            if not pts or pts[-1]!=xy:pts.append(xy)
        if pts and pts[0]==pts[-1]:pts.pop()
        if len(set(pts))<3:continue
        depth=sum(other.containsPoint(poly[0],Qt.FillRule.OddEvenFill) for other in polys if other is not poly)
        area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(pts,pts[1:]+pts[:1]))
        if (area>0)!=bool(depth%2):pts.reverse()
        pen.moveTo(pts[0])
        for point in pts[1:]:pen.lineTo(point)
        pen.closePath()
    return pen.glyph()
names={ord(c):f'uni{ord(c):04X}' for c in P};order=['.notdef']+[names[cp] for cp in sorted(names)]
glyphs={names[ord(c)]:glyph(p) for c,p in P.items()};glyphs['.notdef']=glyph(merge(line((60,0),(60,700),(460,700),(460,0),(60,0)),line((60,0),(460,700))))
fb=FontBuilder(1000,isTTF=True);fb.setupGlyphOrder(order);fb.setupCharacterMap(names);fb.setupGlyf(glyphs)
fb.setupHorizontalMetrics({n:(520 if n=='.notdef' else W[chr(next(cp for cp,v in names.items() if v==n))],g.xMin if hasattr(g,'xMin') else 0) for n,g in glyphs.items()})
fb.setupHorizontalHeader(ascent=960,descent=-260,lineGap=30)
fb.setupNameTable({'familyName':'Zuuned Sans','styleName':'Regular','fullName':'Zuuned Sans Regular','psName':'ZuunedSans-Regular','uniqueFontIdentifier':'ZuunedSans-Regular-1.0','version':'Version 1.000',
 'copyright':'Copyright 2026 Zuuned contributors.','description':'Original humanist sans with distinct lowercase and declared Latin coverage. Generated from authored geometry; no external font outlines.','licenseDescription':'Licensed under the SIL Open Font License, Version 1.1. No Reserved Font Names.','licenseInfoURL':'https://openfontlicense.org/open-font-license-official-text/'})
fb.setupOS2(sTypoAscender=960,sTypoDescender=-260,sTypoLineGap=30,usWinAscent=960,usWinDescent=260,sCapHeight=700,sxHeight=510,usWeightClass=400,fsType=0)
fb.setupPost();fb.setupMaxp()
features=['feature kern {']
for a,b,value in [('A','V',-50),('A','W',-40),('A','Y',-45),('V','A',-50),('W','A',-40),('Y','A',-45),('T','o',-45),('T','a',-45),('T','e',-45),('Y','o',-40),('L','T',-25),('L','Y',-35),('F','a',-20)]:features.append(f'pos {names[ord(a)]} {names[ord(b)]} {value};')
features.append('} kern;')
for m in marks:features.append(f'markClass {names[ord(m)]} <anchor 0 0> @{"below" if m in chr(0x327)+chr(0x328) else "above"};')
features.append('feature mark {')
for c in P:
    if c.isalpha() and W[c]>0:features.append(f'pos base {names[ord(c)]} <anchor {W[c]//2} {740 if c.isupper() else 550}> mark @above <anchor {W[c]//2} 0> mark @below;')
features.append('} mark;');addOpenTypeFeaturesFromString(fb.font,'\n'.join(features))
fb.font.recalcTimestamp=False;fb.font['head'].created=3875126400;fb.font['head'].modified=3875126400
fb.save(OUT/'ZuunedSans-Regular.ttf')
manifest={'family':'Zuuned Sans','version':'1.000','license':'OFL-1.1','mappedCharacters':len(names),'glyphs':len(order),'scope':'Printable ASCII; Latin-1 Supplement; Latin Extended-A; selected punctuation/currency and combining accents. Distinct lowercase. No Greek/Cyrillic/CJK coverage claim.','characters':[{'code':f'U+{cp:04X}','character':chr(cp),'name':unicodedata.name(chr(cp),'UNNAMED'),'glyph':names[cp]} for cp in sorted(names)]}
(OUT/'coverage.json').write_text(json.dumps(manifest,indent=2,ensure_ascii=False)+'\n')
print(f'Built Zuuned Sans: {len(names)} characters, {len(order)} glyphs')
