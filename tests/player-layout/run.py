from pathlib import Path
import subprocess, json, os, sys
root=Path(__file__).resolve().parents[2]/'build/player-layout'
cases=[('720p',1280,720,1),('768p',1366,768,1),('1080p',1920,1080,1),('1440p',2560,1440,1),('4k',3840,2160,1),('wide-short',2048,732,1),('4k-200pct',1920,1080,2),('1080p-150pct',1280,720,1.5)]
if len(sys.argv)>1:cases=[c for c in cases if c[0] in sys.argv[1:]]
if not cases:raise SystemExit('No matching screen cases')
for cmd in (
    ['ffmpeg','-y','-hide_banner','-loglevel','error','-f','lavfi','-i','anullsrc=r=22050:cl=mono','-t','120',str(root/'sample.wav')],
    ['ffmpeg','-y','-hide_banner','-loglevel','error','-f','lavfi','-i','color=c=0x273449:s=640x360:r=10','-t','120','-c:v','mpeg4','-q:v','8',str(root/'sample.mp4')]):
    subprocess.run(cmd,check=True)
errors=[]
for name,w,h,dpr in cases:
    out=root/name;out.mkdir(exist_ok=True)
    (out/'results.json').unlink(missing_ok=True)
    config=out/'screen.json';config.write_text(json.dumps({'screens':[{'name':'layout-audit','width':round(w*dpr),'height':round(h*dpr),'logicalDpi':96,'logicalBaseDpi':96}]}))
    env=dict(os.environ)
    env.update({'AUDIT_DIR':str(root),'AUDIT_CASE':name,'QT_QPA_PLATFORM':'offscreen:configfile='+str(config),'QT_QPA_OFFSCREEN_NO_GLX':'1','QT_SCALE_FACTOR':str(dpr),'QT_QUICK_BACKEND':'software','QT_QUICK_CONTROLS_STYLE':'Basic','QT_QPA_PLATFORMTHEME':'','ZUUNED_MPV_AO':'null','HOME':str(out/'home'),'XDG_DATA_HOME':str(out/'data'),'XDG_CONFIG_HOME':str(out/'config'),'XDG_CACHE_HOME':str(out/'cache'),'XDG_STATE_HOME':str(out/'state'),'XDG_RUNTIME_DIR':str(out/'runtime')})
    for n in ('HOME','XDG_DATA_HOME','XDG_CONFIG_HOME','XDG_CACHE_HOME','XDG_STATE_HOME','XDG_RUNTIME_DIR'):Path(env[n]).mkdir(mode=0o700,exist_ok=True)
    for n in ('DISPLAY','WAYLAND_DISPLAY','DBUS_SESSION_BUS_ADDRESS'):env.pop(n,None)
    cmd=['bwrap','--die-with-parent','--ro-bind','/','/','--bind',str(root),str(root),'--dev','/dev','--tmpfs','/sys','--tmpfs','/tmp','--unshare-net','--unshare-pid','--proc','/proc','--','dbus-run-session','--',str(root/'zuuned-layout')]
    with (out/'console.log').open('w') as log:
        try:
            r=subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=90)
            print(name,r.returncode,flush=True)
            if r.returncode or not (out/'results.json').exists():errors.append(name)
            else:
                rows=json.loads((out/'results.json').read_text())
                first=rows[0]
                failures=[row['case'] for row in rows if row.get('passed') is False or row.get('closed') is False or row.get('entered') is False or row.get('returned') is False]
                if failures:
                    print(name,'FAILED',', '.join(failures),flush=True);errors.append(name)
                if (first['width'],first['height'],first['dpr'],first['fullscreen'])!=(w,h,dpr,True):
                    print(name,'screen geometry mismatch',first['width'],first['height'],first['dpr']);errors.append(name)
        except subprocess.TimeoutExpired:
            print(name,'TIMEOUT',flush=True);errors.append(name)
if errors:raise SystemExit('Audit did not complete: '+', '.join(errors))
