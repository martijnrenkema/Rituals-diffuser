const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
const RING=2*Math.PI*52;
const REL='https://github.com/martijnrenkema/Rituals-diffuser/releases';
let state={on:false,speed:50,intervalMode:false,intOn:30,intOff:30};
let info={mqttHost:'',mqttPort:1883,platform:''};
let isESP8266=false;
let speedTimer,dragging=false;  // dragging: don't let polling move the slider under the finger

// ---------- helpers ----------
let toastTimer;
function toast(msg,bad){
    const t=$('#toast');
    t.textContent=msg;
    t.className='toast'+(bad?' bad':'');
    clearTimeout(toastTimer);
    toastTimer=setTimeout(()=>t.classList.add('hidden'),3500);
}
async function post(url,params){
    const r=await fetch(url,{method:'POST',body:new URLSearchParams(params||{})});
    let d={};
    try{d=await r.json()}catch(e){}
    if(!r.ok)throw new Error(d.error||('HTTP '+r.status));
    return d;
}
async function getJSON(url){
    const r=await fetch(url);
    return r.json();
}
const esc=s=>String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
const pad=n=>String(n).padStart(2,'0');

// ---------- views (hash routing) ----------
function showView(){
    const v=(location.hash||'#control').slice(1);
    const name=['control','settings','firmware'].includes(v)?v:'control';
    $$('.view').forEach(el=>el.classList.toggle('hidden',el.id!=='v-'+name));
    $$('.tabs a').forEach(a=>{
        const on=a.dataset.view===name;
        a.classList.toggle('on',on);
        if(on)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current');
    });
    if(name==='firmware')fetchUpdateStatus();
    window.scrollTo(0,0);
}
window.addEventListener('hashchange',showView);
showView();

// ---------- status polling ----------
let pollInterval=setInterval(fetchStatusLite,5000);
document.addEventListener('visibilitychange',()=>{
    if(document.hidden){
        clearInterval(pollInterval);
        pollInterval=null;
    }else{
        if(!pollInterval)pollInterval=setInterval(fetchStatusLite,5000);
        fetchStatusLite();
    }
});
fetchStatus();  // Full status only at page load

async function fetchStatus(){
    try{update(await getJSON('/api/status'))}catch(e){console.error(e)}
}
async function fetchStatusLite(){
    try{updateLite(await getJSON('/api/status/lite'))}catch(e){console.error(e)}
}

function setConn(wifi,mqtt){
    if(wifi){
        $('#wifi-dot').classList.toggle('on',!!(wifi.connected||wifi.ap_mode));
        $('#wifi-label').textContent=wifi.ap_mode?'Setup AP':'WiFi';
    }
    if(mqtt&&mqtt.connected!==undefined){
        $('#mqtt-dot').classList.toggle('on',mqtt.connected);
        $('#mqtt-sum').textContent=mqtt.connected?'Connected to '+info.mqttHost+':'+info.mqttPort
            :info.mqttHost?'Not connected ('+info.mqttHost+')':'Not configured';
    }
}

function updateLite(d){
    if(d.mqtt&&d.mqtt.host!==undefined){info.mqttHost=d.mqtt.host;info.mqttPort=d.mqtt.port}
    setConn(d.wifi,d.mqtt);
    updateFan(d.fan);
    if(d.rfid)updateRfid(d.rfid);
}

