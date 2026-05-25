#ifdef ESP_PLATFORM

#include "WifiAdminUI.h"
#include <WiFi.h>
#include <AsyncWebSocket.h>

namespace {

// Singleton pointer for the free-function packet push helpers.
WifiAdminUI* g_admin_ui = nullptr;

// Shared CSS served at /style.css so admin sub-pages (MQTT setup, Channels) can
// link to it instead of duplicating styles.
const char SHARED_CSS[] PROGMEM = R"CSS(
:root{--bg:#f6f7fb;--card:#fff;--ink:#111;--mute:#666;--line:#e2e5ec;--accent:#1a73e8;--warn:#ef6c00;--danger:#c62828;--good:#2e7d32;--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
@media (prefers-color-scheme:dark){:root{--bg:#0e1116;--card:#161a22;--ink:#e6e8ee;--mute:#8b93a3;--line:#252b36}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.4 system-ui,sans-serif}
.wrap{max-width:780px;margin:0 auto;padding:.8em}
header.top{display:flex;flex-wrap:wrap;align-items:baseline;gap:.6em;justify-content:space-between;padding:.4em .2em .6em}
header.top h1{font-size:1.25em;margin:0}
header.top .meta{color:var(--mute);font-size:.85em}
.btns{display:flex;flex-wrap:wrap;gap:.4em}
button,a.btn{font:inherit;padding:.4em .8em;border-radius:5px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer;text-decoration:none;display:inline-block}
button:hover,a.btn:hover{border-color:var(--accent)}
button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
button.warn{background:var(--warn);color:#fff;border:0}
button.danger{background:var(--danger);color:#fff;border:0}
.card{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:.8em;margin-top:.6em}
.card h2{margin:0 0 .4em;font-size:.95em}
label{display:block;margin:.6em 0 .2em;font-size:.9em;color:var(--mute)}
input,select{font:inherit;width:100%;padding:.5em;box-sizing:border-box;border:1px solid var(--line);background:var(--bg);color:var(--ink);border-radius:4px}
.row{display:flex;gap:.5em}.row .grow{flex:1}.row .port{flex:0 0 6em}
.msg{padding:.5em .6em;border-radius:4px;margin-top:.8em;font-size:.9em}
.msg.ok{background:rgba(46,125,50,.12);color:var(--good)}
.msg.err{background:rgba(198,40,40,.12);color:var(--danger)}
small{color:var(--mute)}
table{width:100%;border-collapse:collapse;font:13px var(--mono)}
th,td{text-align:left;padding:.35em .4em;border-bottom:1px solid var(--line)}
th{color:var(--mute);font-weight:normal;font-size:.85em}
.tag{display:inline-block;padding:.1em .45em;border-radius:3px;font-size:.75em;border:1px solid var(--line);color:var(--mute)}
.tag.persistent{border-color:var(--accent);color:var(--accent)}
)CSS";

const char BLACKLIST_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=en><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>MeshCore Blacklist</title><link rel=stylesheet href=/style.css></head>
<body><div class=wrap>
<header class=top>
  <div><h1>Forwarding blacklist</h1>
    <div class=meta>drop ADVERTs from nodes whose name matches a pattern</div></div>
  <div class=btns><a class=btn href="/">&larr; Home</a></div>
</header>
<div class=card><h2>Active patterns</h2>
  <table><thead><tr><th>Pattern</th><th>Hits</th><th></th></tr></thead><tbody id=pats></tbody></table>
  <small id=cnt></small>
</div>
<div class=card><h2>Add pattern</h2>
  <form id=f onsubmit="return add(event)">
    <label>Pattern (use * and ? for wildcards)<input id=pat name=pattern required maxlength=31 placeholder="e.g. BadActor* or *spammer*"></label>
    <button type=submit class=primary>Add &amp; save</button>
  </form>
  <div id=msg></div>
  <small>Patterns are matched against the advertised node name. ADVERT packets from matches are dropped (not forwarded, not added to neighbours). Direct messages still pass through since we can't see the sender of an encrypted DM. Capacity: 8 patterns.</small>
</div>
<script>
const $=(id)=>document.getElementById(id);
function escHtml(s){return (s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function refresh(){
  const t=await(await fetch('/api/blacklist')).text();
  const rows=t.split('\n').filter(l=>l.indexOf('|')>0);
  $('pats').innerHTML=rows.map(l=>{const p=l.split('|');return `<tr><td><code>${escHtml(p[0])}</code></td><td>${escHtml(p[1])}</td><td><button onclick="del('${escHtml(p[0]).replace(/'/g,"\\'")}')">Remove</button></td></tr>`}).join('');
  if(!rows.length)$('pats').innerHTML='<tr><td colspan=3 style=color:var(--mute)>no patterns configured</td></tr>';
  $('cnt').textContent=`${rows.length} pattern${rows.length===1?'':'s'} configured`;
}
async function add(ev){ev.preventDefault();const m=$('msg');m.className='msg';m.textContent='Saving…';
  const fd=new FormData($('f'));
  try{const r=await fetch('/api/blacklist-save',{method:'POST',body:new URLSearchParams(fd)});
    const t=await r.text();
    if(r.ok){m.className='msg ok';m.textContent='Saved. '+t;$('pat').value='';refresh();}
    else{m.className='msg err';m.textContent='Failed: '+t}
  }catch(e){m.className='msg err';m.textContent='Failed: '+e}return false}
async function del(pat){if(!confirm('Remove pattern '+pat+'?'))return;
  try{const r=await fetch('/api/blacklist-remove',{method:'POST',body:new URLSearchParams({pattern:pat})});
    if(r.ok)refresh();else alert('Remove failed: '+await r.text());
  }catch(e){alert('Remove failed: '+e)}}
refresh();
</script></body></html>)HTML";

const char CHANNELS_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=en><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>MeshCore Channels</title><link rel=stylesheet href=/style.css></head>
<body><div class=wrap>
<header class=top>
  <div><h1>Group channels</h1>
    <div class=meta>configure additional channels so the repeater can decrypt their messages</div></div>
  <div class=btns><a class=btn href="/">&larr; Home</a></div>
</header>
<div class=card><h2>Configured channels</h2>
  <table><thead><tr><th>Name</th><th>Hash</th><th>Source</th></tr></thead><tbody id=chans></tbody></table>
  <small id=cnt></small>
</div>
<div class=card><h2>Add channel</h2>
  <form id=f onsubmit="return add(event)">
    <label>Channel name<input id=name name=name required maxlength=23 placeholder="e.g. NorCal"></label>
    <label>PSK (base64, 16 or 32 bytes)<input id=psk name=psk required maxlength=47 placeholder="e.g. izOH6cXN6mrJ5e26oRXNcg=="></label>
    <button type=submit class=primary>Add &amp; save</button>
  </form>
  <div id=msg></div>
  <small>Channels persist in NVS and reload on boot. Capacity: 8 total incl. built-in Public.</small>
</div>
<script>
const $=(id)=>document.getElementById(id);
function escHtml(s){return (s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
async function refresh(){
  const t=await(await fetch('/api/channels')).text();
  const rows=t.split('\n').filter(l=>l.indexOf('|')>0);
  $('chans').innerHTML=rows.map(l=>{const p=l.split('|');return `<tr><td>${escHtml(p[0])}</td><td>0x${escHtml(p[1])}</td><td><span class="tag ${p[2]==='p'?'persistent':''}">${p[2]==='p'?'NVS':'built-in'}</span></td></tr>`}).join('');
  if(!rows.length)$('chans').innerHTML='<tr><td colspan=3 style=color:var(--mute)>none configured</td></tr>';
  $('cnt').textContent=`${rows.length} channel${rows.length===1?'':'s'} configured`;
}
async function add(ev){ev.preventDefault();const m=$('msg');m.className='msg';m.textContent='Saving…';
  const fd=new FormData($('f'));
  try{const r=await fetch('/api/channels-save',{method:'POST',body:new URLSearchParams(fd)});
    const t=await r.text();
    if(r.ok){m.className='msg ok';m.textContent='Saved. '+t;$('name').value='';$('psk').value='';refresh();}
    else{m.className='msg err';m.textContent='Failed: '+t}
  }catch(e){m.className='msg err';m.textContent='Failed: '+e}return false}
refresh();
</script></body></html>)HTML";

const char ADMIN_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html lang=en><head>
<meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1">
<title>MeshCore Admin</title>
<style>
:root{--bg:#f6f7fb;--card:#fff;--ink:#111;--mute:#666;--line:#e2e5ec;--accent:#1a73e8;--rx:#1565c0;--tx:#2e7d32;--warn:#ef6c00;--danger:#c62828;--mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
@media (prefers-color-scheme:dark){:root{--bg:#0e1116;--card:#161a22;--ink:#e6e8ee;--mute:#8b93a3;--line:#252b36;--rx:#64b5f6;--tx:#81c784}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:14px/1.4 system-ui,sans-serif}
.wrap{max-width:1280px;margin:0 auto;padding:.8em}
header{display:flex;flex-wrap:wrap;align-items:baseline;gap:.6em;justify-content:space-between;padding:.4em .2em .6em}
header h1{font-size:1.25em;margin:0}
header .meta{color:var(--mute);font-size:.85em}
.btns{display:flex;flex-wrap:wrap;gap:.4em}
button{font:inherit;padding:.4em .8em;border-radius:5px;border:1px solid var(--line);background:var(--card);color:var(--ink);cursor:pointer}
button:hover{border-color:var(--accent)}
button.primary{background:var(--accent);color:#fff;border-color:var(--accent)}
button.warn{background:var(--warn);color:#fff;border:0}
button.danger{background:var(--danger);color:#fff;border:0}
button.ghost{background:transparent}
.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:.6em;margin:.6em 0}
.card{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:.6em}
.card h3{margin:0 0 .3em;font-size:.85em;color:var(--mute);text-transform:uppercase;letter-spacing:.04em}
.card pre{margin:0;font:12px/1.4 var(--mono);white-space:pre-wrap;word-break:break-all;color:var(--ink)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:.18em .8em;font:13px/1.4 system-ui,sans-serif}
.kv .k{color:var(--mute);font-size:.85em}
.kv .v{font-family:var(--mono);text-align:right;color:var(--ink)}
.kv .v.warn{color:var(--warn)}.kv .v.bad{color:var(--danger)}.kv .v.good{color:var(--tx)}
.kv input[type=number],.kv select{padding:.15em .4em;font:inherit;font-family:var(--mono);border:1px solid var(--line);border-radius:3px;background:var(--bg);color:var(--ink);text-align:right}
.kv select{text-align:left;text-align-last:left}
.kv input[type=number]{width:7em}.kv select{min-width:9em;max-width:14em}
.rcard{background:var(--card);border:1px solid var(--line);border-radius:6px;padding:.6em;margin:.6em 0}
.rcard h3{margin:0 0 .5em;font-size:.85em;color:var(--mute);text-transform:uppercase;letter-spacing:.04em}
.rcard .kv{grid-template-columns:minmax(9em,11em) auto;column-gap:1em;row-gap:.3em}
.rcard .kv .v{text-align:left}
.rcard .unit{color:var(--mute);margin-left:.4em;font-family:var(--mono);font-size:.9em}
.rcard .presets{margin-top:.7em;padding-top:.5em;border-top:1px dashed var(--line)}
.rcard .presets .lbl{font-size:.8em;color:var(--mute);margin-bottom:.3em}
.rcard .presets .row{display:flex;flex-wrap:wrap;gap:.35em}
.rcard .presets button{font-size:.85em;padding:.3em .7em}
.main{display:grid;grid-template-columns:1fr 360px;gap:.6em;margin-top:.4em}
@media (max-width:880px){.main{grid-template-columns:1fr}}
.panel{background:var(--card);border:1px solid var(--line);border-radius:6px;display:flex;flex-direction:column;min-height:320px}
.panel header{padding:.4em .6em;border-bottom:1px solid var(--line);background:transparent}
.panel header h2{margin:0;font-size:.95em}
.panel header .ctrls{display:flex;gap:.3em}
.feed{flex:1;overflow:auto;font:12px/1.35 var(--mono);padding:.3em .6em;max-height:60vh;min-height:280px}
.feed .row{padding:.15em 0;border-bottom:1px dotted var(--line);white-space:pre-wrap;word-break:break-all}
.feed .row:last-child{border:0}
.feed .row.rx{color:var(--rx)}
.feed .row.tx{color:var(--tx)}
.feed .ts{color:var(--mute)}
.chat{flex:1;overflow:auto;padding:.4em .6em;max-height:32vh;min-height:140px;display:flex;flex-direction:column;gap:.35em;font:13px/1.35 system-ui,sans-serif}
.chat .msg{padding:.35em .5em;border-radius:6px;background:rgba(127,127,127,.08);border-left:3px solid var(--accent)}
.chat .msg .h{font-size:.78em;color:var(--mute);display:flex;gap:.5em;flex-wrap:wrap;margin-bottom:.15em}
.chat .msg .h .ch{color:var(--accent);font-weight:600}
.chat .msg .h .who{color:var(--ink);font-weight:600}
.chat .msg .t{white-space:pre-wrap;word-break:break-word}
.chat .empty{color:var(--mute);font-size:.85em;font-style:italic}
.cli{padding:.5em .6em;border-top:1px solid var(--line)}
.cli textarea{width:100%;font:12px/1.4 var(--mono);min-height:5em;max-height:14em;padding:.4em;border:1px solid var(--line);background:var(--bg);color:var(--ink);border-radius:4px;resize:vertical}
.cli .input{display:flex;gap:.3em;margin-top:.3em}
.cli .input input{flex:1;font:13px var(--mono);padding:.4em;border:1px solid var(--line);background:var(--bg);color:var(--ink);border-radius:4px}
.presets{display:flex;flex-wrap:wrap;gap:.25em;margin-top:.4em}
.presets button{font-size:.8em;padding:.25em .55em}
.neigh{margin-top:.6em}
.neigh table{width:100%;border-collapse:collapse;font:12px var(--mono)}
.neigh th,.neigh td{text-align:left;padding:.25em .4em;border-bottom:1px solid var(--line)}
.neigh th{color:var(--mute);font-weight:normal;font-size:.85em}
.statusbar{position:sticky;bottom:0;background:var(--card);border-top:1px solid var(--line);padding:.3em .6em;font-size:.8em;color:var(--mute);display:flex;gap:1em;justify-content:space-between;margin-top:.6em;border-radius:0 0 6px 6px}
.dot{display:inline-block;width:.7em;height:.7em;border-radius:50%;background:#888;margin-right:.3em;vertical-align:-.05em}
.dot.on{background:#2e7d32}.dot.off{background:#c62828}
</style></head><body><div class=wrap>

<header>
  <div><h1 id=name>MeshCore</h1>
    <div class=meta><span id=role></span> &middot; fw <span id=fw>?</span> &middot; up <span id=up>?</span></div></div>
  <div class=btns>
    <button onclick=refresh()>Refresh</button>
    <button id=chan-btn class=ghost style="display:none" onclick="location.href='/channels'">Channels</button>
    <button id=blacklist-btn class=ghost style="display:none" onclick="location.href='/blacklist'">Blacklist</button>
    <button id=mqtt-btn class=ghost style="display:none" onclick="location.href='/mqtt-setup'">MQTT</button>
    <button class=warn onclick="if(confirm('Reboot node?'))fetch('/api/reboot',{method:'POST'}).then(()=>setTimeout(refresh,5000))">Reboot</button>
    <button class=danger onclick="if(confirm('Wipe WiFi creds and reboot into AP setup?'))fetch('/wipe-wifi',{method:'POST'}).then(()=>document.body.innerHTML='<p>Rebooting&hellip;</p>')">Wipe WiFi</button>
  </div>
</header>

<section class=cards>
  <div class=card><h3>Network</h3><div id=net class=kv>&hellip;</div></div>
  <div class=card><h3>Mesh</h3><div id=stats class=kv>&hellip;</div></div>
  <div class=card><h3>Radio stats</h3><div id=rstats class=kv>&hellip;</div></div>
  <div class=card><h3>Packets</h3><div id=pstats class=kv>&hellip;</div></div>
</section>

<section class=rcard>
  <h3>Radio config</h3>
  <div id=rcfg class=kv>&hellip;</div>
  <div id=rcfg-presets class=presets></div>
</section>

<div class=main>

  <div style="display:flex;flex-direction:column;gap:.6em;min-width:0">
    <div class=panel>
      <header><h2>Chat <span id=chat-count style="color:var(--mute);font-weight:normal;font-size:.85em"></span></h2>
        <div class=ctrls>
          <button id=chat-pause-btn onclick=toggleChatPause()>Pause</button>
          <button onclick="document.getElementById('chat').innerHTML='<div class=empty>cleared</div>'">Clear</button>
        </div>
      </header>
      <div id=chat class=chat><div class=empty>waiting for chat messages on known channels (Public)&hellip;</div></div>
    </div>
    <div class=panel>
      <header><h2>Packet feed</h2>
        <div class=ctrls>
          <button id=pause-btn onclick=togglePause()>Pause</button>
          <button onclick="document.getElementById('feed').innerHTML=''">Clear</button>
        </div>
      </header>
      <div id=feed class=feed>(connecting&hellip;)</div>
    </div>
  </div>

  <div class=panel>
    <header><h2>Console</h2><div class=ctrls></div></header>
    <div class=cli>
      <textarea id=out readonly placeholder="command output&hellip;"></textarea>
      <div class=input>
        <input id=cmd autocomplete=off placeholder="type a CLI command, e.g. 'neighbors'">
        <button class=primary onclick=runCmd()>Run</button>
      </div>
      <div class=presets>
        <button onclick="preset('ver')">ver</button>
        <button onclick="preset('clock')">clock</button>
        <button onclick="preset('neighbors')">neighbors</button>
        <button onclick="preset('advert')">advert</button>
        <button onclick="preset('get name')">get name</button>
        <button onclick="preset('get freq')">get freq</button>
        <button onclick="preset('get tx_power')">get tx_power</button>
        <button onclick="preset('help')">help</button>
      </div>
      <div class=neigh><table id=neigh-tbl><thead><tr><th>id</th><th>age (s)</th><th>snr</th></tr></thead><tbody id=neigh-body></tbody></table></div>
    </div>
  </div>

</div>

<div class=statusbar>
  <div><span class=dot id=ws-dot></span><span id=ws-state>websocket connecting&hellip;</span></div>
  <div id=pkt-counts>rx 0 / tx 0</div>
</div>

</div>
<script>
const $=(id)=>document.getElementById(id);
let paused=false, chatPaused=false, rxCount=0, txCount=0, chatCount=0, ws=null;
let history=JSON.parse(localStorage.getItem('mc_hist')||'[]'), histIdx=history.length;

function togglePause(){paused=!paused;$('pause-btn').textContent=paused?'Resume':'Pause'}
function toggleChatPause(){chatPaused=!chatPaused;$('chat-pause-btn').textContent=chatPaused?'Resume':'Pause'}
function escHtml(s){return (s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function fmtChatTs(ts){if(!ts)return fmtTs();const d=new Date(ts*1000);return pad2(d.getHours())+':'+pad2(d.getMinutes())+':'+pad2(d.getSeconds())}
function onChat(m){
  if(chatPaused)return;
  const el=$('chat');
  if(el.firstChild && el.firstChild.classList && el.firstChild.classList.contains('empty'))el.innerHTML='';
  const sender=m.sender||'(unknown)';
  const div=document.createElement('div');div.className='msg';
  div.innerHTML=`<div class=h><span class=ts>${fmtChatTs(m.ts)}</span><span class=ch>#${escHtml(m.channel)}</span><span class=who>${escHtml(sender)}</span></div><div class=t>${escHtml(m.text)}</div>`;
  el.appendChild(div);
  while(el.children.length>200)el.removeChild(el.firstChild);
  el.scrollTop=el.scrollHeight;
  chatCount++;$('chat-count').textContent=`(${chatCount})`;
}
function pad2(n){return n<10?'0'+n:''+n}
function fmtTs(){const d=new Date();return pad2(d.getHours())+':'+pad2(d.getMinutes())+':'+pad2(d.getSeconds())}
function appendRow(text,cls){
  if(paused)return;
  const el=$('feed');
  if(el.firstChild && el.firstChild.textContent==='(connecting…)')el.innerHTML='';
  const div=document.createElement('div');div.className='row '+cls;div.innerHTML=text;
  el.appendChild(div);
  while(el.children.length>500)el.removeChild(el.firstChild);
  el.scrollTop=el.scrollHeight;
}
function onPacket(p){
  const truncRaw=p.raw.length>64?p.raw.slice(0,64)+'…':p.raw;
  if(p.dir==='rx'){
    rxCount++;
    appendRow(`<span class=ts>${fmtTs()}</span>  RX  rssi=${p.rssi} snr=${p.snr.toFixed(1)}  len=${p.len}  ${truncRaw}`,'rx');
  } else {
    txCount++;
    appendRow(`<span class=ts>${fmtTs()}</span>  TX                       len=${p.len}  ${truncRaw}`,'tx');
  }
  $('pkt-counts').textContent=`rx ${rxCount} / tx ${txCount}`;
}

function wsConnect(){
  const proto=location.protocol==='https:'?'wss:':'ws:';
  ws=new WebSocket(proto+'//'+location.host+'/ws');
  ws.onopen=()=>{$('ws-dot').classList.add('on');$('ws-dot').classList.remove('off');$('ws-state').textContent='websocket connected';
    appendRow('<span class=ts>'+fmtTs()+'</span>  -- live feed --','ts');};
  ws.onclose=()=>{$('ws-dot').classList.add('off');$('ws-dot').classList.remove('on');$('ws-state').textContent='websocket disconnected, retrying';
    setTimeout(wsConnect,2000);};
  ws.onerror=()=>{};
  ws.onmessage=(e)=>{try{const m=JSON.parse(e.data);if(m.type==='chat')onChat(m);else if(m.dir)onPacket(m);}catch(err){}};
}

function fmtUptime(s){s=s|0;if(s<60)return s+'s';
  const d=(s/86400)|0,h=((s%86400)/3600)|0,m=((s%3600)/60)|0,sec=s%60;
  if(d)return `${d}d ${h}h ${m}m`;
  if(h)return `${h}h ${m}m`;
  return `${m}m ${sec}s`;}
function fmtSecsCompact(s){s=s|0;if(s<60)return s+' s';if(s<3600)return ((s/60)|0)+'m '+(s%60)+'s';return ((s/3600)|0)+'h '+(((s%3600)/60)|0)+'m';}
function fmtRssi(v){if(v==null)return '-';const cls=v>-65?'good':v>-85?'':'warn';return `<span class="v ${cls}">${v} dBm</span>`}
function rssiClass(v){return v>-65?'good':v>-85?'':'warn'}
function fmtBatt(mv){if(mv==null||mv===0)return '-';const v=(mv/1000).toFixed(2);return `${v} V <span style="color:var(--mute)">(${mv} mV)</span>`}
function rowHTML(rows){return rows.map(([k,v,cls])=>`<div class=k>${k}</div><div class="v ${cls||''}">${v}</div>`).join('')}
function renderKV(el,rows){el.innerHTML=rowHTML(rows)}

function renderNetwork(s){renderKV($('net'),[
  ['SSID', s.ssid||'-'],
  ['IP', s.ip||'-'],
  ['RSSI', `${s.rssi} dBm`, rssiClass(s.rssi)],
  ['MAC', s.mac||'-'],
]);}
function renderMesh(j){
  if(!j)return $('stats').textContent='(unavailable)';
  const errs=j.errors||0;
  renderKV($('stats'),[
    ['Battery', fmtBatt(j.battery_mv)],
    ['Uptime', fmtUptime(j.uptime_secs)],
    ['TX queue', (j.queue_len|0)+' pkt'],
    ['Errors', errs, errs?'bad':'good'],
  ]);
}
function renderRadio(j){
  if(!j)return $('rstats').textContent='(unavailable)';
  renderKV($('rstats'),[
    ['Noise floor', `${j.noise_floor} dBm`],
    ['Last RSSI', `${j.last_rssi} dBm`, rssiClass(j.last_rssi)],
    ['Last SNR', `${(+j.last_snr).toFixed(1)} dB`],
    ['Air TX', fmtSecsCompact(j.tx_air_secs)],
    ['Air RX', fmtSecsCompact(j.rx_air_secs)],
  ]);
}
let _lastRadioCfg=null;
// Common starting points by region. Format: [label, freq, bw, sf, cr]
const RADIO_PRESETS=[
  ['USA / Canada (Recommended)', 910.525, 62.5, 7, 5],
  ['EU868 (Default)',            869.618, 62.5, 8, 5],
  ['Legacy Wide (pre-1.14)',     869.525, 250.0, 11, 5],
];
function renderRadioCfg(j){
  if(!j||!j.freq){return $('rcfg').textContent='(unavailable)';}
  _lastRadioCfg=j;
  const phb=j.path_hash_bytes|0;
  const phCls=phb>=2?'good':'warn';
  const bwOpts=[7.8,10.4,15.6,20.8,31.25,41.7,62.5,125,250,500];
  const fEd=`<input id=f-ed type=number min=150 max=2500 step=0.025 value="${j.freq.toFixed(3)}" onchange="setFreq(this.value)"><span class=unit>MHz</span>`;
  const bwEd=`<select id=bw-ed onchange="setBw(this.value)">`+
    bwOpts.map(b=>`<option value=${b} ${b===j.bw?'selected':''}>${b.toFixed(b<10?2:1)} kHz</option>`).join('')+`</select>`;
  const sfEd=`<select id=sf-ed onchange="setSf(this.value)">`+
    [5,6,7,8,9,10,11,12].map(s=>`<option value=${s} ${s==j.sf?'selected':''}>SF${s}</option>`).join('')+`</select>`;
  const crEd=`<select id=cr-ed onchange="setCr(this.value)">`+
    [5,6,7,8].map(c=>`<option value=${c} ${c==j.cr?'selected':''}>4/${c}</option>`).join('')+`</select>`;
  const txEd=`<input id=tx-ed type=number min=-9 max=22 step=1 value="${j.tx_power}" onchange="setTx(this.value)"><span class=unit>dBm</span>`;
  const phEd=`<select id=ph-ed onchange="setPh(this.value)">`+
    [[0,'1B (default)'],[1,'2B (recommended)'],[2,'3B']].map(([v,l])=>`<option value=${v} ${v==j.path_hash_mode?'selected':''}>${l}</option>`).join('')+`</select>`;
  renderKV($('rcfg'),[
    ['Frequency', fEd],
    ['Bandwidth', bwEd],
    ['Spreading factor', sfEd],
    ['Coding rate', crEd],
    ['TX power', txEd],
    ['Path hash size', phEd, phCls],
    ['RX boosted gain', j.rx_boosted ? 'on' : 'off'],
  ]);
  $('rcfg-presets').innerHTML=
    `<div class=lbl>Region presets (applies freq/BW/SF/CR; reboot to take effect):</div>`+
    `<div class=row>`+
    RADIO_PRESETS.map((p,i)=>`<button onclick="applyPreset(${i})">${p[0]}</button>`).join('')+
    `</div>`;
}
async function setRadio(freq,bw,sf,cr,reason){
  const cmd=`set radio ${freq},${bw},${sf},${cr}`;
  const t=await _runCmd(cmd);
  $('out').value+=`\n> ${cmd}\n${t}`;$('out').scrollTop=$('out').scrollHeight;
  if(t.indexOf('reboot')>=0)alert(`${reason} saved. Reboot the node from the home page to apply.`);
  refresh();
}
async function setFreq(v){if(!_lastRadioCfg)return;const j=_lastRadioCfg;setRadio(parseFloat(v),j.bw,j.sf,j.cr,'Frequency');}
async function setBw(v){if(!_lastRadioCfg)return;const j=_lastRadioCfg;setRadio(j.freq,parseFloat(v),j.sf,j.cr,'Bandwidth');}
async function setSf(v){if(!_lastRadioCfg)return;const j=_lastRadioCfg;setRadio(j.freq,j.bw,parseInt(v,10),j.cr,'Spreading factor');}
function applyPreset(i){const p=RADIO_PRESETS[i];if(!confirm(`Apply preset "${p[0]}":\n  ${p[1]} MHz, BW ${p[2]}, SF${p[3]}, CR 4/${p[4]}\n\nReboot required to take effect.`))return;
  setRadio(p[1],p[2],p[3],p[4],`Preset "${p[0]}"`);
}
async function _runCmd(cmd){
  const r=await fetch('/api/cmd',{method:'POST',body:new URLSearchParams({cmd})});
  return await r.text();
}
async function setTx(v){
  const t=await _runCmd('set tx '+v);
  $('out').value+=`\n> set tx ${v}\n${t}`;$('out').scrollTop=$('out').scrollHeight;
  refresh();
}
async function setPh(v){
  const t=await _runCmd('set path.hash.mode '+v);
  $('out').value+=`\n> set path.hash.mode ${v}\n${t}`;$('out').scrollTop=$('out').scrollHeight;
  refresh();
}
async function setCr(v){
  if(!_lastRadioCfg){return}
  const j=_lastRadioCfg;
  const cmd=`set radio ${j.freq},${j.bw},${j.sf},${v}`;
  const t=await _runCmd(cmd);
  $('out').value+=`\n> ${cmd}\n${t}`;$('out').scrollTop=$('out').scrollHeight;
  if(t.indexOf('reboot')>=0)alert('Coding rate saved. Reboot the node from the home page to apply.');
  refresh();
}
function renderPackets(j){
  if(!j)return $('pstats').textContent='(unavailable)';
  const errs=j.recv_errors|0;
  renderKV($('pstats'),[
    ['Received', j.recv|0],
    ['Sent', j.sent|0],
    ['Flood TX / RX', `${j.flood_tx|0} / ${j.flood_rx|0}`],
    ['Direct TX / RX', `${j.direct_tx|0} / ${j.direct_rx|0}`],
    ['RX errors', errs, errs?'warn':'good'],
  ]);
}
async function jsonOrNull(url){try{const r=await fetch(url);if(!r.ok)return null;const t=await r.text();try{return JSON.parse(t)}catch(e){return null}}catch(e){return null}}

async function refresh(){
  const s=await jsonOrNull('/api/status');
  if(s){
    $('name').textContent=s.name||'MeshCore';
    $('role').textContent=s.role||'';
    $('fw').textContent=s.fw||'?';
    $('up').textContent=fmtUptime(s.uptime_s);
    renderNetwork(s);
  }
  renderRadioCfg(await jsonOrNull('/api/radio-config'));
  renderMesh(await jsonOrNull('/api/stats'));
  renderRadio(await jsonOrNull('/api/radio-stats'));
  renderPackets(await jsonOrNull('/api/packet-stats'));
  const ntext=await(await fetch('/api/neighbours')).text().catch(()=>'');
  renderNeigh(ntext);
}
function renderNeigh(t){
  const body=$('neigh-body');body.innerHTML='';
  if(!t||t==='-none-'){body.innerHTML='<tr><td colspan=3 style=color:var(--mute)>no neighbours yet</td></tr>';return}
  for(const line of t.split('\n')){
    const m=line.match(/^([0-9a-fA-F]+):(\d+):(-?\d+)/);
    if(!m)continue;
    const tr=document.createElement('tr');
    tr.innerHTML=`<td>${m[1]}</td><td>${m[2]}</td><td>${m[3]}</td>`;
    body.appendChild(tr);
  }
}

async function runCmd(){
  const v=$('cmd').value.trim();if(!v)return;
  $('out').value+=(($('out').value?'\n':'')+'> '+v);
  $('cmd').value='';
  history.push(v);if(history.length>50)history.shift();histIdx=history.length;localStorage.setItem('mc_hist',JSON.stringify(history));
  try{
    const r=await fetch('/api/cmd',{method:'POST',body:new URLSearchParams({cmd:v})});
    const t=await r.text();
    $('out').value+='\n'+(t||'(no reply)');
    $('out').scrollTop=$('out').scrollHeight;
    refresh();
  }catch(e){$('out').value+='\nERROR: '+e}
}
function preset(c){$('cmd').value=c;runCmd();}

$('cmd').addEventListener('keydown',(e)=>{
  if(e.key==='Enter'){e.preventDefault();runCmd();}
  else if(e.key==='ArrowUp' && histIdx>0){histIdx--;$('cmd').value=history[histIdx]||''}
  else if(e.key==='ArrowDown' && histIdx<history.length){histIdx++;$('cmd').value=history[histIdx]||''}
});

fetch('/mqtt-status').then(r=>{if(r.ok)$('mqtt-btn').style.display='inline-block'}).catch(()=>{});
fetch('/api/channels').then(r=>{if(r.ok)$('chan-btn').style.display='inline-block'}).catch(()=>{});
fetch('/api/blacklist').then(r=>{if(r.ok)$('blacklist-btn').style.display='inline-block'}).catch(()=>{});
refresh();setInterval(refresh,15000);
wsConnect();
</script></body></html>)HTML";

}  // namespace

WifiAdminUI& _adminInstance() {
  static WifiAdminUI* dummy = nullptr; (void)dummy;
  return *g_admin_ui;
}

void wifiAdminPushRxPacket(float snr, float rssi, const uint8_t* raw, int len) {
  if (g_admin_ui) g_admin_ui->pushRxPacket(snr, rssi, raw, len);
}

void wifiAdminPushTxPacket(const uint8_t* raw, int len) {
  if (g_admin_ui) g_admin_ui->pushTxPacket(raw, len);
}

void wifiAdminPushChat(const char* channel, const char* sender, const char* text, uint32_t timestamp) {
  if (g_admin_ui) g_admin_ui->pushChat(channel, sender, text, timestamp);
}

WifiAdminUI::WifiAdminUI(AsyncWebServer* server, MeshInfoProvider* mesh, WifiCmdHandler cmd_handler)
  : _server(server), _mesh(mesh), _cmd_handler(cmd_handler) {
  g_admin_ui = this;
}

namespace {
String _packetToJson(bool is_rx, float snr, float rssi, const uint8_t* raw, int len) {
  static const char H[] = "0123456789abcdef";
  String hex; hex.reserve(2 * len);
  for (int i = 0; i < len; i++) { hex += H[(raw[i] >> 4) & 0xF]; hex += H[raw[i] & 0xF]; }
  String out = "{";
  if (is_rx) {
    out += "\"dir\":\"rx\",\"rssi\":"; out += String((int)rssi);
    out += ",\"snr\":"; out += String(snr, 2);
    out += ",\"len\":"; out += String(len);
  } else {
    out += "\"dir\":\"tx\",\"len\":"; out += String(len);
  }
  out += ",\"raw\":\""; out += hex; out += "\"}";
  return out;
}
}

void WifiAdminUI::pushRxPacket(float snr, float rssi, const uint8_t* raw, int len) {
  if (!_ws || _ws->count() == 0) return;
  _ws->textAll(_packetToJson(true, snr, rssi, raw, len));
}

void WifiAdminUI::pushTxPacket(const uint8_t* raw, int len) {
  if (!_ws || _ws->count() == 0) return;
  _ws->textAll(_packetToJson(false, 0, 0, raw, len));
}

namespace {
String _jsEscape(const char* s) {
  String out; out.reserve(strlen(s) + 8);
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); out += b; }
    else out += c;
  }
  return out;
}
}

void WifiAdminUI::pushChat(const char* channel, const char* sender, const char* text, uint32_t timestamp) {
  if (!_ws || _ws->count() == 0) return;
  String out = "{\"type\":\"chat\",\"ts\":";
  out += String(timestamp);
  out += ",\"channel\":\""; out += _jsEscape(channel ? channel : "");
  out += "\",\"sender\":\""; out += _jsEscape(sender ? sender : "");
  out += "\",\"text\":\"";   out += _jsEscape(text ? text : "");
  out += "\"}";
  _ws->textAll(out);
}

void WifiAdminUI::begin() {
  if (!_server) return;

  _ws = new AsyncWebSocket("/ws");
  _ws->onEvent([](AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType type, void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT) {
      c->text("{\"hello\":\"meshcore\"}");
    }
  });
  _server->addHandler(_ws);

  _server->on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", ADMIN_HTML);
  });

  _server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
    String out = "{";
    out += "\"name\":\""; out += _mesh->nodeName(); out += "\",";
    out += "\"role\":\""; out += _mesh->role(); out += "\",";
    out += "\"fw\":\"";   out += _mesh->firmwareVer(); out += "\",";
    out += "\"uptime_s\":"; out += String(millis() / 1000); out += ",";
    out += "\"ssid\":\""; out += WiFi.SSID(); out += "\",";
    out += "\"ip\":\"";   out += WiFi.localIP().toString(); out += "\",";
    out += "\"rssi\":";   out += String(WiFi.RSSI()); out += ",";
    out += "\"mac\":\"";  out += WiFi.macAddress(); out += "\"";
    out += "}";
    req->send(200, "application/json", out);
  });

  auto text_route = [this](const char* path, void (MeshInfoProvider::*fn)(char*)) {
    _server->on(path, HTTP_GET, [this, fn](AsyncWebServerRequest* req) {
      char buf[2048];
      buf[0] = '\0';
      (_mesh->*fn)(buf);
      req->send(200, "text/plain", buf);
    });
  };
  text_route("/api/stats",        &MeshInfoProvider::formatStats);
  text_route("/api/radio-stats",  &MeshInfoProvider::formatRadioStats);
  text_route("/api/packet-stats", &MeshInfoProvider::formatPacketStats);
  text_route("/api/neighbours",   &MeshInfoProvider::formatNeighbours);
  text_route("/api/radio-config", &MeshInfoProvider::formatRadioConfig);

  _server->on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", "ok");
    delay(200);
    ESP.restart();
  });

  // Shared stylesheet for /, /channels, /mqtt-setup so they look like one app.
  _server->on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncWebServerResponse* r = req->beginResponse_P(200, "text/css", SHARED_CSS);
    r->addHeader("Cache-Control", "max-age=3600");
    req->send(r);
  });

  // Channels admin
  _server->on("/channels", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", CHANNELS_HTML);
  });
  _server->on("/api/channels", HTTP_GET, [this](AsyncWebServerRequest* req) {
    char buf[512]; buf[0] = 0;
    _mesh->listChannels(buf);
    req->send(200, "text/plain", buf);
  });
  _server->on("/api/channels-save", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("name", true) || !req->hasParam("psk", true)) {
      req->send(400, "text/plain", "missing name or psk"); return;
    }
    String name = req->getParam("name", true)->value();
    String psk  = req->getParam("psk", true)->value();
    if (name.length() == 0 || name.length() > 23) { req->send(400, "text/plain", "bad name length"); return; }
    if (psk.length() < 16 || psk.length() > 47) { req->send(400, "text/plain", "bad psk length"); return; }
    if (_mesh->addChannel(name.c_str(), psk.c_str())) {
      req->send(200, "text/plain", "channel added");
    } else {
      req->send(409, "text/plain", "add failed (duplicate, capacity, or invalid PSK)");
    }
  });

  // Forwarding blacklist
  _server->on("/blacklist", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", BLACKLIST_HTML);
  });
  _server->on("/api/blacklist", HTTP_GET, [this](AsyncWebServerRequest* req) {
    char buf[512]; buf[0] = 0;
    _mesh->listBlocked(buf);
    req->send(200, "text/plain", buf);
  });
  _server->on("/api/blacklist-save", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("pattern", true)) { req->send(400, "text/plain", "missing pattern"); return; }
    String pat = req->getParam("pattern", true)->value();
    if (pat.length() == 0 || pat.length() > 31) { req->send(400, "text/plain", "bad pattern length"); return; }
    if (_mesh->addBlocked(pat.c_str())) req->send(200, "text/plain", "pattern added");
    else req->send(409, "text/plain", "add failed (duplicate or capacity)");
  });
  _server->on("/api/blacklist-remove", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!req->hasParam("pattern", true)) { req->send(400, "text/plain", "missing pattern"); return; }
    String pat = req->getParam("pattern", true)->value();
    if (_mesh->removeBlocked(pat.c_str())) req->send(200, "text/plain", "removed");
    else req->send(404, "text/plain", "not found");
  });

  // POST /api/cmd  body: cmd=<cli string>  → reply text
  // NOTE: handler runs on the AsyncTCP task. MeshCore is not formally thread-safe;
  // typical CLI commands read simple state so the practical risk is low. If a race
  // surfaces, this should be queued and processed from the main loop instead.
  _server->on("/api/cmd", HTTP_POST, [this](AsyncWebServerRequest* req) {
    if (!_cmd_handler) { req->send(503, "text/plain", "no cmd handler"); return; }
    if (!req->hasParam("cmd", true)) { req->send(400, "text/plain", "missing cmd"); return; }
    String cmd = req->getParam("cmd", true)->value();
    if (cmd.length() == 0 || cmd.length() > 200) { req->send(400, "text/plain", "bad cmd length"); return; }
    char buf[256]; strncpy(buf, cmd.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    char reply[512]; reply[0] = 0;
    _cmd_handler(buf, reply);
    req->send(200, "text/plain", reply);
  });
}

#endif // ESP_PLATFORM
