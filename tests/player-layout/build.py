from pathlib import Path
import subprocess, shlex
repo=Path(__file__).resolve().parents[2]; build=repo/'build'; audit=build/'player-layout'
audit.mkdir(parents=True,exist_ok=True)
(audit/'audit.h').write_text((repo/'tests/player-layout/audit.h').read_text())
src=(repo/'src/main.cpp').read_text()
src=src.replace('int main(int argc, char *argv[]) {','#include "audit.h"\nint main(int argc, char *argv[]) {')
src=src.replace('QSGRendererInterface::OpenGL','QSGRendererInterface::Software')
assert src.count('    return app.exec();') == 1, 'main entry point changed; review audit injection'
src=src.replace('    return app.exec();','    QTimer::singleShot(3500, &app, [&] { runLayoutAudit(engine, player, videoPlayer); });\n    return app.exec();')
(audit/'main.cpp').write_text(src)
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','zuuned'],text=True).splitlines()
compile=next(shlex.split(c) for c in commands if ' -c ' in c and c.endswith('/src/main.cpp'))
compile[compile.index('-o')+1]=str(audit/'main.o')
compile[compile.index('-c')+1]=str(audit/'main.cpp')
compile[compile.index('-MF')+1]=str(audit/'main.d')
compile+=['-I'+str(repo/'src')]+shlex.split(subprocess.check_output(['pkg-config','--cflags','Qt6Test'],text=True))
subprocess.run(compile,cwd=build,check=True)
link=shlex.split(commands[-1].removeprefix(': && ').removesuffix(' && :'))
link=[str(audit/'main.o') if c=='CMakeFiles/zuuned.dir/src/main.cpp.o' else c for c in link]
link=[c for c in link if not c.startswith('-Wl,--dependency-file=')]
link[link.index('-o')+1]=str(audit/'zuuned-layout')
link+=['-lQt6Test']
subprocess.run(link,cwd=build,check=True)
