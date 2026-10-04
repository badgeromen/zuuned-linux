// Export clean components from their captured Git objects, never mutable work
// files. Explicitly dirty development components retain a labeled working copy.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFileSync} from 'node:child_process';
const git = (cwd, ...args) => execFileSync('git', ['-C', cwd, ...args], {encoding:'utf8'}).trim();

function committedTree(root, revision, destination) {
  const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'zuuned-source-archive-'));
  try {
    const archive = path.join(temporary, 'source.tar');
    // --output streams the archive to disk; media assets never accumulate in
    // execFileSync's stdout buffer. The exact captured object ID is immutable.
    execFileSync('git', ['-C', root, 'archive', '--format=tar', `--output=${archive}`, revision],
      {stdio:['ignore','ignore','pipe']});
    execFileSync('tar', ['--extract', '--file', archive, '--directory', destination, '--no-same-owner'],
      {stdio:['ignore','ignore','pipe']});
  } finally {
    fs.rmSync(temporary, {recursive:true, force:true});
  }
}

function workingTree(root, destination, excludedPath) {
  const files = execFileSync('git', ['-C',root,'ls-files','-z','--cached','--others','--exclude-standard'], {encoding:'utf8'}).split('\0').filter(Boolean);
  for (const relative of new Set(files)) {
    if (path.isAbsolute(relative) || relative.split('/').includes('..')) throw new Error('Unsafe source path');
    if (relative === excludedPath || relative.startsWith(excludedPath + '/')) continue;
    const input = path.join(root, relative), output = path.join(destination, relative);
    let stat;
    try { stat = fs.lstatSync(input); }
    catch (error) { if (error.code === 'ENOENT') continue; throw error; } // Deleted tracked files.
    if (stat.isDirectory()) continue; // Submodule handled by its own inventory.
    // A dirty tree can replace an indexed directory with a symlink. Never
    // follow that link while reading indexed children or creating output dirs.
    let inputParent = root, outputParent = destination;
    for (const part of relative.split('/').slice(0, -1)) {
      inputParent = path.join(inputParent, part);
      if (!fs.lstatSync(inputParent).isDirectory()) throw new Error('Source parent is not a directory');
      outputParent = path.join(outputParent, part);
      try { fs.mkdirSync(outputParent); }
      catch (error) { if (error.code !== 'EEXIST') throw error; }
      if (!fs.lstatSync(outputParent).isDirectory()) throw new Error('Snapshot parent is not a directory');
    }
    if (stat.isSymbolicLink()) fs.symlinkSync(fs.readlinkSync(input), output);
    else fs.copyFileSync(input, output, fs.constants.COPYFILE_EXCL);
  }
}

// Exported for isolated fixture tests. The CLI only exports this repository;
// it has no arbitrary-source or overwrite mode.
export function exportSnapshot(sourceDirectory, destinationDirectory) {
  const source = fs.realpathSync(sourceDirectory);
  const requestedDestination = path.resolve(destinationDirectory);
  if (fs.existsSync(requestedDestination)) throw new Error('Pass a new snapshot directory');
  let parent = path.dirname(requestedDestination);
  const suffix = [path.basename(requestedDestination)];
  while (!fs.existsSync(parent)) { suffix.unshift(path.basename(parent)); parent = path.dirname(parent); }
  const destination = path.join(fs.realpathSync(parent), ...suffix);
  if (destination === source || destination.startsWith(source + path.sep))
    throw new Error('The snapshot must be outside the source tree');
  if (fs.realpathSync(git(source, 'rev-parse', '--show-toplevel')) !== source)
    throw new Error('Source must be the app repository root');

  const components = [['', 'SOURCE'], ['libzune', 'LIBZUNE']].map(([subdir, key]) => {
    const root = path.join(source, subdir);
    return {root, subdir, key, revision:git(root, 'rev-parse', 'HEAD'),
      dirty:!!git(root, 'status', '--porcelain', '--untracked-files=normal', '--ignore-submodules=none')};
  });
  const [app, protocol] = components;
  const gitlink = git(source, 'ls-tree', app.revision, '--', 'libzune').match(/^160000 commit ([a-f0-9]{40})\tlibzune$/);
  if (!gitlink) throw new Error('The captured app commit must contain the libzune submodule');
  // A clean submodule at a different HEAD is still a dirty parent snapshot.
  // This comparison also closes a pin-change race between the two status reads.
  if (protocol.dirty || protocol.revision !== gitlink[1]) app.dirty = true;
  const epoch = git(source, 'show', '-s', '--format=%ct', app.revision);

  fs.mkdirSync(path.dirname(destination), {recursive:true});
  fs.mkdirSync(destination); // Exclusive: never overwrite an existing snapshot.
  for (const component of components) {
    const output = path.join(destination, component.subdir);
    fs.mkdirSync(output, {recursive:true});
    if (component.dirty) workingTree(component.root, output, component.key === 'SOURCE' ? 'libzune' : '');
    else committedTree(component.root, component.revision, output);
  }
  // Publish provenance only after both exports finish. A failed partial export
  // remains available for diagnosis but cannot be mistaken for a complete one.
  fs.writeFileSync(path.join(destination, 'snapshot.env'),
    `export ZUUNED_SOURCE_REVISION=${app.revision}\nexport ZUUNED_LIBZUNE_REVISION=${protocol.revision}\n`+
    `export ZUUNED_SOURCE_DIRTY=${app.dirty}\nexport ZUUNED_LIBZUNE_DIRTY=${protocol.dirty}\n`+
    `export SOURCE_DATE_EPOCH=${epoch}\n`, {flag:'wx'});
  return destination;
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (!process.argv[2]) throw new Error('Pass a new snapshot directory');
  console.log(exportSnapshot(path.resolve(import.meta.dirname, '../..'), process.argv[2]));
}
