// Variáveis globais de controle
let controls = { t: 0, y: 0, p: 0, r: 0 };
const sendInterval = 50; // Envia dados a cada 50ms

let isArmed = false;

function toggleArm() {
    isArmed = !isArmed;
    const btn = document.getElementById('armBtn');
    if(isArmed) {
            fetch('/arm');
            btn.innerText = "DESARMAR";
            btn.style.background = "#28a745";
    } else {
        fetch('/disarm');
        btn.innerText = "ARMAR";
        btn.style.background = "#ff4d4d";
    }
}

// Loop de Telemetria
setInterval(() => {
    fetch('/telemetry')
        .then(r => r.json())
        .then(data => {
            document.getElementById('valP').innerText = data.p.toFixed(1);
            document.getElementById('valR').innerText = data.r.toFixed(1);
            // Se o drone desarmar por segurança no ESP32, atualiza o site
            if(data.a == 0 && isArmed) {
                isArmed = false;
                document.getElementById('armBtn').innerText = "ARMAR";
                document.getElementById('armBtn').style.background = "#ff4d4d";
            }
        });
}, 200);

// --- LÓGICA DE MODOS DE VOO ---
function sendMode(m) {
    const display = document.getElementById('mode-display');
    const modes = ["MANUAL", "STABILIZED", "HYBRID"];
    
    // Atualiza o texto na tela
    display.innerText = modes[m];

    // Feedback visual nos botões
    const buttons = document.querySelectorAll('.btn-group button');
    buttons.forEach((btn, index) => {
        if (index === m) {
            btn.style.background = "#007bff";
            btn.style.borderColor = "#00d4ff";
        } else {
            btn.style.background = "#333";
            btn.style.borderColor = "#555";
        }
    });

    // Envia para o ESP32
    fetch(`/setMode?m=${m}`);
}

// --- LÓGICA DE CALIBRAÇÃO ---
function calibrate() {
    if (confirm("Mantenha o drone em uma superfície plana e nivelada. Iniciar calibração?")) {
        const display = document.getElementById('mode-display');
        const oldText = display.innerText;
        display.innerText = "CALIBRANDO...";
        
        fetch('/calibrate')
            .then(response => {
                alert("Calibração concluída e salva!");
                display.innerText = oldText;
            })
            .catch(err => alert("Erro ao calibrar. Verifique a conexão."));
    }
}

// --- LÓGICA DO JOYSTICK ---
function initJoystick(id, isLeft) {
    const zone = document.getElementById(id);
    const stick = zone.querySelector('.stick');
    const base = zone.querySelector('.joystick-base');
    const rect = base.getBoundingClientRect();
    const centerX = rect.width / 2;
    const centerY = rect.height / 2;

    const handleTouch = (e) => {
        e.preventDefault();
        const touch = e.touches[0];
        let x = touch.clientX - rect.left - centerX;
        let y = touch.clientY - rect.top - centerY;
        
        // Limita o movimento ao círculo da base
        const limit = rect.width / 2;
        const dist = Math.sqrt(x*x + y*y);
        if (dist > limit) {
            x *= limit/dist;
            y *= limit/dist;
        }

        // Move a bolinha visualmente
        stick.style.transform = `translate(${x}px, ${y}px)`;

        // Mapeia os valores para o drone
        if (isLeft) {
            // Esquerda: Y = Throttle (0 a 255), X = Yaw (-50 a 50)
            controls.t = Math.round(mapValue(y, limit, -limit, 0, 255));
            controls.y = Math.round(mapValue(x, -limit, limit, -50, 50));
        } else {
            // Direita: Y = Pitch (-50 a 50), X = Roll (-50 a 50)
            controls.p = Math.round(mapValue(y, limit, -limit, -50, 50));
            controls.r = Math.round(mapValue(x, -limit, limit, -50, 50));
        }
    };

    const resetStick = () => {
        if (!isLeft) {
            // Direita (Pitch/Roll) volta ao centro total
            controls.p = 0;
            controls.r = 0;
            stick.style.transform = `translate(0px, 0px)`;
        } else {
            // Esquerda: Yaw volta ao centro, Throttle mantém a posição
            controls.y = 0;
            // Mantém a altura visual do stick baseada no throttle atual
            const currentY = mapValue(controls.t, 0, 255, limit, -limit);
            stick.style.transform = `translate(0px, ${currentY}px)`;
        }
    };

    zone.addEventListener('touchstart', handleTouch);
    zone.addEventListener('touchmove', handleTouch);
    zone.addEventListener('touchend', resetStick);
}

function mapValue(v, inMin, inMax, outMin, outMax) {
    return (v - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// Inicializa os joysticks
initJoystick('zone-left', true);
initJoystick('zone-right', false);

// Loop de envio de comandos para o ESP32
setInterval(() => {
    // Só envia se o throttle for maior que 0 ou houver comando, para economizar banda
    fetch(`/ctrl?t=${controls.t}&y=${controls.y}&p=${controls.p}&r=${controls.r}`);
}, sendInterval);

// Inicia em modo OFF por padrão visualmente
window.onload = () => sendMode(0);