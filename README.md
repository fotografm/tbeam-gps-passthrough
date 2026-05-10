# T-Beam GPS Passthrough

Turns a **TTGO T-Beam v1.2** into a simple USB GPS dongle. Connect it over USB, open any serial monitor or GPS application at **115200 baud**, and receive a clean stream of NMEA-0183 sentences from the onboard u-blox M8030 GPS module.

No drivers, no configuration files, no cloud — just NMEA out of a serial port.

---

## Hardware

| Part | Detail |
|------|--------|
| Board | TTGO T-Beam v1.2 |
| GPS module | u-blox M8030-KT (NEO-M8N compatible) |
| PMIC | AXP2101 (manages GPS power rail DLDO1) |
| MCU | ESP32-D0WD |
| Connection | USB-C or Micro-USB depending on your T-Beam variant |

> **T-Beam version check:** Look for the AXP2101 IC on the underside of the board. If you have an AXP192 instead, you have a v1.1 — the register addresses differ and this sketch will not power the GPS correctly. The AXP2101 is on all v1.2 boards.

---

## NMEA output

After flashing, the device outputs all six standard NMEA sentence types at 1 Hz:

| Sentence | Contains |
|----------|----------|
| `$GPGGA` | Position, altitude, fix type, HDOP, satellite count |
| `$GPGLL` | Latitude / longitude |
| `$GPGSA` | Active satellite PRNs, DOP values |
| `$GPGSV` | Satellites in view — elevation, azimuth, SNR (signal bars) |
| `$GPRMC` | Position, speed, course, date |
| `$GPVTG` | Track and speed over ground |

---

## Flashing

There are two methods. The **web flasher** (Option A) requires no software installation.

### Option A — Web flasher (easiest, Chrome/Edge only)

The web flasher can erase and flash the device directly from your browser using WebSerial.

1. **Download the pre-built binary**  
   Go to the [Releases](../../releases) page and download `merged.bin`.

