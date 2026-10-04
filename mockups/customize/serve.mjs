// Optional local preview server. Node built-ins only; binds to this machine.
// Opening index.html directly also works without a server or dependencies.
import {createServer} from 'node:http';
import {readFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import {join,extname} from 'node:path';

const root=fileURLToPath(new URL('.',import.meta.url));
const types={'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.js':'text/javascript; charset=utf-8','.png':'image/png','.ttf':'font/ttf'};
const staticPaths=new Set(['/index.html','/style.css','/mockup.js','/assets/after-the-last-train.png','/assets/artist.png','/assets/series.png']);
const fonts=new Map([['/qml/fonts/PermanentMarker-Regular.ttf',join(root,'../../qml/fonts/PermanentMarker-Regular.ttf')],['/qml/fonts/SpecimenAlien-Regular.ttf',join(root,'../../qml/fonts/SpecimenAlien-Regular.ttf')]]);
const server=createServer(async(req,res)=>{
  try{
    let path=new URL(req.url,'http://localhost').pathname;
    if(path==='/')path='/index.html';
    const file=fonts.get(path)||(staticPaths.has(path)?join(root,path):null);
    if(!file||!['GET','HEAD'].includes(req.method)){res.writeHead(404);res.end('Not found');return;}
    const data=await readFile(file);
    res.writeHead(200,{'Content-Type':types[extname(file)]||'application/octet-stream','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});
    res.end(req.method==='HEAD'?undefined:data);
  }catch{res.writeHead(404);res.end('Not found');}
});
server.listen(8338,'127.0.0.1',()=>console.log('ZUUNED customize studies: http://127.0.0.1:8338'));
server.on('error',err=>{console.error(err.message);process.exitCode=1;});
