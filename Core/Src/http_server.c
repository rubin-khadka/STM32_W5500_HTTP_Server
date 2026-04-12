/*
 * http_server.c
 *
 *  Created on: Apr 12, 2026
 *  Author: Rubin Khadka
 */

#include "http_server.h"
#include "socket.h"
#include "usart1.h"
#include "gpio.h"
#include "dht11.h"
#include "ds3231.h"
#include <stdio.h>
#include <string.h>

// Global variables
SensorData_t g_sensor_data = {0, 0, 0};
uint8_t g_led_state = 0;

// Buffers
static uint8_t rx_buffer[HTTP_BUFFER_SIZE];
static uint8_t tx_buffer[HTTP_BUFFER_SIZE];

// RTC time cache
static DS3231_Time_t g_rtc_time;

// HTML Page
static const char index_page[] =
    "<!DOCTYPE html>"
        "<html>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<title>STM32 Environmental Monitor</title>"
        "<style>"
        "*{margin:0;padding:0;box-sizing:border-box;}"
        "body{font-family:'Segoe UI',Arial;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:20px;}"
        ".container{max-width:600px;margin:0 auto;}"
        ".card{background:white;border-radius:15px;padding:25px;margin-bottom:20px;box-shadow:0 10px 30px rgba(0,0,0,0.2);}"
        ".time-card{text-align:center;background:linear-gradient(135deg,#1e3c72,#2a5298);color:white;}"
        ".time{font-size:48px;font-weight:bold;font-family:'Courier New',monospace;}"
        ".date{font-size:24px;margin-top:10px;}"
        ".sensor-row{display:flex;justify-content:space-around;margin:20px 0;}"
        ".sensor-item{text-align:center;}"
        ".sensor-value{font-size:36px;font-weight:bold;}"
        ".temp-value{color:#e94560;}"
        ".hum-value{color:#4CAF50;}"
        ".led-control{text-align:center;}"
        ".led-btn{padding:15px 30px;margin:10px;font-size:18px;border:none;border-radius:10px;cursor:pointer;transition:transform 0.2s;}"
        ".led-btn:active{transform:scale(0.95);}"
        ".btn-on{background:#4CAF50;color:white;}"
        ".btn-off{background:#f44336;color:white;}"
        ".led-status{margin-top:15px;padding:10px;background:#f0f0f0;border-radius:8px;}"
        ".status-ok{color:#4CAF50;}"
        ".status-error{color:#f44336;}"
        ".refresh-time{font-size:12px;color:#999;margin-top:20px;text-align:center;}"
        "</style>"
        "</head>"
        "<body>"
        "<div class='container'>"
        "<div class='card time-card'>"
        "<div class='time' id='time'>--:--:--</div>"
        "<div class='date' id='date'>----/--/--</div>"
        "</div>"

        "<div class='card'>"
        "<h3 style='text-align:center;'>🌡️ Environmental Data</h3>"
        "<div class='sensor-row'>"
        "<div class='sensor-item'>"
        "<div class='sensor-value temp-value' id='temperature'>--°C</div>"
        "<div class='sensor-label'>Temperature</div>"
        "</div>"
        "<div class='sensor-item'>"
        "<div class='sensor-value hum-value' id='humidity'>--%</div>"
        "<div class='sensor-label'>Humidity</div>"
        "</div>"
        "</div>"
        "<div id='sensorStatus' style='text-align:center;font-size:12px;'></div>"
        "</div>"

        "<div class='card'>"
        "<h3 style='text-align:center;'>💡 LED Control</h3>"
        "<div class='led-control'>"
        "<button class='led-btn btn-on' onclick='setLED(1)'>🔴 LED ON</button>"
        "<button class='led-btn btn-off' onclick='setLED(0)'>⚫ LED OFF</button>"
        "</div>"
        "<div class='led-status' id='ledStatus'>LED Status: Unknown</div>"
        "</div>"

        "<div class='refresh-time'>Data auto-refreshes every 3 seconds</div>"
        "</div>"

        "<script>"
        "function updateData(){"
        "    fetch('/api/data')"
        "        .then(response => response.json())"
        "        .then(data => {"
        "            document.getElementById('temperature').innerHTML = data.temp + '°C';"
        "            document.getElementById('humidity').innerHTML = data.hum + '%';"
        "            document.getElementById('time').innerHTML = data.time;"
        "            document.getElementById('date').innerHTML = data.date;"
        "            if(data.sensor_valid) {"
        "                document.getElementById('sensorStatus').innerHTML = '<span class=\"status-ok\">✓ Sensor OK</span>';"
        "            } else {"
        "                document.getElementById('sensorStatus').innerHTML = '<span class=\"status-error\">✗ Sensor Error</span>';"
        "            }"
        "        })"
        "        .catch(error => {"
        "            console.log('Error:', error);"
        "            document.getElementById('sensorStatus').innerHTML = '<span class=\"status-error\">✗ Connection Error</span>';"
        "        });"
        "}"
        "function setLED(state){"
        "    let url = state ? '/api/led/on' : '/api/led/off';"
        "    fetch(url)"
        "        .then(response => response.json())"
        "        .then(data => {"
        "            document.getElementById('ledStatus').innerHTML = 'LED Status: ' + (data.state ? 'ON' : 'OFF');"
        "        });"
        "}"
        "function getLEDStatus(){"
        "    fetch('/api/led/status')"
        "        .then(response => response.json())"
        "        .then(data => {"
        "            document.getElementById('ledStatus').innerHTML = 'LED Status: ' + (data.state ? 'ON' : 'OFF');"
        "        });"
        "}"
        "// Initial load"
        "updateData();"
        "getLEDStatus();"
        "// Auto refresh every 3 seconds"
        "setInterval(updateData, 3000);"
        "</script>"
        "</body>"
        "</html>";

