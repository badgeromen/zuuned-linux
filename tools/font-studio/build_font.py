#!/usr/bin/env python3
"""Original Specimen outlines. No source fonts are read or transformed.
Developer-only generator: Python, PySide6, fontTools, Pillow. No app dependency.
"""
from pathlib import Path
import math, random, unicodedata
from PySide6.QtCore import QPointF, Qt
from PySide6.QtGui import QPainterPath, QPainterPathStroker, QPolygonF
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.feaLib.builder import addOpenTypeFeaturesFromString
from fontTools.ttLib import TTFont

OUT = Path(__file__).resolve().parent / 'output'
OUT.mkdir(exist_ok=True)

def polygon(points):
    p = QPainterPath()
    p.addPolygon(QPolygonF([QPointF(x,y) for x,y in points])); p.closeSubpath()
    return p

def rect(x,y,w,h):
    p=QPainterPath(); p.addRect(x,y,w,h); return p

def line(points,width=165):
    p=QPainterPath(); p.moveTo(*points[0])
    for xy in points[1:]: p.lineTo(*xy)
    s=QPainterPathStroker(); s.setWidth(width)
    s.setCapStyle(Qt.PenCapStyle.FlatCap); s.setJoinStyle(Qt.PenJoinStyle.BevelJoin)
    return s.createStroke(p)

def merge(*paths):
    p=QPainterPath()
    for x in paths: p=p.united(x)
    return p

# Hand-authored centerline drawings in a 560 x 700 design space. Numeric
# proportions, paths, punctuation and decorative cuts are original to this file.
# The alphabet uses clipped bowls, off-centre crossbars and a slight forward lean.
def letter(c):
    stem=lambda x=100: line([(x,0),(x,700)])
    cross=lambda y,x1=100,x2=470: line([(x1,y),(x2,y)])
    bowl=lambda: line([(160,70),(100,135),(100,565),(205,650),(400,605),(485,520),(450,140),(365,45),(160,70)])
    paths={
      'A':lambda:merge(line([(80,0),(210,600),(320,650),(490,0)]),line([(145,225),(425,280)],140)),
      'B':lambda:merge(stem(),line([(100,630),(390,630),(470,550),(470,455),(370,365),(100,365)]),line([(100,365),(395,365),(485,275),(485,150),(400,70),(100,70)])),
      'C':lambda:line([(475,555),(400,630),(168,630),(100,552),(100,148),(175,70),(410,70),(480,145)]),
      'D':lambda:merge(stem(),line([(100,630),(335,630),(470,510),(470,185),(340,70),(100,70)])),
      'E':lambda:merge(stem(),cross(630),cross(352,100,410),cross(70)),
      'F':lambda:merge(stem(),cross(630),cross(352,100,415)),
      'G':lambda:merge(line([(477,550),(397,630),(165,630),(100,552),(100,145),(175,70),(408,70),(475,140),(475,330),(310,330)])),
      'H':lambda:merge(stem(),stem(470),line([(100,300),(260,355),(470,325)],142)),
      'I':lambda:line([(155,0),(155,700)],172),
      'J':lambda:merge(cross(630,190,475),line([(405,630),(405,150),(330,70),(175,70),(100,155),(100,260)])),
      'K':lambda:merge(stem(),line([(470,700),(110,315)]),line([(280,485),(490,0)])),
      'L':lambda:merge(stem(),cross(70)),
      'M':lambda:line([(100,0),(100,630),(150,655),(285,430),(410,600),(470,630),(470,0)],128),
      'N':lambda:merge(stem(),stem(470),line([(100,620),(245,420),(320,390),(470,75)],128)),
      'O':bowl,
      'P':lambda:merge(stem(),line([(100,630),(390,630),(475,550),(475,435),(390,350),(100,350)])),
      'Q':lambda:merge(bowl(),line([(328,225),(525,-38)],110)),
      'R':lambda:merge(stem(),line([(100,630),(390,630),(475,550),(475,450),(380,355),(100,355)]),line([(310,355),(492,0)])),
      'S':lambda:line([(478,555),(403,630),(175,630),(100,552),(100,445),(175,365),(395,335),(475,255),(475,150),(397,70),(170,70),(90,145)]),
      'T':lambda:merge(line([(265,0),(305,635)],165),line([(35,600),(285,645),(535,610)],165)),
      'U':lambda:line([(100,700),(100,150),(175,70),(395,70),(470,150),(470,700)]),
      'V':lambda:line([(70,700),(245,70),(320,70),(500,700)]),
      'W':lambda:line([(65,700),(125,70),(190,70),(280,365),(370,70),(435,70),(495,700)],115),
      'X':lambda:merge(line([(85,700),(490,0)],140),line([(490,700),(85,0)],140)),
      'Y':lambda:merge(line([(70,700),(280,350),(495,700)]),line([(280,350),(280,0)])),
      'Z':lambda:line([(65,630),(485,630),(85,70),(510,70)]),
      '0':bowl,
      '1':lambda:merge(line([(145,510),(280,630),(335,630),(335,70)]),cross(70,130,480)),
      '2':lambda:line([(100,530),(175,630),(395,630),(470,550),(470,440),(105,145),(105,70),(495,70)]),
      '3':lambda:merge(line([(90,630),(470,630),(470,430),(385,350),(260,350)]),line([(385,350),(470,270),(470,150),(390,70),(175,70),(95,145)])),
      '4':lambda:merge(line([(330,700),(90,310),(90,265),(510,265)],126),line([(420,700),(420,0)],126)),
      '5':lambda:line([(480,630),(110,630),(110,380),(380,380),(470,285),(470,150),(390,70),(170,70),(95,145)]),
      '6':lambda:merge(line([(455,590),(375,630),(185,630),(100,525),(100,150),(175,70),(390,70),(470,150),(470,290),(395,365),(100,365)])),
      '7':lambda:line([(75,630),(480,630),(480,575),(235,0)]),
      '8':lambda:merge(line([(180,630),(390,630),(465,555),(465,440),(390,350),(175,350),(100,435),(100,550),(180,630)]),line([(175,350),(100,260),(100,145),(175,70),(395,70),(470,145),(470,265),(390,350)])),
      '9':lambda:line([(465,335),(180,335),(100,420),(100,550),(175,630),(390,630),(465,550),(465,150),(390,70),(170,70),(110,125)]),
    }
    return paths[c]()

