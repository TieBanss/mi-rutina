#include <ArduinoBLE.h>
#include <LSM6DS3.h>
#include <Wire.h>

/*
 * MI RUTINA - SENSOR XIAO nRF52840 SENSE (con diagnostico por Serial)
 * Streaming de datos brutos por BLE a ~40 Hz
 * Payload: 12 bytes (Ax, Ay, Az, Gx, Gy, Gz) en int16_t
 */

LSM6DS3 myIMU(I2C_MODE, 0x6A);

#define SERVICE_UUID       "19b10000-e8f2-537e-4f6c-d104768a1214"
#define CHAR_RAW_IMU_UUID  "19b10001-e8f2-537e-4f6c-d104768a1214"

struct __attribute__((packed)) ImuRawPacket {
  int16_t ax, ay, az; // milli-G
  int16_t gx, gy, gz; // grados/s
};

BLEService routineService(SERVICE_UUID);
BLECharacteristic rawImuChar(CHAR_RAW_IMU_UUID, BLERead | BLENotify, sizeof(ImuRawPacket));

unsigned long lastSend = 0;
unsigned long lastHeartbeat = 0;
unsigned long packetsSent = 0;
const unsigned long SEND_INTERVAL_MS = 25;    // 40 Hz
const unsigned long HEARTBEAT_MS = 1000;      // latido cada 1 s
bool wasConnected = false;

// Si algo falla: repite el error por Serial cada segundo y parpadea el LED rojo
void fatalError(const char* msg) {
  while (1) {
    Serial.print("[ERROR] ");
    Serial.println(msg);
    digitalWrite(LED_RED, LOW);
    delay(500);
    digitalWrite(LED_RED, HIGH);
    delay(500);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);

  // Esperar hasta 5 s a que se abra el Monitor Serie (parpadeo azul mientras espera)
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000) {
    digitalWrite(LED_BLUE, (millis() / 150) % 2 ? LOW : HIGH);
    delay(10);
  }
  digitalWrite(LED_BLUE, HIGH);

  Serial.println();
  Serial.println("=========================================");
  Serial.println("   MI RUTINA - DIAGNOSTICO DE ARRANQUE");
  Serial.println("=========================================");

  Serial.print("[1/3] Iniciando sensor IMU LSM6DS3... ");
  if (myIMU.begin() != 0) {
    Serial.println("FALLO");
    fatalError("No se detecta el IMU. Revisa que la placa elegida sea la version Sense.");
  }
  Serial.println("OK");

  Serial.print("[2/3] Iniciando Bluetooth... ");
  if (!BLE.begin()) {
    Serial.println("FALLO");
    fatalError("No se pudo iniciar BLE. Revisa que este instalada la libreria ArduinoBLE.");
  }
  Serial.println("OK");

  Serial.print("[3/3] Configurando servicio y anuncio BLE... ");
  BLE.setDeviceName("MiRutina");
  BLE.setLocalName("MiRutina");
  BLE.setAdvertisedService(routineService);
  routineService.addCharacteristic(rawImuChar);
  BLE.addService(routineService);

  ImuRawPacket initPacket = {0, 0, 0, 0, 0, 0};
  rawImuChar.writeValue((byte*)&initPacket, sizeof(initPacket));

  if (!BLE.advertise()) {
    Serial.println("FALLO");
    fatalError("No se pudo iniciar el anuncio BLE.");
  }
  Serial.println("OK");

  Serial.print("      Direccion MAC: ");
  Serial.println(BLE.address());
  Serial.println("      Nombre Bluetooth: MiRutina");
  Serial.println("=========================================");
  Serial.println(">> Listo. Esperando conexion desde la web...");
  Serial.println();

  digitalWrite(LED_BLUE, LOW); // Azul fijo = anunciando
}

void loop() {
  BLE.poll();
  BLEDevice central = BLE.central();
  bool connected = central && central.connected();

  // Avisar cambios de conexion
  if (connected && !wasConnected) {
    Serial.print(">> CONECTADO a: ");
    Serial.println(central.address());
    packetsSent = 0;
  } else if (!connected && wasConnected) {
    Serial.println(">> DESCONECTADO. Volviendo a anunciar...");
    BLE.advertise();
  }
  wasConnected = connected;

  // LEDs de estado
  digitalWrite(LED_BLUE, connected ? HIGH : LOW);
  digitalWrite(LED_GREEN, connected ? LOW : HIGH);

  unsigned long now = millis();

  // Enviar datos solo si hay conexion
  if (connected && now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;
    ImuRawPacket pkt;
    pkt.ax = (int16_t)(myIMU.readFloatAccelX() * 1000.0f);
    pkt.ay = (int16_t)(myIMU.readFloatAccelY() * 1000.0f);
    pkt.az = (int16_t)(myIMU.readFloatAccelZ() * 1000.0f);
    pkt.gx = (int16_t)(myIMU.readFloatGyroX());
    pkt.gy = (int16_t)(myIMU.readFloatGyroY());
    pkt.gz = (int16_t)(myIMU.readFloatGyroZ());
    rawImuChar.writeValue((byte*)&pkt, sizeof(pkt));
    packetsSent++;
  }

  // Latido por Serial cada 1 s (siempre, conectado o no)
  if (now - lastHeartbeat >= HEARTBEAT_MS) {
    lastHeartbeat = now;
    Serial.print("[");
    Serial.print(now / 1000);
    Serial.print(" s] BLE: ");
    Serial.print(connected ? "CONECTADO " : "anunciando");
    Serial.print(" | Acel (G): ");
    Serial.print(myIMU.readFloatAccelX(), 2); Serial.print(", ");
    Serial.print(myIMU.readFloatAccelY(), 2); Serial.print(", ");
    Serial.print(myIMU.readFloatAccelZ(), 2);
    if (connected) {
      Serial.print(" | Paquetes enviados: ");
      Serial.print(packetsSent);
    }
    Serial.println();
  }
}
