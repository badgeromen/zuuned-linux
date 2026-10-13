// Public-source safety gate; no private header or history is needed.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {sourceLayout} from '../../ci/source-layout.mjs';
import {omitVisualizer} from '../../tools/public-export-without-visualizer.mjs';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../..');
if(sourceLayout(root).mode!=='public-vendored') throw Error('Public gate requires verified vendored source');
if(fs.existsSync(path.join(root,'libzune/src/mtpz_keys.h'))) throw Error('Public source contains private authentication header');
const manifest=JSON.parse(fs.readFileSync(path.join(root,'PUBLIC_EXPORT.json'),'utf8'));
if(manifest.visualizerExcluded!==true) throw Error('Public release must explicitly exclude private visualizer');
const privateSymbols=/VisualizerService|PortalRenderer|PortalState|PortalShapeMorph|VisualizerPresets|MusicStreamIdentity|musicStreamIdentity|ZUUNED_VISUALIZER|projectM-4|PkgConfig::PROJECTM/;
function visit(relative) {
    const absolute=path.join(root,relative);
    for(const entry of fs.readdirSync(absolute,{withFileTypes:true})) {
        const name=path.posix.join(relative,entry.name);
        if(omitVisualizer(name)) throw Error('Private visualizer file remains: '+name);
        if(entry.isDirectory()) visit(name);
        else if(entry.isFile() && /\.(?:qml|cpp|[ch]|hpp|mjs|sh|txt)$/.test(name)
             && !name.startsWith('tests/public-source/')
             && privateSymbols.test(fs.readFileSync(path.join(root,name),'utf8')))
            throw Error('Private visualizer integration remains: '+name);
    }
}
for(const directory of ['src','qml','tests','ci','packaging']) visit(directory);
if(privateSymbols.test(fs.readFileSync(path.join(root,'CMakeLists.txt'),'utf8')))
    throw Error('Private visualizer dependency remains in CMakeLists.txt');
if(fs.existsSync(path.join(root,'docs/MUSIC_VISUALIZER.md'))) throw Error('Private visualizer design document remains');
console.log('PASS public vendored provenance, authentication-header exclusion and visualizer absence');
console.log('NOT APPLICABLE private exporter comparison: this checkout contains already-sanitized public source');