def punctuation(c):
    dot=lambda x=245,y=0: polygon([(x,y),(x+92,y),(x+105,y+98),(x+8,y+108)])
    if c=='.': return dot()
    if c==',': return merge(dot(),polygon([(245,10),(337,10),(255,-120),(213,-120)]))
    if c==':': return merge(dot(y=60),dot(y=410))
    if c==';': return merge(punctuation(','),dot(y=410))
    if c=='!': return merge(line([(280,210),(292,700)],128),dot())
    if c=='?': return merge(line([(100,545),(185,630),(385,630),(470,550),(470,460),(290,315),(290,220)],130),dot())
    if c in '-–—_': return rect(80,-65 if c=='_' else 285,740 if c=='—' else 470 if c=='–' else 400,95)
    if c in '/\\': return line([(90 if c=='/' else 465,-45),(465 if c=='/' else 90,740)],100)
    if c in "'’‘": return polygon([(242,515),(320,540),(340,700),(235,700)])
    if c in '"“”': return merge(punctuation("'"),polygon([(400,515),(478,540),(498,700),(393,700)]))
    if c in '()[]{}':
        if c in '()': p=line([(350,735),(235,575),(210,350),(235,125),(350,-35)],92)
        elif c in '[]': p=line([(365,735),(210,735),(210,-35),(365,-35)],92)
        else: p=line([(375,735),(255,735),(255,430),(165,350),(255,265),(255,-35),(375,-35)],83)
        if c in ')]}':
            from PySide6.QtGui import QTransform
            p=QTransform(-1,0,0,1,570,0).map(p)
        return p
    if c=='+': return merge(rect(55,300,460,95),rect(238,115,95,460))
    if c=='=': return merge(rect(70,195,430,92),rect(70,405,430,92))
    if c in '<>': return line([(425 if c=='<' else 145,600),(135 if c=='<' else 435,350),(425 if c=='<' else 145,100)],100)
    if c=='|': return rect(240,-75,95,850)
    if c=='*': return merge(*[line([(285,500),(285+185*math.sin(a),500+185*math.cos(a))],85) for a in [i*math.pi*2/5 for i in range(5)]])
    if c=='#': return merge(rect(40,220,490,90),rect(40,440,490,90),line([(170,0),(245,700)],85),line([(345,0),(420,700)],85))
    if c=='$': return merge(letter('S'),rect(250,-90,65,870))
    if c=='%': return merge(line([(110,0),(465,700)],85),rect(60,450,170,235).subtracted(rect(115,510,60,115)),rect(345,0,170,235).subtracted(rect(400,60,60,115)))
    if c=='&': return merge(line([(480,0),(125,455),(125,555),(190,630),(315,630),(385,555),(385,470),(115,230),(115,145),(190,70),(355,70),(480,290)],105))
    if c=='@': return merge(line([(475,80),(160,80),(85,170),(85,530),(170,620),(410,620),(495,535),(495,230),(375,230),(375,460),(215,460),(190,390),(190,250),(255,205),(375,280)],75))
    if c=='^': return line([(120,435),(280,645),(440,435)],88)
    if c=='`': return line([(195,700),(305,535)],80)
    if c=='~': return line([(75,290),(170,375),(355,285),(480,375)],80)
    if c=='€': return merge(letter('C'),rect(10,260,355,70),rect(10,390,355,70))
    if c=='£': return merge(line([(450,550),(370,630),(225,630),(170,550),(190,120),(110,70),(470,70)],120),rect(70,310,310,85))
    if c=='…': return merge(dot(55),dot(245),dot(435))
    if c in '•·': return polygon([(220,240),(330,240),(360,320),(315,390),(210,380),(185,300)])
    raise ValueError(c)

