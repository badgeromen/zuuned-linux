#!/usr/bin/env bash
# Isolated packaging helpers. No application, provider, system or USB access.
set -euo pipefail
cd "$(dirname "$0")/../.."
zuuned_test_root=$(mktemp -d /tmp/zuuned-packaging.XXXXXX)
trap 'rm -rf "$zuuned_test_root"' EXIT
export ZUUNED_PACKAGING_TEST_ROOT="$zuuned_test_root"
for script in packaging/build-appimage.sh packaging/verify-appdir.sh packaging/verify-elf-dependencies.sh \
              packaging/tests/run.sh packaging/tests/elf-inventory.sh packaging/AppRun; do
    bash -n "$script"
done
node --input-type=module <<'JS'
import {spawnSync} from 'node:child_process';
import {readFileSync, writeFileSync, mkdirSync, statSync, copyFileSync, chmodSync} from 'node:fs';
import {join} from 'node:path';
import assert from 'node:assert/strict';
const root = process.cwd(), temp = process.env.ZUUNED_PACKAGING_TEST_ROOT;
let checks = 0;
function pass(label) { ++checks; console.log(`PASS ${label}`); }
function run(cmd, args, env={}) {
    return spawnSync(cmd, args, {encoding:'utf8', env:{...process.env,...env}});
}
function cmake(source, out, values={}) {
    const result = run('cmake', [`-DSOURCE_DIR=${source}`,`-DOUTPUT_DIR=${out}`,
        '-DPROJECT_VERSION_VALUE=0.1.0','-DQT_VERSION_VALUE=6.8.2',
        ...Object.entries(values).map(([k,v])=>`-D${k}=${v}`),
        '-P',join(root,'packaging/WriteBuildInfo.cmake')]);
    assert.equal(result.status, 0, result.stderr);
    return JSON.parse(readFileSync(join(out,'zuuned-build.json'),'utf8'));
}
const live = cmake(root,join(temp,'live'));
assert.equal(live.application,run('git',['rev-parse','HEAD']).stdout.trim());
assert.equal(live.libzune,run('git',['-C','libzune','rev-parse','HEAD']).stdout.trim());
assert.equal(typeof live.applicationDirty,'boolean');
pass('actual checkout build identification includes both real revisions');
const branchOf = dir => {
    const result = run('git',['-C',dir,'symbolic-ref','--quiet','--short','HEAD']);
    return result.status === 0 ? result.stdout.trim() : null;
};
const identity = (branch, sha, dirty) => (branch ? `${branch} @ ` : '') + sha.slice(0,12) + (dirty ? '+dirty' : '');
const zuneHeader = readFileSync(join(root,'libzune/include/zune.h'),'utf8');
const versionPart = name => zuneHeader.match(new RegExp(`^#define LIBZUNE_VERSION_${name} (\\d+)$`,'m'))[1];
assert.equal(live.applicationBranch, branchOf(root));
assert.equal(live.libzuneBranch, branchOf(join(root,'libzune')));
assert.equal(live.libzuneVersion, ['MAJOR','MINOR','PATCH'].map(versionPart).join('.'));
assert.ok(readFileSync(join(temp,'live/ZuunedBuildInfo.h'),'utf8').includes(
    `version[] = "0.1.0 (${identity(live.applicationBranch, live.application, live.applicationDirty)}; libzune ` +
    `${live.libzuneVersion}, ${identity(live.libzuneBranch, live.libzune, live.libzuneDirty)})"`));
