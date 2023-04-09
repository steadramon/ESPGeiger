/*
  htmlOptions.h - Static html to print the options on the cofig form
  
  Copyright (C) 2023 @steadramon

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

const char IOTWEBCONF_DASHBOARD_STYLE_INNER[] PROGMEM = "table{margin:20px auto;}h3{text-align:center;}.card{height:12em;margin:10px;text-align:left;font-family:Arial;border:3px groove;border-radius:0.3rem;display:inline-block;padding:10px;min-width:260px;}td{padding:0 10px;}textarea{resize:vertical;width:100%;margin:0;height:318px;padding:5px;overflow:auto;}#c1{width:98%;padding:5px;}#t1{width:98%}.console{display:inline-block;text-align:center;margin:10px 0;width:98%;max-width:1080px;}.G{color:green;}.R{color:red}";

const char HTTP_HEAD_MREFRESH[] PROGMEM = "<meta http-equiv='refresh' content='10; url=/'>";

static const char STATUS_PAGE_BODY[] PROGMEM = R"HTML(<center><h1>{v}</h1></center>
<style>
textarea{resize:vertical;width:100%;margin:0;height:250px;padding:5px;overflow:auto;}
.wrap{min-width: 350px;max-width: 900px;width: 50vw; }
</style>
<canvas id="g1" style="height:200px; width: 100%; min-width: 350px; border:2px solid #000;"></canvas>
<div id="g2" class="wdr"></div>
<table>
<tr><th>CPM:</th><td><span id="cpm">-</span></td></tr>
<tr><th>uSv:</th><td><span id="usv">-</span></td></tr>
<tr><th>Uptime:</th><td><span id="uptime">-</span></td></tr>
</table>
<div>
<h3>Console</h3>
<textarea readonly='' id='t1' wrap='off'></textarea>
</div>
<script src="/js"></script>
)HTML";

static const char STATUS_PAGE_FOOT[] PROGMEM = "<br/><small><a target='_blank' href='https://github.com/steadramon/ESPGeiger'>{1}</a> v{2}</small>";

static const char statusJS[] PROGMEM = R"JS("use strict";
!function(){let e=new Graph("g1",["CPM","CPM5","CPM15"],"cpm","g2",15,null,0,!0,!0,5,5);!function t(){var n=new XMLHttpRequest;n.open("GET","/json",!0),n.onload=function(){if(n.status>=200&&n.status<400){var o=JSON.parse(n.responseText);byID('uptime').innerHTML=o.uptime;byID('cpm').innerHTML=o.cpm;byID('usv').innerHTML=(o.cpm/o.ratio).toFixed(4);e.update([o.cpm,o.cpm5,o.cpm15]),setTimeout(t,3e3)}},n.onerror=function(){setTimeout(t,6e3)},n.send()}()}();

var x=null,lt,to,tp,pc='';var sn=0,id=0;

function f(){
  var c,o='',t;
  clearTimeout(lt);
  t = document.getElementById('t1');

    x=new XMLHttpRequest();
    x.onload = function() {
      if(x.status==200){
        var z,d;var a=x.responseText;
        id=a.substr(0,a.indexOf('\n'));
        z=a.substr(a.indexOf('\n')+1);
        if(z.length>0){
          t.value+=z;
        }
        if (t.scrollTop >= sn) {
          t.scrollTop=99999;
          sn=t.scrollTop;
        }
      }
      lt=setTimeout(f,3210);
    };
    x.onerror = function() {
      lt=setTimeout(f,6000);
    };

    x.open('GET','/cs?c1='+id,true);
    x.send();
  
  return false;
}

window.addEventListener('load', f);

)JS";

// picograph.js - https://github.com/RainingComputers/picograph.js
static const char picographJS[] PROGMEM = R"JS(
function byID(t){return document.getElementById(t)}function createValueIDs(t,s){const i=[];for(let e=0;e<t.length;e++)i[e]=s+t[e].replace(" ","")+"value";return i}function createLegendRect(t,s,i,e){const h=`<span>${i}</span>`,n=":"==i.at(-1)?`<span id="${e}"></span>`:"";byID(t).innerHTML+=`\n        <div style="display: inline-block;">\n            <svg width="10" height="10">\n                <rect width="10" height="10" style="fill: ${s}"/>\n            </svg> \n            ${h}\n            ${n}\n        <div>\n    `}class Graph{constructor(t,s,i,e,h,n,a=0,l=!1,o=!1,r=5,c=1,m=2){const d=createValueIDs(s,t);this.initializeElements(t,e,d),this.setConfig(s,i,n,a,l,o,r,c,m),this.tscount=0,this.vlcount=0,this.resetState(h),""===byID(e).innerHTML&&this.createLegends()}initializeElements(t,s,i){this.canvas=byID(t),this.ctx=this.canvas.getContext("2d"),this.setWidthHeightAndCssScale(),this.labelDivID=s,this.valueIDs=i}setConfig(t,s,i,e,h,n,a,l,o){this.labels=t,this.unit=s,this.vlinesFreq=l,this.vlines=h,this.timestamps=n,this.noLabels=t.length,this.colors=colorArray(this.noLabels),this.setScalingConfig(i,e,a,o)}setScalingConfig(t,s,i,e){this.maxVal=t,this.minVal=s,this.scalesteps=i,this.autoScaleMode=e,0==[1,2].includes(e)&&(this.autoScaleMode=0,!1===Number.isFinite(s)&&(this.minVal=0),!1===Number.isFinite(t)&&(this.maxVal=100))}resetState(t){this.intervalSize=t*this.cssScale,this.nPointsFloat=this.width/this.intervalSize,this.nPoints=Math.round(this.nPointsFloat)+1,this.points=emptyArray2D(this.noLabels,this.nPoints),this.timestampsArray=emptyArray(this.nPoints,""),this.fontSize=null,this.hstep=null,this.sstep=null}createLegends(){byID(this.labelDivID).innerHTML="";for(let t=0;t<this.noLabels;t++)createLegendRect(this.labelDivID,this.colors[t],this.labels[t]+":",this.valueIDs[t])}updateConfig(t,s,i,e,h=0,n=!1,a=!1,l=5,o=1,r=1){this.clear(),this.setConfig(t,s,e,h,n,a,l,o,r),this.resetState(i),this.createLegends()}setColors(t){this.colors=t,this.createLegends()}updateMinMax(t){t<this.minVal&&(this.minVal=t),t>this.maxVal&&(this.maxVal=t);let s=Math.abs(this.maxVal-this.minVal),i=Math.ceil(Math.max.apply(Math,this.points.flat().filter(Number.isFinite)))+.05*s,e=Math.floor(Math.min.apply(Math,this.points.flat().filter(Number.isFinite)))-.05*s;if(2==this.autoScaleMode)return this.maxVal=i;t>this.maxVal-.05*s&&(this.maxVal=i),t<this.minVal+.05*s&&(this.minVal=e)}updatePoints(t){for(let s=0;s<this.noLabels;s++)this.autoScaleMode>0&&this.updateMinMax(t[s]),this.points[s]=shiftArrayLeft(this.points[s],t[s])}updateLegends(t){for(let s=0;s<this.noLabels;s++)byID(this.valueIDs[s]).innerHTML=t[s].toFixed(2)+" "+this.unit}updateTimestamps(){const t=getTimestamp();this.timestampsArray=shiftArrayLeft(this.timestampsArray,t)}drawVerticalLines(){this.vlcount++;for(let t=this.nPoints-this.vlcount;t>=0;t-=this.vlinesFreq){const s=t*this.intervalSize;this.ctx.beginPath(),this.ctx.moveTo(s,0),this.ctx.lineTo(s,this.height),this.ctx.strokeStyle="#DDD",this.ctx.stroke()}this.vlcount==this.vlinesFreq&&(this.vlcount=0)}clear(){this.ctx.clearRect(0,0,this.width,this.height)}setWidthHeightAndCssScale(){this.cssScale=window.devicePixelRatio,this.canvas.width=this.canvas.clientWidth*this.cssScale,this.canvas.height=this.canvas.clientHeight*this.cssScale,this.width=this.ctx.canvas.width,this.height=this.ctx.canvas.height}setIntervalSizeAndLineWidth(){this.intervalSize=this.width/this.nPointsFloat,this.ctx.lineWidth=2*this.cssScale}setStepAndFontSizePixels(){this.hstep=this.height/this.scalesteps,this.sstep=(this.maxVal-this.minVal)/this.scalesteps,this.fontSize=Math.min(.5*this.hstep,15*this.cssScale),this.ctx.font=this.fontSize+"px monospace"}drawHorizontalLines(){let t=document.createElement("textarea");t.innerHTML=this.unit;for(let s=1;s<=this.scalesteps;s++){const i=this.height-s*this.hstep,e=2,h=this.fontSize+2*this.cssScale;this.ctx.fillText((s*this.sstep+this.minVal).toFixed(2)+" "+t.value,e,i+h),this.ctx.beginPath(),this.ctx.moveTo(0,i),this.ctx.lineTo(this.width,i),this.ctx.strokeStyle="#DDD",this.ctx.stroke()}}drawTimestamps(){const t=this.ctx.measureText((this.scalesteps*this.sstep).toFixed(2)).width,s=Math.floor(t/this.intervalSize+1)+1;this.tscount++;for(let t=this.nPoints-this.tscount;t>=s;t-=this.vlinesFreq){const s=t*this.intervalSize,i=this.fontSize+2*this.cssScale,e=this.ctx.measureText(this.timestampsArray[t]).width+4*this.cssScale;this.ctx.rotate(Math.PI/2),this.ctx.fillText(this.timestampsArray[t],this.height-e,-s+i),this.ctx.stroke(),this.ctx.rotate(-Math.PI/2)}this.tscount==this.vlinesFreq&&(this.tscount=0)}drawGraph(){for(let t=this.noLabels-1;t>=0;t--)for(let s=this.nPoints-1;s>0;s--){const i=s*this.intervalSize,e=(s-1)*this.intervalSize,h=scaleInvert(this.points[t][s],this.minVal,this.maxVal,this.height),n=scaleInvert(this.points[t][s-1],this.minVal,this.maxVal,this.height);this.ctx.beginPath(),this.ctx.moveTo(i,h),this.ctx.lineTo(e,n),this.ctx.strokeStyle=this.colors[t],this.ctx.stroke()}}update(t){this.updatePoints(t),this.updateLegends(t),this.updateTimestamps(),this.clear(),this.setWidthHeightAndCssScale(),this.setIntervalSizeAndLineWidth(),this.setStepAndFontSizePixels(),this.vlines&&this.drawVerticalLines(),this.drawHorizontalLines(),this.timestamps&&this.drawTimestamps(),this.drawGraph()}}function switchGraph(t,s){t.clear(),s.createLegends()}function getTimestamp(){const t=new Date;return(t.getHours()<10?"0":"")+t.getHours()+(t.getMinutes()<10?":0":":")+t.getMinutes()+(t.getSeconds()<10?":0":":")+t.getSeconds()}function scaleInvert(t,s,i,e){return(1-(t-s)/(i-s))*e}function shiftArrayLeft(t,s){return t.shift(),t.push(s),t}function emptyArray2D(t,s,i){return Array.from({length:t},(t=>Array.from({length:s},(t=>i))))}function emptyArray(t,s){return Array.from({length:t},(t=>s))}const colors=["#D35","#090","#00F","#F0F","#933","#009","#099","#999"];function colorArray(t){const s=[];for(let i=0;i<t;i++)s[i]=colors[i%colors.length];return s}
)JS";