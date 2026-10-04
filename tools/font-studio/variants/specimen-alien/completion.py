"""Original supplementary glyphs and declared Latin display-font coverage."""
import unicodedata
from PySide6.QtGui import QTransform, QPainterPath

EXTRAS='–—‘’‚“”„†‡•…‰‹›€™℗−±×÷≠≤≥∞√≈←↑→↓↔✓✕★☆♥'
MARKS='\u0300\u0301\u0302\u0303\u0304\u0306\u0307\u0308\u030a\u030b\u030c\u0327\u0328'
REQUIRED=set(range(32,127))|set(range(160,384))|set(map(ord,EXTRAS+MARKS))|{0x1e9e}

def extend(ns):
    paths,cmap,widths,accents=(ns[k] for k in ('paths','cmap','widths','accents'))
    line,rect,merge,polygon=(ns[k] for k in ('line','rect','merge','polygon'))
    def move(p,x=0,y=0,sx=1,sy=1):return QTransform(sx,0,0,sy,x,y).map(p)
    def put(c,p,w=620):
        if unicodedata.category(c)[0] in 'PS' and not p.isEmpty(): p=ns['finish'](p,ord(c),False)
        name=f'uni{ord(c):04X}';paths[name]=p;cmap[ord(c)]=name;widths[name]=w
        return name
    def original(c):return paths[cmap[ord(c)]]
    def ring():
        outer=polygon([(280,700),(465,620),(545,350),(465,80),(280,0),(95,80),(15,350),(95,620)])
        inner=polygon([(280,625),(407,565),(470,350),(407,135),(280,75),(153,135),(90,350),(153,565)])
        return outer.subtracted(inner)
    accents.update({
        '\u0304':rect(150,795,280,60),
        '\u0306':line([(145,865),(225,785),(350,785),(430,865)],55),
        '\u0307':rect(247,785,86,86),
        '\u030b':merge(line([(145,775),(250,890)],56),line([(315,775),(420,890)],56)),
        '\u030c':line([(150,880),(290,780),(430,880)],60),
        '\u0328':line([(400,35),(310,-75),(340,-155),(450,-140)],57),
    })
    # Special Latin forms; composed from our own drawn skeletons/outlines only.
    specials={
        'Æ':(merge(move(paths['A'],sx=.78),move(paths['E'],x=395,sx=.78)),920),
        'Œ':(merge(move(paths['O'],sx=.8),move(paths['E'],x=400,sx=.78)),930),
        'Ð':(merge(paths['D'],rect(0,295,280,75)),620),
        'Đ':(merge(paths['D'],rect(0,295,280,75)),620),
        'Ø':(merge(paths['O'],line([(90,50),(455,655)],72)),620),
        'Þ':(merge(line([(100,0),(100,700)],160),line([(100,505),(360,505),(465,415),(455,280),(350,200),(100,200)],135)),620),
        'Ħ':(merge(paths['H'],rect(0,500,560,63)),620),
        'Ĳ':(merge(paths['I'],move(paths['J'],x=310)),930),
        'Ŀ':(merge(paths['L'],rect(340,330,95,95)),620),
        'Ł':(merge(paths['L'],line([(0,220),(320,430)],80)),620),
        'Ŋ':(merge(paths['N'],line([(470,110),(470,-70),(395,-155),(255,-155)],130)),620),
        'Ŧ':(merge(paths['T'],rect(100,315,355,64)),620),
        'ŉ':(merge(move(original('’'),x=-170,y=45),move(paths['N'],x=185)),815),
        'ß':(line([(100,0),(100,525),(185,630),(335,630),(420,545),(360,430),(270,355),(410,255),(430,140),(345,65),(230,65)],137),590),
    }
    for c,(p,w) in specials.items():
        if c in 'Þß': p=ns['finish'](p,ord(c),False)
        put(c,p,w)
    put('ẞ',original('ß'),590)
    for c in 'æœðđøþħĳŀłŋŧ':
        u=c.upper();put(c,original(u),widths[cmap[ord(u)]])
    put('ĸ',paths['K']);put('ı',paths['I'],350);put('ſ',paths['S'])
    # Complete precomposed Latin-1 / Extended-A, retaining the chosen unicase look.
    for code in range(192,384):
        c=chr(code)
        if code in cmap:continue
        d=unicodedata.normalize('NFD',c.upper())
        if len(d)==2 and d[0] in paths and d[1] in accents:
            accent=move(accents[d[1]],x=-127 if d[0]=='I' else 0)
            put(c,merge(paths[d[0]],accent),widths[d[0]])
        elif c.upper() in paths:
            put(c,paths[c.upper()],widths[c.upper()])
    symbols={
      '¡':move(original('!'),x=540,y=700,sx=-1,sy=-1),
      '¿':move(original('?'),x=570,y=700,sx=-1,sy=-1),
      '¢':merge(paths['C'],rect(255,-50,55,800)),
      '¤':merge(move(ring(),x=95,y=125,sx=.65,sy=.65),line([(85,100),(485,600)],65),line([(85,600),(485,100)],65)),
      '¥':merge(paths['Y'],rect(90,240,395,60),rect(90,355,395,60)),
      '¦':merge(rect(250,-20,85,280),rect(250,410,85,290)),
      '§':line([(430,615),(340,660),(185,600),(170,465),(410,270),(410,160),(320,75),(160,100),(135,170),(170,245),(410,455),(410,535),(350,580),(200,550),(160,450),(200,345),(345,245)],65),
      '¨':move(accents['\u0308'],y=-155),
      '©':merge(ring(),move(paths['C'],x=160,y=195,sx=.45,sy=.45)),
      '®':merge(ring(),move(paths['R'],x=160,y=195,sx=.45,sy=.45)),
      '℗':merge(ring(),move(paths['P'],x=160,y=195,sx=.45,sy=.45)),
      'ª':move(paths['A'],x=120,y=360,sx=.55,sy=.48),
      'º':move(paths['O'],x=120,y=360,sx=.55,sy=.48),
      '«':merge(line([(250,560),(90,350),(250,140)],75),line([(480,560),(320,350),(480,140)],75)),
      '»':merge(line([(90,560),(250,350),(90,140)],75),line([(320,560),(480,350),(320,140)],75)),
      '¬':line([(70,420),(475,420),(475,210)],85),
      '¯':rect(110,650,380,65),
      '°':move(ring(),x=150,y=410,sx=.48,sy=.4),
      '±':merge(original('+'),rect(55,10,460,75)),
      '´':move(accents['\u0301'],y=-120),
      'µ':line([(100,500),(100,-160),(100,150),(190,70),(300,70),(400,160),(400,500),(400,60),(505,60)],95),
      '¶':merge(rect(295,0,80,700),rect(440,0,80,700),rect(200,620,320,80),polygon([(300,700),(130,700),(40,570),(40,430),(140,330),(300,330)])),
      '¸':move(accents['\u0327'],y=180),
      '×':merge(line([(100,130),(460,570)],80),line([(100,570),(460,130)],80)),
      '÷':merge(rect(60,300,460,80),rect(240,95,90,90),rect(240,510,90,90)),
      '†':merge(rect(245,0,75,700),rect(80,440,405,75)),
      '‡':merge(rect(245,0,75,700),rect(80,440,405,75),rect(80,210,405,75)),
      '−':rect(60,305,460,85),
      '∞':line([(285,350),(180,475),(95,475),(45,350),(95,225),(180,225),(390,475),(480,475),(530,350),(480,225),(390,225),(285,350)],65),
      '√':line([(45,275),(165,90),(300,640),(520,640)],78),
      '✓':line([(65,285),(220,95),(500,635)],85),
      '✕':merge(line([(100,90),(480,620)],95),line([(100,620),(480,90)],95)),
      '♥':polygon([(285,45),(35,360),(35,535),(130,620),(230,605),(285,515),(345,605),(440,620),(535,535),(535,360)]),
    }
    for c,p in symbols.items():
        if c=='µ':p=ns['finish'](p,ord(c),False)
        put(c,p)
    for c,base,y in [('²','2',350),('³','3',350),('¹','1',350)]:put(c,move(paths[base],x=70,y=y,sx=.52,sy=.5),390)
    for c,top,bottom in [('¼','1','4'),('½','1','2'),('¾','3','4')]:
        put(c,merge(move(paths[top],y=370,sx=.46,sy=.46),line([(100,0),(520,700)],55),move(paths[bottom],x=350,sx=.46,sy=.46)),680)
    put('\u00ad',QPainterPath(),0)  # soft hyphen is a format character
    put('‚',move(original('’'),y=-515),410)
    put('„',move(original('”'),y=-515))
    put('‹',move(original('<'),x=120,sx=.65),440)
    put('›',move(original('>'),x=120,sx=.65),440)
    put('™',merge(move(paths['T'],y=350,sx=.48,sy=.48),move(paths['M'],x=300,y=350,sx=.48,sy=.48)),650)
    put('‰',merge(move(original('%'),sx=.8),move(original('°'),x=470,y=-360)),890)
    put('≠',merge(original('='),line([(120,80),(460,620)],65)))
    for c,b in [('≤','<'),('≥','>')]:put(c,merge(move(original(b),y=100,sy=.8),rect(85,20,410,65)))
    put('≈',merge(move(original('~'),y=110),move(original('~'),y=-110)))
    arrow=merge(line([(55,350),(485,350)],70),line([(345,515),(510,350),(345,185)],75))
    for c,angle in [('→',0),('↑',90),('←',180),('↓',270)]:
        t=QTransform();t.translate(285,350);t.rotate(angle);t.translate(-285,-350);put(c,t.map(arrow))
    put('↔',merge(arrow,line([(200,515),(35,350),(200,185)],75)))
    import math
    star=polygon([(285+245*(1 if j%2==0 else .43)*math.sin(j*math.pi/5),
                   350+290*(1 if j%2==0 else .43)*math.cos(j*math.pi/5)) for j in range(10)])
    put('★',star);put('☆',star.subtracted(move(star,x=85.5,y=105,sx=.7,sy=.7)))
    for c in MARKS:put(c,accents[c],0)
    missing=REQUIRED-set(cmap)
    assert not missing,[(hex(cp),unicodedata.name(chr(cp),'?')) for cp in sorted(missing)]
    return MARKS,REQUIRED