def finish(p,seed,stencil=True):
    rng=random.Random(seed)
    # Irregular stencil edges are drawn into the outline, not painted over in
    # a preview. Preserve our underlying original letter shapes.
    rough=QPainterPath(); rough.setFillRule(Qt.FillRule.OddEvenFill)
    for contour in p.toSubpathPolygons():
        points=[]
        for a,b in zip(list(contour),list(contour)[1:]):
            dx=b.x()-a.x(); dy=b.y()-a.y(); length=math.hypot(dx,dy)
            steps=max(1,math.ceil(length/18))
            for i in range(steps):
                t=i/steps; shift=rng.uniform(-4.5,4.5)
                points.append((a.x()+dx*t-dy/max(1,length)*shift,
                               a.y()+dy*t+dx/max(1,length)*shift))
        if len(points)>2: rough.addPath(polygon(points))
    p=rough.simplified()
    bridges=QPainterPath()
    if stencil:
        # Each glyph has deliberately authored bridge positions/directions.
        # Cuts open enclosed counters or articulate an individual stroke.
        cuts={
          'A':[(215,630,340,630,44),(70,215,240,280,42)],
          'B':[(375,470,550,530,46),(340,40,365,160,45)],
          'C':[(20,330,185,395,48)],
          'D':[(315,540,425,685,46)],
          'E':[(20,435,180,465,48),(305,0,330,140,40)],
          'F':[(265,555,290,715,47)],
          'G':[(10,275,190,340,46),(385,300,460,395,40)],
          'H':[(210,260,250,395,45)],
          'I':[(45,365,265,430,52)],
          'J':[(320,370,495,425,50)],
          'K':[(290,470,390,600,48),(20,235,185,280,40)],
          'L':[(235,-10,275,150,49)],
          'M':[(12,405,175,470,48),(380,530,520,570,43)],
          'N':[(220,365,350,455,46)],
          'O':[(10,470,200,510,47),(390,200,555,240,43)],
          'P':[(270,560,310,715,50)],
          'Q':[(355,525,470,660,46),(365,30,465,125,42)],
          'R':[(390,485,560,555,47),(330,150,485,205,43)],
          'S':[(245,265,275,430,50)],
          'T':[(345,540,405,725,49)],
          'U':[(10,415,185,470,48),(345,-5,380,165,43)],
          'V':[(40,450,230,495,48)],
          'W':[(285,175,435,250,46),(40,475,190,520,43)],
          'X':[(210,330,355,390,46)],
          'Y':[(290,435,440,485,48)],
          'Z':[(210,250,350,350,49)],
          '0':[(230,535,270,720,48),(310,-15,345,165,42)],
          '1':[(230,285,440,350,48)],
          '2':[(280,535,315,720,46)],
          '3':[(355,160,560,235,48)],
          '4':[(315,340,520,390,48)],
          '5':[(15,455,200,485,47)],
          '6':[(260,-10,310,170,48)],
          '7':[(240,240,425,305,48)],
          '8':[(10,475,190,505,48),(385,170,555,235,44)],
          '9':[(265,540,305,720,48)],
        }
        assert set(cuts)==set('ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789')
        for x1,y1,x2,y2,width in cuts[chr(seed)]:
            bridge=line([(x1,y1),(x2,y2)],width)
            assert not p.intersected(bridge).isEmpty(), chr(seed)
            bridges=bridges.united(bridge)
        p=p.subtracted(bridges)
    body=p
    bounds=body.boundingRect()
    def shard(x,y,r):
        return polygon([(x+math.cos(j*math.pi*2/5)*r*rng.uniform(.45,1.4),
                         y+math.sin(j*math.pi*2/5)*r*rng.uniform(.45,1.4))
                        for j in range(5)])
    # Clumped missing paint: concentrated patches, not evenly spread confetti.
    for _ in range(11 if stencil else 4):
        x=rng.uniform(bounds.left(),bounds.right())
        y=rng.uniform(bounds.top(),bounds.bottom())
        for j in range(rng.randint(9,22)):
            xx=x+rng.gauss(0,32); yy=y+rng.gauss(0,43)
            p=p.subtracted(shard(xx,yy,rng.uniform(4,15)))
    for _ in range(520 if stencil else 140):
        x=rng.uniform(bounds.left()-12,bounds.right()+12)
        y=rng.uniform(bounds.top()-12,bounds.bottom()+12)
        if not body.contains(QPointF(x,y)): continue
        edge=any(not body.contains(QPointF(x+dx,y+dy))
                 for dx,dy in [(22,0),(-22,0),(0,22),(0,-22)])
        if edge or rng.random()<.16:
            p=p.subtracted(shard(x,y,rng.uniform(4,17) if edge else rng.uniform(2,8)))
    if stencil:
        # Real detached ink islands in the glyph, strongest close to the rim,
        # with a few larger flecks away from the top and side edges.
        for _ in range(1050):
            x=rng.uniform(bounds.left()-95,bounds.right()+95)
            y=rng.uniform(bounds.top()-95,bounds.bottom()+110)
            if body.contains(QPointF(x,y)): continue
            distance=min((d for d in (10,22,40,65,90,115)
                          if any(body.contains(QPointF(x+math.cos(a)*d,y+math.sin(a)*d))
                                 for a in (0,1.57,3.14,4.71))),default=999)
            if distance==999 or rng.random()> .95-distance/155: continue
            # Fine droplets dominate; occasional irregular paint blobs add texture.
            # All extra paint stays outside the intact stencil body.
            radius=rng.uniform(1.2,4.5) if rng.random()<.88 else rng.uniform(7,13)
            fleck=shard(x,y,radius)
            if not fleck.intersects(bridges): p=p.united(fleck)
    return p

