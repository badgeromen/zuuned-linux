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
pass('archives without metadata never claim clean provenance');
const overrides = {ZUUNED_SOURCE_REVISION:live.application,ZUUNED_LIBZUNE_REVISION:live.libzune,
    ZUUNED_SOURCE_DIRTY:'false',ZUUNED_LIBZUNE_DIRTY:'true'};
const supplied = cmake(archive,join(temp,'supplied'),overrides);
assert.equal(supplied.application,live.application);
assert.equal(supplied.applicationDirty,false);
assert.equal(supplied.libzuneDirty,true);
assert.match(readFileSync(join(temp,'supplied/ZuunedBuildInfo.h'),'utf8'),/libzune [a-f0-9]+\+dirty/);
pass('archive overrides retain explicit clean/dirty states');
delete overrides.ZUUNED_SOURCE_DIRTY;
const unspecified = cmake(archive,join(temp,'unspecified'),overrides);
assert.equal(unspecified.applicationDirty,null);
pass('known archive revision without state remains unverified');
const bad = run('cmake',[`-DSOURCE_DIR=${archive}`,`-DOUTPUT_DIR=${temp}/bad`,
    '-DPROJECT_VERSION_VALUE=0.1.0','-DQT_VERSION_VALUE=6.8.2','-DZUUNED_SOURCE_REVISION=not-a-hash',
    '-P',join(root,'packaging/WriteBuildInfo.cmake')]);
assert.notEqual(bad.status,0); assert.match(bad.stderr,/40-character/);
pass('malformed provenance is rejected');
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
