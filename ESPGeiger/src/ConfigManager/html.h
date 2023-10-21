/*
  html.h - Static html to print the options on the cofig form
  
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

const char HTTP_HEAD_MREFRESH[] PROGMEM = "<meta http-equiv='refresh' content='10; url=/'>";
static const char STATUS_PAGE_BODY_HEAD[] PROGMEM = R"HTML(<center><h1 title="{t}">{v} - Status</h1></center>)HTML";

static const char STATUS_PAGE_BODY[] PROGMEM = R"HTML(<style>textarea{resize:vertical;width:100%;margin:0;height:250px;padding:5px;overflow:auto;} .wrap{min-width:350px;max-width:900px;width:50vw}</style>
<canvas id="g1" style="height:200px;width:100%;min-width:350px;border:2px solid #000;"></canvas><div id="g2" class="wdr"></div>
<table style="width:75%"><tr><th>CPM:</th><td><span id="cpm">-</span></td><th>CPS:</th><td id="cs"></td></tr><tr><th>μSv/h:</th><td><span id="usv">-</span></td><th>Total Clicks:</th><td id="tc"></td></tr><tr><th>Uptime:</th><td><span id="upt">-</span></td></tr></table>
<div><h3>Console</h3><textarea readonly='' id='t1' wrap='off'></textarea></div>
<script src="/js"></script>)HTML";
static const char HV_STATUS_PAGE_BODY_HEAD[] PROGMEM = R"HTML(<center><h1>{v}</h1></center>)HTML";

static const char HV_STATUS_PAGE_BODY[] PROGMEM = R"HTML(<style>.wrap{min-width:350px;max-width:900px;width:50vw}.wa{padding:20px;width:75%;font-size:20px;margin:auto;border-radius:5px;box-shadow:rgb(0 0 0 / 25%) 0 5px 10px 2px;background-color:#ffd48a;border-left:5px solid #8a5700}.cl{float:right;margin-left:18px;font-size:30px;line-height:20px;cursor:pointer;transition:0.4s;border-color:#8a5700;color:#8a5700}</style>
<canvas id="g1" style="height:200px;width:100%;min-width:350px;border:2px solid #000;"></canvas><div id="g2" class="wdr"></div>
<div class="wa">
<span class="cl" onclick="this.parentElement.style.display='none';">×</span>
<strong>Disconnect your tube before adjusting! This reading is indicative only.</strong> Confirm with your HV meter.
</div>
<table><tr><th>Frequency:</th><td><input type="range" id="freq" min="0" max="9000" step="250" value="50"><span id="sfreq">-</span></td></tr>
<tr><th>Duty:</th><td><input type="range" id="duty" min="0" max="255" value="50"><span id="sduty">-</span></td></tr>
<tr><th>Ratio:</th><td><input id="ratio" value="-"></td></tr>

<tr><td><button id="submit">Submit</button></td></tr></table>
<script src="/hvjs"></script>
)HTML";

static const char NTP_TZ_JS[] PROGMEM = R"HTML(
<script>var locations={af:["Abidjan","Algiers","Bissau","Cairo","Casablanca","El_Aaiun","Johannesburg","Juba","Khartoum","Lagos","Maputo","Monrovia","Nairobi","Ndjamena","Sao_Tome","Tripoli","Tunis","Windhoek","Cape_Verde","Mauritius"],eu:["Ceuta","Danmarkshavn","Nuuk","Scoresbysund","Thule","Anadyr","Barnaul","Chita","Irkutsk","Kamchatka","Khandyga","Krasnoyarsk","Magadan","Novokuznetsk","Novosibirsk","Omsk","Sakhalin","Srednekolymsk","Tomsk","Ust-Nera","Vladivostok","Yakutsk","Yekaterinburg","Azores","Canary","Faroe","Madeira","Andorra","Astrakhan","Athens","Belgrade","Berlin","Brussels","Bucharest","Budapest","Chisinau","Dublin","Gibraltar","Helsinki","Istanbul","Kaliningrad","Kirov","Kyiv","Lisbon","London","Madrid","Malta","Minsk","Moscow","Paris","Prague","Riga","Rome","Samara","Saratov","Sofia","Tallinn","Tirane","Ulyanovsk","Vienna","Vilnius","Volgograd","Warsaw","Zurich"],as:["Almaty","Amman","Aqtau","Aqtobe","Ashgabat","Atyrau","Baghdad","Baku","Bangkok","Beirut","Bishkek","Choibalsan","Colombo","Damascus","Dhaka","Dili","Dubai","Dushanbe","Famagusta","Gaza","Hebron","Ho_Chi_Minh","Hong_Kong","Hovd","Jakarta","Jayapura","Jerusalem","Kabul","Karachi","Kathmandu","Kolkata","Kuching","Macau","Makassar","Manila","Nicosia","Oral","Pontianak","Pyongyang","Qatar","Qostanay","Qyzylorda","Riyadh","Samarkand","Seoul","Shanghai","Singapore","Taipei","Tashkent","Tbilisi","Tehran","Thimphu","Tokyo","Ulaanbaatar","Urumqi","Yangon","Yerevan","Chagos","Maldives"],au:["Perth","Eucla","Adelaide","Broken_Hill","Darwin","Brisbane","Hobart","Lindeman","Melbourne","Sydney","Lord_Howe"],na:["Adak","Anchorage","Bahia_Banderas","Barbados","Belize","Boise","Cambridge_Bay","Cancun","Chicago","Chihuahua","Ciudad_Juarez","Costa_Rica","Dawson","Dawson_Creek","Denver","Detroit","Edmonton","El_Salvador","Fort_Nelson","Glace_Bay","Goose_Bay","Grand_Turk","Guatemala","Halifax","Havana","Hermosillo","Indiana/Indianapolis","Indiana/Knox","Indiana/Marengo","Indiana/Petersburg","Indiana/Tell_City","Indiana/Vevay","Indiana/Vincennes","Indiana/Winamac","Inuvik","Iqaluit","Jamaica","Juneau","Kentucky/Louisville","Kentucky/Monticello","Los_Angeles","Managua","Martinique","Matamoros","Mazatlan","Menominee","Merida","Metlakatla","Mexico_City","Miquelon","Moncton","Monterrey","New_York","Nome","North_Dakota/Beulah","North_Dakota/Center","North_Dakota/New_Salem","Ojinaga","Panama","Phoenix","Port-au-Prince","Puerto_Rico","Rankin_Inlet","Regina","Resolute","Santo_Domingo","Sitka","St_Johns","Swift_Current","Tegucigalpa","Tijuana","Toronto","Vancouver","Whitehorse","Winnipeg","Yakutat","Yellowknife","Bermuda","Honolulu"],sa:["Araguaina","Argentina/Buenos_Aires","Argentina/Catamarca","Argentina/Cordoba","Argentina/Jujuy","Argentina/La_Rioja","Argentina/Mendoza","Argentina/Rio_Gallegos","Argentina/Salta","Argentina/San_Juan","Argentina/San_Luis","Argentina/Tucuman","Argentina/Ushuaia","Asuncion","Bahia","Belem","Boa_Vista","Bogota","Campo_Grande","Caracas","Cayenne","Cuiaba","Eirunepe","Fortaleza","Guayaquil","Guyana","La_Paz","Lima","Maceio","Manaus","Montevideo","Noronha","Paramaribo","Porto_Velho","Punta_Arenas","Recife","Rio_Branco","Santarem","Santiago","Sao_Paulo","Palmer","South_Georgia","Stanley","Easter","Galapagos"],at:["Cape_Verde","Canary","Faroe","Madeira","Azores","Bermuda","South_Georgia","Stanley"],in:["Mauritius","Maldives","Chagos"],pa:["Palau","Guam","Port_Moresby","Bougainville","Efate","Guadalcanal","Kosrae","Norfolk","Noumea","Auckland","Fiji","Kwajalein","Nauru","Tarawa","Chatham","Apia","Fakaofo","Kanton","Tongatapu","Kiritimati","Pitcairn","Gambier","Marquesas","Rarotonga","Tahiti","Niue","Pago_Pago","Honolulu","Easter","Galapagos"],aq:["Troll","Mawson","Davis","Casey","Rothera","Macquarie","Palmer"],etc:["Greenwich","Universal","Zulu","GMT-14","GMT-13","GMT-12","GMT-11","GMT-10","GMT-9","GMT-8","GMT-7","GMT-6","GMT-5","GMT-4","GMT-3","GMT-2","GMT-1","GMT","GMT+1","GMT+2","GMT+3","GMT+4","GMT+5","GMT+6","GMT+7","GMT+8","GMT+9","GMT+10","GMT+11","GMT+12","UCT","UTC"]};
var regions={af:"Africa",as:"Asia",au:"Australia",aq:"Antarctica",eu:"Europe",na:"America",sa:"America",at:"Atlantic",in:"Indian",pa:"Pacific",etc:"Etc"};
var region_g={na: "North America", sa: "South America", in: "Indian Ocean"};)HTML";

static const char NTP_JS[] PROGMEM = R"HTML(
var x = document.getElementById("tz");
var sel = x.getAttribute('data-option');
Object.entries(regions).forEach(entry => {
  const [key, v] = entry;
  var optgroup = document.createElement("optgroup");
  optgroup.label = (key in region_g) ? region_g[key]:v;
  x.add(optgroup);
	Object.values(locations[key]).sort().forEach(l => {
    var option = document.createElement("option");
    option.text = v + '/' + l;
    if (v + '/' + l == sel) {
      option.selected=true;
    }
    x.add(option);
  });
});</script>)HTML";

static const char NTP_HTML[] PROGMEM = R"HTML(<h1>NTP Settings</h1>
<form method='POST' action='ntpset'>
<label for="tz">Timezone</label>
<select name="t" id="tz" data-option="{v}"></select>
<label for="s">NTP Server</label>
<input id="s" name="s" value="{i}">
<button type='submit'>Save</button>
</form>
)HTML";

static const char STATUS_PAGE_FOOT[] PROGMEM = "<br/><small><a target='_blank' href='https://github.com/steadramon/ESPGeiger'>ESPGeiger</a> {1}/{2}</small>";

static const char statusJS[] PROGMEM = R"JS(
!function(){let e=new Graph("g1",["CPM","CPM5","CPM15"],"cpm","g2",15,null,0,!0,!0,5,5);!function t(){var n=new XMLHttpRequest;n.open("GET","/json",!0),n.onload=function(){if(n.status>=200&&n.status<400){var o=JSON.parse(n.responseText);byID('upt').innerHTML=o.u;byID('cpm').innerHTML=o.c.toFixed(2);byID('tc').innerHTML=(o.tc);byID('usv').innerHTML=(o.c/o.r).toFixed(4);byID('cs').innerHTML=(o.cs).toFixed(2);e.update([o.c,o.c5,o.c15]),setTimeout(t,3e3)}},n.onerror=function(){setTimeout(t,6e3)},n.send()}()}();
var lt,to,tp,x=null,pc="",sn=0,id=0;
function f(){var t;return clearTimeout(lt),t=byID("t1"),(x=new XMLHttpRequest).onload=function(){if(200==x.status){var e,n=x.responseText;id=n.substr(0,n.indexOf("\n")),(e=n.substr(n.indexOf("\n")+1)).length>0&&(t.value+=e),t.scrollTop>=sn&&(t.scrollTop=99999,sn=t.scrollTop)}lt=setTimeout(f,3210)},x.onerror=function(){lt=setTimeout(f,6e3)},x.open("GET","/cs?c1="+id,!0),x.send(),!1}window.addEventListener("load",f);
)JS";

static const char hvJS[] PROGMEM = R"JS("use strict";
var e=new Graph("g1", ["Volts"], "V", "g2", 15, null, 0, true, true, 5,5);
var x=null,lt,to,tp,pc='';sn=0,id=0;

function gethv() {
  var c,o='',t;
  clearTimeout(lt);
  t = document.getElementById('t1');

    x=new XMLHttpRequest();
    x.onload = function() {
      if(x.status==200){
        var o=JSON.parse(x.responseText)
        e.update([o.volts]);
        if (byID('sfreq').innerHTML=="-") {
          byID('freq').value=o.freq;
          byID('duty').value=o.duty;
          byID('ratio').value=o.ratio;
          byID('sfreq').innerHTML=o.freq;
          byID('sduty').innerHTML=o.duty;
        }
      }
      lt=setTimeout(gethv,3000);
    };

    x.open('GET','/hvjson',true);
    x.send();

  return false;
}
window.addEventListener("load",gethv);
byID('freq').addEventListener("change", function() { byID('sfreq').innerHTML=this.value });
byID('duty').addEventListener("change", function() { byID('sduty').innerHTML=this.value });
byID('submit').addEventListener("click", function() {
  var duty = byID('duty').value;
  var freq = byID('freq').value;
  var ratio = byID('ratio').value;

   x=new XMLHttpRequest();
    x.onload = function() {
      if(x.status==200){
          byID('sfreq').innerHTML='-';
          byID('sduty').innerHTML='-';
      }
    };
    x.open('GET','/hvset?f='+freq+'&d='+duty+'&r='+ratio,true);
    x.send();
});
)JS";

// picograph.js - https://github.com/RainingComputers/picograph.js
// minimised, with adjustments to allow fixed scroll
static const char picographJS[] PROGMEM = R"JS(function byID(t){return document.getElementById(t)}function cVID(t,s){const i=[];for(let e=0;e<t.length;e++)i[e]=s+t[e].replace(" ","")+"value";return i}function cLeg(t,s,i,e){const h=`<span>${i}</span>`,n=":"==i.at(-1)?`<span id="${e}"></span>`:"";byID(t).innerHTML+=`<div style="display: inline-block;"><svg width="10" height="10"><rect width="10" height="10" style="fill: ${s}"/></svg> ${h} ${n}</div>`}class Graph{constructor(t,s,i,e,h,n,a=0,l=!1,o=!1,r=5,c=1,m=2){const d= cVID(s,t);this.initE(t,e,d),this.sCfg(s,i,n,a,l,o,r,c,m),this.tsct=0,this.vlcount=0,this.rset(h),""===byID(e).innerHTML&&this.cLgs()}initE(t,s,i){this.canvas=byID(t),this.ctx=this.canvas.getContext("2d"),this.sWH(),this.lID=s,this.vIDs=i}sCfg(t,s,i,e,h,n,a,l,o){this.labels=t,this.unit=s,this.vlFrq=l,this.vlines=h,this.tms=n,this.noLabels=t.length,this.colors=cA(this.noLabels),this.scCfg(i,e,a,o)}scCfg(t,s,i,e){this.mv=t,this.lv=s,this.sstps=i,this.auSM=e,0==[1,2].includes(e)&&(this.auSM=0,!1===Number.isFinite(s)&&(this.lv=0),!1===Number.isFinite(t)&&(this.mv=100))}rset(t){this.intSz=t*this.cssScl,this.nPF=this.width/this.intSz,this.nPts=Math.round(this.nPF)+1,this.points=eA2D(this.noLabels,this.nPts),this.tsA=eA(this.nPts,""),this.fontSize=null,this.hstep=null,this.sstp=null}cLgs(){byID(this.lID).innerHTML="";for(let t=0;t<this.noLabels;t++)cLeg(this.lID,this.colors[t],this.labels[t]+":",this.vIDs[t])}uCfg(t,s,i,e,h=0,n=!1,a=!1,l=5,o=1,r=1){this.clr(),this.sCfg(t,s,e,h,n,a,l,o,r),this.rset(i),this.cLgs()}uMM(t){t<this.lv&&(this.lv=t),t>this.mv&&(this.mv=t);let s=Math.abs(this.mv-this.lv),i=Math.ceil(Math.max.apply(Math,this.points.flat().filter(Number.isFinite))+.05*s),e=Math.floor(Math.min.apply(Math,this.points.flat().filter(Number.isFinite)))-.05*s;if(2==this.auSM)return this.mv=i;t>this.mv-.05*s&&(this.mv=i),t<this.lv+.05*s&&(this.lv=e)}upPts(t){for(let s=0;s<this.noLabels;s++)this.auSM>0&&this.uMM(t[s]),this.points[s]=sAL(this.points[s],t[s])}upLgs(t){for(let s=0;s<this.noLabels;s++)byID(this.vIDs[s]).innerHTML=t[s].toFixed(2)+" "+this.unit}uTS(){const t=gTS();this.tsA=sAL(this.tsA,t)}drawVL(){this.vlcount++;for(let t=this.nPts-this.vlcount;t>=0;t-=this.vlFrq){const s=t*this.intSz;this.ctx.beginPath(),this.ctx.moveTo(s,0),this.ctx.lineTo(s,this.height),this.ctx.strokeStyle="#DDD",this.ctx.stroke()}this.vlcount==this.vlFrq&&(this.vlcount=0)}clr(){this.ctx.clearRect(0,0,this.width,this.height)}sWH(){this.cssScl=window.devicePixelRatio,this.canvas.width=this.canvas.clientWidth*this.cssScl,this.canvas.height=this.canvas.clientHeight*this.cssScl,this.width=this.ctx.canvas.width,this.height=this.ctx.canvas.height}sIWH(){this.intSz=this.width/this.nPF,this.ctx.lineWidth=2*this.cssScl}setStp(){this.hstep=this.height/this.sstps,this.sstp=(this.mv-this.lv)/this.sstps,this.fontSize=Math.min(.5*this.hstep,15*this.cssScl),this.ctx.font=this.fontSize+"px monospace"}drawHL(){let t=document.createElement("textarea");t.innerHTML=this.unit;for(let s=1;s<=this.sstps;s++){const i=this.height-s*this.hstep,e=2,h=this.fontSize+2*this.cssScl;this.ctx.fillText((s*this.sstp+this.lv).toFixed(2)+" "+t.value,e,i+h),this.ctx.beginPath(),this.ctx.moveTo(0,i),this.ctx.lineTo(this.width,i),this.ctx.strokeStyle="#DDD",this.ctx.stroke()}}dTS(){const t=this.ctx.measureText((this.sstps*this.sstp).toFixed(2)).width,s=Math.floor(t/this.intSz+1)+1;this.tsct++;for(let t=this.nPts-this.tsct;t>=s;t-=this.vlFrq){const s=t*this.intSz,i=this.fontSize+2*this.cssScl,e=this.ctx.measureText(this.tsA[t]).width+4*this.cssScl;this.ctx.rotate(Math.PI/2),this.ctx.fillText(this.tsA[t],this.height-e,-s+i),this.ctx.stroke(),this.ctx.rotate(-Math.PI/2)}this.tsct==this.vlFrq&&(this.tsct=0)}draw(){for(let t=this.noLabels-1;t>=0;t--)for(let s=this.nPts-1;s>0;s--){const i=s*this.intSz,e=(s-1)*this.intSz,h=sclInv(this.points[t][s],this.lv,this.mv,this.height),n=sclInv(this.points[t][s-1],this.lv,this.mv,this.height);this.ctx.beginPath(),this.ctx.moveTo(i,h),this.ctx.lineTo(e,n),this.ctx.strokeStyle=this.colors[t],this.ctx.stroke()}}update(t){this.upPts(t),this.upLgs(t),this.uTS(),this.clr(),this.sWH(),this.sIWH(),this.setStp(),this.vlines&&this.drawVL(),this.drawHL(),this.tms&&this.dTS(),this.draw()}}function gTS(){const t=new Date;return t.toLocaleTimeString()}function sclInv(t,s,i,e){return(1-(t-s)/(i-s))*e}function sAL(t,s){return t.shift(),t.push(s),t}function eA2D(t,s,i){return Array.from({length:t},(t=>Array.from({length:s},(t=>i))))}function eA(t,s){return Array.from({length:t},(t=>s))}const cls=["#D35","#090","#00F","#F0F","#933","#009","#099","#999"];function cA(t){const s=[];for(let i=0;i<t;i++)s[i]=cls[i%cls.length];return s})JS";