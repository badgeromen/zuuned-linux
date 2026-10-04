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
    const run=name=>spawnSync(process.execPath,[path.join(source,'tools/export-public-source.mjs'),path.join(root,name)],{encoding:'utf8'});
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
    console.log(`${checks} public-export checks passed`);
} finally { fs.rmSync(root,{recursive:true,force:true}); }
