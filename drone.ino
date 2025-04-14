#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* ssid = "Shaolin";
const char* password = "senzekken";

// Pinos dos motores
const int motorFL = 20; // Frente esquerda
const int motorFR = 21; // Frente direita
const int motorBL = 0;  // Trás esquerda
const int motorBR = 1;  // Trás direita

int potenciaBase = 0; // Potência geral, controlada pelo slider (0-255)

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Drone</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body {
      margin: 0;
      padding: 0;
      background-color: #121212;
      color: white;
      font-family: sans-serif;
      text-align: center;
      overflow: hidden; /* trava o scroll */
    }
    .container {
      display: flex;
      flex-direction: row;
      justify-content: space-around;
      align-items: center;
      height: 100vh;
      padding: 10px;
    }
    .controls {
      flex: 1;
    }
    .slider-container {
      flex: 1;
      display: flex;
      justify-content: center;
      align-items: center;
      transform: rotate(270deg);
    }
    .slider-container input[type=range] {
      width: 300px;
      height: 40px;
      accent-color: #00bcd4;
    }
    button {
      padding: 15px 30px;
      font-size: 18px;
      margin: 10px;
      border: none;
      border-radius: 8px;
      background-color: #00bcd4;
      color: white;
    }
    button:hover {
      background-color: #0097a7;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="controls">
      <h2>Potência: <span id="potVal">0</span></h2>
      <button onclick="send('up')">↑</button><br>
      <button onclick="send('left')">←</button>
      <button onclick="send('stop')">■</button>
      <button onclick="send('right')">→</button><br>
      <button onclick="send('down')">↓</button>
    </div>
    <div class="slider-container">
      <input type="range" min="0" max="255" value="0" id="slider" oninput="updatePower(this.value)">
    </div>
  </div>

  <script>
    function send(dir) {
      fetch(`/move?dir=${dir}`);
    }

    function updatePower(val) {
      document.getElementById("potVal").innerText = val;
      fetch(`/power?val=${val}`);
    }
  </script>
</body>
</html>
)rawliteral";

AsyncWebServer server(80);

void setup() {
  Serial.begin(115200);

  // Inicializar pinos
  for (int pin : {motorFL, motorFR, motorBL, motorBR}) {
    pinMode(pin, OUTPUT);
  }

  // Iniciar AP
  WiFi.softAP(ssid, password);
  Serial.println("AP criado em:");
  Serial.println(WiFi.softAPIP());

  // Página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Slider de potência
  server.on("/power", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("val")) {
      potenciaBase = request->getParam("val")->value().toInt();
      Serial.println("Potência ajustada: " + String(potenciaBase));
    }
    request->send(200, "text/plain", "Potência ajustada");
  });

  // Comando de movimento
  server.on("/move", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("dir")) {
      String dir = request->getParam("dir")->value();
      Serial.println("Comando: " + dir);

      int fl = 0, fr = 0, bl = 0, br = 0;

      if (dir == "up") {
        fl = fr = bl = br = potenciaBase;
      } else if (dir == "left") {
        fl = bl = potenciaBase / 2;
        fr = br = potenciaBase;
      } else if (dir == "right") {
        fr = br = potenciaBase / 2;
        fl = bl = potenciaBase;
      } else if (dir == "down") {
        fl = fr = bl = br = potenciaBase / 2;
      } else if (dir == "stop") {
        fl = fr = bl = br = 0;
      }

      analogWrite(motorFL, fl);
      analogWrite(motorFR, fr);
      analogWrite(motorBL, bl);
      analogWrite(motorBR, br);
    }
    request->send(200, "text/plain", "Movimento executado");
  });

  server.begin();
}

void loop() {}
