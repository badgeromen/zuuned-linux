// Isolated Git repositories and source files only; no real checkout mutation.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {exportSnapshot} from '../containers/snapshot.mjs';

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'zuuned-snapshot-test-'));
const source = path.join(temporary, 'source');
const protocol = path.join(source, 'libzune');
const realGit = execFileSync('sh', ['-c', 'command -v git'], {encoding:'utf8'}).trim();
const git = (cwd, ...args) => execFileSync(realGit, ['-C', cwd,
  '-c', 'user.name=Snapshot Fixture', '-c', 'user.email=fixture@example.invalid',
  '-c', 'commit.gpgsign=false', '-c', 'core.hooksPath=/dev/null', ...args],
  {encoding:'utf8', stdio:['ignore','pipe','pipe']}).trim();
let checks = 0;
function pass(label) { console.log(`PASS ${label}`); ++checks; }
function contents(root, file) { return fs.readFileSync(path.join(root, file), 'utf8'); }
function metadata(root) {
  return Object.fromEntries(contents(root, 'snapshot.env').trim().split('\n')
    .map(line => line.replace(/^export /, '').split('=')));
}
const initialPath = process.env.PATH;
try {
  fs.mkdirSync(protocol, {recursive:true});
  git(source, 'init', '-q', '--initial-branch=main');
  git(protocol, 'init', '-q', '--initial-branch=master');
  fs.writeFileSync(path.join(protocol, 'protocol.c'), 'committed protocol\n');
  git(protocol, 'add', 'protocol.c');
  git(protocol, 'commit', '-qm', 'Fixture protocol');
  const protocolRevision = git(protocol, 'rev-parse', 'HEAD');
  fs.writeFileSync(path.join(source, 'app.cpp'), 'committed app\n');
  fs.writeFileSync(path.join(source, 'removed.txt'), 'present in commit\n');
  const largeAsset = Buffer.alloc(2 * 1024 * 1024, 0x5a);
  fs.writeFileSync(path.join(source, 'large-art.bin'), largeAsset);
  fs.writeFileSync(path.join(source, '.gitignore'), 'ignored-artifact\n');
  fs.writeFileSync(path.join(source, '.gitmodules'), '[submodule "libzune"]\n\tpath = libzune\n\turl = ../fixture-protocol\n');
  fs.symlinkSync('app.cpp', path.join(source, 'app-link'));
  fs.writeFileSync(path.join(source, 'executable.sh'), '#!/bin/sh\nexit 0\n', {mode:0o755});
  git(source, 'add', '.');
  git(source, 'commit', '-qm', 'Fixture app');
  const revision = git(source, 'rev-parse', 'HEAD');

  // Deterministically edit the worktree only when archive begins, after its
  // revision and clean state were captured. Clean export must ignore these
  // concurrent bytes instead of falsely claiming they are committed source.
  const bin = path.join(temporary, 'bin');
  fs.mkdirSync(bin);
  const wrapper = `#!${process.execPath}
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
const args = process.argv.slice(2);
if (args.includes('archive')) {
  const root = args[args.indexOf('-C') + 1];
  fs.writeFileSync(path.join(root, root === ${JSON.stringify(source)} ? 'app.cpp' : 'protocol.c'), 'concurrent edit\\n');
}
const result = spawnSync(${JSON.stringify(realGit)}, args, {stdio:'inherit'});
if (result.error) throw result.error;
process.exit(result.status ?? 1);
`;
  fs.writeFileSync(path.join(bin, 'git.mjs'), wrapper, {mode:0o755});
  fs.symlinkSync('git.mjs', path.join(bin, 'git'));
  process.env.PATH = `${bin}:${initialPath}`;
  const clean = exportSnapshot(source, path.join(temporary, 'clean'));
  process.env.PATH = initialPath;
  assert.equal(contents(source, 'app.cpp'), 'concurrent edit\n');
  assert.equal(contents(protocol, 'protocol.c'), 'concurrent edit\n');
  assert.equal(contents(clean, 'app.cpp'), 'committed app\n');
  assert.equal(contents(clean, 'libzune/protocol.c'), 'committed protocol\n');
  assert.deepEqual(metadata(clean), {
    ZUUNED_SOURCE_REVISION:revision, ZUUNED_LIBZUNE_REVISION:protocolRevision,
    ZUUNED_SOURCE_DIRTY:'false', ZUUNED_LIBZUNE_DIRTY:'false',
    SOURCE_DATE_EPOCH:git(source, 'show', '-s', '--format=%ct', revision)
  });
  pass('clean snapshots archive exact captured app/submodule commits despite concurrent worktree edits');
  assert.deepEqual(fs.readFileSync(path.join(clean, 'large-art.bin')), largeAsset);
  pass('committed media larger than the subprocess stdout buffer exports intact');
  assert.equal(fs.readlinkSync(path.join(clean, 'app-link')), 'app.cpp');
  assert.ok(fs.statSync(path.join(clean, 'executable.sh')).mode & 0o111);
  assert.equal(fs.existsSync(path.join(clean, '.git')), false);
  assert.equal(fs.existsSync(path.join(clean, 'libzune/.git')), false);
  pass('committed archives preserve executable bits/symlinks without Git metadata');

  fs.rmSync(path.join(source, 'removed.txt'));
  fs.writeFileSync(path.join(source, 'untracked.txt'), 'developer addition\n');
  fs.writeFileSync(path.join(source, 'ignored-artifact'), 'ignored build bytes\n');
  fs.writeFileSync(path.join(protocol, 'untracked.c'), 'protocol addition\n');
  fs.symlinkSync('missing-file', path.join(source, 'dangling-link'));
  const dirty = exportSnapshot(source, path.join(temporary, 'dirty'));
  assert.equal(contents(dirty, 'app.cpp'), 'concurrent edit\n');
  assert.equal(contents(dirty, 'libzune/protocol.c'), 'concurrent edit\n');
  assert.equal(contents(dirty, 'untracked.txt'), 'developer addition\n');
  assert.equal(contents(dirty, 'libzune/untracked.c'), 'protocol addition\n');
  assert.equal(fs.existsSync(path.join(dirty, 'removed.txt')), false);
  assert.equal(fs.existsSync(path.join(dirty, 'ignored-artifact')), false);
  assert.equal(fs.readlinkSync(path.join(dirty, 'dangling-link')), 'missing-file');
  assert.equal(metadata(dirty).ZUUNED_SOURCE_DIRTY, 'true');
  assert.equal(metadata(dirty).ZUUNED_LIBZUNE_DIRTY, 'true');
  pass('dirty snapshots retain edits/additions/deletions/symlinks and explicit dirty labels');

  fs.writeFileSync(path.join(source, 'app.cpp'), 'committed app\n');
  fs.writeFileSync(path.join(source, 'removed.txt'), 'present in commit\n');
  fs.rmSync(path.join(source, 'untracked.txt'));
  fs.rmSync(path.join(source, 'dangling-link'));
  git(protocol, 'add', '.');
  git(protocol, 'commit', '-qm', 'New protocol head, parent pin unchanged');
  const newProtocolRevision = git(protocol, 'rev-parse', 'HEAD');
  const mismatch = exportSnapshot(source, path.join(temporary, 'mismatch'));
  assert.equal(contents(mismatch, 'libzune/protocol.c'), 'concurrent edit\n');
  assert.equal(metadata(mismatch).ZUUNED_SOURCE_REVISION, revision);
  assert.equal(metadata(mismatch).ZUUNED_LIBZUNE_REVISION, newProtocolRevision);
  assert.equal(metadata(mismatch).ZUUNED_SOURCE_DIRTY, 'true');
  assert.equal(metadata(mismatch).ZUUNED_LIBZUNE_DIRTY, 'false');
  pass('changed submodule HEAD records the included commit and marks the unchanged parent pin dirty');

  assert.throws(() => exportSnapshot(source, clean), /new snapshot directory/);
  assert.throws(() => exportSnapshot(source, path.join(source, 'recursive-copy')), /outside the source tree/);
  fs.symlinkSync(source, path.join(temporary, 'source-alias'));
  assert.throws(() => exportSnapshot(source, path.join(temporary, 'source-alias/recursive-copy')), /outside the source tree/);
  assert.equal(contents(clean, 'app.cpp'), 'committed app\n');
  assert.equal(fs.existsSync(path.join(source, 'recursive-copy')), false);
  pass('export refuses overwrite and source-contained destinations without modifying them');

  fs.rmSync(path.join(bin, 'git'));
  const failedArchive = path.join(temporary, 'failed-archive-path');
  fs.writeFileSync(path.join(bin, 'tar.mjs'), `#!${process.execPath}
import fs from 'node:fs';
const args = process.argv.slice(2);
fs.writeFileSync(${JSON.stringify(failedArchive)}, args[args.indexOf('--file') + 1]);
process.exit(23);
`, {mode:0o755});
  fs.symlinkSync('tar.mjs', path.join(bin, 'tar'));
  process.env.PATH = `${bin}:${initialPath}`;
  const failed = path.join(temporary, 'failed');
  assert.throws(() => exportSnapshot(source, failed), /Command failed/);
  process.env.PATH = initialPath;
  assert.equal(fs.existsSync(path.join(failed, 'snapshot.env')), false);
  assert.equal(contents(failed, 'app.cpp'), 'committed app\n');
  assert.equal(fs.existsSync(path.dirname(fs.readFileSync(failedArchive, 'utf8'))), false);
  pass('failed archive keeps partial evidence without provenance and removes only its temporary tar tree');
  console.log(`${checks} isolated snapshot export checks passed`);
} finally {
  process.env.PATH = initialPath;
  fs.rmSync(temporary, {recursive:true, force:true});
}