function update(d){
    updateLite(d);
    if(d.wifi){
        const w=d.wifi;
        $('#wifi-sum').textContent=w.ap_mode?'Setup access point · '+w.ip
            :w.connected?[w.ssid,w.rssi+' dBm',w.ip].join(' · '):'Not connected';
        if(w.rssi)$('#rssi').textContent=w.rssi;
        if(w.ssid&&!w.ap_mode)$('#w-ssid').value=w.ssid;
    }
    if(d.device){
        const dv=d.device;
        if(dv.name){$('#dev-name').textContent=dv.name;$('#name-sum').textContent=dv.name;$('#device-name').value=dv.name;document.title=dv.name}
        if(dv.mac)$('#mac').textContent=dv.mac;
        if(dv.version)$$('.version').forEach(el=>el.textContent='v'+dv.version);
        if(dv.platform){
            info.platform=dv.platform;
            $('#platform').textContent=dv.platform;
            isESP8266=dv.platform==='ESP8266';
            $('#esp8266-info').classList.toggle('hidden',!isESP8266);
        }
    }
    if(d.mqtt&&d.mqtt.host){$('#m-host').value=d.mqtt.host;$('#m-port').value=d.mqtt.port}
    if(d.stats){
        $('#total-runtime').textContent=d.stats.total_runtime.toFixed(1)+' h';
        $('#session-runtime').textContent=d.stats.session_runtime;
    }
    if(d.night)updateNight(d.night);
    if(d.update)updateUpdateUI(d.update);
    // No RFID reader compiled in (ESP8266 lite builds): hide the card
    if(!d.rfid)$('#rfid-section').classList.add('hidden');
}

// ---------- fan ----------
function setRing(speed,on){
    $('#speed-circle').style.strokeDashoffset=RING*(1-(on?speed:0)/100);
    $('.ring').classList.toggle('off',!on);
}

function updateFan(f){
    if(!f)return;
    state.on=f.on;
    state.speed=f.speed;
    if(f.interval_mode!==undefined)state.intervalMode=f.interval_mode;
    if(f.interval_on!==undefined)state.intOn=f.interval_on;
    if(f.interval_off!==undefined)state.intOff=f.interval_off;

    const p=$('#power');
    p.classList.toggle('on',f.on);
    p.setAttribute('aria-label',f.on?'Turn diffuser off':'Turn diffuser on');
    if(!dragging){
        $('#speed').value=f.speed;
        $('#speed-val').textContent=f.speed;
        setRing(f.speed,f.on);
    }
    if(f.rpm!==undefined)$('#rpm-text').textContent=f.on?f.rpm.toLocaleString('en-US')+' rpm':'Standby';

    $('#state-title').textContent=f.on?'Diffusing':'Off';
    $('#state-sub').textContent=!f.on?'Tap to start'
        :state.intervalMode?'Interval · '+state.intOn+' s on, '+state.intOff+' s off':'Continuous';

    $$('.chip').forEach(b=>{
        const t=+b.dataset.t;
        let on=false;
        if(f.timer_active)on=t>0&&f.remaining_minutes<=t&&f.remaining_minutes>t-30;
        else on=t===0&&f.on;
        b.classList.toggle('on',on);
        b.setAttribute('aria-pressed',on);
    });
    $('#remaining').textContent=!f.on?'Not running':f.timer_active?'Turns off in '+f.remaining_minutes+' min':'Runs until turned off';

    $('#interval').checked=state.intervalMode;
    if(document.activeElement!==$('#int-on'))$('#int-on').value=state.intOn;
    if(document.activeElement!==$('#int-off'))$('#int-off').value=state.intOff;
}

async function cmd(p){
    try{
        const d=await post('/api/fan',p);
        if(d.fan)updateFan(d.fan);
    }catch(e){toast('Could not reach the diffuser',true)}
}

$('#power').onclick=()=>cmd({power:state.on?'off':'on'});

$('#speed').oninput=e=>{
    dragging=true;
    const v=+e.target.value;
    $('#speed-val').textContent=v;
    setRing(v,v>0||state.on);
    clearTimeout(speedTimer);
    speedTimer=setTimeout(async()=>{await cmd({speed:v});dragging=false},150);  // Debounce while dragging
};

$$('.chip').forEach(b=>b.onclick=()=>cmd({timer:b.dataset.t}));
$('#interval').onchange=e=>cmd({interval:e.target.checked});

let intTimer;
function saveInterval(){
    clearTimeout(intTimer);
    intTimer=setTimeout(()=>cmd({interval_on:$('#int-on').value,interval_off:$('#int-off').value}),400);
}
$('#int-on').onchange=saveInterval;
$('#int-off').onchange=saveInterval;

