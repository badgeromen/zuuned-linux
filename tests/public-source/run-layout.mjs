import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import assert from 'node:assert/strict';
import {execFileSync} from 'node:child_process';
import {sourceLayout} from '../../ci/source-layout.mjs';
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'zuuned-source-layout-'));
const git=(root,...args)=>execFileSync('git',['-C',root,...args],{encoding:'utf8'}).trim();
let checks=0;
function check(name,fn){fn();checks++;console.log('PASS '+name);}
function init(root){fs.mkdirSync(root,{recursive:true});git(root,'init','-q');git(root,'config','user.name','Fixture');git(root,'config','user.email','fixture@example.invalid');}
function commit(root){git(root,'add','.');git(root,'commit','-qm','fixture');}
try {
    const library=path.join(temporary,'library');init(library);
    fs.writeFileSync(path.join(library,'protocol.c'),'int protocol(void) { return 1; }\n');commit(library);
    const revision=git(library,'rev-parse','HEAD'),tree=git(library,'rev-parse','HEAD^{tree}');
    const app=path.join(temporary,'app');init(app);fs.mkdirSync(path.join(app,'libzune'));
    fs.copyFileSync(path.join(library,'protocol.c'),path.join(app,'libzune/protocol.c'));
    const manifest=path.join(app,'PUBLIC_LIBZUNE.json');
    const save=value=>fs.writeFileSync(manifest,JSON.stringify(value)+'\n');save({revision,tree});commit(app);
    check('vendored layout reports library revision, not app revision',()=>{
        assert.deepEqual(sourceLayout(app),{mode:'public-vendored',revision,tree});
        assert.notEqual(revision,git(app,'rev-parse','HEAD'));
    });
    save({revision,tree:'0'.repeat(40)});commit(app);
    check('committed tree mismatch rejected',()=>assert.throws(()=>sourceLayout(app),/does not match/));
    save({revision,tree});commit(app);
    fs.appendFileSync(path.join(app,'libzune/protocol.c'),'changed\n');
    check('modified vendored bytes rejected',()=>assert.throws(()=>sourceLayout(app),/working tree differs/));
    git(app,'restore','libzune/protocol.c');save({revision:'1'.repeat(40),tree});
    check('uncommitted manifest edit rejected',()=>assert.throws(()=>sourceLayout(app),/working tree differs/));
    git(app,'restore','PUBLIC_LIBZUNE.json');fs.writeFileSync(path.join(app,'libzune/untracked.h'),'extra');
    check('untracked library input rejected',()=>assert.throws(()=>sourceLayout(app),/working tree differs/));
    fs.unlinkSync(path.join(app,'libzune/untracked.h'));save({revision:'short',tree});commit(app);
    check('malformed revision rejected',()=>assert.throws(()=>sourceLayout(app),/full revision/));
    fs.unlinkSync(manifest);commit(app);
    check('missing committed provenance rejected',()=>assert.throws(()=>sourceLayout(app),/committed PUBLIC_LIBZUNE/));
    const privateApp=path.join(temporary,'private');init(privateApp);
    git(privateApp,'-c','protocol.file.allow=always','submodule','add','-q',library,'libzune');commit(privateApp);
    check('private submodule keeps actual independent revision',()=>assert.deepEqual(sourceLayout(privateApp),{mode:'private-submodule',revision}));
    git(privateApp,'submodule','deinit','-f','--','libzune');
    check('uninitialized submodule cannot silently report app revision',()=>assert.throws(()=>sourceLayout(privateApp),/not initialized/));
    console.log(checks+' source layout checks passed');
} finally {fs.rmSync(temporary,{recursive:true,force:true});}
