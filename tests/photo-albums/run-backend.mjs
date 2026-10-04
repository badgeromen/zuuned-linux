// No package dependencies. Reuse the native app's built objects and toolchain.
import { spawnSync } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { resolve, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const testDir = dirname(fileURLToPath(import.meta.url));
const buildDir = resolve(testDir, '../..', process.argv.slice(2).find(arg => !arg.startsWith('--')) || 'build');
const root = mkdtempSync('/tmp/zuuned-photo-albums-backend-');
function command(executable, args, options = {}) {
    const result = spawnSync(executable, args, { cwd: buildDir, encoding: 'utf8',
        maxBuffer: 64 * 1024 * 1024, ...options });
    if (result.error || result.status !== 0) {
        if (result.stdout && !result.error) process.stderr.write(result.stdout.slice(-8000));
        if (result.stderr) process.stderr.write(result.stderr.slice(-8000));
        throw result.error || new Error(`${executable} exited ${result.status}`);
    }
    return result.stdout || '';
}
// Tokenize Ninja's already-generated compiler commands without executing a shell.
function words(line) {
    const tokens = []; let token = '', quote = '', escaped = false, present = false;
    for (const c of line.trim()) {
        if (escaped) { token += c; escaped = false; present = true; }
        else if (c === '\\' && quote !== "'") escaped = true;
        else if (quote) { if (c === quote) quote = ''; else token += c; present = true; }
        else if (c === '"' || c === "'") { quote = c; present = true; }
        else if (/\s/.test(c)) { if (present) tokens.push(token); token = ''; present = false; }
        else { token += c; present = true; }
    }
    if (quote || escaped) throw new Error('Unsupported compiler command quoting');
    if (present) tokens.push(token);
    return tokens;
}
try {
    const compileLine = command('ninja', ['-t', 'commands',
        'CMakeFiles/zuuned.dir/src/LibraryService.cpp.o']).trim().split('\n').at(-1);
    const compile = words(compileLine);
    const object = join(root, 'backend.o');
    const args = [];
    for (let i = 1; i < compile.length; ++i) {
        const arg = compile[i];
        if (['-MT', '-MF'].includes(arg)) { ++i; continue; }
        if (arg === '-MD') continue;
        if (arg === '-o') { args.push(arg, object); ++i; }
        else if (arg === '-c') { args.push(arg, join(testDir, 'backend.cpp')); ++i; }
        else args.push(arg);
    }
    console.log('Compiling photo album integration against the native app objects…');
    command(compile[0], args);
    const linkLine = command('ninja', ['-t', 'commands', 'zuuned']).trim().split('\n').at(-1);
    const link = words(linkLine).filter(token => token !== ':' && token !== '&&');
    const executable = join(root, 'backend');
    const linkArgs = [];
    let replacedMain = false;
    for (let i = 1; i < link.length; ++i) {
        const arg = link[i];
        if (arg.startsWith('-Wl,--dependency-file=')) continue;
        if (arg.endsWith('/src/main.cpp.o')) { linkArgs.push(object); replacedMain = true; }
        else if (arg === '-o') { linkArgs.push(arg, executable); ++i; }
        else linkArgs.push(arg);
    }
    if (!replacedMain) throw new Error('Could not identify the app main object');
    command(link[0], linkArgs);
    const env = { ...process.env, PHOTO_ALBUMS_TEST_ROOT: root,
        XDG_CONFIG_HOME: join(root, 'config'), XDG_DATA_HOME: join(root, 'data'),
        XDG_CACHE_HOME: join(root, 'cache'), XDG_RUNTIME_DIR: join(root, 'runtime'),
        QT_QPA_PLATFORM: 'offscreen', QT_QPA_PLATFORMTHEME: '', ZUUNED_MPV_AO: 'null' };
    for (const dir of ['config', 'data', 'cache', 'runtime'])
        mkdirSync(join(root, dir), { mode: 0o700 });
    process.stdout.write(command(executable, [], { env, timeout: 30000 }));
} catch (error) {
    console.error(error.message);
    process.exitCode = 1;
} finally {
    rmSync(root, { recursive: true, force: true });
}
