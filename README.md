# STM32 HTTP Webserver using W5500

## Project Overview
This project implements a complete HTTP web server on an STM32 Blue Pill (STM32F103C8T6) using the W5500 Ethernet module. The server hosts a modern web dashboard that displays real-time environmental data from a DHT11 sensor and DS3231 RTC, while also providing remote LED control through a REST API.

**Key Features:**
- HTTP web server on port 80 (accessible via any web browser)
- REST API endpoints for sensor data and LED control
- Real-time temperature and humidity monitoring (DHT11)
- Real-time clock and date display (DS3231)
- Remote LED control with instant feedback
- AJAX-powered auto-refresh (every 3 seconds)

## Video Demonstrations

### Hardware Configuration and LED Control

https://github.com/user-attachments/assets/e983dc5c-8117-4929-9182-9529ad646817

Shows hardware setup and LED control via web interface

### HTTP Webserver

https://github.com/user-attachments/assets/9199d5c9-36e3-4880-b31e-08776d452f63

Demonstrates web dashboard with real-time sensor data and LED control

### Webserver Outlook

<img width="1278" height="952" alt="webserver_outlook" src="https://github.com/user-attachments/assets/8b03d476-b102-49a7-bae0-3c39b8003984" />

## Project Schematic

## Pin Configuration
| Peripheral | Pin | Connection | Notes |
|------------|-----|------------|-------|
| **Wiznet W5500** | PA5 | SCK | SPI1 Clock |
| | PA6 | MISO | SPI1 Master In Slave Out |
| | PA7 | MOSI | SPI1 Master Out Slave In |
| | PA4 | CS | Chip Select |
| | PA3 | Reset | Reset Pin |
| | 3.3V | VCC | Power |
| | GND | GND | Common ground |
| **DHT11** | PB0 | Data | Module contain pull-up resistor |
| | 5V | VCC | Power |
| | GND | GND | Common ground |
| **DS3231 RTC** | PB10 (SCL) | SCL | I2C2 Clock |
| | PB11 (SDA) | SDA | I2C2 Data |
| | 3.3V | VCC | Power |
| | GND | GND | Common ground |
| **LED** | PC13 | Anode | With current-limiting resistor |
| | GND | Cathode | Common ground |
| **Push Button** | PA0 | GPIO Input | Button |
| | GND | GND | Common ground |
| **USART1(Debug)** | PA9 | TX to USB-Serial RX | 115200 baud, 8-N-1 |
| | PA10 | RX to USB-Serial TX | Optional for commands |

## HTTP Server Architecture
### Protocol Stack Implementation
The server implements a full HTTP/1.1-compliant stack on top of the W5500's hardware TCP/IP offload engine.

| Layer | Protocol | Implementation |
|-------|----------|----------------|
| Application | HTTP | Custom request parser + router |
| Transport | TCP | W5500 hardware (RFC 793 compliant) |
| Network | IP | W5500 hardware with ARP support |
| Data Link | Ethernet | W5500 integrated MAC/PHY (10/100BASE-T) |

### Request-Response Lifecycle
1. Socket Establishment
- W5500 maintains socket 0 in `SOCK_LISTEN` state on port 80
- `listen()` initiates passive TCP open
- On SYN arrival, state transitions: `LISTEN → SYNRECV → ESTABLISHED`

2. HTTP Request Parsing
- Raw TCP payload extracted via `recv()`
- Stateless parser scans for CRLF `(\r\n)` delimiters
- Extracts: HTTP method (GET only), request URI, protocol version
- Query parameters and headers are parsed but unused (reserved for future)

3. Routing Logic
- Path string compared against registered endpoints
- Uses `strcmp()` for exact matching (no regex to save flash)
- Dispatch to handler functions via conditional branching

### API Endpoints
| Endpoint | Method | Response | Description |
|----------|--------|----------|-------------|
| `/` or `/index.html` | GET | HTML page | Web dashboard |
| `/api/data` | GET | JSON | Temperature, humidity, time, date |
| `/api/led/on` | GET | JSON | Turn LED ON |
| `/api/led/off` | GET | JSON | Turn LED OFF |
| `/api/led/status` | GET | JSON | Get current LED state |

## DHT11 Sensor Driver

The DHT11 uses a **single-wire protocol** with precise timing:

| Phase | Duration | Description |
|-------|----------|-------------|
| **Start Signal** | 18ms LOW + 20µs HIGH | MCU wakes sensor |
| **Sensor Response** | 80µs LOW + 80µs HIGH | Sensor acknowledges |
| **Bit "0"** | 50µs LOW + 26-28µs HIGH | Logic 0 |
| **Bit "1"** | 50µs LOW + 70µs HIGH | Logic 1 |
| **Data Frame** | 40 bits | 5 bytes (humidity ×2 + temp ×2 + checksum) |

Instead of measuring pulse width, I used a **simpler approach** looking at datasheet:

For each bit:
1. Wait for line to go HIGH
2. Delay exactly 40µs
3. If line still HIGH → logic 1 <br>
   If line is LOW → logic 0

To ensure the timing is not interrupted, **interrupts are disabled** while communicating with the sensor. The checksum provided by the sensor is used to verify data integrity.

## DS3231 RTC Driver

The DS3231 is a **precision Real-Time Clock (RTC)** with an integrated temperature-compensated crystal oscillator and **I2C interface**.
The driver provides two ways to access RTC data:
1. **Time/Date Data**: Current time (hours, minutes, seconds) and date (day, month, year)
2. **Temperature Data**: Built-in temperature sensor reading (±3°C accuracy)

**Implementation:**
- **Burst read** of all 7 time registers in a single I2C transaction
- **BCD conversion** handles register format automatically
- **Data Flow:** I2C Read (0x00-0x06) → 7 bytes → Convert BCD → Update time structure
- **Oscillator monitoring** detects power failures via status register

### Output Values

| Measurement | Range | Resolution | Format |
|-------------|-------|------------|--------|
| **Time** | 00:00:00 - 23:59:59 | 1 second | HH:MM:SS |
| **Date** | 01/01/00 - 31/12/99 | 1 day | DD/MM/YY |

> **Note**: LCD display shows **formatted time/date strings** and temperature in physical units. Raw BCD values are automatically converted using internal helper functions. The RTC maintains accurate time even when main power is off using a CR2032 backup battery.

## Related Projects 
- [STM32_W5500_TCP_Server (Non-RTOS version)](https://github.com/rubin-khadka/STM32_W5500_TCP_Server)
- [STM32_FreeRTOS_W5500_TCP_Server](https://github.com/rubin-khadka/STM32_FreeRTOS_W5500_TCP_Server)
- [STM32_W5500_TCP_Client](https://github.com/rubin-khadka/STM32_W5500_TCP_Client)

## Resources
- [STM32F103 Datasheet](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf)
- [STM32F103 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0008-stm32f101xx-stm32f102xx-stm32f103xx-stm32f105xx-and-stm32f107xx-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)
- [WIZNET W5500 Datasheet](https://cdn.sparkfun.com/datasheets/Dev/Arduino/Shields/W5500_datasheet_v1.0.2_1.pdf)
- [DHT11 Sensor Datasheet](https://www.mouser.com/datasheet/2/758/DHT11-Technical-Data-Sheet-Translated-Version-1143054.pdf)
- [RTC DS3231 Datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf)

## Project Status
- **Status**: Complete
- **Version**: v1.0
- **Last Updated**: April 2026

## Contact
**Rubin Khadka Chhetri**  
📧 rubinkhadka84@gmail.com <br>
🐙 GitHub: https://github.com/rubin-khadka