// ---------- scent cartridge ----------
function updateRfid(r){
    const badge=$('#rfid-badge');
    $('#rfid-section').classList.remove('hidden');
    if(r.cartridge_present){
        badge.textContent='Detected';badge.className='badge ok';
        if(r.last_scent)$('#scent-name').textContent=r.last_scent;
        $('#scent-uid').textContent='';
    }else if(r.connected){
        badge.textContent='Waiting';badge.className='badge wait';
        if(r.has_tag&&r.last_scent){
            $('#scent-name').textContent=r.last_scent;
            $('#scent-uid').textContent='Removed · place the cartridge back';
        }else{
            $('#scent-name').textContent='No cartridge';
            $('#scent-uid').textContent='Place a Rituals cartridge in the diffuser';
        }
    }else{
        badge.textContent='No reader';badge.className='badge';
        $('#scent-name').textContent='RFID reader not found';
        $('#scent-uid').textContent='Check the reader wiring';
    }
}

// ---------- settings: WiFi / MQTT ----------
$('#wifi-form').onsubmit=async e=>{
    e.preventDefault();
    try{
        const d=await post('/api/wifi',{ssid:$('#w-ssid').value,password:$('#w-pass').value});
        toast(d.message||'Saved');
        $('#w-pass').value='';
    }catch(err){toast(err.message,true)}
};

$('#mqtt-form').onsubmit=async e=>{
    e.preventDefault();
    try{
        const d=await post('/api/mqtt',{host:$('#m-host').value,port:$('#m-port').value,user:$('#m-user').value,password:$('#m-pass').value});
        info.mqttHost=$('#m-host').value;info.mqttPort=$('#m-port').value;
        toast(d.message||'Saved');
        $('#m-pass').value='';
    }catch(err){toast(err.message,true)}
};

// ---------- settings: night mode ----------
['#night-start','#night-end'].forEach(id=>{
    const s=$(id);
    for(let h=0;h<24;h++)s.add(new Option(pad(h)+':00',h));
});
function nightSummary(n){
    $('#night-sum').textContent=n.enabled?pad(n.start)+':00 – '+pad(n.end)+':00 · '+n.brightness+'%':'Off';
}
function updateNight(n){
    $('#night-enable').checked=n.enabled;
    $('#night-start').value=n.start;
    $('#night-end').value=n.end;
    $('#night-bright').value=n.brightness;
    $('#night-bright-val').textContent=n.brightness+'%';
    nightSummary(n);
}
$('#night-bright').oninput=e=>{$('#night-bright-val').textContent=e.target.value+'%'};
async function saveNightMode(showToast){
    const n={enabled:$('#night-enable').checked,start:+$('#night-start').value,end:+$('#night-end').value,brightness:+$('#night-bright').value};
    try{
        await post('/api/night',n);
        nightSummary(n);
        if(showToast)toast('Night mode saved');
    }catch(err){toast(err.message,true)}
}
$('#night-enable').onchange=()=>saveNightMode(false);
$('#save-night').onclick=()=>saveNightMode(true);

// ---------- settings: device / passwords ----------
$('#device-form').onsubmit=async e=>{
    e.preventDefault();
    const name=$('#device-name').value.trim();
    if(!name)return;
    try{
        await post('/api/device',{name:name});
        $('#dev-name').textContent=name;$('#name-sum').textContent=name;
        toast('Name saved. Restart the diffuser to update Home Assistant.');
    }catch(err){toast(err.message,true)}
};

async function fetchPasswords(){
    try{
        const d=await getJSON('/api/passwords');
        const custom=[d.ota_custom&&'OTA',d.ap_custom&&'access point'].filter(Boolean);
        $('#pass-sum').textContent=custom.length?'Custom: '+custom.join(', '):'Default';
    }catch(e){console.error(e)}
}
fetchPasswords();

$('#pass-form').onsubmit=async e=>{
    e.preventDefault();
    const params={};
    if($('#p-ota').value)params.ota_password=$('#p-ota').value;
    if($('#p-ap').value)params.ap_password=$('#p-ap').value;
    if(!params.ota_password&&!params.ap_password){toast('Enter at least one new password',true);return}
    try{
        const d=await post('/api/passwords',params);
        toast(d.message||'Saved');
        $('#p-ota').value='';$('#p-ap').value='';
        fetchPasswords();
    }catch(err){toast(err.message,true)}
};