def glyph(p):
    pen=TTGlyphPen(None)
    # Qt Boolean operations emit oriented outer/hole subpaths. Preserve contour
    # direction, then reverse for TrueType's clockwise outer convention.
    polygons=p.toSubpathPolygons()
    for poly in polygons:
        pts=[]
        for q in poly:
            xy=(round(q.x()+q.y()*.035+25),round(q.y()))
            if not pts or pts[-1]!=xy: pts.append(xy)
        if pts and pts[-1]==pts[0]: pts.pop()
        if len(set(pts))<3: continue
        depth=sum(other.containsPoint(poly[0],Qt.FillRule.OddEvenFill) for other in polygons if other is not poly)
        area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(pts,pts[1:]+pts[:1]))
        if (area>0) != bool(depth%2): pts.reverse()
        pen.moveTo(pts[0])
        for point in pts[1:]: pen.lineTo(point)
        pen.closePath()
    return pen.glyph()

fb=FontBuilder(1000,isTTF=True)
paths={}; cmap={}; widths={}; chars='ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
for c in chars:
    paths[c]=finish(letter(c),ord(c)); widths[c]=350 if c=='I' else 620
for c in '!"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~–—‘’“”€£…•·':
    name=f'uni{ord(c):04X}'; paths[name]=finish(punctuation(c),ord(c),False)
    cmap[ord(c)]=name; widths[name]=880 if c=='—' else 720 if c=='–' else 610
