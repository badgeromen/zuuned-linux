// Read-only provenance check for private submodules and public vendored source.
import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
export function sourceLayout(root) {
    root=fs.realpathSync(root);
    const git=(...args)=>execFileSync('git',['-C',root,...args],{encoding:'utf8'}).trim();
    const revision=git('rev-parse','HEAD');
    const entry=git('ls-tree',revision,'--','libzune');
    if (/^160000 commit [a-f0-9]{40}\tlibzune$/.test(entry)) {
        const lib=path.join(root,'libzune');
        const top=execFileSync('git',['-C',lib,'rev-parse','--show-toplevel'],{encoding:'utf8'}).trim();
        if(fs.realpathSync(top)!==fs.realpathSync(lib)) throw Error('libzune submodule is not initialized');
        return {mode:'private-submodule',revision:execFileSync('git',['-C',lib,'rev-parse','HEAD'],{encoding:'utf8'}).trim()};
    }
    if (!/^040000 tree [a-f0-9]{40}\tlibzune$/.test(entry)) throw Error('Expected committed libzune submodule or vendored tree');
    if(!/^100(?:644|755) blob [a-f0-9]{40}\tPUBLIC_LIBZUNE\.json$/.test(git('ls-tree',revision,'--','PUBLIC_LIBZUNE.json')))
        throw Error('Vendored libzune requires committed PUBLIC_LIBZUNE.json');
    let manifest;
    try {manifest=JSON.parse(git('show',revision+':PUBLIC_LIBZUNE.json'));}
    catch {throw Error('Invalid PUBLIC_LIBZUNE.json');}
    if(typeof manifest?.revision!=='string' || typeof manifest?.tree!=='string'
        || !/^[a-f0-9]{40}$/.test(manifest.revision) || !/^[a-f0-9]{40}$/.test(manifest.tree))
        throw Error('PUBLIC_LIBZUNE.json requires full revision and tree hashes');
    if(git('rev-parse',revision+':libzune')!==manifest.tree)
        throw Error('PUBLIC_LIBZUNE.json tree does not match committed libzune');
    if(git('diff','HEAD','--','libzune','PUBLIC_LIBZUNE.json') || git('ls-files','--others','--exclude-standard','--','libzune'))
        throw Error('Vendored libzune/provenance working tree differs from committed source');
    return {mode:'public-vendored',revision:manifest.revision,tree:manifest.tree};
}
if(process.argv[1] && path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
    try {console.log(JSON.stringify(sourceLayout(process.argv[2]??path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'))));}
    catch(error) {console.error(error.message);process.exitCode=1;}
}
