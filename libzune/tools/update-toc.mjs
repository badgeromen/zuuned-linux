#!/usr/bin/env node
// Development-only documentation generator. No compiler or device is needed.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const files = dir => fs.readdirSync(path.join(root, dir)).sort().map(name => `${dir}/${name}`);
const link = (file, line) => `[${file}${line ? `:${line}` : ''}](../${file}${line ? `#L${line}` : ''})`;
const lineAt = (source, index) => source.slice(0, index).split('\n').length;
const blank = text => text.replace(/[^\n]/g, ' ');
// Retain offsets/newlines and every conditional-compilation branch. Never read
// mtpz_keys.h: authentication data has no place in a generated symbol index.
function codeOnly(source) {
    return source.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'/g, blank)
        .replace(/^[ \t]*#(?:[^\n\\]|\\[^\n]|\\\n)*/gm, blank);
}
function functions(file, terminator) {
    const source = read(file);
    const clean = codeOnly(source);
    // The project uses ordinary C function declarators, with return type and
    // name on one line. Prototypes/function-pointer members are not definitions.
    const pattern = new RegExp(`^[A-Za-z_][\\w \\t*]*?[ \\t*]+([A-Za-z_]\\w*)[ \\t]*\\([^;{}]*?\\)\\s*${terminator}`, 'gm');
    return [...clean.matchAll(pattern)].filter(m => !m[0].startsWith('typedef ')).map(m => ({
        name: m[1], file, line: lineAt(source, m.index),
        signature: source.slice(m.index, m.index + m[0].length).replace(/\s*[;{]\s*$/, '').replace(/\s+/g, ' ').trim(),
        internal: /^static\b/.test(m[0]),
    }));
}
const sourceFiles = files('src').filter(f => /\.[ch]$/.test(f) && !f.endsWith('/mtpz_keys.h'));
const definitions = sourceFiles.flatMap(f => functions(f, '\\{'));
const publicFunctions = functions('include/zune.h', ';');
const byName = name => definitions.filter(f => f.name === name);
const missing = publicFunctions.filter(f => byName(f.name).length === 0);
if (missing.length) throw new Error(`Public declarations missing definitions: ${missing.map(f => f.name).join(', ')}`);
const publicNames = new Set(publicFunctions.map(f => f.name));
const docs = files('docs').filter(f => f.endsWith('.md') && !f.endsWith('/TOC.md'));
const tools = files('tools').filter(f => /\.(c|py|mjs)$/.test(f));
const tests = files('tests').filter(f => /\.(c|sh)$/.test(f));
let out = `# libzune: source and documentation index

Generated from the current checkout by \`node tools/update-toc.mjs\`.
Run \`node tools/update-toc.mjs --check\` to detect stale references.
Node.js is needed only to regenerate this document, not to build or use libzune.

The public API is [include/zune.h](../include/zune.h). This index covers every
ordinary C function definition in \`src/*.c\` and inline definition in \`src/*.h\`,
including static helpers and both platform/FFmpeg conditional branches.
Definitions under inactive build conditions are indexed, not a promise that
every symbol is present in every compiled library. Authentication values are
never read or reproduced by the generator. Tool and test functions are listed
separately below. Source links point to definition starts; header links carry
complete public signatures and their contracts.

## Start here

- [README](../README.md): build, integration and the Rebellion API.
- [API reference](API_REFERENCE.md): every public signature, ownership, status codes and implementation limits.
- [Public authentication setup](PUBLIC_CREDENTIALS.md): embedded-header and external-file behavior.
- [Wire capture findings](WIRE_CAPTURE_FINDINGS.md): observed Windows-client protocol behavior.
- [Linux testing](LINUX_TESTING.md): hardware findings and test workflow.
- [Artist reuse](ARTIST_REUSE.md): \`zune_forge_artist\` reuses a live matching artist;
  failed inventory reads do not justify creating one, and existing duplicates are not deleted.
- [Metadata probe](METADATA_PROBE.md): native/fallback tags and disc/year behavior.
- [Video titles](VIDEO_TITLES.md): independent display titles and wire filenames.

The macOS DriverKit backend is included in this repository. The corresponding
\`ZuneUSBDriver\` extension implementation belongs to the consuming macOS project;
there is no \`Driver/\` source tree here. Linux uses the libusb backend.

## Public API

${publicFunctions.length} declarations in \`include/zune.h\`. Consult the linked header for
ownership, return values and threading requirements before calling an operation.

| Function | Declaration | Implementation |
|---|---|---|
`;
for (const f of publicFunctions) out += `| \`${f.name}()\` | ${link(f.file, f.line)} | ${byName(f.name).map(d => link(d.file, d.line)).join('<br>')} |\n`;
out += '\n## Library definitions\n\n';
out += `${definitions.length} function definitions, including conditional alternatives. \`public\` means\ndeclared in \`include/zune.h\`; \`internal\` means a non-static implementation\nsymbol outside that API; \`static\` means file-local or header-inline.\n`;
for (const file of sourceFiles) {
    const list = definitions.filter(f => f.file === file);
    if (!list.length) continue;
    out += `\n### ${file}\n\n| Function | Scope | Definition |\n|---|---|---|\n`;
    for (const f of list) out += `| \`${f.name}()\` | ${f.internal ? 'static' : publicNames.has(f.name) ? 'public' : 'internal'} | ${link(file, f.line)} |\n`;
}
out += '\n## Headers, types and constants\n\n';
out += '| Header | Contents |\n|---|---|\n';
const headerDescriptions = {
    'include/zune.h': 'Public functions, opaque device handle, models/families, metadata, media records, database scan types and progress callback.',
    'src/zune_internal.h': 'Device state, shared internal helpers and caches.',
    'src/usb.h': 'USB backend vtable, device identifiers, endpoints and transport abstraction.',
    'src/ptp.h': 'PTP sessions, containers, response codes, MTP/vendor operation and property constants.',
    'src/mtp.h': 'MTP storage/object/property data structures and operation declarations.',
    'src/mtpz.h': 'MTPZ key-loading and authentication declarations.',
};
for (const [file, description] of Object.entries(headerDescriptions)) out += `| ${link(file)} | ${description} |\n`;
out += '\nOptional `src/mtpz_keys.h` supplies embedded authentication material. It is not\npart of this generated index or required for a credential-free library build.\nSee [PUBLIC_CREDENTIALS.md](PUBLIC_CREDENTIALS.md) for the runtime override.\n';
out += '\n## Source inventory\n\n| File | Lines |\n|---|---|\n';
for (const file of ['include/zune.h', ...sourceFiles]) out += `| ${link(file)} | ${read(file).split('\n').length - (read(file).endsWith('\n') ? 1 : 0)} |\n`;
out += '\n## Tools and regression tests\n\nThese are development tools, not additional library API. Software tests do not\nreplace device authentication, transfer and readback tests on real hardware.\n\n';
for (const file of [...tools, ...tests]) {
    out += `- ${link(file)}\n`;
    if (!file.endsWith('.c')) continue;
    for (const f of functions(file, '\\{')) out += `  - \`${f.name}()\`: ${link(file, f.line)}\n`;
}
out += '\n## Documentation map\n\nThe linked source is authoritative for current signatures. Historical protocol\nnotes describe the captures and experiments stated in each document; older\narchitecture/API narratives may describe an earlier implementation.\n\n| Document | Title |\n|---|---|\n';
for (const file of docs) {
    const title = read(file).match(/^#\s+(.+)$/m)?.[1] ?? path.basename(file, '.md');
    out += `| [${path.basename(file)}](${path.basename(file)}) | ${title.replace(/\|/g, '\\|')} |\n`;
}
const target = path.join(root, 'docs/TOC.md');
if (process.argv.includes('--check')) {
    if (fs.readFileSync(target, 'utf8') !== out) { console.error('docs/TOC.md is stale; run node tools/update-toc.mjs'); process.exit(1); }
    console.log(`TOC current: ${publicFunctions.length} public declarations, ${definitions.length} library definitions, ${docs.length} documentation pages.`);
} else {
    fs.writeFileSync(target, out);
    console.log(`Updated docs/TOC.md: ${publicFunctions.length} public declarations, ${definitions.length} library definitions.`);
}