// ---------- settings: restart / reset ----------
$('#restart').onclick=async()=>{
    if(!confirm('Restart the diffuser?'))return;
    try{await post('/api/restart');toast('Restarting...')}catch(err){toast(err.message,true)}
};
$('#reset').onclick=async()=>{
    if(!confirm('Erase all settings (WiFi, MQTT, passwords) and restart?'))return;
    try{await post('/api/reset');toast('Resetting... connect to the setup access point afterwards.')}catch(err){toast(err.message,true)}
};

// ---------- diagnostics ----------
async function fetchDiagnostic(){
    try{updateDiagnostic(await getJSON('/api/diagnostic'))}catch(e){console.error(e)}
}
function updateDiagnostic(d){
    if(d.pins){
        $('#diag-platform').textContent=d.pins.platform;
        $('#diag-fan-pwm').textContent=d.pins.fan_pwm;
        $('#diag-fan-tacho').textContent=d.pins.fan_tacho;
        $('#diag-led').textContent=d.pins.led;
        $('#diag-btn-front').textContent=d.pins.btn_front;
        $('#diag-btn-rear').textContent=d.pins.btn_rear;
        $('#btn-front-pin').textContent=d.pins.btn_front;
        $('#btn-rear-pin').textContent=d.pins.btn_rear;
    }
    if(d.led){
        $('#led-status').classList.toggle('on',d.led.connected);
        $('#led-status-text').textContent='Mode '+d.led.mode+' · brightness '+d.led.brightness+'%';
    }
    if(d.fan){
        const running=d.fan.on&&d.fan.rpm>0;
        $('#fan-status').classList.toggle('on',running);
        $('#fan-rpm').textContent=d.fan.rpm;
        $('#fan-pwm').textContent=d.fan.pwm!==undefined?d.fan.pwm:'--';
        $('#fan-status-text').textContent=d.fan.calibrating?'Calibrating...':d.fan.on?(d.fan.rpm>0?'Running at '+d.fan.speed+'%':'No RPM detected'):'Off';
        if(d.fan.min_pwm!==undefined){
            $('#min-pwm-val').textContent=d.fan.min_pwm;
            $('#min-pwm-input').value=d.fan.min_pwm;
        }
    }
}
async function pollButtons(){
    try{
        const d=await getJSON('/api/diagnostic/buttons');
        $('#btn-front-status').classList.toggle('on',d.front.pressed);
        $('#btn-front-text').textContent=d.front.pressed?'Pressed':'Released';
        $('#btn-rear-status').classList.toggle('on',d.rear.pressed);
        $('#btn-rear-text').textContent=d.rear.pressed?'Pressed':'Released';
    }catch(e){}
}
$$('.diag-led').forEach(btn=>btn.onclick=async()=>{
    try{
        await post('/api/diagnostic/led',{action:btn.dataset.color});
        setTimeout(fetchDiagnostic,btn.dataset.color==='test'?4000:500);
    }catch(err){toast(err.message,true)}
});
$$('.diag-fan').forEach(btn=>btn.onclick=async()=>{
    const a=btn.dataset.action;
    try{
        await post('/api/diagnostic/fan',{action:a});
        if(a==='test')$('#fan-status-text').textContent='Testing...';
        if(a==='calibrate')$('#fan-status-text').textContent='Calibrating...';
        setTimeout(fetchDiagnostic,a==='test'?5000:a==='calibrate'?15000:500);
    }catch(err){toast(err.message,true)}
});
$('#set-min-btn').onclick=async()=>{
    try{
        const d=await post('/api/diagnostic/fan',{action:'setmin',value:$('#min-pwm-input').value});
        $('#min-pwm-val').textContent=d.min_pwm;
        toast('Minimum PWM set to '+d.min_pwm);
    }catch(err){toast(err.message,true)}
};
// Poll buttons every 2s only while diagnostics are open (keeps ESP8266 load low)
let buttonPoll=null;
$('#diag-section').addEventListener('toggle',function(){
    clearInterval(buttonPoll);buttonPoll=null;
    if(this.open){fetchDiagnostic();buttonPoll=setInterval(pollButtons,2000)}
});

