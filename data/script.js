const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
const circumference=2*Math.PI*54;
let state={on:false,speed:50};

setInterval(fetchStatus,5000);  // Poll every 5 seconds instead of 2
fetchStatus();

async function fetchStatus(){
    try{
        const r=await fetch('/api/status');
        const d=await r.json();
        update(d);
    }catch(e){console.error(e)}
}

function updateFan(f){
    if(!f)return;
    state.on=f.on;
    state.speed=f.speed;

    $('#power').classList.toggle('on',f.on);
    $('#speed').value=f.speed;
    $('#speed-val').textContent=f.speed;
    if(f.rpm!==undefined)$('#rpm').textContent=f.rpm;

    const offset=circumference-(f.speed/100)*circumference;
    $('#speed-circle').style.strokeDashoffset=offset;

    $$('.timer-btn').forEach(b=>{
        b.classList.remove('active');
        const t=+b.dataset.t;
        if(f.timer_active){
            if(t>0&&f.remaining_minutes<=t&&f.remaining_minutes>t-30)b.classList.add('active');
        }else if(t===0&&f.on)b.classList.add('active');
    });

    $('#remaining').textContent=f.timer_active?f.remaining_minutes+' min remaining':'';

    $('#interval').checked=f.interval_mode;
    $('#interval-cfg').classList.toggle('show',f.interval_mode);
    if(f.interval_on!==undefined)$('#int-on').value=f.interval_on;
    if(f.interval_off!==undefined)$('#int-off').value=f.interval_off;
}

function update(d){
    // Only update status dots if we have valid data
    if(d.wifi&&(d.wifi.connected!==undefined||d.wifi.ap_mode!==undefined)){
        $('#wifi-dot').classList.toggle('on',d.wifi.connected||d.wifi.ap_mode);
    }
    if(d.mqtt&&d.mqtt.connected!==undefined){
        $('#mqtt-dot').classList.toggle('on',d.mqtt.connected);
    }

    updateFan(d.fan);

    if(d.wifi){
        if(d.wifi.ssid)$('#cur-ssid').textContent=d.wifi.ssid;
        if(d.wifi.ip)$('#cur-ip').textContent=d.wifi.ip;
        if(d.wifi.rssi)$('#rssi').textContent=d.wifi.rssi;
    }
    if(d.device){
        if(d.device.mac)$('#mac').textContent=d.device.mac;
        if(d.device.name)$('#device-name').placeholder=d.device.name;
    }

    if(d.mqtt&&d.mqtt.host){
        $('#m-host').value=d.mqtt.host;
        $('#m-port').value=d.mqtt.port;
    }
}

async function cmd(p){
    try{
        const r=await fetch('/api/fan',{method:'POST',body:new URLSearchParams(p)});
        const d=await r.json();
        if(d.fan)updateFan(d.fan);
    }catch(e){console.error(e)}
}

$('#power').onclick=()=>cmd({power:state.on?'off':'on'});

let speedTimer;
$('#speed').oninput=e=>{
    $('#speed-val').textContent=e.target.value;
    const offset=circumference-(e.target.value/100)*circumference;
    $('#speed-circle').style.strokeDashoffset=offset;
    clearTimeout(speedTimer);
    speedTimer=setTimeout(()=>cmd({speed:e.target.value}),50);  // Snellere respons
};

$$('.timer-btn').forEach(b=>b.onclick=()=>cmd({timer:b.dataset.t}));

$('#interval').onchange=e=>cmd({interval:e.target.checked});

$('#save-int').onclick=()=>cmd({interval_on:$('#int-on').value,interval_off:$('#int-off').value});

$('#wifi-form').onsubmit=async e=>{
    e.preventDefault();
    try{
        const r=await fetch('/api/wifi',{method:'POST',body:new URLSearchParams({ssid:$('#w-ssid').value,password:$('#w-pass').value})});
        const d=await r.json();
        alert(d.message||'Saved');
    }catch(e){alert('Error')}
};

$('#mqtt-form').onsubmit=async e=>{
    e.preventDefault();
    try{
        const r=await fetch('/api/mqtt',{method:'POST',body:new URLSearchParams({host:$('#m-host').value,port:$('#m-port').value,user:$('#m-user').value,password:$('#m-pass').value})});
        const d=await r.json();
        alert(d.message||'Saved');
    }catch(e){alert('Error')}
};

$('#reset').onclick=async()=>{
    if(confirm('Reset all settings?')){
        await fetch('/api/reset',{method:'POST'});
        alert('Resetting...');
    }
};

