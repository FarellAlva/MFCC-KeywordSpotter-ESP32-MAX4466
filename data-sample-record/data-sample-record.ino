#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "AcerN58";      
const char* password = "farellpunya";   

#define MIC_PIN 34         // Pin Analog Mikrofon (MAX4466 Out)
#define SAMPLE_RATE 16000  // 16 kHz (Standar untuk Voice Recognition)
#define DURATION 1         // Durasi rekam 1 Detik
#define NUM_SAMPLES (SAMPLE_RATE * DURATION)


int16_t audioBuffer[NUM_SAMPLES];

WebServer server(80);


const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Keyword Collector</title>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; background-color: #2c3e50; color: white; margin-top: 40px;}
    h2 { color: #ecf0f1; margin-bottom: 5px; }
    p { color: #bdc3c7; font-size: 0.9rem; }
    
    .container {
      display: flex; flex-direction: column; align-items: center; gap: 15px;
    }

    button {
      width: 80%; max-width: 300px; 
      padding: 18px; 
      font-size: 20px; font-weight: bold; color: white;
      border: none; border-radius: 12px; cursor: pointer;
      box-shadow: 0 4px 6px rgba(0,0,0,0.3);
      transition: transform 0.1s;
    }
    button:active { transform: scale(0.98); }
    
    .btn-yes { background-color: #27ae60; } /* Hijau */
    .btn-no { background-color: #c0392b; }  /* Merah */
    .btn-noise { background-color: #7f8c8d; } /* Abu-abu */
    
    #status { 
      font-size: 1.2rem; font-weight: bold; color: #f1c40f; 
      margin-top: 30px; min-height: 1.5em; 
    }
    .footer { margin-top: 50px; font-size: 0.8rem; color: #7f8c8d; }
  </style>
</head>
<body>
  <h2>ESP32 Keyword Collector</h2>
  <p>Pastikan mikrofon dekat dengan sumber suara (5-10cm)</p>
  
  <div id="status">Siap Merekam...</div>

  <div class="container">
    <button class="btn-yes" onclick="record('yes')">SAY "YES"</button>
    
    <button class="btn-no" onclick="record('no')">SAY "NO"</button>
    
    <button class="btn-noise" onclick="record('suara-lain')">Suara Lainnya</button>
  </div>

  <div class="footer">File .wav akan terdownload otomatis</div>

<script>
function record(label) {
  var status = document.getElementById("status");
  status.innerHTML = "🎤 MEREKAM: " + label.toUpperCase() + "...";
  status.style.color = "#f1c40f"; // Kuning
  
  // Kirim perintah rekam ke ESP32
  var time = new Date().getTime();
  window.location.href = "/record?label=" + label + "&t=" + time;
  
  // Kembalikan status setelah selesai (estimasi 2 detik)
  setTimeout(function(){
    status.innerHTML = "✅ Tersimpan: " + label + ".wav";
    status.style.color = "#2ecc71"; // Hijau
  }, 2200);
}
</script>
</body>
</html>
)rawliteral";


void createWavHeader(byte* header, int waveDataSize) {
  int sampleRate = SAMPLE_RATE;
  int numChannels = 1;
  int bitsPerSample = 16;
  int byteRate = sampleRate * numChannels * (bitsPerSample / 8);
  int blockAlign = numChannels * (bitsPerSample / 8);
  int fileSize = waveDataSize + 36;

  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
  header[20] = 1; header[21] = 0;
  header[22] = (byte)numChannels; header[23] = 0;
  header[24] = (byte)(sampleRate & 0xFF);
  header[25] = (byte)((sampleRate >> 8) & 0xFF);
  header[26] = (byte)((sampleRate >> 16) & 0xFF);
  header[27] = (byte)((sampleRate >> 24) & 0xFF);
  header[28] = (byte)(byteRate & 0xFF);
  header[29] = (byte)((byteRate >> 8) & 0xFF);
  header[30] = (byte)((byteRate >> 16) & 0xFF);
  header[31] = (byte)((byteRate >> 24) & 0xFF);
  header[32] = (byte)blockAlign; header[33] = 0;
  header[34] = (byte)bitsPerSample; header[35] = 0;
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (byte)(waveDataSize & 0xFF);
  header[41] = (byte)((waveDataSize >> 8) & 0xFF);
  header[42] = (byte)((waveDataSize >> 16) & 0xFF);
  header[43] = (byte)((waveDataSize >> 24) & 0xFF);
}

// --- URL HANDLERS ---

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleRecord() {
  if (!server.hasArg("label")) {
    server.send(400, "text/plain", "Label Missing");
    return;
  }
  
  String label = server.arg("label");
  Serial.println("Recording: " + label);

 
  unsigned long interval = 1000000 / SAMPLE_RATE;
  unsigned long prevMicros = micros();
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    while ((micros() - prevMicros) < interval) { /* Tunggu */ }
    prevMicros = micros();
    
    // Baca Sensor (0-4095)
    int raw = analogRead(MIC_PIN);
    
   
    audioBuffer[i] = (raw - 1900) * 15; 
  }

  // 2. BUAT HEADER WAV
  byte header[44];
  createWavHeader(header, NUM_SAMPLES * 2);

  // 3. KIRIM DOWNLOAD KE BROWSER
  String filename = label + "_" + String(millis()) + ".wav";
  
  server.sendHeader("Content-Type", "audio/wav");
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Connection", "close");
  
  WiFiClient client = server.client();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: audio/wav");
  client.println("Content-Disposition: attachment; filename=\"" + filename + "\"");
  client.println("Content-Length: " + String(44 + NUM_SAMPLES * 2));
  client.println();
  
  client.write(header, 44);
  client.write((byte*)audioBuffer, NUM_SAMPLES * 2);
  
  Serial.println("Sent: " + filename);
}

void setup() {
  Serial.begin(115200);
  
  // Konfigurasi ADC Mikrofon
  analogSetAttenuation(ADC_11db); // Rentang 0-3.3V
  pinMode(MIC_PIN, INPUT);

  // Koneksi WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("\nMenghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  
  Serial.println("\n\n=== WEB SERVER SIAP ===");
  Serial.print("Buka alamat ini di Browser: http://");
  Serial.println(WiFi.localIP());
  Serial.println("=======================");

  server.on("/", handleRoot);
  server.on("/record", handleRecord);
  server.begin();
}

void loop() {
  server.handleClient();
}