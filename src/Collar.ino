#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <MPU9250_asukiaaa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "secrets.h"

#define SDA_PIN 21
#define SCL_PIN 22
#define PIEZO_PIN 34  // ADC pin for piezo sensor

MPU9250_asukiaaa imu;

Adafruit_MLX90614 mlx = Adafruit_MLX90614();
MAX30105 particleSensor;

// MAX30102 variables
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;

unsigned long previousTempRead = 0;

// Network credentials and server URLs
const char* ssid = "Moto edge 50 pro";
const char* password = "Pvmmnjaaa";
const char* flask_server_url = "http://192.168.169.52:5000/predict";
const char* render_server_url = "https://test-ptcb.onrender.com/data";

// For collecting MPU data
const int WINDOW_SIZE = 100;
float mpuWindow[WINDOW_SIZE][6];
int mpuIndex = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);
  pinMode(PIEZO_PIN, INPUT);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  imu.setWire(&Wire);
  imu.beginAccel();
  imu.beginGyro();
  Serial.println("MPU9250 Initialized successfully!");

  bool mlxSuccess = false;
  for (int i = 0; i < 5; i++) {
    if (mlx.begin()) {
      mlxSuccess = true;
      break;
    }
    delay(1);
  }
  if (!mlxSuccess) {
    Serial.println("MLX90614 initialization failed!");
    while (1);
  }

  if (!particleSensor.begin(Wire, I2C_SPEED_STANDARD)) {
    Serial.println("MAX30102 initialization failed!");
    while (1);
  }
  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x1F);
}

void sendMPUData() {
  HTTPClient http;
  http.begin(flask_server_url);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"sensor_data\":[";
  for (int i = 0; i < WINDOW_SIZE; i++) {
    payload += "[";
    for (int j = 0; j < 6; j++) {
      payload += String(mpuWindow[i][j], 6);
      if (j < 5) payload += ",";
    }
    payload += "]";
    if (i < WINDOW_SIZE - 1) payload += ",";
  }
  payload += "]}";

  int httpResponseCode = http.POST(payload);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("ML prediction: ");
    Serial.println(response);
  } else {
    Serial.print("Error sending MPU data: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void sendOtherSensorData(float ambient, float object, int bpm, int piezoRaw, float piezoVolt) {
  HTTPClient http;
  http.begin(render_server_url);
  http.addHeader("Content-Type", "application/json");

  String jsonPayload = "{\"ambient_temp\":" + String(ambient, 2) +
                       ",\"object_temp\":" + String(object, 2) +
                       ",\"avg_bpm\":" + String(bpm) +
                       ",\"piezo_raw\":" + String(piezoRaw) +
                       ",\"piezo_voltage\":" + String(piezoVolt, 3) + "}";

  int httpResponseCode = http.POST(jsonPayload);
  if (httpResponseCode > 0) {
    Serial.print("Render server response: ");
    Serial.println(http.getString());
  } else {
    Serial.print("Error sending to render server: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void loop() {
  unsigned long currentMillis = millis();

  // Temperature Reading
  static float ambient = 0, object = 0;
  if (currentMillis - previousTempRead >= 1000) {
    ambient = mlx.readAmbientTempC();
    object = mlx.readObjectTempC();

    if (isnan(ambient) || isnan(object)) {
      Serial.println("Temperature sensor error!");
      mlx.begin(); // Reinitialize sensor
    } else {
      Serial.print("Ambient: "); Serial.print(ambient);
      Serial.print("°C\tObject: "); Serial.print(object);
      Serial.println("°C");
    }

    previousTempRead = currentMillis;
  }

  // MAX30102
  long irValue = particleSensor.getIR();
  if (irValue > 50000) {
    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);
      if (beatsPerMinute > 50 && beatsPerMinute < 255) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
    }
    Serial.print("IR: "); Serial.print(irValue);
    Serial.print(" | BPM: "); Serial.print(beatsPerMinute);
    Serial.print(" | Avg: "); Serial.println(beatAvg);
  } else {
    Serial.println("collar not placed");
    beatsPerMinute = 0;
    beatAvg = 0;
  }

  // MPU9250
  imu.accelUpdate();
  imu.gyroUpdate();

  float ax = imu.accelX();
  float ay = imu.accelY();
  float az = imu.accelZ();
  float gx = imu.gyroX();
  float gy = imu.gyroY();
  float gz = imu.gyroZ();

  mpuWindow[mpuIndex][0] = ax;
  mpuWindow[mpuIndex][1] = ay;
  mpuWindow[mpuIndex][2] = az;
  mpuWindow[mpuIndex][3] = gx;
  mpuWindow[mpuIndex][4] = gy;
  mpuWindow[mpuIndex][5] = gz;

  mpuIndex++;
  if (mpuIndex == WINDOW_SIZE) {
    sendMPUData();
    mpuIndex = 0;
  }

  // Piezo Sensor
  int vibration = analogRead(PIEZO_PIN);
  float voltage = (vibration * 3.3) / 4095;

  Serial.print("Raw Piezo: ");
  Serial.print(vibration);
  Serial.print(" | Voltage: ");
  Serial.println(voltage, 3);

  // Send all non-MPU data to render server every second
  static unsigned long lastRenderSent = 0;
  if (millis() - lastRenderSent > 1000) {
    sendOtherSensorData(ambient, object, beatAvg, vibration, voltage);
    lastRenderSent = millis();
  }

  delay(10);
}