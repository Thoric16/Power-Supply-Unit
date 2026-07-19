#include <SPI.h>
#include <SoftwareSPI.h>
#include <W5500lwIP.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <Wire.h>

// ==========================================
// KONFIGURÁCIA SIETE 
// ==========================================
byte mac[] = { 0x00, 0x08, 0xDC, 0x11, 0x22, 0x33 };
IPAddress staticIP(192, 168, 1, 100);

// W55RP20 Interné piny
#define PIN_SCK 21
#define PIN_MISO 22
#define PIN_MOSI 23
#define PIN_CS 20
#define PIN_RST 25
#define PIN_IRQ 24

SoftwareSPI wizSPI(PIN_SCK, PIN_MISO, PIN_MOSI);
Wiznet5500lwIP eth(PIN_CS, wizSPI, PIN_IRQ);

// TCP Server
const uint16_t TCP_PORT = 50000;
WiFiServer tcpServer(TCP_PORT);

// UDP Multicast
WiFiUDP udp;
IPAddress multicastIP(239, 192, 1, 100);
const uint16_t UDP_PORT = 49150;
const unsigned long TELEMETRY_INTERVAL_MS = 100; // 10 Hz
unsigned long lastTelemetryTime = 0;

// ==========================================
// DEBUG A SÉRIOVÁ LINKA
// ==========================================
unsigned long lastSerialDebugTime = 0;
const unsigned long SERIAL_DEBUG_INTERVAL_MS = 1000; // 1 Hz
bool serialTelemetryEnabled = true;

// ==========================================
// HARDVÉROVÉ MAPOVANIE A KONŠTANTY
// ==========================================
// Socket 1-8 -> GP11-GP4 (Indexované od 0)
const int socketPins[8] = {11, 10, 9, 8, 7, 6, 5, 4};

// I2C (Wire1 pre GP2 a GP3)
const int I2C_SDA = 2;
const int I2C_SCL = 3;
const uint8_t ADS7828_ADDR = 0x48;

// Výpočetné konštanty
const float V_REF = 2.5;             
const float ACS_OFFSET = 2.5;        
const float ACS_SENSITIVITY = 0.066; // 30A verzia: 66 mV/A
const float BATTERY_V_MIN = 3.5*11; 
const float BATTERY_V_MAX = 4.2*11; 

// ==========================================
// DÁTOVÉ ŠTRUKTÚRY
// ==========================================
struct __attribute__((packed)) TelemetryPacket {
  float current_cells_1_5[5];
  float current_cells_6_8;
  float voltage_cells_1_5;
  float voltage_cells_6_8;
  float current;
  float state_of_charge;
};

TelemetryPacket telemetryData;

