#!/usr/bin/env node
// Clean source snapshot only: never copy .git, ignored build outputs or credentials.
import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const destination=process.argv[2] && path.resolve(process.argv[2]);
if (!destination || fs.existsSync(destination) || destination===root || root.startsWith(destination+path.sep)) {
    console.error('Usage: node tools/export-public-source.mjs NEW_EMPTY_DESTINATION'); process.exit(2);
}
const keyHeader=fs.readFileSync(path.join(root,'libzune/src/mtpz_keys.h'),'utf8');
const privateValues=[];
for (const match of keyHeader.matchAll(/static const char (MTPZ_DEFAULT_\w+)\[\]\s*=\s*([\s\S]*?);/g)) {
    const value=[...match[2].matchAll(/"([0-9a-fA-F]+)"/g)].map(m=>m[1]).join('');
    if (value.length>=32) privateValues.push(value);
}
if(privateValues.length!==4) throw Error('Private-header layout changed; review export scanner before continuing.');
const needles=[];
for (const value of privateValues) {
    needles.push(Buffer.from(value),Buffer.from(value.toUpperCase()),Buffer.from(Buffer.from(value).toString('base64')));
    if (/^[0-9a-f]+$/i.test(value) && value.length%2===0) {
        const binary=Buffer.from(value,'hex');
        needles.push(binary,Buffer.from(binary.toString('base64')));
        for(let i=0;i+32<=value.length;i+=32) {
            const chunk=value.slice(i,i+32);
            if(new Set(chunk.toLowerCase()).size>4) needles.push(Buffer.from(chunk),Buffer.from(chunk.toUpperCase()),Buffer.from(chunk,'hex'));
        }
    }
}
const excluded=[];
function list(dir){return execFileSync('git',['-C',dir,'ls-files','-z','--cached','--others','--exclude-standard'],{encoding:'utf8'}).split('\0').filter(Boolean);}
function rejected(name) {
    return name==='.gitmodules' || name==='libzune' || name==='libzune/src/mtpz_keys.h'
        || name.startsWith('libzune/vendor/') || name.split('/').some(p=>p==='.git' || p==='.mtpz-data' || p==='mtpz-data' || p==='node_modules' || p==='.env' || p.startsWith('.env.'))
        || /\.(?:AppImage|a|o|so(?:\.\d+)*|dylib|dll|exe|bin|pcapng?|zip|tar(?:\.gz|\.xz)?|tgz|log|db|sqlite3?|patch|pem|key|p12|pfx|crt|cer)$/i.test(name);
}
const files=[...new Set([...list(root),...list(path.join(root,'libzune')).map(p=>'libzune/'+p)])].sort();
const staged=[];
for(const name of files) {
    if(rejected(name)){excluded.push(name);continue;}
    const src=path.join(root,name);
    if(!fs.existsSync(src)) continue; // Intentionally deleted working-tree files.
    let stat=fs.lstatSync(src);
    if(stat.isSymbolicLink()) {
        const target=fs.realpathSync(src);
        if(!target.startsWith(root+path.sep)) throw Error('External source symlink: '+name);
        stat=fs.statSync(src); // Materialize safe internal links; never traverse external SDKs.
    }
    if(!stat.isFile()) throw Error('Review non-regular source entry: '+name);
    let bytes=fs.readFileSync(src);
    if(needles.some(n=>n.length && bytes.includes(n))) throw Error('Credential material detected in '+name+'; nothing exported.');
    if(bytes.subarray(0,4).equals(Buffer.from([0x7f,0x45,0x4c,0x46])) || bytes.subarray(0,8).toString()==='!<arch>\n') throw Error('Compiled artifact detected in '+name);
    staged.push({name,bytes,mode:stat.mode&0o777});
}
fs.mkdirSync(destination,{recursive:true,mode:0o700});
for(const item of staged){const target=path.join(destination,item.name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,item.bytes,{mode:item.mode});}
fs.appendFileSync(path.join(destination,'.gitignore'),'\n# Private authentication material must never enter the public repository.\n/libzune/src/mtpz_keys.h\n.mtpz-data\n*.pem\n*.key\n');
const readme=path.join(destination,'README.md');
if(fs.existsSync(readme)) {
    const publicReadme=fs.readFileSync(readme,'utf8')
        .replace('git clone --recurse-submodules https://github.com/badgeromen/zuuned-linux.git','git clone https://github.com/badgeromen/zuuned-linux.git')
        .replace('git clone --recurse-submodules https://github.com/badgeromen/zuuned-linux.git','git clone https://github.com/badgeromen/zuuned-linux.git')
        .replace('cd zuunedlinux\n','cd zuuned-linux\n')
        .replace('[libzune](https://github.com/badgeromen/libzune)','[libzune](libzune/)')
        .replace('libzune/      submodule','libzune/      included source');
    fs.writeFileSync(readme,'> **Public source snapshot:** read [PUBLIC_SOURCE.md](PUBLIC_SOURCE.md) for credential requirements and differences from private builds.\n\n'+publicReadme);
}
fs.writeFileSync(path.join(destination,'PUBLIC_SOURCE.md'),`# Public source snapshot\n\nThis snapshot contains application and libzune source, without private Git history.\nlibzune is included as ordinary source, not a private Gitea submodule.\n\nMTPZ certificate/key material is deliberately absent. This source builds without\nembedded credentials. Zune authentication requires separately supplied local\ncredentials in ~/.mtpz-data. Local library and playback do not require them.\nSee [Building with Zune authentication](docs/BUILD_WITH_MTPZ.md) for obtaining\nthe external data file and creating a local embedded-data header. No credential\nvalues are included in this source snapshot.\n\nBundled TMDB/Fanart API defaults are retained as requested. The existing\ntmdbApiKey and fanarttvApiKey settings still allow overrides.\n\nExisting AppImages built with embedded credentials still contain those credentials.\nThis snapshot is not represented as the exact source for those older binaries.\nRetain all original and third-party license notices. This export is a technical\nseparation, not a conclusion about third-party redistribution rights.\n\nBuild on Linux using the dependencies in README.md:\n\n    cmake -B build -G Ninja\n    cmake --build build\n\n`);
fs.writeFileSync(path.join(destination,'PUBLIC_EXPORT.json'),JSON.stringify({format:1,historyIncluded:false,privateCredentialsIncluded:false,bundledApiDefaultsIncluded:true,files:staged.length,excluded},null,2)+'\n');
console.log('Public source staged: '+destination+' ('+staged.length+' files; '+excluded.length+' excluded).');
