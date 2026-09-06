// Run with NODE_PATH pointing to a directory containing jsdom.
const assert=require('node:assert/strict');
const fs=require('node:fs');
const {JSDOM}=require('jsdom');
const html=fs.readFileSync('src/web/index.html','utf8').replace('<script src="i18n.js"></script>',()=>'<script>'+fs.readFileSync('src/web/i18n.js','utf8')+'</script>');
async function test(language,label){
  const calls=[];
  const dom=new JSDOM(html,{url:'http://127.0.0.1:12389/',runScripts:'dangerously',beforeParse(w){
    w.fetch=async(url,options)=>{
      calls.push({url,options});let data={};
      if(url==='/api/logs')return {ok:true,text:async()=>'[INFO] Torrent started <script>bad()</script>'};
      if(url==='/api/status')data={language,capacity:8};
      else if(url==='/api/torrents')data={torrents:[]};
      else if(url==='/api/paths')data={paths:[{path:'/data/',label:'Data',exists:1,writable:1}]};
      else if(url==='/api/files/mkdir'){
        const fields=new URLSearchParams(options.body);
        data={success:true,path:fields.get('path')+'/'+fields.get('name')};
      }
      else if(url.startsWith('/api/files')){
        const query=new URL(url,'http://localhost').searchParams;
        const path=query.get('path').replace(/\/$/,'')||'/';
        data=query.get('directories')==='1'?{path,writable:true,entries:path==='/data'?[{name:'Downloads',directory:true}]:[],next:-1}:{path,entries:path==='/'?[{name:'data',directory:true}]:[{name:'<test>&.torrent',directory:false}],next:-1};
      } else if(url==='/api/torrents/local')data={success:true,name:'test'};
      return {ok:true,json:async()=>data};
    };
  }});
  const w=dom.window,$=id=>w.document.getElementById(id);
  const tick=()=>new Promise(resolve=>setTimeout(resolve,10));
  await tick();
  assert.equal(w.document.documentElement.lang,language);
  assert.equal($('openAdd').textContent.trim(),label);
  assert.equal(w.fmtTime(0),language.startsWith('en')?'Calculating':'Calculando');
  assert($('uploadComputer').classList.contains('hidden'));
  $('openLogs').click();await tick();
  assert(!$('logScreen').classList.contains('hidden'));
  assert.equal($('logOutput').textContent,'[INFO] Torrent started <script>bad()</script>');
  assert.equal($('logOutput').querySelector('script'),null);
  assert.equal(w.getComputedStyle($('logScreen')).backgroundColor,'rgb(0, 0, 0)');
  assert.equal(w.getComputedStyle($('logOutput')).color,'rgb(255, 255, 255)');
  $('closeLogs').click();assert($('logScreen').classList.contains('hidden'));assert.equal(w.logTimer,null);
  let nativeClicks=0;$('fileInput').click=()=>nativeClicks++;
  $('openAdd').click();$('dropZone').click();await tick();
  assert.equal(nativeClicks,0);
  assert($('fileEntries').querySelector('svg.folder-icon path'));
  $('fileEntries').querySelector('button').click();await tick();
  assert.equal($('browsePath').textContent,'/data');
  assert.equal($('fileEntries').querySelector('button').textContent,'<test>&.torrent');
  assert($('fileEntries').querySelector('svg.torrent-icon path'));
  assert.equal($('fileEntries').querySelector('test'),null);
  $('fileEntries').querySelector('button').click();
  assert.equal(w.state.selectedSource,'/data/<test>&.torrent');
  assert.equal($('customPath').tagName,'BUTTON');
  $('customPath').click();await tick();
  assert.equal($('destinationPath').textContent,'/data');
  assert.notEqual(w.document.activeElement.id,'newFolderName');
  assert($('destinationEntries').querySelector('svg.folder-icon'));
  $('destinationEntries').querySelector('button').click();await tick();
  assert.equal($('destinationPath').textContent,'/data/Downloads');
  $('newFolder').click();
  assert.equal(w.document.activeElement.id,'newFolderName');
  $('newFolderName').value='Jogos & Apps';$('createFolder').click();await tick();
  const mkdir=calls.find(c=>c.url==='/api/files/mkdir');assert(mkdir);
  assert.equal(new URLSearchParams(mkdir.options.body).get('name'),'Jogos & Apps');
  assert.equal(new URLSearchParams(mkdir.options.body).get('path'),'/data/Downloads');
  assert.equal($('destinationPath').textContent,'/data/Downloads/Jogos & Apps');
  $('useDestination').click();
  assert.equal(w.state.customDestination,'/data/Downloads/Jogos & Apps');
  assert($('destinationBrowser').classList.contains('hidden'));
  $('customPath').click();await tick();
  assert.equal($('destinationPath').textContent,'/data');
  $('cancelDestination').click();
  assert.equal(w.state.customDestination,'/data/Downloads/Jogos & Apps');
  $('startDownload').click();await tick();
  const local=calls.find(c=>c.url==='/api/torrents/local');
  assert(local);assert.equal(new URLSearchParams(local.options.body).get('source'),'/data/<test>&.torrent');
  assert.equal(new URLSearchParams(local.options.body).get('path'),'/data/Downloads/Jogos & Apps');
  w.selectPath('/data/');assert.equal(w.state.customDestination,'');
  assert.equal(w.state.selectedSource,'');
  w.state.torrents=[{id:0,state:'downloading',name:'Test',size:123,downloaded:0,speed_down:0}];w.render();
  assert.equal(w.document.querySelector('.torrent-name').textContent,'Test');
  assert.equal(w.document.querySelector('.state').textContent,language.startsWith('pt')?'Baixando':language.startsWith('es')?'Descargando':'Downloading');
  dom.window.close();
  console.log(language+': source/destination browsing, folder creation, cancel, submit, escaping and translations passed');
}
(async()=>{for(const [lang,label] of [['pt-BR','Novo torrent'],['pt-PT','Novo torrent'],['en-US','New torrent'],['es-ES','Nuevo torrent']])await test(lang,label)})().catch(e=>{console.error(e);process.exit(1)});
