/*
  AppWeb.cpp - ESPGeiger's part of the portal style, theme script and the graph lib.

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include <stddef.h>
#include <stdint.h>
#include <pgmspace.h>
#include <EGHttpServer.h>
#include "Corium/System/AppHooks.h"
#include "Corium/WebPortal/WebPortal.h"

#include "STYLE_CSS.gz.h"
#include "THEME_JS.gz.h"
#include "picographJS.gz.h"

// after the core style.css
#if !EG_GZ_STYLE_CSS
extern const char APP_CSS[] PROGMEM = R"CSS(.stat>.row.envflex{display:flex;gap:2em}
.stat>.row.envflex>div{flex:1;display:flex;gap:2em;align-items:baseline}
.bar-row{display:flex;align-items:flex-end;gap:1px;height:80px;width:100%;overflow:hidden;margin:.4em 0;border-bottom:1px solid var(--border)}
.bar-row.bot{margin:.4em 0 1em}
.bar-lbls{display:flex;gap:1px;font-size:.65em;color:var(--muted);white-space:nowrap;overflow:hidden}
.bar-cell{flex:1;min-width:0}
#g1{height:200px;width:100%;background:var(--bg);border:1px solid var(--border);border-radius:4px}
#g2{display:flex;flex-wrap:wrap;gap:.2em 1.2em;margin:.4em 0;font-size:1.05em}
#t1{width:100%;height:250px;font-family:monospace;font-size:.8em;line-height:1.3;resize:vertical}
:root.crt{--bg:#000;--page:#000;--fg:#0f0;--muted:#080;--border:#050;--accent:#0a0;--link:#0a0;--card:#001a00}
:root.crt[data-theme=light]{--bg:#212529;--card:#2b3035;--border:#373b3e;background:#1a1d20}
:root.crt[data-theme=dark]{background:#000}
:root.crt body{font-family:Consolas,Menlo,monospace;font-weight:600;box-shadow:none}
:root.crt :is(h1,h2){color:#0f0;text-shadow:0 0 4px #0f0;font-weight:700}
:root.crt :is(th,td,p,span,div){font-weight:600}
:root.crt :is(button:not(.btn-o),input[type=submit],.menu a,.back){color:#000;text-shadow:none}
:root.crt .devhead h1 .tag{color:#0a0;text-shadow:none;opacity:1}
:root.crt::after{content:'';position:fixed;inset:0;pointer-events:none;background:repeating-linear-gradient(0deg,rgba(0,0,0,.18) 0,rgba(0,0,0,.18) 1px,transparent 1px,transparent 3px);z-index:9998;animation:crtroll 4s linear infinite}
@keyframes crtroll{to{background-position:0 3px}})CSS";
#endif

// after the core theme.js
#if !EG_GZ_THEME_JS
extern const char APP_THEME_JS[] PROGMEM = R"JS(!function(){
var d=document.documentElement,L=addEventListener,LS=localStorage,SS=sessionStorage,
TE=()=>dispatchEvent(new Event('themechange')),
AR=el=>{
  var u=d.classList.contains('crt'),
      list=el&&el.dataset?[el]:document.querySelectorAll('.usv');
  list.forEach(e=>{
    var v=parseFloat(e.dataset.uv);
    if(isNaN(v))return;
    e.textContent=u?(v*114).toFixed(v*114>=10?1:2):v.toFixed(3);
    e.title=u?v.toFixed(3)+' µSv/h':'';
  });
  document.querySelectorAll('.usvL').forEach(e=>{e.textContent=u?(e.dataset.on||'µR/h'):(e.dataset.off||'µSv/h')});
},
AD=el=>{
  var u=d.classList.contains('crt'),
      list=el&&el.dataset?[el]:document.querySelectorAll('.dose');
  list.forEach(e=>{
    var v=parseFloat(e.dataset.dose);
    if(isNaN(v))return;
    var dv=u?v*114:v,s=u?'R':'Sv',p='µ',sc=1;
    if(dv>=1e6){p='';sc=1e6}
    else if(dv>=1e3){p='m';sc=1e3}
    dv/=sc;
    e.textContent=(dv>=10?dv.toFixed(1):dv.toFixed(2))+' '+p+s;
  });
};
window.setUsv=(el,v)=>{el.dataset.uv=v;AR(el)};
window.setDose=(el,v)=>{el.dataset.dose=v;AD(el)};
window.applyRad=AR;
window.toggleCrt=()=>{LS.crt=d.classList.toggle('crt')?'1':'0';AR();AD();TE();window.applySnd&&window.applySnd()};
function C(){var s=LS.crt,ds=new Date().toDateString().substr(4,6),hh=2166136261;
for(var hi=0;hi<6;hi++)hh=Math.imul(hh^ds.charCodeAt(hi),16777619)>>>0;
if(s==='1')d.classList.add('crt');
else if(s==='0')d.classList.remove('crt');
AR();AD();
var msg={1332614041:'4piiIEhhcHB5IGJpcnRoZGF5IE1hcmllIEN1cmllIC0gYm9ybiAxODY3Lg==',1368767674:'8J+puyBIYXBweSBiaXJ0aGRheSBXaWxoZWxtIFLDtm50Z2VuIC0gYm9ybiAxODQ1Lg=='}[hh];
if(msg&&location.pathname==='/'&&SS.dayDis!=hh&&!byID('dyB')){var p=document.createElement('div');p.id='dyB';p.textContent=atob(msg);p.onclick=()=>{SS.dayDis=hh;p.remove()};var ap=()=>document.body.appendChild(p);document.body?ap():L('DOMContentLoaded',ap)}}
C();L('pageshow',e=>{if(e.persisted)C()});
var k=[38,38,40,40,37,39,37,39,66,65],i=0;
L('keydown',e=>{i=e.keyCode===k[i]?i+1:0;
if(i===k.length){toggleCrt();i=0;}})
}();)JS";
#endif

// picograph.js - https://github.com/RainingComputers/picograph.js
// minimised, with adjustments to allow fixed scroll
extern const char picographJS[] PROGMEM = R"JS(
var cVID=(t,s)=>t.map(x=>s+x.replace(" ","")+"value"),
gTS=()=>new Date().toLocaleTimeString(),
sclInv=(t,s,i,e)=>(1-(t-s)/(i-s))*e,
sAL=(t,s)=>(t.shift(),t.push(s),t),
eA2D=(t,s,i)=>Array(t).fill().map(_=>Array(s).fill(i)),
eA=(t,s)=>Array(t).fill(s),
cls=["#D35","#090","#00F","#F0F","#933","#009","#099","#999"],
cA=t=>Array(t).fill().map((_,i)=>cls[i%cls.length]);
function cLeg(t,s,i,e){const h=`<span>${i}</span>`,n=":"==i.at(-1)?`<span id="${e}"></span>`:"";byID(t).innerHTML+=`<div style=display:inline-block><svg width=10 height=10><rect width=10 height=10 fill="${s}"/></svg> ${h} ${n}</div>`}
class Graph{
constructor(t,s,i,e,h,n,a=0,l=!1,o=!1,r=5,c=1,m=2){const d=cVID(s,t);this.iE(t,e,d),this.sC(s,i,n,a,l,o,r,c,m),this.tc=0,this.vc=0,this.rs(h),""===byID(e).innerHTML&&this.cL(),addEventListener("themechange",()=>{this.fg=null;this.rd()})}
iE(t,s,i){this.cv=byID(t),this.ctx=this.cv.getContext("2d"),this.sW(),this.lID=s,this.vIDs=i}
sC(t,s,i,e,h,n,a,l,o){this.lb=t,this.u=s,this.vF=l,this.vl=h,this.tm=n,this.nL=t.length,this.co=cA(this.nL);var d=document.createElement("textarea");d.innerHTML=s;this.uD=d.value;this.scC(i,e,a,o)}
scC(t,s,i,e){this.mv=t,this.lv=s,this.ss=i,this.aS=e,e!=1&&e!=2&&(this.aS=0,Number.isFinite(s)||(this.lv=0),Number.isFinite(t)||(this.mv=100))}
rs(t){this.iS=t*this.S,this.nF=this.w/this.iS,this.nP=Math.round(this.nF)+1,this.P=eA2D(this.nL,this.nP),this.tsA=eA(this.nP,""),this.fz=null,this.hs=null,this.sp=null}
cL(){byID(this.lID).innerHTML="";for(let t=0;t<this.nL;t++)cLeg(this.lID,this.co[t],this.lb[t]+":",this.vIDs[t])}
uCfg(t,s,i,e,h=0,n=!1,a=!1,l=5,o=1,r=1){this.cl(),this.sC(t,s,e,h,n,a,l,o,r),this.rs(i),this.cL()}
uMM(t){t<this.lv&&(this.lv=t),t>this.mv&&(this.mv=t);let s=Math.abs(this.mv-this.lv),mn=Infinity,mx=-Infinity;for(const r of this.P)for(const v of r)Number.isFinite(v)&&(v<mn&&(mn=v),v>mx&&(mx=v));const i=Math.ceil(mx+.05*s),e=Math.floor(mn)-.05*s;if(2==this.aS)return this.mv=i;t>this.mv-.05*s&&(this.mv=i),t<this.lv+.05*s&&(this.lv=e)}
uP(t){for(let s=0;s<this.nL;s++)this.aS>0&&this.uMM(t[s]),this.P[s]=sAL(this.P[s],t[s])}
uL(t){for(let s=0;s<this.nL;s++)byID(this.vIDs[s]).innerHTML=t[s].toFixed(2)+" "+this.u}
uT(ts){this.tsA=sAL(this.tsA,ts||gTS())}
dV(){this.vc++;const c=this.ctx,h=this.h;c.strokeStyle=this.gC;c.beginPath();for(let t=this.nP-this.vc;t>=0;t-=this.vF){const s=t*this.iS;c.moveTo(s,0);c.lineTo(s,h)}c.stroke();this.vc==this.vF&&(this.vc=0)}
cl(){this.ctx.clearRect(0,0,this.w,this.h)}
sW(){this.S=window.devicePixelRatio,this.cv.width=this.cv.clientWidth*this.S,this.cv.height=this.cv.clientHeight*this.S,this.w=this.ctx.canvas.width,this.h=this.ctx.canvas.height}
sIW(){this.iS=this.w/this.nF,this.ctx.lineWidth=2*this.S}
sS(){this.hs=this.h/this.ss,this.sp=(this.mv-this.lv)/this.ss,this.fz=Math.min(.5*this.hs,15*this.S),this.ctx.font=this.fz+"px monospace";if(!this.fg){const cs=getComputedStyle(this.cv);this.fg=cs.color;this.gC=cs.borderTopColor}this.ctx.fillStyle=this.fg}
dH(){const c=this.ctx,w=this.w,h=this.h,fs=this.fz+2*this.S,u=this.uD;c.strokeStyle=this.gC;c.beginPath();for(let s=1;s<=this.ss;s++){const i=h-s*this.hs;c.fillText((s*this.sp+this.lv).toFixed(2)+" "+u,2,i+fs);c.moveTo(0,i);c.lineTo(w,i)}c.stroke()}
dT(){const c=this.ctx,h=this.h,fs=this.fz+2*this.S,t=c.measureText((this.ss*this.sp).toFixed(2)).width,s=Math.floor(t/this.iS+1)+1;this.tc++;for(let t=this.nP-this.tc;t>=s;t-=this.vF){const s=t*this.iS,e=c.measureText(this.tsA[t]).width+4*this.S;c.rotate(Math.PI/2),c.fillText(this.tsA[t],h-e,-s+fs),c.stroke(),c.rotate(-Math.PI/2)}this.tc==this.vF&&(this.tc=0)}
dr(){const c=this.ctx,lv=this.lv,mv=this.mv,h=this.h,sz=this.iS,F=Number.isFinite;for(let t=this.nL-1;t>=0;t--){c.strokeStyle=this.co[t];c.beginPath();let st=0;for(let s=this.nP-1;s>=0;s--){const v=this.P[t][s];if(!F(v)){st=0;continue}const y=sclInv(v,lv,mv,h),x=s*sz;if(st)c.lineTo(x,y);else c.moveTo(x,y);st=1}c.stroke()}}
rd(){this.cl(),this.sW(),this.sIW(),this.sS(),this.vl&&this.dV(),this.dH(),this.tm&&this.dT(),this.dr()}
update(t,ts){this.uP(this.sm(t)),this.uL(t),this.uT(ts),this.rd()}
sm(t){if(!(this.av>1))return t;this.hb=this.hb||[];return t.map((v,i)=>{const h=this.hb[i]=this.hb[i]||[];h.push(v),h.length>this.av&&h.shift();return h.reduce((a,b)=>a+b,0)/h.length})}
})JS";

// /status and /hv load it before their own
static void hGraphJs(EGHttpRequest& req, EGHttpResponse& res, void*) {
#if EG_GZ_picographJS
  WebPortal::sendGz(res, FPSTR(HTTP_CT_JS), picographJS_GZ, picographJS_GZ_LEN);
#else
  res.addHeader(FPSTR(HTTP_H_CACHE), FPSTR(HTTP_CACHE_FOREVER));
  res.send(200, FPSTR(HTTP_CT_JS), FPSTR(picographJS));
#endif
}

namespace AppWeb {
void routes(EGHttpServer& http) {
  http.on(F("/pgjs"), EGHttpRequest::GET, hGraphJs);
}
}