// ==========================================
// POMOCNÉ FUNKCIE
// ==========================================
uint16_t readADS7828(uint8_t channel) {
  if (channel > 7) return 0;
  const uint8_t ch_map[8] = {0x00, 0x40, 0x10, 0x50, 0x20, 0x60, 0x30, 0x70};
  uint8_t cmd = 0x8C | ch_map[channel];

  Wire1.beginTransmission(ADS7828_ADDR);
  Wire1.write(cmd);
  Wire1.endTransmission();

  Wire1.requestFrom(ADS7828_ADDR, (uint8_t)2);
  if (Wire1.available() >= 2) {
    uint8_t msb = Wire1.read();
    uint8_t lsb = Wire1.read();
    return (msb << 8) | lsb; 
  }
  return 0;
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(2000); 
  Serial.println("\n--- PICO-PSU V2 STARTING ---");

  // 1. Inicializácia Socketov
  Serial.println("Inicializujem sockety (GP11-GP4)...");
  for (int i = 0; i < 8; i++) {
    pinMode(socketPins[i], OUTPUT);
    digitalWrite(socketPins[i], HIGH);
    delay(200);
  }

  // 2. Inicializácia I2C (Wire1)
  Serial.println("Inicializujem I2C pre ADS7828 na Wire1...");
  Wire1.setSDA(I2C_SDA);
  Wire1.setSCL(I2C_SCL);
  Wire1.begin();

  // 3. Inicializácia W5500 (SoftwareSPI + lwIP)
  Serial.println("Inicializujem siet W5500...");
  pinMode(PIN_RST, OUTPUT);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(50);

  eth.setSPISpeed(20000000);
  
  if (!eth.begin(mac)) {
    Serial.println("-> HW chyba: W5500 nenajdeny!");
  } else {
    Serial.print("Pripojovanie k DHCP...");
    unsigned long startDHCP = millis();
    
    // Čakáme max 10 sekúnd na DHCP, aby to neblokovalo bez kábla
    while (eth.localIP() == IPAddress(0,0,0,0) && (millis() - startDHCP < 10000)) {
      delay(500);
      Serial.print(".");
    }
    
    if (eth.localIP() == IPAddress(0,0,0,0)) {
      Serial.println("\n-> DHCP timeout! Pouzivam staticku IP.");
      eth.config(staticIP, IPAddress(172, 16, 10, 20), IPAddress(255, 255, 255, 0));
    } else {
      Serial.println("\n-> DHCP uspesne.");
    }
  }

  Serial.print("PSU IP Adresa: ");
  Serial.println(eth.localIP());

  // Štart serverov
  tcpServer.begin();
  
  // Multicast pripojenie
  udp.beginMulticast(multicastIP, UDP_PORT);
  
  Serial.println("-------------------------------------------------");
  Serial.println("TCP Server bezi na porte 50000.");
  Serial.println("Ovladanie cez Serial: 'ON x' alebo 'OFF x' (x = 1-8)");
  Serial.println("Pre telemetriu: 'TEL ON' alebo 'TEL OFF'");
  Serial.println("-------------------------------------------------\n");
}

// ==========================================
// HLAVNÁ SLUČKA (LOOP)
// ==========================================
void loop() {
  handleTCP();
  handleSerialCommands();

  // UDP telemetria v intervale
  if (millis() - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
    lastTelemetryTime = millis();
    updateAndSendTelemetry();
  }
}

// ==========================================
// SÉRIOVÉ OVLÁDANIE
// ==========================================
void handleSerialCommands() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    input.toUpperCase(); 

    if (input.length() == 0) return;

    if (input.startsWith("ON ")) {
      int socket = input.substring(3).toInt();
      if (socket >= 1 && socket <= 8) {
        digitalWrite(socketPins[socket - 1], HIGH);
        Serial.print("[SERIAL] -> ZAPINAM socket "); Serial.println(socket);
      }
    } 
    else if (input.startsWith("OFF ")) {
      int socket = input.substring(4).toInt();
      if (socket >= 1 && socket <= 8) {
        digitalWrite(socketPins[socket - 1], LOW);
        Serial.print("[SERIAL] -> VYPINAM socket "); Serial.println(socket);
      }
    } 
    else if (input == "TEL ON") {
      serialTelemetryEnabled = true;
      Serial.println("[SERIAL] -> Telemetria ZAPNUTA");
    }
    else if (input == "TEL OFF") {
      serialTelemetryEnabled = false;
      Serial.println("[SERIAL] -> Telemetria VYPNUTA");
    }
  }
}

// ==========================================
// TCP REQUEST-RESPONSE
// ==========================================
void handleTCP() {
  WiFiClient client = tcpServer.available();
  if (client) {
    if (client.connected() && client.available() > 0) {
      uint8_t req = client.read();
      uint8_t cmd = (req >> 4) & 0x0F;
      uint8_t arg = req & 0x0F;
      uint8_t resp = 0;

      Serial.print("[TCP] Prijate: 0x"); Serial.print(req, HEX);
      Serial.print(" (CMD="); Serial.print(cmd); Serial.print(", ARG="); Serial.print(arg); Serial.println(")");

      if ((cmd == 0x01 || cmd == 0x02) && (arg <= 7)) {
        digitalWrite(socketPins[arg], (cmd == 0x01) ? HIGH : LOW);
        resp = (cmd << 4) | (arg + 8); // OK response
        
        Serial.print("[TCP] -> Akcia: ");
        Serial.print((cmd == 0x01) ? "ZAPNUTY" : "VYPNUTY");
        Serial.print(" Socket "); Serial.println(arg + 1); 
      } else {
        resp = (cmd << 4) | 0; // FAIL response
        Serial.println("[TCP] -> Akcia ZAMIETNUTA");
      }
      
      client.write(resp);
    }
    client.stop(); // Vždy okamžite uzavrieť spojenie
  }
}