pass('checkout build identification names each branch beside its revision, and the libzune version');
const header = join(temp,'live/ZuunedBuildInfo.h');
const before = statSync(header,{bigint:true}).mtimeNs;
cmake(root,join(temp,'live'));
assert.equal(statSync(header,{bigint:true}).mtimeNs,before);
pass('unchanged build identification does not force recompilation');
const archive = join(temp,'archive');
mkdirSync(join(archive,'libzune'),{recursive:true});
const unknown = cmake(archive,join(temp,'unknown'));
assert.equal(unknown.application,'unknown');
assert.equal(unknown.libzune,'unknown');
assert.equal(unknown.applicationDirty,null);
assert.equal(unknown.libzuneDirty,null);
assert.equal(unknown.applicationBranch,null);
assert.equal(unknown.libzuneBranch,null);
assert.equal(unknown.libzuneVersion,null);
assert.ok(readFileSync(join(temp,'unknown/ZuunedBuildInfo.h'),'utf8').includes('"0.1.0 (unknown; libzune unknown)"'));
pass('archives without metadata never claim clean provenance');
const overrides = {ZUUNED_SOURCE_REVISION:live.application,ZUUNED_LIBZUNE_REVISION:live.libzune,
    ZUUNED_SOURCE_DIRTY:'false',ZUUNED_LIBZUNE_DIRTY:'true'};
const supplied = cmake(archive,join(temp,'supplied'),overrides);
assert.equal(supplied.application,live.application);
assert.equal(supplied.applicationDirty,false);
assert.equal(supplied.libzuneDirty,true);
assert.match(readFileSync(join(temp,'supplied/ZuunedBuildInfo.h'),'utf8'),/libzune [a-f0-9]+\+dirty/);
assert.equal(supplied.applicationBranch,null);
pass('archive overrides retain explicit clean/dirty states');
const branched = cmake(archive,join(temp,'branched'),
    {...overrides,ZUUNED_SOURCE_BRANCH:'release/0.3',ZUUNED_LIBZUNE_BRANCH:'master'});
assert.equal(branched.applicationBranch,'release/0.3');
assert.equal(branched.libzuneBranch,'master');
assert.ok(readFileSync(join(temp,'branched/ZuunedBuildInfo.h'),'utf8').includes(
    `"0.1.0 (release/0.3 @ ${live.application.slice(0,12)}; libzune master @ ${live.libzune.slice(0,12)}+dirty)"`));
pass('archive overrides can name each branch');
delete overrides.ZUUNED_SOURCE_DIRTY;
const unspecified = cmake(archive,join(temp,'unspecified'),overrides);
assert.equal(unspecified.applicationDirty,null);
pass('known archive revision without state remains unverified');
const bad = run('cmake',[`-DSOURCE_DIR=${archive}`,`-DOUTPUT_DIR=${temp}/bad`,
    '-DPROJECT_VERSION_VALUE=0.1.0','-DQT_VERSION_VALUE=6.8.2','-DZUUNED_SOURCE_REVISION=not-a-hash',
    '-P',join(root,'packaging/WriteBuildInfo.cmake')]);
assert.notEqual(bad.status,0); assert.match(bad.stderr,/40-character/);
pass('malformed provenance is rejected');
for (const [values, message] of [
        [{ZUUNED_SOURCE_REVISION:live.application,ZUUNED_SOURCE_BRANCH:'bad"name'}, /only letters, digits/],
        [{ZUUNED_SOURCE_BRANCH:'release/0.3'}, /needs the archive revision/]]) {
    const result = run('cmake',[`-DSOURCE_DIR=${archive}`,`-DOUTPUT_DIR=${temp}/badbranch`,
        '-DPROJECT_VERSION_VALUE=0.1.0','-DQT_VERSION_VALUE=6.8.2',
        ...Object.entries(values).map(([k,v])=>`-D${k}=${v}`),
        '-P',join(root,'packaging/WriteBuildInfo.cmake')]);
    assert.notEqual(result.status,0); assert.match(result.stderr,message);
}
pass('unsafe or unanchored archive branches are rejected');
const scratch = join(temp,'scratch');
const git = (dir, ...args) => {
    const result = run('git',['-C',dir,'-c','user.name=packaging-test','-c','user.email=test@example.invalid',
        '-c','commit.gpgsign=false','-c','core.hooksPath=/dev/null',...args]);
    assert.equal(result.status,0,result.stderr);
};
mkdirSync(join(scratch,'libzune/include'),{recursive:true});
git(scratch,'init','-q','-b','topic/test');
writeFileSync(join(scratch,'.gitignore'),'libzune/\n');
git(scratch,'add','.gitignore'); git(scratch,'commit','-q','-m','app');
git(join(scratch,'libzune'),'init','-q','-b','main');
writeFileSync(join(scratch,'libzune/include/zune.h'),
    '#define LIBZUNE_VERSION_MAJOR 9\n#define LIBZUNE_VERSION_MINOR 8\n#define LIBZUNE_VERSION_PATCH 7\n');
