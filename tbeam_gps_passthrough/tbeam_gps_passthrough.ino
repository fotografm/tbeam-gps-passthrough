/*
 * tbeam_gps_passthrough.ino
 * ============================================================
 * T-Beam v1.2 — GPS NMEA USB Passthrough
 * ============================================================
 *
 * WHAT THIS DOES
 * --------------
 * Turns a TTGO T-Beam v1.2 into a simple USB GPS dongle.
 * It:
 *   1. Wakes up the onboard GPS module via the AXP2101 power IC
 *   2. Sends UBX binary commands to the GPS to enable all NMEA
 *      sentence types (including satellite signal bars / GSV)
 *   3. Relays every NMEA line from the GPS straight out the USB
 *      serial port at 115200 baud
 *
 * Connect the T-Beam over USB, open any serial monitor or GPS
 * application at 115200 baud, and you will see a stream of
 * standard NMEA-0183 sentences like:
 *
 *   $GPGGA,123456.00,5123.45678,N,00012.34567,W,1,08,...
 *   $GPGSV,3,1,12,01,72,045,42,03,55,190,41,...
 *   $GPRMC,123456.00,A,5123.45678,N,00012.34567,W,...
 *   ...
 *
 * HARDWARE
 * --------
 *   Board   : TTGO T-Beam v1.2
 *   GPS chip: u-blox M8030-KT (NEO-M8N compatible)
 *   PMIC    : AXP2101 (manages power rails including GPS supply)
 *   MCU     : ESP32-D0WD
 *
 * BOARD SELECTION IN ARDUINO IDE
 * --------------------------------
 *   Tools → Board → ESP32 Arduino → TTGO LoRa32-OLED v1
 *   (listed as "T1" in some board package versions)
 *
 *   If you don't see that board, add the ESP32 boards URL:
 *     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 *   Then install "esp32 by Espressif Systems" via Boards Manager.
 *
 * FLASH / UPLOAD SETTINGS
 * ------------------------
 *   Upload Speed : 921600
 *   Flash Mode   : DIO
 *   Flash Freq   : 80MHz
 *   Partition    : Default 4MB with spiffs
 *   Port         : your T-Beam's COM / /dev/ttyUSBx port
 *
 * AUTHORS / HISTORY
 * -----------------
 *   v1 — basic passthrough, GPS→USB and USB→GPS relay
 *   v2 — USB→GPS relay removed to prevent accidental GPS config
 *        corruption; GSV (satellite view) enabled on startup
 */

#include <Wire.h>          // I2C bus — used to talk to the AXP2101 PMIC
#include <HardwareSerial.h> // ESP32 UART — used for the GPS serial port


// ── Pin assignments ──────────────────────────────────────────────────────────
// These are fixed by the T-Beam v1.2 PCB layout.
// GPS UART: ESP32 UART1 mapped to these pins.
// I2C bus:  shared by AXP2101 (PMIC) and (if fitted) LoRa module.

#define GPS_RX_PIN  34   // ESP32 input  ← GPS TX
#define GPS_TX_PIN  12   // ESP32 output → GPS RX
#define I2C_SDA     21   // I2C data line (AXP2101, etc.)
#define I2C_SCL     22   // I2C clock line


// ── AXP2101 power management IC ─────────────────────────────────────────────
// The T-Beam v1.2 uses an AXP2101 PMIC to manage multiple voltage rails.
// The GPS module is powered from a rail called DLDO1 (DC-DC LDO output 1).
// By default DLDO1 is OFF at boot — we must enable it to power the GPS.
//
// AXP2101 I2C address (7-bit): 0x34

#define AXP2101_ADDR      0x34

// Register 0x99: DLDO1 output voltage selector
//   Value 0x1C → 3.3 V (the GPS module requires 3.3 V supply)
//   Formula:  voltage = 0.5V + (value * 0.1V), value range 0x00–0x1C = 0.5–3.3 V
#define AXP2101_DLDO1_VOL 0x99

// Register 0x9C: DLDO enable bits
//   Bit 0 = DLDO1 enable.  Read-modify-write to avoid touching other rails.
#define AXP2101_DLDO_EN   0x9C


