#include "ESPAsyncWebServer.h"
#include <Arduino_JSON.h>
#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "BluetoothSerial.h"

constexpr char WIFI_SSID[] = "SSID-da-modificare";
constexpr char WIFI_PASS[] = "PASSWORD-da-modificare";

// Setta un indirizzo IP Fisso
IPAddress local_IP(192, 168, 1, 200);
// Setta l'indirizzo del Gateway
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8); //opzionale
IPAddress secondaryDNS(8, 8, 4, 4); //opzionale

// Struttura dati, deve corrispondere a quella del mittente
typedef struct struttura_dati {
  char  v0[32];
  int   v1;
  float v2;
  float v3;
  float v4;
  unsigned int progressivo;
} struttura_dati;

struttura_dati LettureSensori;

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

BluetoothSerial SerialBT;
JSONVar board;
AsyncWebServer server(80);
AsyncEventSource events("/events");

volatile int interruptCounter;
int totalInterruptCounter;
hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
#define DELAY_RECONNECT 60



void IRAM_ATTR onTimer() 
{
  // https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Timer/RepeatTimer/RepeatTimer.ino
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter++;
  if (WiFi.status() != WL_CONNECTED)
  {
    ESP.restart();
  }
  portEXIT_CRITICAL_ISR(&timerMux);
}

void suDatiRicevuti(const uint8_t * mac_addr, const uint8_t *incomingData, int len) { 
  // Copi l'indirizzo MAC del mittente
  char macStr[18];
  Serial.print("Pacchetto ricevuto da: ");
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);
  memcpy(&LettureSensori, incomingData, sizeof(LettureSensori));
  
  board["v1"] = LettureSensori.v1;
  board["v2"] = LettureSensori.v2;
  board["v3"] = LettureSensori.v3;
  board["v4"] = LettureSensori.v4;
  board["progressivo"] = String(LettureSensori.progressivo);
  String jsonString = JSON.stringify(board);
  events.send(jsonString.c_str(), "new_readings", millis());
  
  Serial.printf("Board ID %u: %u bytes\n", LettureSensori.v1, len);
  Serial.printf("t valore: %4.2f \n", LettureSensori.v2);
  Serial.printf("h valore: %4.2f \n", LettureSensori.v3);
  Serial.printf("Progressivo: %d \n", LettureSensori.progressivo);
  Serial.println();
}

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Robotdazero - rete "Ambientale" con ESP32</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    p {  font-size: 1.2rem;}
    body {  margin: 0;}
    .topnav { overflow: hidden; background-color: #2f4468; color: white; font-size: 1.7rem; }
    .content { padding: 20px; }
    .card { background-color: white; box-shadow: 2px 2px 12px 1px rgba(140,140,140,.5); }
    .cards { max-width: 700px; margin: 0 auto; display: grid; grid-gap: 2rem; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); }
    .reading { font-size: 2.8rem; }
    .packet { color: #bebebe; }
    .card.temperature { color: #fd7e14; }
    .card.humidity { color: #1b78e2; }
  </style>
</head>
<body>
  <div class="topnav">
    <h3>ROBOTDAZERO - rete "Ambientale" con ESP32</h3>
  </div>
  <div class="content">
    <div class="cards">
      <div class="card temperature">
        <h4><i class="fas fa-thermometer-half"></i> SCHEDA #1 - TEMPERATURA</h4><p><span class="reading"><span id="t1"></span> &deg;C</span></p><p class="packet">sensore DHT11: <span id="rt1"></span></p>
      </div>
      <div class="card humidity">
        <h4><i class="fas fa-tint"></i> SCHEDA #1 - UMIDITA'</h4><p><span class="reading"><span id="h1"></span> &percnt;</span></p><p class="packet">sensore DHT11: <span id="rh1"></span></p>
      </div>
      <div class="card temperature">
        <h4><i class="far fa-bell"></i> SCHEDA #1 - Fumo/Metano</h4><p><span class="reading"><span id="t2"></span> ppm</span></p><p class="packet">sensore MQ-2: <span id="rt2"></span></p>
      </div>
      <div class="card humidity">
        <h4><i class="far fa-bell"></i> SCHEDA #1 - Qualita' dell'aria</h4><p><span class="reading"><span id="h2"></span> ppm</span></p><p class="packet">sensore MQ-135: <span id="rh2"></span></p>
      </div>
    </div>
  </div>
<script>
if (!!window.EventSource) {
 var source = new EventSource('/events');
 
 source.addEventListener('open', function(e) {
  console.log("Events Connected");
 }, false);
 source.addEventListener('error', function(e) {
  if (e.target.readyState != EventSource.OPEN) {
    console.log("Events Disconnected");
  }
 }, false);
 
 source.addEventListener('message', function(e) {
  console.log("message", e.data);
 }, false);
 
 source.addEventListener('new_readings', function(e) {
  console.log("new_readings", e.data);
  var obj = JSON.parse(e.data);
  document.getElementById("t1").innerHTML = Math.round(obj.v2 * 100) / 100;
  document.getElementById("h1").innerHTML = obj.v1;
  document.getElementById("t2").innerHTML = obj.v3;
  document.getElementById("h2").innerHTML = obj.v4;
 }, false);
}
</script>
</body>
</html>)rawliteral";

void initBT() {
  SerialBT.begin("ESP32-sensori");    
  Serial.println("Dispositivo avviato, puoi accoppiarlo con bluetooth...");
}

void initWiFi() {
    WiFi.mode(WIFI_MODE_APSTA);

    if(!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("STA Failed to configure");
    }

    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.printf("Connecting to %s .", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) { Serial.print("."); delay(200); }
    Serial.println("ok");

    IPAddress ip = WiFi.localIP();

    Serial.printf("SSID: %s\n", WIFI_SSID);
    Serial.printf("Channel: %u\n", WiFi.channel());
    Serial.printf("IP: %u.%u.%u.%u\n", ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, ip >> 24);
}

void initEspNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP NOW failed to initialize");
        while (1);
    }
    esp_now_register_recv_cb(suDatiRicevuti);
}

void setup() {
  Serial.begin(115200);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disabilita brownout detector

  initWiFi();
  initEspNow();
  initBT();

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, DELAY_RECONNECT * 1000000, true);
  timerAlarmEnable(timer);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });
   
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Riconnessione! Ultmo messaggio ricevuto: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);
  server.begin();
}
 
void loop() {
  static unsigned long lastEventTime = millis();
  static const unsigned long EVENT_INTERVAL_MS = 5000;
  if ((millis() - lastEventTime) > EVENT_INTERVAL_MS) {
    events.send("ping",NULL,millis());
    lastEventTime = millis();
  }
}
