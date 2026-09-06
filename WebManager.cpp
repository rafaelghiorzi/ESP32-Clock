#include "WebManager.h"
#include "AlarmManager.h"
#include "TimeManager.h"
#include "ConnManager.h"
#include <ESPmDNS.h>
#include <ArduinoJson.h>

WebManager Web;

// =====================================================================
// Página única (HTML+CSS+JS inline, vanilla JS, sem framework/CDN) —
// vive na flash como string, servida direto, sem filesystem.
// =====================================================================
static const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Alarmes</title>
<style>
  :root{
    --bg:#0b0b0d; --card:#17171a; --border:#2a2a2e; --text:#eaeaec; --muted:#8b8b93;
    --accent:#4f7fff; --accent-dim:#2c3e6b; --danger:#e5484d; --ok:#2fb673;
  }
  *{box-sizing:border-box;}
  body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
       background:var(--bg);color:var(--text);margin:0;padding:20px 16px 80px;max-width:520px;
       margin-left:auto;margin-right:auto;}
  h1{font-size:1.05em;font-weight:600;letter-spacing:.02em;text-align:left;margin:0 0 2px;color:var(--text);}
  .status{color:var(--muted);margin-bottom:20px;font-size:.82em;}
  .ring-banner{display:none;background:var(--danger);color:#fff;border-radius:10px;padding:14px 16px;
       margin-bottom:16px;align-items:center;justify-content:space-between;gap:12px;}
  .ring-banner .label{font-weight:600;font-size:.95em;}
  .ring-banner button{background:#fff;color:var(--danger);}
  .card{background:var(--card);border:1px solid var(--border);border-radius:12px;padding:14px;margin-bottom:10px;
        transition:border-color .15s;}
  .card.disabled{opacity:.55;}
  .row{display:flex;align-items:center;gap:10px;margin-bottom:12px;flex-wrap:wrap;}
  .row:last-child{margin-bottom:0;}
  .row.between{justify-content:space-between;}
  .time-input{display:flex;align-items:baseline;gap:2px;font-variant-numeric:tabular-nums;}
  .time-input input{background:transparent;color:var(--text);border:none;border-bottom:2px solid var(--border);
       width:2.1ch;font-size:1.6em;font-weight:600;text-align:center;padding:2px 0;font-family:inherit;}
  .time-input input:focus{outline:none;border-color:var(--accent);}
  .time-input span{font-size:1.6em;font-weight:600;color:var(--muted);}
  .days{display:flex;gap:6px;}
  .day{width:30px;height:30px;border-radius:8px;border:1px solid var(--border);background:transparent;
       color:var(--muted);display:flex;align-items:center;justify-content:center;font-size:.72em;
       font-weight:600;cursor:pointer;user-select:none;transition:.12s;}
  .day.active{background:var(--accent);color:#fff;border-color:var(--accent);}
  .label-input{background:transparent;color:var(--text);border:none;border-bottom:1px solid var(--border);
       padding:4px 0;font-size:.92em;font-family:inherit;flex:1;min-width:80px;}
  .label-input:focus{outline:none;border-color:var(--accent);}
  .repeat-toggle{font-size:.78em;color:var(--muted);display:flex;align-items:center;gap:6px;cursor:pointer;
       user-select:none;}
  .switch{width:34px;height:20px;border-radius:10px;background:var(--border);position:relative;
       transition:.15s;flex-shrink:0;}
  .switch::after{content:'';position:absolute;width:16px;height:16px;border-radius:50%;background:#fff;
       top:2px;left:2px;transition:.15s;}
  .switch.on{background:var(--accent);}
  .switch.on::after{left:16px;}
  button{background:var(--accent);color:#fff;border:none;border-radius:8px;padding:8px 16px;
       font-size:.85em;font-weight:600;cursor:pointer;font-family:inherit;transition:.12s;}
  button:active{transform:scale(.96);}
  button.ghost{background:transparent;color:var(--muted);border:1px solid var(--border);}
  .toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(20px);
       background:var(--card);border:1px solid var(--border);color:var(--text);padding:10px 18px;
       border-radius:10px;font-size:.85em;opacity:0;transition:.2s;pointer-events:none;}
  .toast.show{opacity:1;transform:translateX(-50%) translateY(0);}
  .toast.ok{border-color:var(--ok);}
  .toast.err{border-color:var(--danger);}
</style>
</head>
<body>
<h1>Alarmes</h1>
<div class="status" id="status">carregando...</div>
<div class="ring-banner" id="ringBanner">
  <span class="label" id="ringLabel">Tocando</span>
  <button onclick="dismiss()">Parar</button>
</div>
<div id="alarms"></div>
<div class="toast" id="toast"></div>
<script>
const DAY_LABELS=["D","S","T","Q","Q","S","S"];
let alarms=[];

function toast(msg,type){
  const t=document.getElementById('toast');
  t.textContent=msg;
  t.className='toast show '+(type||'');
  clearTimeout(t._hideTimer);
  t._hideTimer=setTimeout(()=>{t.className='toast';},2200);
}

async function load(){
  try{
    const r=await fetch('/api/alarms');
    const data=await r.json();
    alarms=data.alarms;
    render();
  }catch(e){ toast('Falha ao carregar alarmes','err'); }
}
async function loadStatus(){
  try{
    const r=await fetch('/api/status');
    const s=await r.json();
    document.getElementById('status').textContent='Relogio: '+s.time+' - '+s.date;
    const banner=document.getElementById('ringBanner');
    banner.style.display=s.ringing?'flex':'none';
    if(s.ringing) document.getElementById('ringLabel').textContent=s.ringingLabel||'Tocando';
  }catch(e){}
}
function clampTime(i){
  alarms[i].hour=Math.max(0,Math.min(23,alarms[i].hour|0));
  alarms[i].minute=Math.max(0,Math.min(59,alarms[i].minute|0));
}
function render(){
  const el=document.getElementById('alarms');
  el.innerHTML='';
  alarms.forEach((a,i)=>{
    const card=document.createElement('div');
    card.className='card'+(a.enabled?'':' disabled');
    const hh=String(a.hour).padStart(2,'0'), mm=String(a.minute).padStart(2,'0');
    const daysHtml=DAY_LABELS.map((d,bit)=>
      '<div class="day '+((a.days&(1<<bit))?'active':'')+'" data-bit="'+bit+'">'+d+'</div>'
    ).join('');
    card.innerHTML=
      '<div class="row between">'+
        '<div class="time-input">'+
          '<input type="number" min="0" max="23" value="'+hh+'" class="hh">'+
          '<span>:</span>'+
          '<input type="number" min="0" max="59" value="'+mm+'" class="mm">'+
        '</div>'+
        '<div class="switch enabled-switch '+(a.enabled?'on':'')+'"></div>'+
      '</div>'+
      '<div class="row">'+
        '<input type="text" class="label-input" value="'+a.label+'" maxlength="15" placeholder="Nome do alarme">'+
      '</div>'+
      '<div class="row between">'+
        '<div class="days">'+daysHtml+'</div>'+
        '<div class="repeat-toggle"><span>Repete</span><div class="switch repeat-switch '+(a.repeat?'on':'')+'"></div></div>'+
      '</div>'+
      '<div class="row between"><button class="ghost save-btn">Salvar</button></div>';

    card.querySelectorAll('.day').forEach(function(dayEl){
      dayEl.addEventListener('click',function(){
        const bit=parseInt(dayEl.dataset.bit,10);
        alarms[i].days^=(1<<bit);
        dayEl.classList.toggle('active');
      });
    });
    card.querySelector('.hh').addEventListener('change',function(e){ alarms[i].hour=parseInt(e.target.value,10)||0; clampTime(i); });
    card.querySelector('.mm').addEventListener('change',function(e){ alarms[i].minute=parseInt(e.target.value,10)||0; clampTime(i); });
    card.querySelector('.label-input').addEventListener('change',function(e){ alarms[i].label=e.target.value; });
    card.querySelector('.enabled-switch').addEventListener('click',function(){
      alarms[i].enabled=!alarms[i].enabled;
      this.classList.toggle('on');
      card.classList.toggle('disabled');
    });
    card.querySelector('.repeat-switch').addEventListener('click',function(){
      alarms[i].repeat=!alarms[i].repeat;
      this.classList.toggle('on');
    });
    card.querySelector('.save-btn').addEventListener('click',function(){ save(i,this); });

    el.appendChild(card);
  });
}
async function save(i,btn){
  const original=btn.textContent;
  btn.textContent='Salvando...';
  btn.disabled=true;
  const a=alarms[i];
  try{
    const r=await fetch('/api/alarms',{method:'POST',headers:{'Content-Type':'application/json'},
      body:JSON.stringify({index:i,hour:a.hour,minute:a.minute,days:a.days,repeat:a.repeat,enabled:a.enabled,label:a.label})});
    const j=await r.json();
    if(j.ok){ toast('Alarme salvo','ok'); }
    else { toast('Erro: '+(j.error||'desconhecido'),'err'); }
  }catch(e){
    toast('Falha de conexao ao salvar','err');
  }finally{
    btn.textContent=original;
    btn.disabled=false;
    loadStatus();
  }
}
async function dismiss(){
  try{
    await fetch('/api/dismiss',{method:'POST'});
    toast('Alarme dispensado','ok');
  }catch(e){ toast('Falha ao dispensar','err'); }
  loadStatus();
}
load();
loadStatus();
setInterval(loadStatus,4000);
</script>
</body>
</html>
)HTML";

