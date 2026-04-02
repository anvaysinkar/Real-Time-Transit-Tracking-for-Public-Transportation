#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

const char* ssid = "Passenger Counting";
const char* password = "123456789";

AsyncWebServer server(80);

// Sensor configuration
const int sensorPin = 34;
const int threshold = 700;
const int noPressureMin = 1000;
const int noPressureMax = 4095;
unsigned long stepCooldown = 700; // ms

int counter1 = 0;
int counter2 = 0;
bool counting1 = false;
bool counting2 = false;
bool stepDetected = false;
unsigned long lastStepTime = 0;

// HTML Page
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Smart Passenger Counter</title>
  <style>
    body { font-family: Arial; text-align: center; background: #f0f0f0; padding: 20px; }
    h1 { color: #333; }
    .counter { font-size: 2em; margin: 20px; }
    button { padding: 10px 20px; font-size: 1em; margin: 5px; }
    .box { background: #fff; padding: 20px; border-radius: 10px; margin: 20px auto; width: 300px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }
  </style>
</head>
<body>
  <h1>Smart Passenger Counter</h1>
  <div class="box">
    <h2>Bus Stop 1</h2>
    <div class="counter" id="counter1">0</div>
    <button onclick="sendCommand('start1')">Start</button>
    <button onclick="sendCommand('stop1')">Stop</button>
  </div>

  <div class="box">
    <h2>Bus Stop 2</h2>
    <div class="counter" id="counter2">0</div>
    <button onclick="sendCommand('start2')">Start</button>
    <button onclick="sendCommand('stop2')">Stop</button>
  </div>

  <div class="box">
    <p id="datetime"></p>
    <button onclick="saveData()">Save Data</button>
  </div>

  <script>
    function sendCommand(cmd) {
      fetch("/" + cmd)
        .then(response => response.text())
        .then(data => console.log(data));
    }

    function saveData() {
      const date = new Date().toLocaleString('en-IN');
      const text = `Bus Stop 1: ${document.getElementById('counter1').innerText}\nBus Stop 2: ${document.getElementById('counter2').innerText}\n${date}`;
      const blob = new Blob([text], { type: 'text/plain' });
      const link = document.createElement("a");
      link.href = URL.createObjectURL(blob);
      link.download = "passenger_data.txt";
      link.click();
    }

    function updateDateTime() {
      const now = new Date();
      const options = { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' };
      document.getElementById("datetime").innerText = now.toLocaleDateString('en-IN', options) + " " + now.toLocaleTimeString();
    }

    function updateCounters() {
      fetch("/count")
        .then(response => response.json())
        .then(data => {
          document.getElementById("counter1").innerText = data.counter1;
          document.getElementById("counter2").innerText = data.counter2;
        });
    }

    setInterval(updateDateTime, 1000);
    setInterval(updateCounters, 1000);
  </script>
</body>
</html>
)rawliteral";

// JSON endpoint
String getJSONCounts() {
  return "{\"counter1\":" + String(counter1) + ", \"counter2\":" + String(counter2) + "}";
}

void setup() {
  Serial.begin(115200);
  WiFi.softAP(ssid, password);
  Serial.println("AP started");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", htmlPage);
  });

  server.on("/start1", HTTP_GET, [](AsyncWebServerRequest *request){
    counting1 = true;
    counting2 = false;
    request->send(200, "text/plain", "Start Stop 1");
  });

  server.on("/stop1", HTTP_GET, [](AsyncWebServerRequest *request){
    counting1 = false;
    request->send(200, "text/plain", "Stop Stop 1");
  });

  server.on("/start2", HTTP_GET, [](AsyncWebServerRequest *request){
    counting2 = true;
    counting1 = false;
    request->send(200, "text/plain", "Start Stop 2");
  });

  server.on("/stop2", HTTP_GET, [](AsyncWebServerRequest *request){
    counting2 = false;
    request->send(200, "text/plain", "Stop Stop 2");
  });

  server.on("/count", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", getJSONCounts());
  });

  server.begin();
}

void loop() {
  int val = analogRead(sensorPin);
  Serial.println(val);

  unsigned long currentTime = millis();

  if (val < threshold && !stepDetected && (currentTime - lastStepTime > stepCooldown)) {
    if (counting1) counter1++;
    if (counting2) counter2++;
    stepDetected = true;
    lastStepTime = currentTime;
  }

  if (val >= noPressureMin && val <= noPressureMax) {
    stepDetected = false;
  }

  delay(10);
}