// Function to read DHT11 sensor using your existing driver
static void ReadDHT11Sensor(void)
{
  uint8_t bytes[5] = {0};

  // Start communication (using your existing functions)
  DHT11_Start();

  // Check response
  if(!DHT11_Check_Response())
  {
    g_sensor_data.valid = 0;
    return;
  }

  // Read 40 bits (5 bytes)
  for(int i = 0; i < 5; i++)
  {
    bytes[i] = DHT11_Read();
  }

  // Verify checksum
  if((bytes[0] + bytes[1] + bytes[2] + bytes[3]) == bytes[4])
  {
    g_sensor_data.humidity = bytes[0];
    g_sensor_data.temperature = bytes[2];
    g_sensor_data.valid = 1;

    char msg[50];
    sprintf(msg, "DHT11: %.0f°C, %.0f%%\r\n", g_sensor_data.temperature, g_sensor_data.humidity);
    USART1_SendString(msg);
  }
  else
  {
    g_sensor_data.valid = 0;
    USART1_SendString("DHT11 Checksum error!\r\n");
  }
}

// Function to read RTC time using your DS3231 driver
static void ReadRTCTime(void)
{
  if(DS3231_GetTime(&g_rtc_time) != DS3231_OK)
  {
    USART1_SendString("RTC read error!\r\n");
  }
}

// Send HTTP response with JSON body
static void SendJSONResponse(uint8_t socket, char *json, int status_code)
{
  const char *status_text = (status_code == 200) ? "OK" : "Bad Request";
  int len;

  len = snprintf((char*) tx_buffer, HTTP_BUFFER_SIZE, "HTTP/1.1 %d %s\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %d\r\n"
      "Connection: close\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "\r\n"
      "%s", status_code, status_text, (int) strlen(json), json);

  send(socket, tx_buffer, len);
}

// Send HTML page
static void SendHTMLPage(uint8_t socket)
{
  int len = snprintf((char*) tx_buffer, HTTP_BUFFER_SIZE, "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/html\r\n"
      "Content-Length: %d\r\n"
      "Connection: close\r\n"
      "\r\n"
      "%s", (int) strlen(index_page), index_page);

  send(socket, tx_buffer, len);
}

// Handle /api/data request
static void HandleGetData(uint8_t socket)
{
  char time_str[12];    // Was 10, now 12 for safety
  char date_str[15];    // Was 12, now 15 for safety
  int full_year;

  // Read DHT11 sensor
  ReadDHT11Sensor();

  // Read RTC time from DS3231
  ReadRTCTime();

  // Format time
  full_year = 2000 + g_rtc_time.year;

  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", g_rtc_time.hour, g_rtc_time.minutes, g_rtc_time.seconds);
  snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d", full_year, g_rtc_time.month, g_rtc_time.dayofmonth);

  snprintf(
      (char*) tx_buffer,
      HTTP_BUFFER_SIZE,
      "{\"temp\":%.1f,\"hum\":%.1f,\"time\":\"%s\",\"date\":\"%s\",\"sensor_valid\":%d}",
      g_sensor_data.temperature,
      g_sensor_data.humidity,
      time_str,
      date_str,
      g_sensor_data.valid);

  SendJSONResponse(socket, (char*) tx_buffer, 200);
}