void WebManager::begin() {
    _server.on("/", HTTP_GET, [this]() { handleRoot(); });
    _server.on("/api/alarms", HTTP_GET, [this]() { handleGetAlarms(); });
    _server.on("/api/alarms", HTTP_POST, [this]() { handlePostAlarm(); });
    _server.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
    _server.on("/api/dismiss", HTTP_POST, [this]() { handleDismiss(); });
    _server.onNotFound([this]() { handleNotFound(); });

    _server.begin();
    Serial.println("[Web] servidor HTTP iniciado na porta 80");

    xTaskCreatePinnedToCore(task, "web_server", 6144, this, 1, &_taskHandle, 0);
}

void WebManager::handleRoot() {
    _server.send_P(200, "text/html", INDEX_HTML);
}

void WebManager::handleGetAlarms() {
    DynamicJsonDocument doc(768);
    JsonArray arr = doc.createNestedArray("alarms");

    for (uint8_t i = 0; i < Alarms.count(); i++) {
        Alarm a = Alarms.get(i);
        JsonObject o = arr.createNestedObject();
        o["enabled"] = a.enabled;
        o["hour"]    = a.hour;
        o["minute"]  = a.minute;
        o["days"]    = a.daysMask;
        o["repeat"]  = a.repeat;
        o["label"]   = a.label;
    }

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WebManager::handlePostAlarm() {
    if (!_server.hasArg("plain")) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"corpo vazio\"}");
        return;
    }

    DynamicJsonDocument doc(384);
    DeserializationError err = deserializeJson(doc, _server.arg("plain"));
    if (err) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"json invalido\"}");
        return;
    }

    int index = doc["index"] | -1;
    if (index < 0 || index >= Alarms.count()) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"index invalido\"}");
        return;
    }

    Alarm a;
    a.enabled  = doc["enabled"]  | false;
    a.hour     = doc["hour"]     | 7;
    a.minute   = doc["minute"]   | 0;
    a.daysMask = doc["days"]     | 0;
    a.repeat   = doc["repeat"]   | true;

    const char* label = doc["label"] | "Alarme";
    strncpy(a.label, label, sizeof(a.label) - 1);
    a.label[sizeof(a.label) - 1] = '\0';

    // Validação básica — evita gravar um alarme com hora/minuto absurdos
    // vindo de um cliente mal-comportado.
    if (a.hour > 23 || a.minute > 59) {
        _server.send(400, "application/json", "{\"ok\":false,\"error\":\"hora/minuto invalidos\"}");
        return;
    }

    Alarms.set((uint8_t)index, a);
    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebManager::handleStatus() {
    char timeBuf[6] = "--:--";
    char dateBuf[24] = "";
    if (RtcClock.isTimeValid()) {
        RtcClock.getDisplayTimeString(timeBuf, sizeof(timeBuf));
        RtcClock.getDisplayDateString(dateBuf, sizeof(dateBuf));
    }

    DynamicJsonDocument doc(256);
    doc["time"]    = timeBuf;
    doc["date"]    = dateBuf;
    doc["ringing"] = Alarms.isRinging();
    if (Alarms.isRinging()) doc["ringingLabel"] = Alarms.getRingingLabel();

    String out;
    serializeJson(doc, out);
    _server.send(200, "application/json", out);
}

void WebManager::handleDismiss() {
    Alarms.dismissActive();
    _server.send(200, "application/json", "{\"ok\":true}");
}

void WebManager::handleNotFound() {
    _server.send(404, "text/plain", "Not found");
}

void WebManager::task(void* param) {
    auto* self = static_cast<WebManager*>(param);
    bool mdnsStarted = false;

    for (;;) {
        if (!mdnsStarted && Conn.isConnected()) {
            if (MDNS.begin("esp32clock")) {
                Serial.println("[Web] mDNS ativo: http://esp32clock.local/");
                mdnsStarted = true;
            }
        }
        self->_server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
