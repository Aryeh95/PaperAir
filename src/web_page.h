#pragma once
// The web pages, served as single files. Plain HTML, CSS and JS with no outside resources, so they
// work on a network with no internet (and on the setup hotspot). The dashboard fetches /api/now and
// /api/history and draws the charts as SVG. The settings page has %PLACEHOLDERS% that web.cpp fills.

#define PA_CSS R"CSS(
:root{--bg:#f3f1ec;--card:#fff;--ink:#1d1c1a;--muted:#7a746c;--line:#e7e3db;--soft:#f6f4ef;--accent:#d9682a;--good:#2e9e6a;--info:#4a7fd4;--watch:#d39b14;--act:#dd6a2c;--bad:#c6382c;--shadow:0 1px 2px rgba(30,25,15,.05),0 10px 28px -18px rgba(30,25,15,.3)}
@media(prefers-color-scheme:dark){:root{--bg:#151514;--card:#1e1e1c;--ink:#ece8e0;--muted:#9d978d;--line:#2e2d29;--soft:#262623;--accent:#ea8a4f;--watch:#e2b03a;--shadow:0 1px 2px rgba(0,0,0,.4),0 10px 28px -18px rgba(0,0,0,.7)}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Inter,system-ui,sans-serif;-webkit-font-smoothing:antialiased}
main{max-width:1040px;margin:0 auto;padding:0 16px 56px}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:18px 0 14px;flex-wrap:wrap}
.brand{display:flex;align-items:center;gap:10px;font-weight:700;font-size:18px;letter-spacing:-.01em;color:inherit;text-decoration:none}
.logo{width:32px;height:32px;border-radius:9px;background:var(--accent);display:grid;place-items:center;color:#fff;flex:none}
.logo svg{width:20px;height:20px}
.stamp{color:var(--muted);font-size:13px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:18px 20px;box-shadow:var(--shadow)}
h2{font-size:12px;font-weight:600;color:var(--muted);text-transform:uppercase;letter-spacing:.09em;margin:26px 0 10px;display:flex;justify-content:space-between;align-items:baseline}
.btn{display:inline-flex;align-items:center;gap:6px;font:inherit;font-size:14px;font-weight:500;padding:8px 14px;border:1px solid var(--line);border-radius:9px;background:var(--card);color:var(--ink);cursor:pointer;text-decoration:none;transition:background .15s}
.btn:hover{background:var(--soft)}.btn.p{background:var(--accent);border-color:var(--accent);color:#fff}.btn.p:hover{filter:brightness(1.06)}
.btn.sm{padding:5px 10px;font-size:13px}.btn.danger{color:var(--bad)}.btn svg{width:16px;height:16px}
.btns{display:flex;gap:8px;flex-wrap:wrap}
.muted{color:var(--muted)}
input,select{font:inherit;font-size:16px;width:100%;padding:10px 12px;border:1px solid var(--line);border-radius:9px;background:var(--card);color:var(--ink)}
input:focus,select:focus{outline:2px solid var(--accent);outline-offset:1px;border-color:transparent}
label{display:block;font-size:13px;font-weight:500;color:var(--muted);margin:14px 0 6px}
.hint{font-size:13px;color:var(--muted);margin:6px 0 0}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;padding:3px 10px;border-radius:999px;background:var(--soft);color:var(--muted)}
.pill i{width:8px;height:8px;border-radius:50%;background:currentColor;display:inline-block}
.good{color:var(--good)}.info{color:var(--info)}.watch{color:var(--watch)}.act{color:var(--act)}.bad{color:var(--bad)}
.err{color:var(--bad);margin:0 0 12px;font-size:14px}
)CSS"

static const char kLoginPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PaperAir</title><style>)HTML" PA_CSS R"HTML(
body{display:flex;min-height:100vh;align-items:center;justify-content:center}
form{width:min(340px,92vw)}form .brand{justify-content:center;margin-bottom:18px;font-size:20px}
form .btn{width:100%;justify-content:center;margin-top:16px;padding:11px}
</style></head><body><form class="card" method="post" action="/login">
<div class="brand"><span class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 12h8a3 3 0 1 0-3-3"/><path d="M3 17h12a3 3 0 1 1-3 3"/><path d="M3 7h14a3 3 0 1 0-3-3"/></svg></span>PaperAir</div>%ERR%
<label>Password</label><input type="password" name="password" autofocus autocomplete="current-password">
<button class="btn p">Sign in</button></form></body></html>)HTML";

static const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PaperAir</title><style>)HTML" PA_CSS R"HTML(
.hero{display:grid;grid-template-columns:1fr auto;gap:20px;align-items:center;border-top:4px solid var(--mood,var(--muted));padding-top:16px}
.hero h1{font-size:clamp(24px,4vw,34px);line-height:1.15;letter-spacing:-.02em;margin:10px 0 8px;font-weight:700}
.hero p{margin:0 0 4px;font-size:16px}
.aqis{display:flex;gap:10px}
.aqi{min-width:118px;padding:12px 14px;border-radius:12px;background:var(--soft);text-align:center;border-top:3px solid var(--c,var(--line))}
.aqi small{display:block;font-size:11px;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);font-weight:600}
.aqi b{display:block;font-size:30px;line-height:1.1;margin:4px 0 2px;font-variant-numeric:tabular-nums;color:var(--c,inherit)}
.aqi span{font-size:12px;color:var(--muted)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px 8px;box-shadow:var(--shadow);cursor:pointer;transition:transform .12s,border-color .12s;text-align:left;font:inherit;color:inherit}
.tile:hover{transform:translateY(-1px)}.tile.sel{border-color:var(--accent);box-shadow:0 0 0 3px rgba(217,104,42,.18)}
.tile .l{display:flex;justify-content:space-between;align-items:center;font-size:12px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em}
.tile .st{font-size:11px;font-weight:700;text-transform:none;letter-spacing:0}
.tile .v{font-size:26px;font-weight:650;font-variant-numeric:tabular-nums;letter-spacing:-.02em;margin:4px 0 2px}.tile .u{font-size:13px;color:var(--muted);font-weight:500;margin-left:3px}
.tile svg{width:100%;height:30px;display:block;overflow:visible}
.chart-head{display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;margin-bottom:6px}
.chart-head b{font-size:16px}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:9px;overflow:hidden}
.seg button{font:inherit;font-size:13px;padding:5px 12px;border:0;background:var(--card);color:var(--muted);cursor:pointer}.seg button.on{background:var(--soft);color:var(--ink);font-weight:600}
.chartwrap{position:relative}.chartwrap svg{width:100%;height:auto;display:block;touch-action:pan-y}
.tip{position:absolute;pointer-events:none;background:var(--ink);color:var(--bg);font-size:12px;padding:5px 9px;border-radius:7px;transform:translate(-50%,-125%);white-space:nowrap;display:none;line-height:1.3}
.tip b{display:block;font-size:14px}
.stats{display:flex;gap:24px;flex-wrap:wrap;margin-top:12px;font-size:13px;color:var(--muted)}.stats b{color:var(--ink);font-weight:600;font-variant-numeric:tabular-nums;margin-left:4px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:16px;align-items:start}
.kv{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:12px 20px}
.kv small{display:block;font-size:12px;color:var(--muted)}.kv b{font-weight:600}
.bars{display:inline-flex;gap:2px;align-items:flex-end;height:12px;margin-right:6px;vertical-align:-1px}.bars i{width:3px;background:var(--line);border-radius:1px}.bars i.on{background:var(--good)}
.outbig{display:flex;align-items:center;gap:18px}.outbig b{font-size:44px;line-height:1;font-variant-numeric:tabular-nums}
.outbig .cat{font-size:16px;font-weight:600}
footer{margin-top:28px;font-size:13px;color:var(--muted);text-align:center}
@media(max-width:680px){.hero{grid-template-columns:1fr}.two{grid-template-columns:1fr}.aqi{flex:1}header .stamp{flex-basis:100%;order:9}}
</style></head><body><main>
<header><a class="brand" href="/"><span class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 12h8a3 3 0 1 0-3-3"/><path d="M3 17h12a3 3 0 1 1-3 3"/><path d="M3 7h14a3 3 0 1 0-3-3"/></svg></span>PaperAir</a>
<div class="btns"><span class="stamp" id="stamp" style="align-self:center">Loading…</span><a class="btn sm" href="/settings"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/></svg>Settings</a><a class="btn sm" href="/logout">Sign out</a></div></header>
<section class="card hero" id="hero"><div><span class="pill" id="mood"><i></i><span>Starting</span></span><h1 id="headline">&nbsp;</h1><p id="detail"></p><p class="muted" id="outdoor"></p></div>
<div class="aqis"><div class="aqi" id="aqi-in"><small>Indoor AQI</small><b>--</b><span>from PM2.5</span></div><div class="aqi" id="aqi-out"><small>Outdoor AQI</small><b>--</b><span>&nbsp;</span></div></div></section>
<h2>Right now</h2><div class="grid" id="tiles"></div>
<h2>Trend <span class="muted" style="text-transform:none;letter-spacing:0;font-weight:500">tap a reading above to chart it</span></h2>
<div class="card"><div class="chart-head"><b id="chartname"></b><div class="seg" id="ranges"><button data-r="12">1 h</button><button data-r="72">6 h</button><button data-r="144">12 h</button><button data-r="288" class="on">24 h</button></div></div>
<div class="chartwrap"><svg id="chart" viewBox="0 0 900 280"></svg><div class="tip" id="tip"></div></div><div class="stats" id="stats"></div></div>
<div class="two"><div><h2>Outside</h2><div class="card" id="outside"></div></div>
<div><h2>Device</h2><div class="card"><div class="kv" id="device"></div>
<div class="btns" style="margin-top:16px"><button class="btn p sm" onclick="act('refresh')">Refresh screen</button><button class="btn sm" onclick="act('page')">Switch page</button>
<a class="btn sm" href="/history.csv">Download CSV</a><button class="btn sm danger" onclick="if(confirm('Restart the PaperDIY?'))act('restart')">Restart</button></div></div></div></div>
<footer>PaperAir · <span id="ver"></span> · <a href="/api/now" class="muted">JSON API</a></footer>
</main><script>
const M=[["co2","CO₂","ppm",0],["pm25","PM2.5","µg/m³",1],["pm10","PM10","µg/m³",1],["pm1","PM1","µg/m³",1],["voc","VOC","index",0],["nox","NOx","index",0],["hcho","HCHO","ppb",0],["temp","Temperature","",1],["humidity","Humidity","%",0]];
const MOOD=[["Starting up","var(--muted)"],["No sensor","var(--bad)"],["All clear","var(--good)"],["Worth knowing","var(--info)"],["Take action","var(--act)"],["Act now","var(--bad)"]];
let sel="co2",rng=288,now=null,hist=null,fetched=0;
const $=id=>document.getElementById(id);
const f=(v,d)=>v==null?"--":Number(v).toLocaleString(undefined,{maximumFractionDigits:d,minimumFractionDigits:d});
const dec=k=>M.find(m=>m[0]==k)[3];
const unit=k=>k=="temp"?(now.fahrenheit?"°F":"°C"):M.find(m=>m[0]==k)[2];
function status(k,v){if(v==null||!now.limits[k])return null;const L=now.limits[k];
if(k=="humidity")return v<L.low?["watch","Dry"]:v>L.high?["watch","Humid"]:["good","Good"];
if(v>=L.act)return["act","High"];if(v>=L.watch)return["watch","Elevated"];return["good","Good"]}
function aqiColor(a){return a==null?"var(--muted)":a<=50?"#3da35d":a<=100?"#d9b32a":a<=150?"#ef8a2a":a<=200?"#d9452b":a<=300?"#8f3f97":"#7e0023"}
function spark(pts,cls){const W=120,H=30,vals=pts.filter(v=>v!=null);if(vals.length<2)return"";let lo=Math.min(...vals),hi=Math.max(...vals);if(hi==lo)hi=lo+1;
let d="",on=false;pts.forEach((v,i)=>{if(v==null){on=false;return}const x=W*i/(pts.length-1),y=3+(H-6)*(1-(v-lo)/(hi-lo));d+=(on?"L":"M")+x.toFixed(1)+" "+y.toFixed(1)+" ";on=true});
return`<svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none"><path d="${d}" fill="none" stroke="currentColor" stroke-width="1.5" class="${cls}" opacity=".9" vector-effect="non-scaling-stroke"/></svg>`}
function tiles(){const t=$("tiles");t.innerHTML="";for(const[k,n,,d]of M){if(k=="hcho"&&!now.hcho_fitted)continue;const v=now.readings[k],st=status(k,v);
const el=document.createElement("button");el.className="tile"+(k==sel?" sel":"");
el.innerHTML=`<div class="l"><span>${n}</span>${st?`<span class="st ${st[0]}">${st[1]}</span>`:""}</div><div class="v">${f(v,d)}<span class="u">${unit(k)}</span></div>${hist?spark(hist.metrics[k]||[],st?st[0]:"muted"):""}`;
el.onclick=()=>{sel=k;tiles();chart()};t.appendChild(el)}}
function tAt(i){return new Date(Date.now()-(rng-1-i)*hist.interval_s*1000).toLocaleTimeString([],{hour:"numeric",minute:"2-digit"})}
let C=null;
function chart(){if(!hist)return;const name=M.find(x=>x[0]==sel)[1];$("chartname").textContent=name+" · last "+(rng*5>=60?rng*5/60+" h":rng*5+" min");
const all=hist.metrics[sel]||[],pad0=Math.max(0,rng-all.length),pts=Array(pad0).fill(null).concat(all.slice(-rng));
const svg=$("chart"),narrow=(svg.parentNode.clientWidth||900)<600,W=900,H=narrow?420:280,L=narrow?84:58,R=18,T=18,B=narrow?56:38,fs=narrow?20:12,w=W-L-R,h=H-T-B,vals=pts.filter(v=>v!=null),Lm=now.limits[sel];svg.setAttribute("viewBox",`0 0 ${W} ${H}`);
let lo=vals.length?Math.min(...vals):0,hi=vals.length?Math.max(...vals):1;if(Lm&&Lm.low!=null){lo=Math.min(lo,Lm.low);hi=Math.max(hi,Lm.high)}
if(hi-lo<(sel=="temp"?2:sel=="humidity"?10:1))hi=lo+(sel=="temp"?2:sel=="humidity"?10:1);const pad=(hi-lo)*.12;lo-=pad;hi+=pad;if(sel!="temp"&&lo<0)lo=0;
const X=i=>L+w*i/(rng-1),Y=v=>T+h*(1-(v-lo)/(hi-lo));C={X,Y,pts,L,w,W,H};let s=`<defs><linearGradient id="g" x1="0" x2="0" y1="0" y2="1"><stop offset="0" stop-color="var(--accent)" stop-opacity=".28"/><stop offset="1" stop-color="var(--accent)" stop-opacity="0"/></linearGradient></defs>`;
if(Lm){const band=(a,b,c)=>{a=Math.max(a,lo);b=Math.min(b,hi);if(b>a)s+=`<rect x="${L}" y="${Y(b)}" width="${w}" height="${Y(a)-Y(b)}" fill="${c}" opacity=".09"/>`};
if(Lm.act!=null){band(Lm.watch,Lm.act,"var(--watch)");band(Lm.act,hi,"var(--bad)")}else{band(lo,Lm.low,"var(--watch)");band(Lm.high,hi,"var(--watch)")}}
for(let i=0;i<=4;i++){const v=lo+(hi-lo)*i/4,y=Y(v);s+=`<line x1="${L}" x2="${W-R}" y1="${y}" y2="${y}" stroke="var(--line)"/><text x="${L-10}" y="${y+4}" text-anchor="end" font-size="${fs}" fill="var(--muted)">${f(v,sel=="temp"||sel.startsWith("pm")?1:0)}</text>`}
if(Lm)for(const[key,c]of[["watch","var(--watch)"],["act","var(--bad)"],["low","var(--watch)"],["high","var(--watch)"]]){const v=Lm[key];if(v!=null&&v>lo&&v<hi)s+=`<line x1="${L}" x2="${W-R}" y1="${Y(v)}" y2="${Y(v)}" stroke="${c}" stroke-dasharray="5 5" opacity=".8"/>`}
for(let i=0;i<=4;i++){const m=rng*5*(1-i/4),lab=i==4?"now":(m>=60?m/60+" h":m+" min")+(i==0?" ago":"");s+=`<text x="${L+w*i/4}" y="${H-14}" text-anchor="${i==0?"start":i==4?"end":"middle"}" font-size="${fs}" fill="var(--muted)">${lab}</text>`}
let line="",area="",on=false,start=null;for(let i=0;i<pts.length;i++){const v=pts[i];if(v==null){if(on)area+=`L${X(i-1).toFixed(1)} ${(T+h).toFixed(1)}Z `;on=false;continue}
if(!on){line+=`M${X(i).toFixed(1)} ${Y(v).toFixed(1)} `;area+=`M${X(i).toFixed(1)} ${(T+h).toFixed(1)} L${X(i).toFixed(1)} ${Y(v).toFixed(1)} `;on=true}else{const p=`L${X(i).toFixed(1)} ${Y(v).toFixed(1)} `;line+=p;area+=p}
if(i==pts.length-1&&on)area+=`L${X(i).toFixed(1)} ${(T+h).toFixed(1)}Z`}
s+=`<path d="${area}" fill="url(#g)"/><path d="${line}" fill="none" stroke="var(--accent)" stroke-width="${narrow?3:2.2}" stroke-linejoin="round" stroke-linecap="round"/>`;
const li=pts.length-1-[...pts].reverse().findIndex(v=>v!=null);if(pts[li]!=null)s+=`<circle cx="${X(li)}" cy="${Y(pts[li])}" r="4.5" fill="var(--accent)" stroke="var(--card)" stroke-width="2"/>`;
s+=`<g id="guide" style="display:none"><line id="gl" y1="${T}" y2="${T+h}" stroke="var(--muted)" stroke-dasharray="3 3"/><circle id="gc" r="5" fill="var(--card)" stroke="var(--accent)" stroke-width="2.5"/></g>`;
$("chart").innerHTML=s;const u=unit(sel),d=dec(sel);
$("stats").innerHTML=vals.length?`<span>Low<b>${f(Math.min(...vals),d)} ${u}</b></span><span>Average<b>${f(vals.reduce((a,b)=>a+b,0)/vals.length,d)} ${u}</b></span><span>High<b>${f(Math.max(...vals),d)} ${u}</b></span><span>Points<b>${vals.length} of ${rng}</b></span>`:'<span class="muted">No history yet, the first point arrives after 5 minutes</span>'}
function hover(e){if(!C)return;const svg=$("chart"),r=svg.getBoundingClientRect(),cx=(e.touches?e.touches[0].clientX:e.clientX)-r.left,x=cx/r.width*C.W;
let i=Math.round((x-C.L)/C.w*(rng-1));i=Math.max(0,Math.min(rng-1,i));const v=C.pts[i],g=svg.querySelector("#guide"),tip=$("tip");
if(v==null){g.style.display="none";tip.style.display="none";return}g.style.display="";const gx=C.X(i),gy=C.Y(v);
g.querySelector("#gl").setAttribute("x1",gx);g.querySelector("#gl").setAttribute("x2",gx);g.querySelector("#gc").setAttribute("cx",gx);g.querySelector("#gc").setAttribute("cy",gy);
tip.style.display="block";tip.style.left=gx/C.W*r.width+"px";tip.style.top=gy/C.H*r.height+"px";tip.innerHTML=`<b>${f(v,dec(sel))} ${unit(sel)}</b>${tAt(i)}`}
function leave(){const g=$("chart").querySelector("#guide");if(g)g.style.display="none";$("tip").style.display="none"}
$("chart").addEventListener("mousemove",hover);$("chart").addEventListener("touchmove",hover,{passive:true});$("chart").addEventListener("mouseleave",leave);$("chart").addEventListener("touchend",leave);
$("ranges").onclick=e=>{const b=e.target.closest("button");if(!b)return;rng=+b.dataset.r;for(const x of $("ranges").children)x.classList.toggle("on",x==b);chart()};
function kv(o){return o.map(([k,v])=>`<div><small>${k}</small><b>${v}</b></div>`).join("")}
function bars(rssi){const n=rssi>-60?4:rssi>-70?3:rssi>-80?2:1;return`<span class="bars">${[4,7,10,12].map((h,i)=>`<i style="height:${h}px" class="${i<n?"on":""}"></i>`).join("")}</span>`}
function render(){const a=now.advice,m=MOOD[a.mood]||MOOD[0];$("stamp").textContent=now.time||"";$("hero").style.setProperty("--mood",m[1]);
const pill=$("mood");pill.style.color=m[1];pill.lastElementChild.textContent=m[0];$("headline").textContent=a.headline;$("detail").textContent=a.detail;$("outdoor").textContent=a.outdoor;
const ai=$("aqi-in"),ao=$("aqi-out"),o=now.outdoor;ai.style.setProperty("--c",aqiColor(now.indoor_aqi>=0?now.indoor_aqi:null));ai.children[1].textContent=now.indoor_aqi>=0?now.indoor_aqi:"--";
ao.style.setProperty("--c",aqiColor(o.ok?o.aqi:null));ao.children[1].textContent=o.ok?o.aqi:"--";ao.children[2].textContent=o.ok?o.category:"no reading";
tiles();$("outside").innerHTML=o.ok?`<div class="outbig"><b style="color:${aqiColor(o.aqi)}">${o.aqi}</b><div><div class="cat">${o.category}</div><div class="muted">PM2.5 ${f(o.pm25,1)} µg/m³ · ${o.source||"outdoor sensor"}</div><div class="muted" style="font-size:13px">updated ${o.age}</div></div></div>`:'<span class="muted">No outdoor reading yet. Add a PurpleAir or AirNow key in Settings.</span>';
const d=now.device;$("device").innerHTML=kv([["Wi-Fi",bars(d.rssi)+d.ssid+` <span class="muted">${d.rssi} dBm</span>`],["Address",d.ip],["Up for",d.uptime],["SEN66",d.sen66],["SFA40",d.sfa40],["Screen",d.page+" page"],["Free memory",d.heap],["Clock",now.time_valid?"synced":"not set"]]);$("ver").textContent="firmware "+d.version;chart()}
async function load(){try{const r=await fetch("/api/now",{cache:"no-store"});if(r.status==401){location="/login";return}now=await r.json();
if(!hist||now.history_version!=hist.version){hist=await(await fetch("/api/history",{cache:"no-store"})).json()}fetched=Date.now();render()}catch(e){$("stamp").textContent="Not reachable, retrying…"}}
async function act(a){await fetch("/api/"+a,{method:"POST"});setTimeout(load,a=="restart"?15000:1500)}
load();setInterval(load,30000);
</script></body></html>)HTML";

