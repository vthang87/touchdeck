#include "config_portal.h"

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>

#include "app_config.h"
#include "storage/settings_store.h"
#include "storage/profile_store.h"
#include "storage/icon_store.h"
#include "ui/ui_manager.h"
#include "display/display_driver.h"
#include "system/idle_manager.h"
#include "ui/screenshot.h"

static WebServer* s_server = nullptr;
static DeviceSettings s_current;
static bool s_running = false;

static const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>TouchDeck Setup</title>
  <style>
    body{font-family:system-ui,sans-serif;background:#0b1220;color:#e2e8f0;margin:0;padding:24px}
    h1{font-size:1.4rem;margin:0 0 8px}
    p{color:#94a3b8}
    a{color:#38bdf8}
    form{max-width:420px;margin-top:20px;display:grid;gap:12px}
    label{font-size:.85rem;color:#94a3b8}
    input{padding:12px;border-radius:8px;border:1px solid #334155;background:#1e293b;color:#f8fafc}
    .check{display:flex;align-items:center;gap:10px;color:#e2e8f0;font-size:.95rem}
    .check input{width:18px;height:18px;padding:0;accent-color:#38bdf8}
    .hint{font-size:.8rem;color:#64748b;margin-top:-6px}
    button{padding:14px;border:0;border-radius:8px;background:#38bdf8;color:#0b1220;font-weight:700}
    nav{margin:12px 0 0;display:flex;gap:16px}
  </style>
</head>
<body>
  <h1>TouchDeck Setup</h1>
  <nav><a href="/grid">Grid editor</a><a href="/">Wi-Fi / device</a></nav>
  <p>Connect this device to your Wi-Fi. OTA password is required for wireless updates.</p>
  <form method="POST" action="/save">
    <label>Wi-Fi SSID</label>
    <input name="wifi_ssid" required value="%SSID%"/>
    <label>Wi-Fi Password</label>
    <input name="wifi_password" type="password" value=""/>
    <label>Device Name</label>
    <input name="device_name" value="%DEVICE%"/>
    <label>Bluetooth Name</label>
    <input name="ble_name" value="%BLE%"/>
    <label class="check"><input type="checkbox" name="ble_enabled" value="1" %BLE_EN%/> Bluetooth enabled</label>
    <p class="hint">Connected BLE = volume shows macOS HUD. Disconnected = volume uses companion 3% over Wi-Fi (no HUD). Mute/play/next/prev need BLE.</p>
    <label class="check"><input type="checkbox" name="ble_pair_mode" value="1" %BLE_PAIR%/> Pairing mode</label>
    <p class="hint">On = discoverable for new Mac pairing / reconnect. Off = hide from Bluetooth scan.</p>
    <label>Hostname</label>
    <input name="hostname" value="%HOST%"/>
    <label>OTA Password</label>
    <input name="ota_password" type="password" value=""/>
    <label>Idle dim (seconds)</label>
    <input name="idle_dim_s" type="number" min="0" max="65535" value="%IDLE_DIM%"/>
    <label>Dim brightness (%)</label>
    <input name="idle_dim_pct" type="number" min="1" max="100" value="%IDLE_DIM_PCT%"/>
    <label>Idle clock (seconds)</label>
    <input name="idle_clock_s" type="number" min="0" max="86400" value="%IDLE_CLOCK%"/>
    <label>Idle dim 2 (seconds)</label>
    <input name="idle_dim2_s" type="number" min="0" max="65535" value="%IDLE_DIM2%"/>
    <label>Dim 2 brightness (%)</label>
    <input name="idle_dim2_pct" type="number" min="1" max="100" value="%IDLE_DIM2_PCT%"/>
    <label>Screen off (seconds)</label>
    <input name="idle_off_s" type="number" min="0" max="65535" value="%IDLE_OFF%"/>
    <label>Clock font size</label>
    <select name="clock_font_px">%CLOCK_FONT_OPTS%</select>
    <p class="hint">Defaults: dim 30s at 30% → clock 120s → dim2 300s at 30% → off 1800s. 0 disables a stage.</p>
    <button type="submit">Save & Restart</button>
  </form>
  <p><a href="/reset" style="color:#f87171">Factory reset</a></p>
</body>
</html>
)HTML";

static const char kGridHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8"/>
  <meta name="viewport" content="width=device-width,initial-scale=1"/>
  <title>TouchDeck Grid</title>
  <style>
    :root{color-scheme:dark}
    body{font-family:system-ui,sans-serif;background:#0b1220;color:#e2e8f0;margin:0;padding:20px}
    h1{font-size:1.35rem;margin:0 0 6px}
    p,label{color:#94a3b8;font-size:.9rem}
    a{color:#38bdf8}
    .row{display:flex;flex-wrap:wrap;gap:12px;align-items:end;margin:14px 0}
    select,input,button{padding:10px 12px;border-radius:8px;border:1px solid #334155;background:#1e293b;color:#f8fafc}
    button{background:#38bdf8;color:#0b1220;border:0;font-weight:700;cursor:pointer}
    button.secondary{background:#334155;color:#e2e8f0}
    button.danger{background:#7f1d1d;color:#fecaca}
    #grid{display:grid;gap:10px;margin-top:16px;max-width:960px}
    .tile{border:1px solid #334155;border-radius:12px;padding:10px;display:grid;gap:6px;background:#111827}
    .tile h3{margin:0;font-size:.95rem}
    .msg{margin-top:12px;min-height:1.2em}
    .ok{color:#4ade80}.err{color:#f87171}
  </style>
</head>
<body>
  <h1>Grid editor</h1>
  <p><a href="/">Wi-Fi setup</a> · Changes apply on device without reboot.</p>
  <div class="row">
    <div><label>Columns</label><br/><select id="cols"></select></div>
    <div><label>Rows</label><br/><select id="rows"></select></div>
    <button id="save">Save to device</button>
    <button class="secondary" id="reload">Reload</button>
    <button class="danger" id="reset">Reset defaults</button>
  </div>
  <div class="row" id="iconbar" style="border-top:1px solid #1f2937;padding-top:14px">
    <div><label>Custom icon id</label><br/><input id="iconid" placeholder="e.g. cursor" maxlength="15"/></div>
    <div><label>PNG file (SD card)</label><br/><input id="iconfile" type="file" accept="image/png,image/*"/></div>
    <div><label>Size</label><br/><select id="iconsize"><option value="64">64 px</option><option value="96" selected>96 px</option><option value="128">128 px</option></select></div>
    <button class="secondary" id="iconupload">Upload to SD</button>
    <span id="sdstate" style="color:#94a3b8"></span>
  </div>
  <div id="grid"></div>
  <div class="msg" id="msg"></div>
<script>
const ACTIONS=["volume_up","volume_down","mute","play_pause","next","previous","app"];
const ICONS=["vol_up","vol_down","mute","play","pause","next","prev","shuffle","power","settings","home","bell","mail","wifi","file","folder","gpt","codex","cursor","iterm","terminal","vscode","slack","telegram","safari","chrome","finder","music","messages","app"];
const COLORS=["#475569","#64748B","#0F766E","#0369A1","#BE123C","#16A34A","#7C3AED","#10A37F","#6366F1","#1F2937","#007ACC","#4A154B","#229ED9"];
const PRESETS=[
  {name:"ChatGPT",icon:"gpt",color:"#10A37F",value:"com.openai.chat"},
  {name:"Codex",icon:"codex",color:"#0D8A6A",value:"com.openai.codex"},
  {name:"Cursor",icon:"cursor",color:"#6366F1",value:"com.todesktop.230313mzl4w4u92"},
  {name:"iTerm",icon:"iterm",color:"#1F2937",value:"com.googlecode.iterm2"},
  {name:"VS Code",icon:"vscode",color:"#007ACC",value:"com.microsoft.VSCode"},
  {name:"Slack",icon:"slack",color:"#4A154B",value:"com.tinyspeck.slackmacgap"},
  {name:"Telegram",icon:"telegram",color:"#229ED9",value:"ru.keepcoder.Telegram"},
  {name:"Safari",icon:"safari",color:"#0369A1",value:"com.apple.Safari"},
  {name:"Terminal",icon:"terminal",color:"#475569",value:"com.apple.Terminal"},
  {name:"Finder",icon:"finder",color:"#0EA5E9",value:"com.apple.finder"},
  {name:"Messages",icon:"messages",color:"#16A34A",value:"com.apple.MobileSMS"},
  {name:"Music",icon:"music",color:"#BE123C",value:"com.apple.Music"},
];
let grid={rev:1,cols:4,rows:2,tiles:[]};
let sdIcons=[];
let sdReady=false;
function el(t,a={},c=[]){const n=document.createElement(t);Object.entries(a).forEach(([k,v])=>{if(k==="text")n.textContent=v;else if(k==="html")n.innerHTML=v;else n.setAttribute(k,v)});c.forEach(x=>n.appendChild(x));return n}
function fillSelect(sel,from,to,cur){sel.innerHTML="";for(let i=from;i<=to;i++){const o=el("option",{value:String(i),text:String(i)});if(i===cur)o.selected=true;sel.appendChild(o)}}
function ensureTiles(){
  const n=grid.cols*grid.rows;
  while(grid.tiles.length<n){
    const i=grid.tiles.length;
    grid.tiles.push({id:"tile_"+i,label:"Tile "+(i+1),color:COLORS[i%COLORS.length],icon:"app",action:"app",target:{kind:"bundle",value:"com.apple.Safari"}});
  }
  grid.tiles=grid.tiles.slice(0,n);
}
function render(){
  fillSelect(document.getElementById("cols"),2,5,grid.cols);
  fillSelect(document.getElementById("rows"),1,3,grid.rows);
  ensureTiles();
  const root=document.getElementById("grid");
  root.style.gridTemplateColumns=`repeat(${grid.cols},minmax(160px,1fr))`;
  root.innerHTML="";
  grid.tiles.forEach((t,idx)=>{
    const card=el("div",{class:"tile"});
    card.appendChild(el("h3",{text:`#${idx+1} ${t.id}`}));
    const idIn=el("input",{value:t.id}); idIn.oninput=e=>{t.id=e.target.value};
    const labIn=el("input",{value:t.label}); labIn.oninput=e=>{t.label=e.target.value};
    const icon=el("select");
    ICONS.forEach(i=>{const o=el("option",{value:i,text:i}); if(i===t.icon)o.selected=true; icon.appendChild(o)});
    sdIcons.forEach(i=>{const o=el("option",{value:i,text:"SD: "+i}); if(i===t.icon)o.selected=true; icon.appendChild(o)});
    if(t.icon&&!ICONS.includes(t.icon)&&!sdIcons.includes(t.icon)){const o=el("option",{value:t.icon,text:t.icon}); o.selected=true; icon.appendChild(o);}
    icon.onchange=e=>{t.icon=e.target.value};
    const color=el("select"); COLORS.forEach(c=>{const o=el("option",{value:c,text:c}); if(c.toLowerCase()===(t.color||"").toLowerCase())o.selected=true; color.appendChild(o)}); color.onchange=e=>{t.color=e.target.value};
    const action=el("select"); ACTIONS.forEach(a=>{const o=el("option",{value:a,text:a}); if(a===t.action)o.selected=true; action.appendChild(o)});
    const kind=el("select"); ["bundle","path"].forEach(k=>{const o=el("option",{value:k,text:k}); if((t.target&&t.target.kind)===k)o.selected=true; kind.appendChild(o)});
    const val=el("input",{value:(t.target&&t.target.value)||"",placeholder:"com.apple.Safari or /Applications/X.app"});
    function syncTarget(){
      if(t.action==="app"){t.target={kind:kind.value,value:val.value}; kind.disabled=false; val.disabled=false;}
      else{delete t.target; kind.disabled=true; val.disabled=true;}
    }
    action.onchange=e=>{t.action=e.target.value; syncTarget();};
    kind.onchange=()=>syncTarget();
    val.oninput=()=>syncTarget();
    syncTarget();

    const preset=el("select");
    preset.appendChild(el("option",{value:"",text:"— app preset —"}));
    PRESETS.forEach(p=>preset.appendChild(el("option",{value:p.name,text:p.name})));
    preset.onchange=e=>{
      const p=PRESETS.find(x=>x.name===e.target.value);
      if(!p) return;
      t.id=p.name.toLowerCase().replace(/[^a-z0-9]/g,"_");
      t.label=p.name; t.icon=p.icon; t.color=p.color;
      t.action="app"; t.target={kind:"bundle",value:p.value};
      render();
    };
    card.appendChild(el("label",{text:"preset"})); card.appendChild(preset);
    card.appendChild(el("label",{text:"id"})); card.appendChild(idIn);
    card.appendChild(el("label",{text:"label"})); card.appendChild(labIn);
    card.appendChild(el("label",{text:"icon"})); card.appendChild(icon);
    card.appendChild(el("label",{text:"color"})); card.appendChild(color);
    card.appendChild(el("label",{text:"action"})); card.appendChild(action);
    card.appendChild(el("label",{text:"target kind"})); card.appendChild(kind);
    card.appendChild(el("label",{text:"bundle / path"})); card.appendChild(val);
    root.appendChild(card);
  });
}
function msg(text,ok){const m=document.getElementById("msg"); m.textContent=text; m.className="msg "+(ok?"ok":"err");}
function updateSdState(){
  const s=document.getElementById("sdstate");
  if(!sdReady){s.textContent="No SD card detected — using built-in glyphs.";}
  else{s.textContent=sdIcons.length?("On SD: "+sdIcons.join(", ")):"SD ready — no custom icons yet.";}
}
async function loadIcons(){
  try{
    const r=await fetch("/api/icons"); const j=await r.json();
    sdReady=!!j.ready; sdIcons=j.icons||[];
  }catch(e){sdReady=false; sdIcons=[];}
  updateSdState();
}
// Draw the PNG onto a square canvas over the tile background, then pack RGB565
// (little-endian) into our TDI1 file format. LVGL TRUE_COLOR has no alpha, so
// we composite transparency onto the tile colour to avoid halos.
async function pngToBin(file,dim){
  const img=await createImageBitmap(file);
  const c=document.createElement("canvas"); c.width=dim; c.height=dim;
  const ctx=c.getContext("2d");
  ctx.fillStyle="#161F32"; ctx.fillRect(0,0,dim,dim);
  const scale=Math.min(dim/img.width,dim/img.height)*0.94;
  const w=img.width*scale, h=img.height*scale;
  ctx.drawImage(img,(dim-w)/2,(dim-h)/2,w,h);
  const data=ctx.getImageData(0,0,dim,dim).data;
  const buf=new ArrayBuffer(8+dim*dim*2);
  const dv=new DataView(buf);
  dv.setUint8(0,84);dv.setUint8(1,68);dv.setUint8(2,73);dv.setUint8(3,49); // "TDI1"
  dv.setUint16(4,dim,true); dv.setUint16(6,dim,true);
  let o=8;
  for(let i=0;i<dim*dim;i++){
    const r=data[i*4],g=data[i*4+1],b=data[i*4+2];
    const v=((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3);
    dv.setUint16(o,v,true); o+=2;
  }
  return new Blob([buf]);
}
async function uploadIcon(){
  if(!sdReady){msg("No SD card on the device",false); return;}
  const f=document.getElementById("iconfile").files[0];
  let id=document.getElementById("iconid").value.trim().toLowerCase().replace(/[^a-z0-9_-]/g,"");
  if(!f){msg("Pick a PNG file first",false); return;}
  if(!id){id=(f.name.split(".")[0]||"icon").toLowerCase().replace(/[^a-z0-9_-]/g,"").slice(0,15);}
  id=id.slice(0,15);
  const dim=+document.getElementById("iconsize").value||96;
  try{
    const blob=await pngToBin(f,dim);
    const fd=new FormData(); fd.append("file",blob,id+".bin");
    const r=await fetch("/api/icon",{method:"POST",body:fd});
    const j=await r.json().catch(()=>({}));
    if(!r.ok){msg(j.error||"Upload failed",false); return;}
    sdIcons=j.icons||sdIcons; sdReady=true; updateSdState(); render();
    msg("Uploaded icon '"+id+"'. Select it on a tile, then Save.",true);
  }catch(e){msg("Convert/upload error: "+e.message,false);}
}
async function load(){
  await loadIcons();
  const r=await fetch("/api/grid");
  if(!r.ok){msg("Failed to load grid",false); return;}
  grid=await r.json();
  render();
  msg("Loaded rev "+grid.rev,true);
}
async function save(){
  ensureTiles();
  grid.rev=(grid.rev||0)+1;
  const body="json="+encodeURIComponent(JSON.stringify(grid));
  const r=await fetch("/api/grid",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body});
  const j=await r.json().catch(()=>({}));
  if(!r.ok){msg(j.error||"Save failed",false); return;}
  grid=j.grid||grid;
  render();
  msg("Saved rev "+grid.rev,true);
}
async function reset(){
  const r=await fetch("/api/grid/reset",{method:"POST"});
  const j=await r.json().catch(()=>({}));
  if(!r.ok){msg(j.error||"Reset failed",false); return;}
  grid=j.grid; render(); msg("Reset to defaults",true);
}
document.getElementById("cols").onchange=e=>{grid.cols=+e.target.value; render()};
document.getElementById("rows").onchange=e=>{grid.rows=+e.target.value; render()};
document.getElementById("save").onclick=save;
document.getElementById("reload").onclick=load;
document.getElementById("reset").onclick=reset;
document.getElementById("iconupload").onclick=uploadIcon;
load();
</script>
</body>
</html>
)HTML";

// Only sizes with a bundled font face are offered.
static const uint8_t kClockFontSizes[] = {48, 72, 96, 128, 160};

static String clockFontOptions(uint8_t current) {
  String out;
  for (uint8_t px : kClockFontSizes) {
    out += "<option value=\"";
    out += px;
    out += "\"";
    out += (px == current) ? " selected" : "";
    out += ">";
    out += px;
    out += " px</option>";
  }
  return out;
}

static uint8_t parseClockFontArg(const char* name, uint8_t fallback) {
  if (!s_server->hasArg(name)) {
    return fallback;
  }
  const long v = s_server->arg(name).toInt();
  for (uint8_t px : kClockFontSizes) {
    if (v == px) {
      return px;
    }
  }
  return fallback;
}

static String htmlPage() {
  String page = FPSTR(kIndexHtml);
  page.replace("%SSID%", s_current.wifi_ssid);
  page.replace("%DEVICE%", s_current.device_name);
  page.replace("%BLE%", s_current.ble_name);
  page.replace("%HOST%", s_current.hostname);
  page.replace("%BLE_EN%", s_current.ble_enabled ? "checked" : "");
  page.replace("%BLE_PAIR%", s_current.ble_pair_mode ? "checked" : "");
  page.replace("%IDLE_DIM%", String(s_current.idle_dim_s));
  page.replace("%IDLE_CLOCK%", String(s_current.idle_clock_s));
  page.replace("%IDLE_DIM2%", String(s_current.idle_dim2_s));
  page.replace("%IDLE_OFF%", String(s_current.idle_off_s));
  page.replace("%IDLE_DIM_PCT%", String(s_current.idle_dim_pct));
  page.replace("%IDLE_DIM2_PCT%", String(s_current.idle_dim2_pct));
  page.replace("%CLOCK_FONT_OPTS%", clockFontOptions(s_current.clock_font_px));
  return page;
}

static uint16_t parseU16Arg(const char* name, uint16_t fallback) {
  if (!s_server->hasArg(name)) {
    return fallback;
  }
  const long v = s_server->arg(name).toInt();
  if (v < 0) {
    return fallback;
  }
  if (v > 65535) {
    return 65535;
  }
  return static_cast<uint16_t>(v);
}

static uint8_t parsePctArg(const char* name, uint8_t fallback) {
  if (!s_server->hasArg(name)) {
    return fallback;
  }
  return static_cast<uint8_t>(constrain(s_server->arg(name).toInt(), 1L, 100L));
}

static void handleRoot() {
  s_server->send(200, "text/html; charset=utf-8", htmlPage());
}

static void handleGridPage() {
  s_server->send_P(200, "text/html; charset=utf-8", kGridHtml);
}

static void handleSave() {
  DeviceSettings s = s_current;
  if (s_server->hasArg("wifi_ssid")) s.wifi_ssid = s_server->arg("wifi_ssid");
  if (s_server->hasArg("wifi_password") && s_server->arg("wifi_password").length() > 0) {
    s.wifi_password = s_server->arg("wifi_password");
  }
  if (s_server->hasArg("device_name") && s_server->arg("device_name").length() > 0) {
    s.device_name = s_server->arg("device_name");
  }
  if (s_server->hasArg("ble_name") && s_server->arg("ble_name").length() > 0) {
    s.ble_name = s_server->arg("ble_name");
  }
  if (s_server->hasArg("hostname") && s_server->arg("hostname").length() > 0) {
    s.hostname = s_server->arg("hostname");
  }
  if (s_server->hasArg("ota_password") && s_server->arg("ota_password").length() > 0) {
    s.ota_password = s_server->arg("ota_password");
  }
  s.ble_enabled = s_server->hasArg("ble_enabled");
  s.ble_pair_mode = s_server->hasArg("ble_pair_mode");
  s.idle_dim_s = parseU16Arg("idle_dim_s", s.idle_dim_s);
  s.idle_clock_s = parseU16Arg("idle_clock_s", s.idle_clock_s);
  s.idle_dim2_s = parseU16Arg("idle_dim2_s", s.idle_dim2_s);
  s.idle_off_s = parseU16Arg("idle_off_s", s.idle_off_s);
  s.idle_dim_pct = parsePctArg("idle_dim_pct", s.idle_dim_pct);
  s.idle_dim2_pct = parsePctArg("idle_dim2_pct", s.idle_dim2_pct);
  s.clock_font_px = parseClockFontArg("clock_font_px", s.clock_font_px);
  s.provisioned = s.wifi_ssid.length() > 0;
  settingsStore.save(s);
  s_server->send(200, "text/html; charset=utf-8",
                 "<!DOCTYPE html><html><head><meta charset='utf-8'></head>"
                 "<body style='background:#0b1220;color:#e2e8f0;font-family:sans-serif;padding:24px'>"
                 "<h1>Saved</h1><p>Restarting\xE2\x80\xA6</p></body></html>");
  delay(500);
  ESP.restart();
}

static void handleReset() {
  settingsStore.factoryReset();
  s_server->send(200, "text/plain", "Factory reset. Restarting...");
  delay(500);
  ESP.restart();
}

static void sendJson(int code, const String& json) {
  s_server->send(code, "application/json", json);
}

static void handleApiGridGet() {
  String json;
  if (!profileStore.serialize(profileStore.current(), json)) {
    sendJson(500, "{\"error\":\"serialize failed\"}");
    return;
  }
  sendJson(200, json);
}

static void handleApiGridPost() {
  String body;
  if (s_server->hasArg("json")) {
    body = s_server->arg("json");
  } else if (s_server->hasArg("plain")) {
    body = s_server->arg("plain");
  }
  char err[64];
  GridConfig cfg;
  if (!profileStore.parse(body.c_str(), body.length(), cfg, err, sizeof(err))) {
    String resp = String("{\"error\":\"") + err + "\"}";
    sendJson(400, resp);
    return;
  }
  if (!profileStore.save(cfg)) {
    sendJson(500, "{\"error\":\"save failed\"}");
    return;
  }
  if (!uiManagerReloadGrid()) {
    sendJson(500, "{\"error\":\"ui reload failed\"}");
    return;
  }
  String json;
  profileStore.serialize(profileStore.current(), json);
  sendJson(200, String("{\"ok\":true,\"grid\":") + json + "}");
}

static bool s_icon_write_ok = false;

static void handleIconUpload() {
  HTTPUpload& up = s_server->upload();
  if (up.status == UPLOAD_FILE_START) {
    String name = up.filename;
    const int dot = name.lastIndexOf('.');
    if (dot > 0) {
      name = name.substring(0, dot);
    }
    s_icon_write_ok = iconStoreWriteBegin(name.c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_icon_write_ok && !iconStoreWriteChunk(up.buf, up.currentSize)) {
      s_icon_write_ok = false;
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_icon_write_ok) {
      s_icon_write_ok = iconStoreWriteEnd();
    } else {
      iconStoreWriteAbort();
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    iconStoreWriteAbort();
    s_icon_write_ok = false;
  }
}

static void handleIconUploadDone() {
  if (!iconStoreReady()) {
    sendJson(503, "{\"error\":\"no SD card\"}");
    return;
  }
  if (!s_icon_write_ok) {
    sendJson(400, "{\"error\":\"upload rejected\"}");
    return;
  }
  uiManagerReloadGrid();  // Refresh tiles so the new icon shows immediately.
  sendJson(200, String("{\"ok\":true,\"icons\":") + iconStoreListJson() + "}");
}

static void handleApiIconsList() {
  if (!iconStoreReady()) {
    sendJson(200, "{\"ready\":false,\"icons\":[]}");
    return;
  }
  sendJson(200, String("{\"ready\":true,\"icons\":") + iconStoreListJson() + "}");
}

static void handleApiIconDelete() {
  if (!iconStoreReady()) {
    sendJson(503, "{\"error\":\"no SD card\"}");
    return;
  }
  String id;
  if (s_server->hasArg("id")) {
    id = s_server->arg("id");
  }
  if (id.length() == 0 || !iconStoreDelete(id.c_str())) {
    sendJson(400, "{\"error\":\"delete failed\"}");
    return;
  }
  uiManagerReloadGrid();
  sendJson(200, String("{\"ok\":true,\"icons\":") + iconStoreListJson() + "}");
}

static void handleApiSettingsGet() {
  // Never echo secrets — only whether they are set.
  JsonDocument doc;
  doc["device_name"] = s_current.device_name;
  doc["ble_name"] = s_current.ble_name;
  doc["hostname"] = s_current.hostname;
  doc["wifi_ssid"] = s_current.wifi_ssid;
  doc["wifi_password_set"] = s_current.wifi_password.length() > 0;
  doc["ota_password_set"] = s_current.ota_password.length() > 0;
  doc["ble_enabled"] = s_current.ble_enabled;
  doc["ble_pair_mode"] = s_current.ble_pair_mode;
  doc["provisioned"] = s_current.provisioned;
  doc["idle_dim_s"] = s_current.idle_dim_s;
  doc["idle_clock_s"] = s_current.idle_clock_s;
  doc["idle_dim2_s"] = s_current.idle_dim2_s;
  doc["idle_off_s"] = s_current.idle_off_s;
  doc["idle_dim_pct"] = s_current.idle_dim_pct;
  doc["idle_dim2_pct"] = s_current.idle_dim2_pct;
  doc["clock_font_px"] = s_current.clock_font_px;
  String json;
  serializeJson(doc, json);
  sendJson(200, json);
}

static void handleApiSettingsPost() {
  DeviceSettings s = s_current;
  if (s_server->hasArg("wifi_ssid")) s.wifi_ssid = s_server->arg("wifi_ssid");
  if (s_server->hasArg("wifi_password") && s_server->arg("wifi_password").length() > 0) {
    s.wifi_password = s_server->arg("wifi_password");
  }
  if (s_server->hasArg("device_name") && s_server->arg("device_name").length() > 0) {
    s.device_name = s_server->arg("device_name");
  }
  if (s_server->hasArg("ble_name") && s_server->arg("ble_name").length() > 0) {
    s.ble_name = s_server->arg("ble_name");
  }
  if (s_server->hasArg("hostname") && s_server->arg("hostname").length() > 0) {
    s.hostname = s_server->arg("hostname");
  }
  if (s_server->hasArg("ota_password") && s_server->arg("ota_password").length() > 0) {
    s.ota_password = s_server->arg("ota_password");
  }
  if (s_server->hasArg("ble_enabled")) {
    s.ble_enabled = s_server->arg("ble_enabled") == "1" || s_server->arg("ble_enabled") == "true";
  }
  if (s_server->hasArg("ble_pair_mode")) {
    s.ble_pair_mode = s_server->arg("ble_pair_mode") == "1" || s_server->arg("ble_pair_mode") == "true";
  }
  s.idle_dim_s = parseU16Arg("idle_dim_s", s.idle_dim_s);
  s.idle_clock_s = parseU16Arg("idle_clock_s", s.idle_clock_s);
  s.idle_dim2_s = parseU16Arg("idle_dim2_s", s.idle_dim2_s);
  s.idle_off_s = parseU16Arg("idle_off_s", s.idle_off_s);
  s.idle_dim_pct = parsePctArg("idle_dim_pct", s.idle_dim_pct);
  s.idle_dim2_pct = parsePctArg("idle_dim2_pct", s.idle_dim2_pct);
  s.clock_font_px = parseClockFontArg("clock_font_px", s.clock_font_px);
  s.provisioned = s.wifi_ssid.length() > 0;
  if (!settingsStore.save(s)) {
    sendJson(500, "{\"error\":\"save failed\"}");
    return;
  }
  s_current = s;
  sendJson(200, "{\"ok\":true,\"restarting\":true}");
  delay(400);
  ESP.restart();
}

static void handleApiGridReset() {
  GridConfig cfg;
  if (!profileStore.resetToDefault(cfg)) {
    sendJson(500, "{\"error\":\"reset failed\"}");
    return;
  }
  uiManagerReloadGrid();
  String json;
  profileStore.serialize(profileStore.current(), json);
  sendJson(200, String("{\"ok\":true,\"grid\":") + json + "}");
}

// Live backlight probe: the panel's LED driver may not accept every PWM setting.
static void handleApiBrightness() {
  if (s_server->hasArg("hold")) {
    idleManagerSetHold(s_server->arg("hold") == "1" || s_server->arg("hold") == "true");
  }
  if (s_server->hasArg("hz")) {
    displayDriverSetBacklightFreq(static_cast<uint32_t>(s_server->arg("hz").toInt()));
  }
  if (s_server->hasArg("pct")) {
    const long pct = constrain(s_server->arg("pct").toInt(), 0L, 100L);
    displayDriverSetBacklight(static_cast<uint8_t>(pct));
    Serial.printf("[DISP] backlight probe %ld%%\n", pct);
  }
  JsonDocument doc;
  doc["pct"] = displayDriverGetBacklight();
  doc["hz"] = displayDriverGetBacklightFreq();
  doc["hold"] = idleManagerHold();
  String json;
  serializeJson(doc, json);
  sendJson(200, json);
}

static void handleApiScreenshot() {
  uint8_t* bmp = nullptr;
  size_t len = 0;
  if (!uiScreenshotCaptureBmp(&bmp, &len) || !bmp || len == 0) {
    sendJson(500, "{\"error\":\"screenshot failed\"}");
    return;
  }

  // Stream the BMP; Arduino WebServer String body cannot hold ~750 KB.
  s_server->sendHeader("Cache-Control", "no-store");
  s_server->sendHeader("Content-Disposition", "inline; filename=\"touchdeck.bmp\"");
  s_server->setContentLength(len);
  s_server->send(200, "image/bmp", "");
  WiFiClient client = s_server->client();
  const size_t chunk = 2048;
  size_t sent = 0;
  while (sent < len && client.connected()) {
    const size_t n = min(chunk, len - sent);
    const size_t w = client.write(bmp + sent, n);
    if (w == 0) {
      break;
    }
    sent += w;
    yield();
  }
  uiScreenshotFree(bmp);
  Serial.printf("[SHOT] HTTP sent %u / %u\n", static_cast<unsigned>(sent),
                static_cast<unsigned>(len));
}

static void registerRoutes() {
  s_server->on("/api/brightness", HTTP_GET, handleApiBrightness);
  s_server->on("/api/screenshot", HTTP_GET, handleApiScreenshot);
  s_server->on("/", HTTP_GET, handleRoot);
  s_server->on("/grid", HTTP_GET, handleGridPage);
  s_server->on("/save", HTTP_POST, handleSave);
  s_server->on("/reset", HTTP_GET, handleReset);
  s_server->on("/api/grid", HTTP_GET, handleApiGridGet);
  s_server->on("/api/grid", HTTP_POST, handleApiGridPost);
  s_server->on("/api/grid/reset", HTTP_POST, handleApiGridReset);
  s_server->on("/api/settings", HTTP_GET, handleApiSettingsGet);
  s_server->on("/api/settings", HTTP_POST, handleApiSettingsPost);
  s_server->on("/api/icons", HTTP_GET, handleApiIconsList);
  s_server->on("/api/icon", HTTP_POST, handleIconUploadDone, handleIconUpload);
  s_server->on("/api/icon/delete", HTTP_POST, handleApiIconDelete);
}

void configPortalBegin(const DeviceSettings& current) {
  s_current = current;
  if (!s_server) {
    s_server = new WebServer(80);
    registerRoutes();
  }
  if (!s_running) {
    s_server->begin();
    s_running = true;
  }
  const IPAddress ip = (WiFi.getMode() & WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("[PORTAL] http://%s/  and  /grid\n", ip.toString().c_str());
}

void configPortalEnsureStarted(const DeviceSettings& current) {
  if (!s_running) {
    configPortalBegin(current);
  } else {
    s_current = current;
  }
}

void configPortalTick() {
  if (s_running && s_server) {
    s_server->handleClient();
  }
}

void configPortalStop() {
  if (s_server) {
    s_server->stop();
  }
  s_running = false;
}

bool configPortalIsRunning() { return s_running; }
