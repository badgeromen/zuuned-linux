import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {spawnSync,execFileSync} from 'node:child_process';
const root=fs.mkdtempSync(path.join(os.tmpdir(),'zuuned-public-export-test-'));
const source=path.join(root,'private');
let checks=0;
function write(name,text){const dest=path.join(source,name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,text);}
function check(ok,name){checks++;if(!ok)throw Error(name);console.log('PASS '+name);}
try {
    write('tools/export-public-source.mjs',fs.readFileSync('tools/export-public-source.mjs'));
    write('tools/public-export-without-visualizer.mjs',fs.readFileSync('tools/public-export-without-visualizer.mjs'));
    write('.gitignore','libzune/\n'); write('.gitmodules','private submodule URL');
    write('src/library/TmdbClient.cpp','constexpr auto kBundledTmdbKey = "private-provider-token-for-export-test";\nconstexpr auto kBundledFanartKey = "private-fanart-token-for-export-test";');
    write('src/library/MusicIdentityClient.cpp','constexpr auto kBundledFanartKey = "private-fanart-token-for-export-test";');
    const values=Array.from({length:4},()=>crypto.randomBytes(32).toString('hex'));
    const names=['ENCRYPTION_KEY_HEX','MODULUS','PRIVATE_KEY','CERTIFICATES_HEX'];
    write('libzune/src/mtpz_keys.h',names.map((n,i)=>`static const char MTPZ_DEFAULT_${n}[] = "${values[i]}";`).join('\n'));
    write('libzune/src/plain.c','int example(void) { return 0; }\n');
    write('AGENTS.md','Public-safe guide\n'); fs.symlinkSync('AGENTS.md',path.join(source,'CLAUDE.md'));
    write('build-artifact.bin',Buffer.from('excluded artifact'));
    for(const dir of [source,path.join(source,'libzune')]) execFileSync('git',['init','-q',dir]);
    const run=(name,...flags)=>spawnSync(process.execPath,[path.join(source,'tools/export-public-source.mjs'),path.join(root,name),...flags],{encoding:'utf8'});
    let r=run('clean');check(r.status===0,'clean snapshot exports successfully');
    const clean=path.join(root,'clean');
    check(!fs.existsSync(path.join(clean,'.git'))&&!fs.existsSync(path.join(clean,'.gitmodules')),'private history and submodule URL omitted');
    check(!fs.existsSync(path.join(clean,'libzune/src/mtpz_keys.h'))&&!fs.existsSync(path.join(clean,'build-artifact.bin')),'private header and binary artifacts omitted');
    check(fs.existsSync(path.join(clean,'libzune/src/plain.c')),'libzune source included as ordinary files');
    check(fs.readFileSync(path.join(clean,'src/library/TmdbClient.cpp'),'utf8').includes('private-provider-token'),'approved provider defaults retained in public copy');
    check(fs.readFileSync(path.join(source,'src/library/TmdbClient.cpp'),'utf8').includes('private-provider-token'),'private provider default untouched');
    check(fs.lstatSync(path.join(clean,'CLAUDE.md')).isFile(),'safe internal symlink materialized');
    write('leak.txt',values[2]);r=run('leaked');
    check(r.status!==0&&!fs.existsSync(path.join(root,'leaked'))&&!r.stderr.includes(values[2]),'leak rejected before export without printing credentials');
    write('leak.txt',Buffer.from(values[2],'hex'));r=run('binary-leaked');check(r.status!==0&&!fs.existsSync(path.join(root,'binary-leaked')),'binary credential copy rejected');
    fs.unlinkSync(path.join(source,'leak.txt'));fs.symlinkSync('/etc/hosts',path.join(source,'external.txt'));
    r=run('external');check(r.status!==0&&!fs.existsSync(path.join(root,'external')),'external symlink refused');
    fs.unlinkSync(path.join(source,'external.txt'));r=run('clean');check(r.status!==0,'existing destination never overwritten');
    const inputs=['CMakeLists.txt','qml/Main.qml','qml/NowPlayingIsland.qml',
        'src/player/MpvController.cpp','tests/spoken-media/native/CMakeLists.txt'];
    const originals=new Map(inputs.map(name=>[name,fs.readFileSync(name)]));
    for(const [name,bytes] of originals) write(name,bytes);
    const omitted=['src/player/VisualizerService.cpp','src/player/VisualizerService.h',
        'src/player/PortalRenderer.h','src/player/VisualizerPresets.h',
        'src/player/PortalState.h','src/player/PortalShapeMorph.h',
        'src/player/MusicStreamIdentity.h','docs/MUSIC_VISUALIZER.md',
        'tests/visualizer/CMakeLists.txt','tests/visualizer/new-nested/data.txt',
        'tests/spoken-media/native/VisualizerService.qml'];
    for(const name of omitted) write(name,'PRIVATE-VISUALIZER-FIXTURE');
    r=run('default-with-visualizer');
    check(r.status===0 && fs.existsSync(path.join(root,'default-with-visualizer',omitted[0])),
          'default export behavior retained; visualizer exclusion is explicit');
    r=run('without-visualizer','--without-visualizer');
    check(r.status===0,'public release export strips current integration blocks');
    const publicTree=path.join(root,'without-visualizer');
    check(omitted.every(name=>!fs.existsSync(path.join(publicTree,name))),
          'renderer, presets, stream helper, private docs, test tree and fixture stub omitted');
    check(inputs.every(name=>!/(VisualizerService|ZUUNED_VISUALIZER|projectM-4|musicStreamIdentity)/.test(fs.readFileSync(path.join(publicTree,name),'utf8'))),
          'public application and spoken tests have no dangling visualizer dependency');
    check([...originals].every(([name,bytes])=>fs.readFileSync(path.join(source,name)).equals(bytes))
          && omitted.every(name=>fs.existsSync(path.join(source,name))),
          'private sources and integration remain byte-identical');
    const manifest=JSON.parse(fs.readFileSync(path.join(publicTree,'PUBLIC_EXPORT.json')));
    check(manifest.visualizerExcluded && manifest.transformed.length===inputs.length
          && omitted.every(name=>manifest.excluded.includes(name)),
          'manifest explicitly records exclusions and every transformed file');
    const island=fs.readFileSync(path.join(publicTree,'qml/NowPlayingIsland.qml'),'utf8');
    check(island.includes('save as playlist') && island.includes('closeRequested')
          && fs.readFileSync(path.join(publicTree,'src/player/MpvController.cpp'),'utf8').includes('"vo", "null"'),
          'ordinary playback, dismissal and playlist controls preserved');
    write('leak.txt',values[2]);r=run('visualizer-leak','--without-visualizer');
    check(r.status!==0&&!fs.existsSync(path.join(root,'visualizer-leak'))&&!r.stderr.includes(values[2]),
          'visualizer exclusion still enforces credential scan before output');
    fs.unlinkSync(path.join(source,'leak.txt'));
    write('qml/Main.qml',originals.get('qml/Main.qml').toString().replace('VisualizerService.close()', 'VisualizerService.close(123)'));
    r=run('drift','--without-visualizer');
    check(r.status!==0&&!fs.existsSync(path.join(root,'drift')),'changed integration fails closed before creating destination');
    write('qml/Main.qml',originals.get('qml/Main.qml'));
    write('src/new-integration.cpp','void extra() { VisualizerService::open(); }');
    r=run('unexpected','--without-visualizer');
    check(r.status!==0&&!fs.existsSync(path.join(root,'unexpected')),'new unreviewed integration is rejected');
    fs.unlinkSync(path.join(source,'src/new-integration.cpp'));
    fs.unlinkSync(path.join(source,'qml/Main.qml'));
    r=run('missing','--without-visualizer');
    check(r.status!==0&&!fs.existsSync(path.join(root,'missing')),'missing required integration input fails closed');
    r=run('unknown-option','--unknown');
    check(r.status!==0&&!fs.existsSync(path.join(root,'unknown-option')),'unknown options refused');
    console.log(`${checks} public-export checks passed`);
} finally { fs.rmSync(root,{recursive:true,force:true}); }