// ── GPS serial port ──────────────────────────────────────────────────────────
// Default factory baud rate of the u-blox M8030 is 9600.
// We leave it at 9600 — no need to change it for NMEA passthrough.

#define GPS_BAUD   9600

// USB serial baud rate (Serial = ESP32 CDC over USB).
// 115200 is fast enough for all NMEA sentences with headroom.
#define USB_BAUD   115200

// UART1 is one of ESP32's three hardware UARTs.
// UART0 is the USB/programming port (Serial).
// UART1 is remapped to GPS_RX_PIN / GPS_TX_PIN in setup().
HardwareSerial gpsSerial(1);


// ════════════════════════════════════════════════════════════════════════════
// AXP2101 helpers — simple single-register read and write over I2C
// ════════════════════════════════════════════════════════════════════════════

void axpWrite(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(AXP2101_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t axpRead(uint8_t reg) {
    Wire.beginTransmission(AXP2101_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);        // repeated start — keep bus
    Wire.requestFrom((uint8_t)AXP2101_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0xFF;
}

/*
 * enableGPSPower()
 *
 * Sets DLDO1 to 3.3 V then enables it.
 * Always set the voltage BEFORE enabling — avoids a voltage glitch
 * on the GPS supply rail during the enable transition.
 */
void enableGPSPower() {
    axpWrite(AXP2101_DLDO1_VOL, 0x1C);                         // 3.3 V
    delay(20);                                                  // settle
    axpWrite(AXP2101_DLDO_EN, axpRead(AXP2101_DLDO_EN) | 0x01); // set bit 0
    delay(100);                                                 // GPS boot time
}


// ════════════════════════════════════════════════════════════════════════════
// UBX CFG-MSG — enable NMEA sentence types on the GPS module
// ════════════════════════════════════════════════════════════════════════════
//
// The u-blox M8030 speaks both NMEA-0183 and u-blox's proprietary UBX
// binary protocol on the same UART.  By default not all NMEA sentences
// are enabled.  We use UBX CFG-MSG (class 0x06, ID 0x01) to switch them on.
//
// UBX frame structure:
//   Byte 0-1 : sync chars   0xB5 0x62
//   Byte 2   : message class
//   Byte 3   : message ID
//   Byte 4-5 : payload length (little-endian)
//   Byte 6.. : payload
//   Last 2   : Fletcher-8 checksum (over class, ID, length, payload)
//
// CFG-MSG payload (8 bytes):
//   [0] message class to configure  (0xF0 = standard NMEA)
//   [1] message ID to configure     (see list below)
//   [2] rate on I/O port 0 (DDC/I2C)
//   [3] rate on I/O port 1 (UART1)  ← this is the GPS TX pin we read
//   [4] rate on I/O port 2 (UART2)
//   [5] rate on I/O port 3 (USB)
//   [6] rate on I/O port 4 (SPI)
//   [7] reserved
//
// Rate = N means "output this sentence every N navigation fixes".
// Rate = 0 disables the sentence on that port.
// Rate = 1 outputs every fix (the normal setting).
//
// NMEA class 0xF0 sentence IDs:
//   0x00 GGA  — time, position, fix quality, altitude, HDOP
//   0x01 GLL  — lat/lon, time, status
//   0x02 GSA  — active satellites, DOP values
//   0x03 GSV  — satellites in view with elevation, azimuth, SNR
//   0x04 RMC  — recommended minimum (position, speed, date, variation)
//   0x05 VTG  — course and speed over ground

void enableNMEA(uint8_t msgId) {
    // Build the 8-byte CFG-MSG payload.
    // Port 0 (DDC) = 0, Port 1 (UART1) = 1, all others = 0.
    uint8_t payload[8] = {
        0xF0,    // NMEA message class
        msgId,   // which NMEA sentence to configure
        0x00,    // DDC rate    — off
        0x01,    // UART1 rate  — every fix
        0x00,    // UART2 rate  — off
        0x00,    // USB rate    — off
        0x00,    // SPI rate    — off
        0x00     // reserved
    };

    // Fletcher-8 checksum.
    // Computed over: class(0x06), ID(0x01), len_lo(0x08), len_hi(0x00), payload[8]
    // NOT over sync chars (0xB5, 0x62).
    uint8_t ck_a = 0, ck_b = 0;
    uint8_t toCheck[] = {
        0x06, 0x01,       // class, message ID
        0x08, 0x00,       // payload length = 8, little-endian
        payload[0], payload[1], payload[2], payload[3],
        payload[4], payload[5], payload[6], payload[7]
    };
    for (size_t i = 0; i < sizeof(toCheck); i++) {
        ck_a += toCheck[i];
        ck_b += ck_a;
    }

    // Assemble and send the complete 16-byte UBX frame.
    uint8_t msg[16] = {
        0xB5, 0x62,       // UBX sync header
        0x06, 0x01,       // class=CFG, ID=MSG
        0x08, 0x00,       // payload length = 8
        payload[0], payload[1], payload[2], payload[3],
        payload[4], payload[5], payload[6], payload[7],
        ck_a, ck_b        // checksum
    };

    gpsSerial.write(msg, 16);
    gpsSerial.flush();   // wait until all bytes are transmitted
    delay(150);          // give the GPS time to process and ACK internally
}

/*
 * enableAllNMEA()
 *
 * Enables all six standard NMEA sentence types on UART1.
 * Call once after the GPS has powered up and the serial port is open.
 */
void enableAllNMEA() {
    enableNMEA(0x00);  // GGA — position fix, altitude, fix type, HDOP
    enableNMEA(0x01);  // GLL — latitude/longitude
    enableNMEA(0x02);  // GSA — dilution of precision, active satellite IDs
    enableNMEA(0x03);  // GSV — satellite count, elevation, azimuth, SNR (signal bars)
    enableNMEA(0x04);  // RMC — position, speed, course, date
    enableNMEA(0x05);  // VTG — true/magnetic track, speed in knots and km/h
}


// ════════════════════════════════════════════════════════════════════════════
// setup() — runs once at power-on or after reset
// ════════════════════════════════════════════════════════════════════════════

void setup() {
    // Start USB serial first so we can log progress messages.
    Serial.begin(USB_BAUD);
    delay(500);
    Serial.println("=== T-Beam GPS Passthrough v2 ===");
    Serial.println("Board: T-Beam v1.2 (AXP2101 PMIC)");

    // Bring up I2C bus for AXP2101 communication.
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(100);

    // Probe the AXP2101 before trying to write to it.
    Wire.beginTransmission(AXP2101_ADDR);
    if (Wire.endTransmission() == 0) {
        Serial.println("AXP2101 detected — enabling GPS power (DLDO1 = 3.3 V)...");
        enableGPSPower();
        Serial.println("GPS power rail on.");
    } else {
        // This would happen if the board revision uses a different PMIC
        // (e.g. T-Beam v1.1 uses AXP192 at 0x34 — but that's different registers).
        Serial.println("WARNING: AXP2101 not found at 0x34. GPS may not have power.");
    }

    // Open the hardware UART connected to the GPS module.
    // The GPS defaults to 9600 8N1 from the factory.
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    // Wait for the GPS to fully boot before sending UBX config commands.
    // The M8030 needs ~1 s after power-on to initialise its UART listener.
    delay(1000);

    Serial.println("Sending UBX CFG-MSG to enable: GGA GLL GSA GSV RMC VTG...");
    enableAllNMEA();
    Serial.println("NMEA configuration sent. Streaming NMEA to USB...");
    Serial.println("--- Open your GPS application at 115200 baud ---");
}


// ════════════════════════════════════════════════════════════════════════════
// loop() — runs continuously after setup()
// ════════════════════════════════════════════════════════════════════════════

void loop() {
    // Forward every byte from the GPS UART to USB serial.
    // NMEA sentences are plain ASCII lines ending with \r\n.
    while (gpsSerial.available()) {
        Serial.write(gpsSerial.read());
    }

    // NOTE: USB → GPS relay is intentionally NOT implemented.
    // If raw serial commands from the host were forwarded to the GPS,
    // a user accidentally sending characters in a terminal emulator
    // could corrupt the GPS configuration and break the baud rate or
    // disable NMEA output entirely (recoverable only by cold factory reset).
}
