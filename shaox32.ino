#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"

// Pinos Motores
const int motorFR = 13;
const int motorFL = 12;
const int motorBL = 14;
const int motorBR = 27;

// Config PWM (ESP32 v3.0+)
const int freq = 5000;
const int res = 8;

enum FlightMode { MANUAL, STABLE, HYBRID };
FlightMode currentMode = MANUAL;

// Variáveis de voo
int throttle = 0, yaw = 0, pitch = 0, roll = 0;
float ypr[3]; 
Quaternion q;
VectorFloat gravity;
uint8_t fifoBuffer[64];
MPU6050 mpu;
Preferences prefs;

// Ganhos PID (Padrão)
float Kp_p = 1.8, Ki_p = 0.02, Kd_p = 3.0;
float Kp_r = 1.8, Ki_r = 0.02, Kd_r = 3.0;

AsyncWebServer server(80);
TaskHandle_t FlightTask;

bool armed = false;
int sensorRotation = 0; // 0, 90, 180, 270 graus

// --- TASK DE VOO (CORE 0) ---
void flightLoop(void * pvParameters) {
    // Variáveis internas do PID (mantêm estado entre os loops)
    float erro_p, erro_r, soma_p = 0, soma_r = 0, erro_ant_p = 0, erro_ant_r = 0;
    
    while(true) {
        if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);

            // 1. Pega os ângulos brutos
            float raw_p = ypr[1] * 180 / M_PI;
            float raw_r = ypr[2] * 180 / M_PI;
            float cp, cr; // Ângulos corrigidos pela rotação

            // 2. Aplica a Orientação do Sensor
            if(sensorRotation == 90)       { cp = raw_r;  cr = -raw_p; }
            else if(sensorRotation == 180) { cp = -raw_p; cr = -raw_r; }
            else if(sensorRotation == 270) { cp = -raw_r; cr = raw_p;  }
            else                           { cp = raw_p;  cr = raw_r;  } 

            // 3. Cálculo do PID (Só se não estiver em modo MANUAL/OFF)
            float corr_p = 0, corr_r = 0;
            if (currentMode != MANUAL) {
                // PID PITCH
                erro_p = (pitch * 0.4) - cp;
                soma_p = constrain(soma_p + erro_p, -150, 150);
                corr_p = (Kp_p * erro_p) + (Ki_p * soma_p) + (Kd_p * (erro_p - erro_ant_p));
                erro_ant_p = erro_p;

                // PID ROLL
                erro_r = (roll * 0.4) - cr;
                soma_r = constrain(soma_r + erro_r, -150, 150);
                corr_r = (Kp_r * erro_r) + (Ki_r * soma_r) + (Kd_r * (erro_r - erro_ant_r));
                erro_ant_r = erro_r;

                // Suavização para o modo Híbrido
                if (currentMode == HYBRID) {
                    float fator = max(abs(pitch), abs(roll)) / 100.0;
                    corr_p *= (1.0 - (fator * 0.7)); 
                    corr_r *= (1.0 - (fator * 0.7));
                }
            }

            // 4. Mistura de Motores (Mixer)
            int m_fl = throttle + corr_p + corr_r + yaw;
            int m_fr = throttle + corr_p - corr_r - yaw;
            int m_bl = throttle - corr_p + corr_r - yaw;
            int m_br = throttle - corr_p - corr_r + yaw;

            // 5. SEGURANÇA FINAL (Só escreve nos motores se estiver Armado)
            if (armed && throttle > 10) { 
                ledcWrite(motorFL, constrain(m_fl, 0, 255));
                ledcWrite(motorFR, constrain(m_fr, 0, 255));
                ledcWrite(motorBL, constrain(m_bl, 0, 255));
                ledcWrite(motorBR, constrain(m_br, 0, 255));
            } else {
                // Força motores a zero se desarmado ou throttle muito baixo
                ledcWrite(motorFL, 0); ledcWrite(motorFR, 0);
                ledcWrite(motorBL, 0); ledcWrite(motorBR, 0);
                soma_p = 0; soma_r = 0; // Reseta o erro acumulado (I) ao pousar
            }
        }
        vTaskDelay(2 / portTICK_PERIOD_MS); // 500Hz
    }
}