static const char kSettingsPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PaperAir settings</title><style>)HTML" PA_CSS R"HTML(
main{max-width:640px}.card+h2{margin-top:30px}
.row{display:grid;grid-template-columns:1fr 1fr;gap:0 14px}@media(max-width:480px){.row{grid-template-columns:1fr}}
.pw{position:relative}.pw input{padding-right:64px}.pw button{position:absolute;right:6px;top:6px;bottom:6px;font:inherit;font-size:12px;border:0;background:var(--soft);color:var(--muted);border-radius:6px;padding:0 10px;cursor:pointer}
.nets{display:flex;flex-direction:column;gap:6px;margin-top:8px}.net{display:flex;justify-content:space-between;align-items:center;font:inherit;text-align:left;padding:9px 12px;border:1px solid var(--line);border-radius:9px;background:var(--card);color:var(--ink);cursor:pointer}.net:hover{background:var(--soft)}
.net small{color:var(--muted)}
.seg{display:flex;border:1px solid var(--line);border-radius:9px;overflow:hidden}.seg label{flex:1;margin:0;text-align:center;padding:9px;cursor:pointer;color:var(--muted);font-size:14px}
.seg input{display:none}.seg input:checked+span{color:var(--ink);font-weight:600}.seg label:has(input:checked){background:var(--soft)}
.notice{border-left:4px solid var(--accent);padding:12px 16px;margin-bottom:16px}
.save{position:sticky;bottom:0;padding:14px 0 18px;background:linear-gradient(transparent,var(--bg) 30%);display:flex;gap:10px;align-items:center;justify-content:space-between}
</style></head><body><main>
<header><a class="brand" href="%BACK%"><span class="logo"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 12h8a3 3 0 1 0-3-3"/><path d="M3 17h12a3 3 0 1 1-3 3"/><path d="M3 7h14a3 3 0 1 0-3-3"/></svg></span>PaperAir</a><span class="stamp">Settings</span></header>
%SETUP%
<form method="post" action="/settings" autocomplete="off">
<h2>Wi-Fi</h2><div class="card">
<label>Network</label><div class="pw"><input name="ssid" value="%SSID%" placeholder="Network name"><button type="button" id="scan">Scan</button></div><div class="nets" id="nets"></div>
<label>Password</label><div class="pw"><input type="password" name="pass" placeholder="%PASS_PH%"><button type="button" onclick="tog(this)">Show</button></div>
<p class="hint">Leave the password empty to keep the one already stored.</p></div>
<h2>Outdoor air</h2><div class="card">
<label>PurpleAir read key</label><div class="pw"><input type="password" name="pa_key" value="%PA_KEY%"><button type="button" onclick="tog(this)">Show</button></div>
<div class="row"><div><label>Sensor index</label><input name="pa_idx" inputmode="numeric" value="%PA_IDX%"></div><div><label>Backup sensor (0 = none)</label><input name="pa_bak" inputmode="numeric" value="%PA_BAK%"></div></div>
<p class="hint">Free key at develop.purpleair.com. The index is the number after “select=” in a sensor's address on map.purpleair.com.</p>
<label>AirNow key</label><div class="pw"><input type="password" name="an_key" value="%AN_KEY%"><button type="button" onclick="tog(this)">Show</button></div>
<label>ZIP code</label><input name="an_zip" inputmode="numeric" value="%AN_ZIP%">
<p class="hint">AirNow (docs.airnowapi.org) is the fallback when PurpleAir has nothing recent. Leave both keys empty to show indoor air only.</p></div>
<h2>Display</h2><div class="card">
<div class="row"><div><label>Temperature</label><div class="seg"><label><input type="radio" name="units" value="f" %F_SEL%><span>°F</span></label><label><input type="radio" name="units" value="c" %C_SEL%><span>°C</span></label></div></div>
<div><label>Clock</label><div class="seg"><label><input type="radio" name="clock" value="12" %H12_SEL%><span>12-hour</span></label><label><input type="radio" name="clock" value="24" %H24_SEL%><span>24-hour</span></label></div></div></div>
<label>Temperature offset (°C, usually negative)</label><input name="t_off" inputmode="decimal" value="%T_OFF%">
<p class="hint">The board warms the sensor a little. Compare with a thermometer after an hour and enter the difference.</p>
<label>Time zone</label><select id="tzsel"><option value="EST5EDT,M3.2.0,M11.1.0">Eastern (US)</option><option value="CST6CDT,M3.2.0,M11.1.0">Central (US)</option><option value="MST7MDT,M3.2.0,M11.1.0">Mountain (US)</option><option value="MST7">Arizona</option><option value="PST8PDT,M3.2.0,M11.1.0">Pacific (US)</option><option value="AKST9AKDT,M3.2.0,M11.1.0">Alaska</option><option value="HST10">Hawaii</option><option value="GMT0BST,M3.5.0/1,M10.5.0">UK</option><option value="CET-1CEST,M3.5.0,M10.5.0/3">Central Europe</option><option value="IST-2IDT,M3.4.4/26,M10.5.0">Israel</option><option value="AEST-10AEDT,M10.1.0,M4.1.0/3">Sydney</option><option value="">Other (POSIX string below)</option></select>
<input name="tz" id="tz" value="%TZ%" style="margin-top:8px" placeholder="POSIX TZ string"></div>
<h2>This page</h2><div class="card">
<label>Password for this web page</label><div class="pw"><input type="password" name="web_pw" placeholder="%WEB_PH%"><button type="button" onclick="tog(this)">Show</button></div>
<p class="hint">Needed to open the dashboard from your network. Leave empty to keep the current one; enter a single space to remove it.</p></div>
<div class="save"><a class="btn" href="%BACK%">Cancel</a><button class="btn p" style="padding:11px 22px">Save</button></div>
</form>
<h2>Setup hotspot</h2><div class="card"><p class="muted" style="margin:0 0 12px">Opens the PaperAir-Setup hotspot (password <b>paperair</b>) for 15 minutes so a phone can change these settings without the network. Holding the lower button (G3) for 3 seconds does the same.</p>
<button class="btn" onclick="fetch('/api/setup',{method:'POST'}).then(()=>alert('Hotspot opened. Join PaperAir-Setup and open http://192.168.4.1/'))">Open hotspot</button></div>
<script>
function tog(b){const i=b.parentNode.querySelector("input");i.type=i.type=="password"?"text":"password";b.textContent=i.type=="password"?"Show":"Hide"}
const tzs=document.getElementById("tzsel"),tz=document.getElementById("tz");function syncTz(){tzs.value=[...tzs.options].some(o=>o.value==tz.value&&o.value)?tz.value:""}syncTz();tzs.onchange=()=>{if(tzs.value)tz.value=tzs.value};tz.oninput=syncTz;
document.getElementById("scan").onclick=async function(){this.textContent="Scanning…";this.disabled=true;try{const r=await(await fetch("/api/scan")).json();
document.getElementById("nets").innerHTML=r.length?r.map(n=>`<button type="button" class="net" data-s="${n.ssid.replace(/"/g,"&quot;")}"><span>${n.ssid.replace(/</g,"&lt;")} ${n.secure?"🔒":""}</span><small>${n.rssi} dBm</small></button>`).join(""):'<span class="hint">No networks found. Try again.</span>';
for(const b of document.querySelectorAll(".net"))b.onclick=()=>{document.querySelector("[name=ssid]").value=b.dataset.s;document.querySelector("[name=pass]").focus()}}catch(e){alert("Scan failed: "+e)}this.textContent="Scan";this.disabled=false};
</script></main></body></html>)HTML";

static const char kSavedPage[] PROGMEM = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>PaperAir</title>%REFRESH%<style>)HTML" PA_CSS R"HTML(
body{display:flex;min-height:100vh;align-items:center;justify-content:center}.card{width:min(420px,92vw)}h1{font-size:22px;margin:0 0 10px}
</style></head><body><div class="card"><h1>Saved</h1>%MSG%<p style="margin-top:18px"><a class="btn p" href="%NEXT%">Continue</a></p></div></body></html>)HTML";
