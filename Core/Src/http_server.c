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

const char index_page[] =
    "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>STM32</title><style>"
        "body{background:#1a1a2e;color:#fff;text-align:center;font-family:Arial;padding:20px}"
        ".c{background:#16213e;padding:20px;border-radius:15px;max-width:400px;margin:20px auto}"
        ".v{font-size:48px;font-weight:bold}"
        ".t{color:#e94560}.h{color:#4CAF50}.tm{font-size:32px}"
        "button{padding:15px 30px;margin:10px;font-size:18px;border:none;border-radius:10px;cursor:pointer}"
        ".on{background:#4CAF50}.off{background:#f44336}"
        "</style></head><body>"
        "<div class='c'><div class='tm' id='tm'>--:--:--</div><div id='dt'>----/--/--</div></div>"
        "<div class='c'><h3>🌡️ ENVIRONMENT MONITOR</h3><div class='v t' id='tp'>--°C</div><div>Temperature</div><div class='v h' id='hm'>--%</div><div>Humidity</div></div>"
        "<div class='c'><h3>💡 LED CONTROL</h3><button class='on' onclick='l(1)'>ON</button><button class='off' onclick='l(0)'>OFF</button><div id='ls'>LED: --</div></div>"
        "<script>"
        "function u(){fetch('/api/data').then(r=>r.json()).then(d=>{"
        "document.getElementById('tp').innerHTML=d.temp+'°C';"
        "document.getElementById('hm').innerHTML=d.hum+'%';"
        "document.getElementById('tm').innerHTML=d.time;"
        "document.getElementById('dt').innerHTML=d.date;"
        "})}"
        "function l(s){fetch(s?'/api/led/on':'/api/led/off').then(r=>r.json()).then(d=>{"
        "document.getElementById('ls').innerHTML='LED: '+(d.state?'ON':'OFF');})}"
        "function g(){fetch('/api/led/status').then(r=>r.json()).then(d=>{"
        "document.getElementById('ls').innerHTML='LED: '+(d.state?'ON':'OFF');})}"
        "u();g();setInterval(u,3000);"
        "</script></body></html>";

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
  }
  else
  {
    g_sensor_data.valid = 0;
    USART1_SendString("DHT11 Checksum error!\r\n");
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
      "\r\n"
      "%s", status_code, status_text, (int) strlen(json), json);

  send(socket, tx_buffer, len);
}

// Handle LED status
static void HandleLEDStatus(uint8_t socket)
{
  char json_buffer[30];
  snprintf(json_buffer, sizeof(json_buffer), "{\"state\":%d}", g_led_state);
  SendJSONResponse(socket, json_buffer, 200);
}

// Function to read RTC time using your DS3231 driver
static void ReadRTCTime(void)
{
  if(DS3231_GetTime(&g_rtc_time) != DS3231_OK)
  {
    USART1_SendString("RTC read error!\r\n");
  }
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
  char json_buffer[300];
  char time_str[12];
  char date_str[15];
  int full_year;

  // Read DHT11 sensor
  ReadDHT11Sensor();

  // Read RTC time from DS3231
  ReadRTCTime();

  // Format time and date
  full_year = 2000 + g_rtc_time.year;
  snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", g_rtc_time.hour, g_rtc_time.minutes, g_rtc_time.seconds);
  snprintf(date_str, sizeof(date_str), "%04d/%02d/%02d", full_year, g_rtc_time.month, g_rtc_time.dayofmonth);

  // Create JSON string with all data
  snprintf(
      json_buffer,
      sizeof(json_buffer),
      "{\"temp\":%.1f,\"hum\":%.1f,\"time\":\"%s\",\"date\":\"%s\",\"sensor_valid\":%d}",
      g_sensor_data.temperature,
      g_sensor_data.humidity,
      time_str,
      date_str,
      g_sensor_data.valid);

  // Send the JSON response
  SendJSONResponse(socket, json_buffer, 200);
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

// Handle LED control
static void HandleLEDControl(uint8_t socket, uint8_t state)
{
  g_led_state = state;
  if(state)
  {
    LED_ON();
    USART1_SendString("LED turned ON\r\n");
  }
  else
  {
    LED_OFF();
    USART1_SendString("LED turned OFF\r\n");
  }

  char json_buffer[30];
  snprintf(json_buffer, sizeof(json_buffer), "{\"state\":%d}", g_led_state);
  SendJSONResponse(socket, json_buffer, 200);
}

// Initialize HTTP server
void HTTP_Server_Init(void)
{
  // Initialize LED
  LED_init();
  LED_OFF();
  g_led_state = 0;
  USART1_SendString("LED initialized\r\n");

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
            // ADD THESE THREE LINES:
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