// Password settings
async function fetchPasswords(){
    try{
        const r=await fetch('/api/passwords');
        const d=await r.json();
        $('#ota-status').textContent=d.ota_custom?'(custom set)':'(using default)';
        $('#ap-status').textContent=d.ap_custom?'(custom set)':'(using default)';
    }catch(e){console.error(e)}
}
fetchPasswords();

$('#pass-form').onsubmit=async e=>{
    e.preventDefault();
    const otaPass=$('#p-ota').value;
    const apPass=$('#p-ap').value;

    if(!otaPass&&!apPass){
        alert('Enter at least one password to change');
        return;
    }

    const params={};
    if(otaPass)params.ota_password=otaPass;
    if(apPass)params.ap_password=apPass;

    try{
        const r=await fetch('/api/passwords',{method:'POST',body:new URLSearchParams(params)});
        const d=await r.json();
        if(d.success){
            alert(d.message);
            $('#p-ota').value='';
            $('#p-ap').value='';
            fetchPasswords();
        }else{
            alert(d.error||'Error saving passwords');
        }
    }catch(e){alert('Error')}
};

// Device settings
$('#device-form').onsubmit=async e=>{
    e.preventDefault();
    const name=$('#device-name').value.trim();
    if(!name){
        alert('Please enter a device name');
        return;
    }
    try{
        const r=await fetch('/api/device',{method:'POST',body:new URLSearchParams({name:name})});
        const d=await r.json();
        if(d.success){
            alert('Device name saved. Restart device to update MQTT discovery.');
            $('#device-name').value='';
            $('#device-name').placeholder=name;
        }else{
            alert(d.error||'Error saving device name');
        }
    }catch(e){alert('Error')}
};

// Night mode handlers
$('#night-enable').onchange=async e=>{
    await saveNightMode();
};

$('#night-bright').oninput=e=>{
    $('#night-bright-val').textContent=e.target.value+'%';
};

$('#save-night').onclick=async()=>{
    await saveNightMode();
    alert('Night mode settings saved');
};

async function saveNightMode(){
    const params={
        enabled:$('#night-enable').checked,
        start:$('#night-start').value,
        end:$('#night-end').value,
        brightness:$('#night-bright').value
    };
    try{
        await fetch('/api/night',{method:'POST',body:new URLSearchParams(params)});
    }catch(e){console.error(e)}
}

// Update function extended for new data
function updateExtended(d){
    // Statistics
    if(d.stats){
        $('#total-runtime').textContent=d.stats.total_runtime.toFixed(1);
        $('#session-runtime').textContent=d.stats.session_runtime;
    }

    // Night mode
    if(d.night){
        $('#night-enable').checked=d.night.enabled;
        $('#night-cfg').classList.toggle('show',d.night.enabled);
        $('#night-start').value=d.night.start;
        $('#night-end').value=d.night.end;
        $('#night-bright').value=d.night.brightness;
        $('#night-bright-val').textContent=d.night.brightness+'%';
    }
}

// Wrap original update to include extended data
const originalUpdate=update;
update=function(d){
    originalUpdate(d);
    updateExtended(d);
};

// =====================================================
// Hardware Diagnostics
// =====================================================

// Fetch diagnostic data
async function fetchDiagnostic(){
    try{
        const r=await fetch('/api/diagnostic');
        const d=await r.json();
        updateDiagnostic(d);
    }catch(e){console.error(e)}
}

function updateDiagnostic(d){
    // Pin configuration
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

    // LED status
    if(d.led){
        $('#led-status').classList.toggle('on',d.led.connected);
        $('#led-status-text').textContent=d.led.connected?'Connected (mode: '+d.led.mode+')':'Not connected';
    }

    // Fan status
    if(d.fan){
        const running=d.fan.on&&d.fan.rpm>0;
        $('#fan-status').classList.toggle('on',running);
        $('#fan-rpm').textContent=d.fan.rpm;
        $('#fan-pwm').textContent=d.fan.pwm!==undefined?d.fan.pwm:'--';
        if(d.fan.on){
            $('#fan-status-text').textContent=d.fan.rpm>0?'Running at '+d.fan.speed+'%':'No RPM detected!';
        }else{
            $('#fan-status-text').textContent='Off';
        }
        // Update min PWM display
        if(d.fan.min_pwm!==undefined){
            $('#min-pwm-val').textContent=d.fan.min_pwm;
            $('#min-pwm-input').value=d.fan.min_pwm;
        }
    }
}