// ---------- logs ----------
async function fetchLogs(){
    try{renderLogs(await getJSON('/api/logs'))}
    catch(e){$('#logs-container').innerHTML='<div class="log">Could not load logs</div>'}
}
function renderLogs(logs){
    const c=$('#logs-container');
    if(!logs||!logs.length){c.innerHTML='<div class="log">No log entries</div>';return}
    c.innerHTML=logs.slice().reverse().map(l=>
        '<div class="log '+esc(l.l.toLowerCase())+'"><span class="t">'+formatLogTime(l)+'</span><span class="l">'+esc(l.l)+'</span><span>'+esc(l.m)+'</span></div>'
    ).join('');
}
function formatLogTime(l){
    if(l.e>0){
        const d=new Date(l.e*1000),now=new Date();
        const t=d.toLocaleTimeString([],{hour:'2-digit',minute:'2-digit',second:'2-digit'});
        if(d.toDateString()===now.toDateString())return t;
        if(new Date(now-86400000).toDateString()===d.toDateString())return'Yesterday '+t;
        return d.toLocaleDateString([],{day:'2-digit',month:'2-digit'})+' '+t;
    }
    // No NTP time yet: show uptime
    const s=Math.floor((l.u||0)/1000),m=Math.floor(s/60),h=Math.floor(m/60);
    return h>0?'+'+h+'h'+(m%60)+'m':m>0?'+'+m+'m'+(s%60)+'s':'+'+s+'s';
}
$('#refresh-logs').onclick=fetchLogs;
$('#clear-logs').onclick=async()=>{
    if(!confirm('Clear all log entries?'))return;
    try{await fetch('/api/logs',{method:'DELETE'});fetchLogs()}catch(e){toast('Could not clear logs',true)}
};
$('#logs-section').addEventListener('toggle',function(){if(this.open)fetchLogs()});

// ---------- firmware: update checker ----------
let updateDismissed=false,updatePoll=null;
function updateUpdateUI(d){
    if(!d)return;
    $('#current-ver').textContent=d.current||'--';
    $('#latest-ver').textContent=d.latest||'--';

    const st=$('#update-state');
    const checking=d.state===1,downloading=d.state===2,failed=d.state===3;
    if(downloading){st.className='note';st.textContent='Downloading update...'}
    else if(checking){st.className='note';st.textContent='Checking for updates...'}
    else if(failed){st.className='note bad';st.textContent='Update check failed'+(d.error?': '+d.error:'')}
    else if(d.available){st.className='note';st.textContent='Version '+d.latest+' is available'}
    else if(d.latest){st.className='note ok';st.textContent='Up to date'}
    else{st.className='note';st.textContent='Not checked yet'}

    $('#fw-sum').textContent='v'+(d.current||'--')+(d.available?' · update available':d.latest?' · up to date':'');
    $('#fw-badge').classList.toggle('hidden',!d.available);

    const showProg=d.progress>0&&d.progress<100;
    $('#update-progress-section').classList.toggle('hidden',!showProg);
    if(showProg){
        $('#update-progress-bar').style.width=d.progress+'%';
        $('#update-progress-text').textContent=d.progress+'%';
    }

    $('#do-install').classList.toggle('hidden',!(d.can_auto_update&&d.available));
    $('#download-link').href=d.release_url||REL;
    $('#download-link').textContent=d.available&&!d.can_auto_update?'Download from GitHub':'Release notes';

    const showBanner=d.available&&!updateDismissed;
    $('#update-banner').classList.toggle('hidden',!showBanner);
    if(showBanner)$('#update-version').textContent='v'+d.latest;
}
async function fetchUpdateStatus(){
    try{const d=await getJSON('/api/update/status');updateUpdateUI(d);return d}
    catch(e){return null}
}
$('#check-update').onclick=async()=>{
    $('#update-state').className='note';
    $('#update-state').textContent='Checking for updates...';
    try{
        await post('/api/update/check');
        // HTTPS on ESP8266 can take 5-15 seconds
        [3000,6000,10000,15000].forEach(t=>setTimeout(fetchUpdateStatus,t));
    }catch(err){toast(err.message,true)}
};
$('#do-install').onclick=async()=>{
    if(!confirm('Install the update? The diffuser restarts when the download is complete.'))return;
    try{
        await post('/api/update/install');
        clearInterval(updatePoll);
        updatePoll=setInterval(async()=>{
            const d=await fetchUpdateStatus();
            if(d&&(d.state===0||d.state===3)){clearInterval(updatePoll);updatePoll=null}
        },1000);
    }catch(err){toast(err.message,true)}
};
$('#banner-dismiss').onclick=()=>{updateDismissed=true;$('#update-banner').classList.add('hidden')};
setTimeout(fetchUpdateStatus,3000);
setInterval(fetchUpdateStatus,30000);