// Handle LED control
static void HandleLEDControl(uint8_t socket, uint8_t state)
{
  g_led_state = state;
  if(state)
  {
    LED_ON();
  }
  else
  {
    LED_OFF();
  }

  sprintf((char*) tx_buffer, "{\"state\":%d}", g_led_state);
  SendJSONResponse(socket, (char*) tx_buffer, 200);
}

// Handle LED status
static void HandleLEDStatus(uint8_t socket)
{
  sprintf((char*) tx_buffer, "{\"state\":%d}", g_led_state);
  SendJSONResponse(socket, (char*) tx_buffer, 200);
}

// Parse incoming HTTP request
static void ParseRequest(uint8_t *buffer, char **method, char **path)
{
  char *line = strtok((char*) buffer, "\r\n");
  if(line)
  {
    *method = strtok(line, " ");
    *path = strtok(NULL, " ");
  }
}

// Initialize HTTP server
void HTTP_Server_Init(void)
{
  // Initialize LED
  LED_init();
  LED_OFF();
  g_led_state = 0;

  // Initialize DHT11
  DHT11_Init();
  USART1_SendString("DHT11 initialized\r\n");

  // Initialize DS3231 RTC
  if(DS3231_Init() != DS3231_OK)
  {
    USART1_SendString("DS3231 initialization failed!\r\n");
  }
  else
  {
    USART1_SendString("DS3231 RTC initialized\r\n");
  }

  // Create socket
  if(socket(HTTP_SOCKET, Sn_MR_TCP, HTTP_PORT, 0) != HTTP_SOCKET)
  {
    USART1_SendString("HTTP Socket failed\r\n");
    return;
  }

  // Start listening
  if(listen(HTTP_SOCKET) != SOCK_OK)
  {
    USART1_SendString("HTTP Listen failed\r\n");
    close(HTTP_SOCKET);
    return;
  }

  USART1_SendString("HTTP Server started on port ");
  USART1_SendNumber(HTTP_PORT);
  USART1_SendString("\r\n");
  USART1_SendString("Open browser and go to: http://192.168.1.10\r\n");
}

// Run HTTP server
void HTTP_Server_Run(void)
{
  uint8_t status = getSn_SR(HTTP_SOCKET);
  char *method, *path;

  switch(status)
  {
    case SOCK_ESTABLISHED:
    {
      uint16_t len = getSn_RX_RSR(HTTP_SOCKET);
      if(len > 0)
      {
        int32_t ret = recv(HTTP_SOCKET, rx_buffer, (len < HTTP_BUFFER_SIZE) ? len : HTTP_BUFFER_SIZE - 1);
        if(ret > 0)
        {
          rx_buffer[ret] = '\0';
          ParseRequest(rx_buffer, &method, &path);

          if(strcmp(method, "GET") == 0)
          {
            if(strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0)
            {
              SendHTMLPage(HTTP_SOCKET);
            }
            else if(strcmp(path, API_DATA) == 0)
            {
              HandleGetData(HTTP_SOCKET);
            }
            else if(strcmp(path, API_LED_STATUS) == 0)
            {
              HandleLEDStatus(HTTP_SOCKET);
            }
            else if(strcmp(path, API_LED_ON) == 0)
            {
              HandleLEDControl(HTTP_SOCKET, 1);
            }
            else if(strcmp(path, API_LED_OFF) == 0)
            {
              HandleLEDControl(HTTP_SOCKET, 0);
            }
            else
            {
              char *not_found = "{\"error\":\"Not Found\"}";
              SendJSONResponse(HTTP_SOCKET, not_found, 404);
            }
          }

          disconnect(HTTP_SOCKET);
        }
      }
      break;
    }

    case SOCK_CLOSE_WAIT:
      disconnect(HTTP_SOCKET);
      break;

    case SOCK_CLOSED:
      socket(HTTP_SOCKET, Sn_MR_TCP, HTTP_PORT, 0);
      listen(HTTP_SOCKET);
      break;

    default:
      break;
  }
}

// Function to update sensor data
void HTTP_UpdateSensorData(float temp, float hum, uint8_t valid)
{
  g_sensor_data.temperature = temp;
  g_sensor_data.humidity = hum;
  g_sensor_data.valid = valid;
}