void setup() 
{
    Serial.begin(115200);
    
    if(!LittleFS.begin()){ Serial.println("Erro LittleFS!"); return; }

    Wire.begin();
    Wire.setClock(400000);
    
    ledcAttach(motorFL, freq, res);
    ledcAttach(motorFR, freq, res);
    ledcAttach(motorBL, freq, res);
    ledcAttach(motorBR, freq, res);

    mpu.initialize();
    if (mpu.dmpInitialize() == 0) {
        prefs.begin("offsets", true);

        sensorRotation = prefs.getInt("rot", 0); // Carrega a rotação salva
        
        mpu.setXAccelOffset(prefs.getInt("ax", 0));
        mpu.setYAccelOffset(prefs.getInt("ay", 0));
        mpu.setZAccelOffset(prefs.getInt("az", 0));
        mpu.setXGyroOffset(prefs.getInt("gx", 0));
        mpu.setYGyroOffset(prefs.getInt("gy", 0));
        mpu.setZGyroOffset(prefs.getInt("gz", 0));
        prefs.end();
        mpu.setDMPEnabled(true);
    }

    // Retorna os PIDs atuais para o site preencher os campos
    server.on("/getPID", HTTP_GET, [](AsyncWebServerRequest *r){
        String json = "{";
        json += "\"kpp\":" + String(Kp_p) + ",";
        json += "\"kip\":" + String(Ki_p) + ",";
        json += "\"kdp\":" + String(Kd_p) + ",";
        json += "\"kpr\":" + String(Kp_r) + ",";
        json += "\"kir\":" + String(Ki_r) + ",";
        json += "\"kdr\":" + String(Kd_r);
        json += "}";
        r->send(200, "application/json", json);
    });

    // Salva os novos PIDs enviados pelo site
    server.on("/savePID", HTTP_GET, [](AsyncWebServerRequest *r){
        Kp_p = r->getParam("kpp")->value().toFloat();
        Ki_p = r->getParam("kip")->value().toFloat();
        Kd_p = r->getParam("kdp")->value().toFloat();
        Kp_r = r->getParam("kpr")->value().toFloat();
        Ki_r = r->getParam("kir")->value().toFloat();
        Kd_r = r->getParam("kdr")->value().toFloat();

        // Salva na Flash para não perder ao desligar
        prefs.begin("pid_data", false);
        prefs.putFloat("kpp", Kp_p); prefs.putFloat("kip", Ki_p); prefs.putFloat("kdp", Kd_p);
        prefs.putFloat("kpr", Kp_r); prefs.putFloat("kir", Ki_r); prefs.putFloat("kdr", Kd_r);
        prefs.end();

        r->send(200, "text/plain", "OK");
    });

    WiFi.softAP("Shaolin X32", "fabriciogekah");

    // Servindo arquivos do LittleFS
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(LittleFS, "/index.html", "text/html"); });
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(LittleFS, "/style.css", "text/css"); });
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *r){ r->send(LittleFS, "/script.js", "application/javascript"); });

    server.on("/ctrl", HTTP_GET, [](AsyncWebServerRequest *r){
        throttle = r->getParam("t")->value().toInt();
        yaw = r->getParam("y")->value().toInt();
        pitch = r->getParam("p")->value().toInt();
        roll = r->getParam("r")->value().toInt();
        r->send(200);
    });

    server.on("/setMode", HTTP_GET, [](AsyncWebServerRequest *r){
        int m = r->getParam("m")->value().toInt();
        currentMode = (FlightMode)m;
        r->send(200);
    });

    server.on("/arm", HTTP_GET, [](AsyncWebServerRequest *r){ armed = true; r->send(200); });
    server.on("/disarm", HTTP_GET, [](AsyncWebServerRequest *r){ armed = false; r->send(200); });

    // Rota de Telemetria (Ângulos)
    server.on("/telemetry", HTTP_GET, [](AsyncWebServerRequest *r){
        String json = "{";
        json += "\"p\":" + String(ypr[1] * 180/M_PI) + ",";
        json += "\"r\":" + String(ypr[2] * 180/M_PI) + ",";
        json += "\"a\":" + String(armed ? 1 : 0);
        json += "}";
        r->send(200, "application/json", json);
    });

    // Rota para salvar orientação
    server.on("/setRotation", HTTP_GET, [](AsyncWebServerRequest *r){
        sensorRotation = r->getParam("r")->value().toInt();
        prefs.begin("offsets", false);
        prefs.putInt("rot", sensorRotation);
        prefs.end();
        r->send(200);
    });

    server.begin();
    xTaskCreatePinnedToCore(flightLoop, "Flight", 8192, NULL, 1, &FlightTask, 0);
}

void loop() 
{ 
    vTaskDelay(1000 / portTICK_PERIOD_MS); 
}