// ---------- firmware: manual upload ----------
function prepareOTAMode(){
    const modal=$('#ota-modal'),cd=$('#countdown');
    modal.classList.remove('hidden');
    post('/api/ota/prepare').then(()=>{
        let n=3;
        cd.textContent=n;
        const iv=setInterval(()=>{
            n--;
            if(n>0){cd.textContent=n}
            else{clearInterval(iv);cd.textContent='...';setTimeout(()=>{location.href='/'},1000)}
        },1000);
    }).catch(err=>{
        modal.classList.add('hidden');
        toast('Could not start Safe Update mode: '+err.message,true);
    });
}

function setupUpload(p,endpoint){
    const card=$('#'+p+'-up'),input=$('#'+p+'-file'),btn=$('#'+p+'-upload-btn');
    const name=$('#'+p+'-file-name'),bar=$('#'+p+'-bar'),fill=$('#'+p+'-fill'),pct=$('#'+p+'-pct'),status=$('#'+p+'-status');
    const hint=name.textContent;
    card.querySelector('.pick').onclick=()=>input.click();
    card.addEventListener('dragover',e=>{e.preventDefault();card.classList.add('drag')});
    card.addEventListener('dragleave',()=>card.classList.remove('drag'));
    card.addEventListener('drop',e=>{
        e.preventDefault();card.classList.remove('drag');
        if(e.dataTransfer.files.length){input.files=e.dataTransfer.files;picked()}
    });
    input.onchange=picked;
    function picked(){
        const f=input.files[0];
        name.textContent=f?f.name+' · '+(f.size/1024).toFixed(0)+' KB':hint;
        btn.disabled=!f;
        status.textContent='';
    }
    btn.onclick=()=>{
        if(!input.files.length)return;
        if(isESP8266){prepareOTAMode();return}  // ESP8266 uploads through Safe Update mode
        const fd=new FormData();
        fd.append('file',input.files[0]);
        const xhr=new XMLHttpRequest();
        xhr.open('POST',endpoint,true);
        xhr.upload.onprogress=e=>{
            if(e.lengthComputable){
                const v=Math.round(e.loaded/e.total*100);
                fill.style.width=v+'%';pct.textContent=v+'%';
            }
        };
        xhr.onloadstart=()=>{
            btn.disabled=true;bar.classList.remove('hidden');fill.style.width='0%';pct.textContent='0%';
            status.className='muted sm';status.textContent='Uploading · keep this page open and the diffuser plugged in';
        };
        xhr.onload=()=>{
            if(xhr.status===200){
                status.className='sm';status.textContent='Update installed. Restarting...';
                setTimeout(()=>{location.href='/'},8000);
            }else{
                status.className='sm danger';status.textContent='Upload failed: '+(xhr.responseText||xhr.status);
                btn.disabled=false;
            }
        };
        xhr.onerror=()=>{
            status.className='sm danger';status.textContent='Connection lost during upload';
            btn.disabled=false;
        };
        xhr.send(fd);
    };
}
setupUpload('fw','/api/update/firmware');
setupUpload('fs','/api/update/filesystem');
