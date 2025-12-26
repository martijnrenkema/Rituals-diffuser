const $=s=>document.querySelector(s),$$=s=>document.querySelectorAll(s);
const circumference=2*Math.PI*54;
let state={on:false,speed:50};

setInterval(fetchStatus,2000);
fetchStatus();

async function fetchStatus(){
    try{
        const r=await fetch('/api/status');
        const d=await r.json();
        update(d);
    }catch(e){console.error(e)}
}

function update(d){
    $('#wifi-dot').classList.toggle('on',d.wifi.connected||d.wifi.ap_mode);
    $('#mqtt-dot').classList.toggle('on',d.mqtt.connected);

    state.on=d.fan.on;
    state.speed=d.fan.speed;

    $('#power').classList.toggle('on',d.fan.on);
    $('#speed').value=d.fan.speed;
    $('#speed-val').textContent=d.fan.speed;
    $('#rpm').textContent=d.fan.rpm;

    const offset=circumference-(d.fan.speed/100)*circumference;
    $('#speed-circle').style.strokeDashoffset=offset;

    $$('.timer-btn').forEach(b=>{
        b.classList.remove('active');
        const t=+b.dataset.t;
        if(d.fan.timer_active){
            if(t>0&&d.fan.remaining_minutes<=t&&d.fan.remaining_minutes>t-30)b.classList.add('active');
        }else if(t===0&&d.fan.on)b.classList.add('active');
    });

    $('#remaining').textContent=d.fan.timer_active?d.fan.remaining_minutes+' min remaining':'';

    $('#interval').checked=d.fan.interval_mode;
    $('#interval-cfg').classList.toggle('show',d.fan.interval_mode);
    $('#int-on').value=d.fan.interval_on;
    $('#int-off').value=d.fan.interval_off;

    $('#cur-ssid').textContent=d.wifi.ssid||'--';
    $('#cur-ip').textContent=d.wifi.ip||'--';
    $('#mac').textContent=d.device.mac||'--';
    $('#rssi').textContent=d.wifi.rssi||'--';

    if(d.mqtt.host){
        $('#m-host').value=d.mqtt.host;
        $('#m-port').value=d.mqtt.port;
    }
}

async function cmd(p){
    try{
        const r=await fetch('/api/fan',{method:'POST',body:new URLSearchParams(p)});
        const d=await r.json();
        if(d.fan)update({fan:d.fan,wifi:{},mqtt:{},device:{}});
    }catch(e){console.error(e)}
}

$('#power').onclick=()=>cmd({power:state.on?'off':'on'});

let speedTimer;
$('#speed').oninput=e=>{
    $('#speed-val').textContent=e.target.value;
    const offset=circumference-(e.target.value/100)*circumference;
    $('#speed-circle').style.strokeDashoffset=offset;
    clearTimeout(speedTimer);
    speedTimer=setTimeout(()=>cmd({speed:e.target.value}),300);
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
        $('#ota-status').textContent=d.ota_custom?'(custom)':'(default: '+d.ota_default+')';
        $('#ap-status').textContent=d.ap_custom?'(custom)':'(default: '+d.ap_default+')';
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