2. **Open the web flasher**  
   Navigate to [esptool.spacehuhn.com](https://esptool.spacehuhn.com) in Chrome or Edge.

3. **Erase flash first**  
   - Click **Connect** and select your T-Beam's serial port.  
   - Click **Erase** and wait for "Done".  
   - This clears any previous firmware (Meshtastic, TTGO stock, etc.) completely.

4. **Flash the firmware**  
   - Click **Choose File** and select `merged.bin`.  
   - Set **Flash address** to `0x0` (the merged binary includes the bootloader).  
   - Click **Program** and wait for "Done" (~30 seconds).

5. **Done.** Disconnect and reconnect the USB cable. The device resets automatically.

---

### Option B — esptool.py (command line)

Suitable for Linux, macOS, and Windows with Python installed.

#### Prerequisites

```bash
pip install esptool
```

#### Find your serial port

- **Linux:** `ls /dev/ttyUSB*` or `ls /dev/ttyACM*`
- **macOS:** `ls /dev/cu.usbserial-*`
- **Windows:** Check Device Manager → Ports (COM & LPT)

#### Step 1 — Erase flash

```bash
esptool.py --port /dev/ttyUSB0 --baud 921600 erase_flash
```

Replace `/dev/ttyUSB0` with your actual port. This wipes the entire 4 MB flash — essential if the board previously ran Meshtastic or any other firmware that may have left conflicting bootloader or NVS data.

#### Step 2 — Flash the firmware

```bash
esptool.py --port /dev/ttyUSB0 --baud 921600 write_flash 0x0 merged.bin
```

The `merged.bin` (from [Releases](../../releases)) is a single file containing the bootloader, partition table, and application, pre-merged at offset 0x0. You do not need to flash multiple files at separate offsets.

Expected output:
```
esptool.py v4.x.x
...
Writing at 0x00000000... (100 %)
Leaving...
Hard resetting via RTS pin...
```

---

### Option C — arduino-cli (build from source, no IDE required)

Use this to compile and produce `merged.bin` entirely from the command line.

#### 1. Install arduino-cli

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/.local/bin sh
```

Verify:
```bash
arduino-cli version
```

#### 2. Add ESP32 board support and install the core

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

The core download is ~250 MB — it installs once into `~/.arduino15`.

#### 3. Compile and export

```bash
arduino-cli compile \
  --fqbn esp32:esp32:t-beam:UploadSpeed=921600,FlashFreq=80 \
  --export-binaries \
  --output-dir ./build \
  ./tbeam_gps_passthrough
```

On success you will see something like:
```
Sketch uses 307236 bytes (23%) of program storage space.
Global variables use 23540 bytes (7%) of dynamic memory.
```

The `build/` folder will contain:

| File | Contents | Flash offset |
|------|----------|--------------|
| `tbeam_gps_passthrough.ino.bootloader.bin` | Bootloader only | 0x1000 |
| `tbeam_gps_passthrough.ino.partitions.bin` | Partition table | 0x8000 |
| `tbeam_gps_passthrough.ino.bin` | Application only | 0x10000 |
| `tbeam_gps_passthrough.ino.merged.bin` | **All three merged** | **0x0** |

Use `tbeam_gps_passthrough.ino.merged.bin` with the web flasher (Option A) or esptool (Option B) at offset `0x0`.

#### 4. Flash directly via arduino-cli (optional)

If the T-Beam is plugged in and you want to compile and flash in one step:

```bash
arduino-cli compile --upload \
  --fqbn esp32:esp32:t-beam:UploadSpeed=921600,FlashFreq=80 \
  --port /dev/ttyUSB0 \
  ./tbeam_gps_passthrough
```

---

### Option D — Arduino IDE (build from source)

#### 1. Install Arduino IDE 2.x

Download from [arduino.cc](https://www.arduino.cc/en/software).

#### 2. Add ESP32 board support

1. Open **File → Preferences**.
2. In **Additional Boards Manager URLs** paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Open **Tools → Board → Boards Manager**, search **esp32**, install **"esp32 by Espressif Systems"** (version 2.x or 3.x).

#### 3. Select the correct board

```
Tools → Board → ESP32 Arduino → T-Beam
```

#### 4. Set upload options

```
Tools → Upload Speed   → 921600
Tools → Flash Mode     → DIO
Tools → Flash Frequency → 80MHz
Tools → Partition Scheme → Default 4MB with spiffs
Tools → Port          → (your T-Beam port)
```

#### 5. Open and upload the sketch

Open `tbeam_gps_passthrough/tbeam_gps_passthrough.ino`, then click **Upload** (→ arrow).

#### 6. Export a merged.bin for the web flasher or esptool

Arduino IDE can export a single self-contained binary that includes the bootloader, partition table, and application merged at offset 0x0 — exactly what Options A and B need.

1. With the sketch open and the board/settings configured as above, click:
   ```
   Sketch → Export Compiled Binary
   ```
   Arduino IDE 2.x compiles the sketch and writes the output files into a `build/` subfolder inside the sketch directory.

2. Navigate to that build folder. The path will be something like:
   ```
   tbeam_gps_passthrough/build/esp32.esp32.t-beam/
   ```
   Inside you will find several `.bin` files:

   | File | Contents | Flash offset |
   |------|----------|--------------|
   | `tbeam_gps_passthrough.ino.bootloader.bin` | Bootloader only | 0x1000 |
   | `tbeam_gps_passthrough.ino.partitions.bin` | Partition table | 0x8000 |
   | `tbeam_gps_passthrough.ino.bin` | Application only | 0x10000 |
   | `tbeam_gps_passthrough.ino.merged.bin` | **All three merged** | **0x0** |

3. Use `tbeam_gps_passthrough.ino.merged.bin` (rename it to `merged.bin` for clarity).  
   This is the file you upload to the GitHub Release and flash at offset `0x0`.

> **Arduino IDE 1.x:** The merged binary is not produced automatically. Use the Sketch → Export Compiled Binary option and then manually merge with esptool:
> ```bash
> esptool.py merge_bin \
>   --output merged.bin \
>   0x1000  tbeam_gps_passthrough.ino.bootloader.bin \
>   0x8000  tbeam_gps_passthrough.ino.partitions.bin \
>   0x10000 tbeam_gps_passthrough.ino.bin
> ```

---

## Verifying it works

After flashing, open a serial monitor at **115200 baud** (Arduino IDE serial monitor, `screen`, PuTTY, etc.).

You should see immediately:
```
=== T-Beam GPS Passthrough v2 ===
Board: T-Beam v1.2 (AXP2101 PMIC)
AXP2101 detected — enabling GPS power (DLDO1 = 3.3 V)...
GPS power rail on.
Sending UBX CFG-MSG to enable: GGA GLL GSA GSV RMC VTG...
NMEA configuration sent. Streaming NMEA to USB...
--- Open your GPS application at 115200 baud ---
```

Followed within a few seconds by raw NMEA lines:
```
$GPGGA,123456.00,5123.45678,N,00012.34567,W,0,00,99.99,,,,,,0000*6E
$GPGSV,1,1,00*79
$GPRMC,123456.00,V,,,,,,,100526,,,N*76
```

A `V` (void) in the RMC sentence and `0` fix quality in GGA is normal until the GPS acquires a satellite fix — typically 30–90 seconds outdoors with clear sky view.

### Quick serial monitor commands

```bash
# Linux / macOS — raw serial output
screen /dev/ttyUSB0 115200

# or with minicom
minicom -D /dev/ttyUSB0 -b 115200

# exit screen: Ctrl-A then K
```

---

## Using with GPS software

The device enumerates as a standard serial port. Point any NMEA-capable application at it:

| Application | Platform | Notes |
|-------------|----------|-------|
| GPSd | Linux | `gpsd /dev/ttyUSB0 -F /var/run/gpsd.sock` |
| u-center | Windows | u-blox's own GPS evaluation tool |
| Viking | Linux/macOS | GPS data viewer / track logger |
| JOSM | cross-platform | OpenStreetMap editor with live GPS |
| Xastir | Linux | APRS client (reads NMEA directly) |

For gpsd on Linux, set the baud rate in `/etc/default/gpsd`:
```
DEVICES="/dev/ttyUSB0"
GPSD_OPTIONS="-n"
```

---

## Troubleshooting

**No serial port appears when plugged in**  
The T-Beam uses a CP210x or CH340 USB-serial chip depending on the batch. Install the relevant driver:
- CP210x: [Silicon Labs driver](https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers)
- CH340: [WCH CH340 driver](http://www.wch-ic.com/downloads/CH341SER_EXE.html)

**Sketch uploads but no NMEA output**  
Check the baud rate is set to 115200. The startup messages appear immediately; if you see only garbage, the baud rate is wrong.

**`AXP2101 not found` in the startup log**  
Verify you have a v1.2 board (AXP2101 PMIC). A v1.1 board (AXP192) needs different register addresses — this sketch will not work on it without modification.

**GPS fix never arrives**  
Normal indoors or under heavy tree cover. Take the device outside with open sky. First fix (cold start) can take 1–3 minutes. Subsequent fixes are faster once the almanac is cached.

**`Permission denied: /dev/ttyUSB0`** (Linux)  
Add your user to the `dialout` group:
```bash
sudo usermod -aG dialout $USER
# log out and back in for the change to take effect
```

---

## License

MIT — see [LICENSE](LICENSE).