// ==========================================
// SPRACOVANIE A ODOSLANIE TELEMETRIE
// ==========================================
void updateAndSendTelemetry() {
  float total_current = 0.0;

  // Prúd články 1-5 (ADC 0-4)
  for (int i = 0; i < 5; i++) {
    uint16_t raw_adc = readADS7828(i);
    float v_adc = (raw_adc / 4095.0) * V_REF;
    float v_sensor = v_adc * 2.0; 
    float current = (v_sensor - ACS_OFFSET) / ACS_SENSITIVITY;
    
    
    if (abs(current) < 0.1) current = 0.0;
    
    telemetryData.current_cells_1_5[i] = current;
    total_current += current;
  }

  // Prúd články 6-8 (ADC 7)
  uint16_t raw_i_6_8 = readADS7828(7);
  float v_adc_i_6_8 = (raw_i_6_8 / 4095.0) * V_REF;
  float v_sensor_i_6_8 = v_adc_i_6_8 * 2.0; 
  float current_6_8 = (v_sensor_i_6_8 - ACS_OFFSET) / ACS_SENSITIVITY;
  
  if (abs(current_6_8) < 0.1) current_6_8 = 0.0;
  telemetryData.current_cells_6_8 = current_6_8;
  total_current += current_6_8;

  telemetryData.current = total_current;

  // Napätie 1-5 (ADC 5)
  uint16_t raw_v_1_5 = readADS7828(5);
  float v_adc_1_5 = (raw_v_1_5 / 4095.0) * V_REF;
  telemetryData.voltage_cells_1_5 = v_adc_1_5 * ((220.0 + 10.0) / 10.0);
  //Serial.print("  raw_v_1_5: "); Serial.println(raw_v_1_5);

  // Napätie 6-8 (ADC 6)
  uint16_t raw_v_6_8 = readADS7828(6);
  float v_adc_6_8 = (raw_v_6_8 / 4095.0) * V_REF;
  telemetryData.voltage_cells_6_8 = v_adc_6_8 * ((47.0 + 10.0) / 10.0);
  //Serial.print("  raw_v_6_8: "); Serial.println(raw_v_6_8);

  // State of Charge (SOC)
  float soc = (telemetryData.voltage_cells_1_5 - BATTERY_V_MIN) / (BATTERY_V_MAX - BATTERY_V_MIN);
  if (soc > 1.0) soc = 1.0;
  if (soc < 0.0) soc = 0.0;
  telemetryData.state_of_charge = soc;

  // Odoslanie UDP
  // Uistíme sa, že je vytvorená správna multicast štruktúra hlavičky pre lwIP
  udp.beginPacket(multicastIP, UDP_PORT);
  udp.write((uint8_t*)&telemetryData, sizeof(TelemetryPacket));
  udp.endPacket();

  // Sériový výpis debug telemetrie
  if (serialTelemetryEnabled && (millis() - lastSerialDebugTime >= SERIAL_DEBUG_INTERVAL_MS)) {
    lastSerialDebugTime = millis();
    Serial.println("\n[TELEMETRIA]");
    
    Serial.print("  Prudy 1-5 [A]: ");
    for (int i = 0; i < 5; i++) {
      Serial.print(telemetryData.current_cells_1_5[i], 2);
      Serial.print("  ");
    }
    Serial.println();
    
    Serial.print("  Prud  6-8 [A]: "); Serial.println(telemetryData.current_cells_6_8, 2);
    Serial.print("  Prud Celk [A]: "); Serial.println(telemetryData.current, 2);
    Serial.print("  Napatie 1-5 [V]: "); Serial.println(telemetryData.voltage_cells_1_5, 2);
    Serial.print("  Napatie 6-8 [V]: "); Serial.println(telemetryData.voltage_cells_6_8, 2);
    Serial.print("  SOC        [%]: "); Serial.println(telemetryData.state_of_charge * 100.0, 1);
  }
}