# Avoid unintentional enormous punctuation spacing in headings.
for c in ".,:;!'’‘`": widths[cmap[ord(c)]]=410
paths['space']=QPainterPath(); widths['space']=290; cmap[32]='space'; cmap[160]='space'
paths['.notdef']=rect(80,0,430,700).subtracted(rect(150,70,290,560)); widths['.notdef']=650
for c in chars: cmap[ord(c)]=c
for c in 'abcdefghijklmnopqrstuvwxyz': cmap[ord(c)]=c.upper()
# Latin-1 accents use our original capital base and original accent outlines.
accents={
 '\u0300':line([(225,885),(335,775)],68), '\u0301':line([(240,775),(350,885)],68),
 '\u0302':line([(165,770),(285,870),(405,770)],65),
 '\u0303':line([(150,795),(220,850),(335,795),(425,850)],60),
 '\u0308':merge(rect(150,790,85,85),rect(345,790,85,85)),
 '\u030a':rect(215,765,150,150).subtracted(rect(260,810,60,60)),
 '\u0327':line([(300,0),(260,-80),(335,-100),(285,-170),(185,-170)],65),
}
for code in range(192,256):
    c=chr(code); dec=unicodedata.normalize('NFD',c.upper())
    if len(dec)==2 and dec[0] in paths and dec[1] in accents:
        name=f'uni{code:04X}'; accent=accents[dec[1]]
        if dec[0]=='I':
            from PySide6.QtGui import QTransform
            accent=QTransform.fromTranslate(-127,0).map(accent)
        paths[name]=merge(paths[dec[0]],accent); widths[name]=widths[dec[0]]; cmap[code]=name
order=['.notdef','space']+[n for n in paths if n not in ['.notdef','space']]
fb.setupGlyphOrder(order); fb.setupCharacterMap(cmap)
glyphs={n:glyph(paths[n]) for n in order}; fb.setupGlyf(glyphs)
fb.setupHorizontalMetrics({n:(widths[n],glyphs[n].xMin if hasattr(glyphs[n],'xMin') else 0) for n in order})
fb.setupHorizontalHeader(ascent=960,descent=-230,lineGap=30)
fb.setupNameTable({'familyName':'Specimen','styleName':'Regular','uniqueFontIdentifier':'Specimen-Regular-0.4',
 'fullName':'Specimen Regular','psName':'Specimen-Regular','version':'Version 0.400',
 'description':'Original geometric stencil display face. Uppercase forms with lowercase aliases. Deterministic edge wear. Built from authored geometry; no third-party font outlines.',
 'licenseDescription':'Original project asset. Distribution license not yet assigned by the project owner.'})
fb.setupOS2(sTypoAscender=960,sTypoDescender=-230,sTypoLineGap=30,usWinAscent=960,usWinDescent=230,sCapHeight=700,sxHeight=700,usWeightClass=850,fsType=0)
fb.setupPost(); fb.setupMaxp()
feature='feature kern {\n'+'\n'.join(f'pos {a} {b} {v};' for a,b,v in [('A','V',-65),('A','W',-50),('A','Y',-60),('T','A',-45),('V','A',-65),('W','A',-50),('Y','A',-60),('L','T',-35),('L','Y',-45),('T','O',-20),('Y','O',-30)])+'\n} kern;'
addOpenTypeFeaturesFromString(fb.font,feature)
fb.font.recalcTimestamp=False; fb.font['head'].created=3873700800; fb.font['head'].modified=3873700800
fontpath=OUT/'Specimen-Regular.ttf'; fb.save(fontpath)
font=TTFont(fontpath)
assert all(i in font.getBestCmap() for i in range(32,127))
assert font['OS/2'].fsType==0
for name in font.getGlyphOrder():
    g=font['glyf'][name]
    if g.numberOfContours: assert len(g.coordinates)>0
print(f'{fontpath}: {len(order)} glyphs, {len(cmap)} mapped characters; ASCII coverage verified')