// Poll button status
async function pollButtons(){
    try{
        const r=await fetch('/api/diagnostic/buttons');
        const d=await r.json();

        $('#btn-front-status').classList.toggle('on',d.front.pressed);
        $('#btn-front-text').textContent=d.front.pressed?'PRESSED':'Released';

        $('#btn-rear-status').classList.toggle('on',d.rear.pressed);
        $('#btn-rear-text').textContent=d.rear.pressed?'PRESSED':'Released';
    }catch(e){}
}

// LED test buttons
$$('.diag-led').forEach(btn=>{
    btn.onclick=async()=>{
        const color=btn.dataset.color;
        try{
            await fetch('/api/diagnostic/led',{method:'POST',body:new URLSearchParams({action:color})});
            if(color==='test'){
                $('#led-status-text').textContent='Testing colors...';
                setTimeout(()=>fetchDiagnostic(),4000);
            }
        }catch(e){console.error(e)}
    };
});

// Fan test buttons
$$('.diag-fan').forEach(btn=>{
    btn.onclick=async()=>{
        const action=btn.dataset.action;
        try{
            await fetch('/api/diagnostic/fan',{method:'POST',body:new URLSearchParams({action:action})});
            if(action==='test'){
                $('#fan-status-text').textContent='Testing speeds...';
                setTimeout(()=>fetchDiagnostic(),5000);
            }else{
                setTimeout(()=>fetchDiagnostic(),500);
            }
        }catch(e){console.error(e)}
    };
});

// Manual set min PWM
$('#set-min-btn').onclick=async()=>{
    const val=$('#min-pwm-input').value;
    try{
        const r=await fetch('/api/diagnostic/fan',{method:'POST',body:new URLSearchParams({action:'setmin',value:val})});
        const d=await r.json();
        if(d.success){
            $('#min-pwm-val').textContent=d.min_pwm;
            $('#calibrate-status').textContent='Min PWM ingesteld op '+d.min_pwm;
        }
    }catch(e){console.error(e)}
};

// Initial diagnostic fetch
fetchDiagnostic();

// Poll buttons every 500ms when diagnostic section is open (was 200ms)
let buttonPollInterval=null;
document.querySelector('details:has(.diag-section)')?.addEventListener('toggle',function(e){
    if(this.open){
        fetchDiagnostic();
        buttonPollInterval=setInterval(pollButtons,500);
    }else{
        if(buttonPollInterval)clearInterval(buttonPollInterval);
    }
});

// =====================================================
// System Logs
// =====================================================

async function fetchLogs(){
    try{
        const r=await fetch('/api/logs');
        const logs=await r.json();
        renderLogs(logs);
    }catch(e){
        console.error(e);
        $('#logs-container').innerHTML='<div class="log-error">Error loading logs</div>';
    }
}

function renderLogs(logs){
    const container=$('#logs-container');
    if(!logs||logs.length===0){
        container.innerHTML='<div class="log-empty">No logs available</div>';
        return;
    }

    // Reverse to show newest first
    const html=logs.slice().reverse().map(log=>{
        const time=formatLogTime(log);
        const levelClass=log.l.toLowerCase();
        return `<div class="log-entry log-${levelClass}">
            <span class="log-time">${time}</span>
            <span class="log-level">${log.l}</span>
            <span class="log-msg">${escapeHtml(log.m)}</span>
        </div>`;
    }).join('');

    container.innerHTML=html;
}

function formatLogTime(log){
    // If we have epoch time, format it nicely
    if(log.e>0){
        const d=new Date(log.e*1000);
        return d.toLocaleTimeString('nl-NL',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
    }
    // Otherwise show uptime in seconds
    const secs=Math.floor(log.t/1000);
    const mins=Math.floor(secs/60);
    const hrs=Math.floor(mins/60);
    if(hrs>0)return`+${hrs}h${mins%60}m`;
    if(mins>0)return`+${mins}m${secs%60}s`;
    return`+${secs}s`;
}

function escapeHtml(str){
    return str.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

$('#refresh-logs').onclick=()=>fetchLogs();

$('#clear-logs').onclick=async()=>{
    if(confirm('Clear all logs?')){
        try{
            await fetch('/api/logs',{method:'DELETE'});
            fetchLogs();
        }catch(e){console.error(e)}
    }
};

// Load logs when section is opened
$('#logs-section')?.addEventListener('toggle',function(){
    if(this.open)fetchLogs();
});
