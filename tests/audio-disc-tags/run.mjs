import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import assert from 'node:assert/strict';
const repo=path.resolve(import.meta.dirname,'../..');
const temp=fs.mkdtempSync(path.join(os.tmpdir(),'zuuned-disc-tags-'));
const outputs=[];
let checks=0,serial=0;
const run=(cmd,args)=>execFileSync(cmd,args,{encoding:'utf8',stdio:['ignore','pipe','pipe']}).trim();
const equal=(a,b)=>{assert.equal(a,b);checks++;};
try {
    const flags=run('pkg-config',['--cflags','--libs','libavformat','libavcodec','libavutil','libswresample','libswscale']).split(/\s+/);
    const driver=path.join(temp,'driver');
    run('cc',['-std=gnu99','-g','-O1', ...(process.env.SANITIZE ? ['-fsanitize=address,undefined','-fno-omit-frame-pointer'] : []),
        '-I'+path.join(repo,'transcode'),path.join(repo,'tests/audio-disc-tags/driver.c'),
        path.join(repo,'transcode/libav_transcode.c'),path.join(repo,'transcode/id3_rewrite.c'),...flags,'-lmp3lame','-lm','-o',driver]);
    function fixture(ext,tags={}) {
        const f=path.join(temp,`source-${serial++}.${ext}`);
        const codec=ext==='ogg'?'libvorbis':ext==='mp3'?'libmp3lame':'flac';
        run('ffmpeg',['-v','error','-f','lavfi','-i','sine=frequency=440:duration=0.2','-c:a',codec,
            '-metadata','title=Original title','-metadata','artist=Original artist','-metadata','track=4/9',
            ...Object.entries(tags).flatMap(([k,v])=>['-metadata',`${k}=${v}`]),f]);
        return f;
    }
    function convert(input,kind='audio',disc=0) {
        const out=run(driver,[kind,input,String(disc)]);outputs.push(out);
        const bytes=fs.readFileSync(out);equal(bytes.subarray(0,3).toString(),'ID3');equal(bytes[3],3);
        const info=JSON.parse(run('ffprobe',['-v','error','-show_entries','format_tags','-of','json',out])).format.tags;
        equal(info.artist,'Album Owner');equal(info.title,'Original title');equal(info.track,'4/9');
        run('ffmpeg',['-v','error','-i',out,'-f','null','-']);checks++;
        return {out,tags:info};
    }
    for (const [disc,want] of [['2','2'],[' 02 / 04 ','2/4'],['2147483647','2147483647'],
        ['0',undefined],['-1',undefined],['+2',undefined],['2garbage',undefined],['2/1',undefined],['2/0',undefined],
        ['2/',undefined],['2/4junk',undefined],['2147483648',undefined],['999999999999999999999999',undefined],['',undefined]]) {
        equal(convert(fixture('flac',{disc})).tags.disc,want);
    }
    equal(convert(fixture('flac')).tags.disc,undefined);
    equal(convert(fixture('flac',{discnumber:'3/5'})).tags.disc,'3/5');
    equal(convert(fixture('flac',{disc:'bad',discnumber:'3/5'})).tags.disc,undefined);
    equal(convert(fixture('ogg',{DISCNUMBER:'2/3'})).tags.disc,'2/3');
    // Matroska retains distinct container/stream dictionaries for precedence.
    const layered=path.join(temp,'layered.mka');
    run('ffmpeg',['-v','error','-f','lavfi','-i','sine=duration=0.2',
        '-map','0:a','-map','0:a','-c:a','flac','-metadata','title=Original title',
        '-metadata','track=4/9','-metadata','disc=invalid',
        '-metadata:s:a:0','disc=2/5','-metadata:s:a:1','disc=9/9',layered]);
    equal(convert(layered).tags.disc,'2/5');
    const preferred=path.join(temp,'preferred.mka');
    run('ffmpeg',['-v','error','-i',layered,'-map','0','-c','copy','-metadata','disc=3/6',preferred]);
    equal(convert(preferred).tags.disc,'3/6');
    const source=fixture('flac',{disc:'2/4'});
    equal(convert(source,'audio',7).tags.disc,'7');
    equal(convert(source,'audio',-1).tags.disc,'2/4');
    equal(convert(fixture('flac'),'audio',3).tags.disc,'3');
    const mp3=fixture('mp3',{disc:'2/4'});
    const kept=convert(mp3,'retag');equal(kept.tags.disc,'2/4');
    const changed=convert(mp3,'retag',3);equal(changed.tags.disc,'3');
    const decoded=f=>execFileSync('ffmpeg',['-v','error','-i',f,'-f','s16le','-acodec','pcm_s16le','-']);
    assert.deepEqual(decoded(mp3),decoded(changed.out));checks++;
    equal(convert(fixture('mp3'),'retag').tags.disc,undefined);
    equal(convert(fixture('mp3',{disc:'junk'}),'retag').tags.disc,undefined);
    equal(convert(fixture('mp3',{disc:'2/1'}),'retag').tags.disc,undefined);
    console.log(`PASS ${checks} audio disc-tag checks (real FLAC/Ogg/MP3, decoding and lossless retag)`);
} finally {
    for (const out of outputs) fs.rmSync(out,{force:true});
    fs.rmSync(temp,{recursive:true,force:true});
}
