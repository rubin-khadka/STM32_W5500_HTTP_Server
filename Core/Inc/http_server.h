/*
 * http_server.h
 *
 *  Created on: Apr 12, 2026
 *      Author: Rubin Khadka
 */

#ifndef INC_HTTP_SERVER_H_
#define INC_HTTP_SERVER_H_

#include <stdint.h>
#include <stdbool.h>

// HTTP Server Configuration
#define HTTP_SOCKET         0
#define HTTP_PORT           80
#define HTTP_BUFFER_SIZE    5000

// API Endpoints
#define API_DATA            "/api/data"
#define API_LED_ON          "/api/led/on"
#define API_LED_OFF         "/api/led/off"
#define API_LED_STATUS      "/api/led/status"

// Function Prototypes
void HTTP_Server_Init(void);
void HTTP_Server_Run(void);

// Sensor data structure
typedef struct
{
  float temperature;
  float humidity;
  uint8_t valid;
} SensorData_t;

// Global data
extern SensorData_t g_sensor_data;
extern uint8_t g_led_state;

#endif /* INC_HTTP_SERVER_H_ */