git(join(scratch,'libzune'),'add','.'); git(join(scratch,'libzune'),'commit','-q','-m','lib');
git(join(scratch,'libzune'),'checkout','-q','--detach');
const detached = cmake(scratch,join(temp,'detached'));
assert.equal(detached.applicationBranch,'topic/test');
assert.equal(detached.libzuneBranch,null);
assert.equal(detached.libzuneVersion,'9.8.7');
assert.ok(readFileSync(join(temp,'detached/ZuunedBuildInfo.h'),'utf8').includes(
    `"0.1.0 (topic/test @ ${detached.application.slice(0,12)}; libzune 9.8.7, ${detached.libzune.slice(0,12)})"`));
pass('a detached libzune checkout names no branch and still reports its version');
const qmlRoot = run('qmake6',['-query','QT_INSTALL_QML']);
assert.equal(qmlRoot.status,0,qmlRoot.stderr);
const imports = run('cmake',[`-DZUUNED_QML_IMPORT_ROOT=${qmlRoot.stdout.trim()}`,
    '-P',join(root,'packaging/CheckQmlImports.cmake')]);
assert.equal(imports.status,0,imports.stderr);
const absent = run('cmake',[`-DZUUNED_QML_IMPORT_ROOT=${temp}/missing-qml`,
    '-P',join(root,'packaging/CheckQmlImports.cmake')]);
assert.notEqual(absent.status,0); assert.match(absent.stderr,/Missing QML module/);
pass('QML configuration accepts installed modules and rejects an incomplete runtime');
const appdir=join(temp,'AppDir'), bin=join(appdir,'usr/bin');
mkdirSync(bin,{recursive:true});
copyFileSync(join(root,'packaging/AppRun'),join(appdir,'AppRun'));
for(const name of ['ffmpeg','ffprobe']) {
    writeFileSync(join(bin,name),'#!/bin/sh\nexit 0\n'); chmodSync(join(bin,name),0o755);
}
writeFileSync(join(bin,'zuuned'),`#!/bin/sh
command -v ffmpeg
command -v ffprobe
printf '%s\\n' "$QT_PLUGIN_PATH" "$QML_IMPORT_PATH" "$QT_QPA_PLATFORM" "$QT_QPA_PLATFORMTHEME" "$#" "$1" "$2"
`);chmodSync(join(bin,'zuuned'),0o755);
const launched=run('sh',[join(appdir,'AppRun'),'a spaced argument','--literal=$HOME'],
    {QT_QPA_PLATFORM:'xcb',QT_PLUGIN_PATH:'/host/plugins',QML_IMPORT_PATH:'/host/qml',QT_QPA_PLATFORMTHEME:'host-theme'});
assert.equal(launched.status,0,launched.stderr);
assert.deepEqual(launched.stdout.trimEnd().split('\n'),[join(bin,'ffmpeg'),join(bin,'ffprobe'),
    join(appdir,'usr/plugins'),join(appdir,'usr/qml'),'xcb','xdgdesktopportal','2','a spaced argument','--literal=$HOME']);
pass('AppRun selects bundled tools/modules and desktop portal, preserving arguments without starting the real app');
console.log(`${checks} packaging checks passed`);
JS
bash packaging/tests/elf-inventory.sh
node packaging/tests/snapshot.mjs
bash packaging/tests/runtime-readiness.sh
bash packaging/tests/playlist-readiness.sh
bash packaging/tests/photo-readiness.sh
bash tests/profile-preservation/run.sh
bash tests/profile-preservation/tracks.sh
