/*
 * wizchip_port.c
 *
 *  Created on: Apr 8, 2026
 *      Author: Rubin Khadka
 */

#include "main.h"
#include "wizchip_conf.h"
#include "string.h"
#include "socket.h"
#include "usart1.h"
#include "dhcp.h"
#include "dns.h"
#include <stdbool.h>

// W5500 Initialization Return Codes
typedef enum
{
  W5500_OK = 0,
  W5500_ERR_INIT = -1,
  W5500_ERR_COMM = -2,
  W5500_ERR_LINK_DOWN = 3
} W5500_Status_t;

#define W5500_SPI hspi1

// Network configuration with DHCP support
wiz_NetInfo netInfo =
    {.mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01}, .ip = {192, 168, 1, 10}, .sn = {255, 255, 255, 0}, .gw =
        {192, 168, 1, 1}, .dns = {8, 8, 8, 8}, .dhcp = NETINFO_DHCP    // Use DHCP by default
    };

#define W5500_CS_LOW()     HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_RESET)
#define W5500_CS_HIGH()    HAL_GPIO_WritePin(CS_GPIO_Port, CS_Pin, GPIO_PIN_SET)
#define W5500_RST_LOW()    HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_RESET)
#define W5500_RST_HIGH()   HAL_GPIO_WritePin(RESET_GPIO_Port, RESET_Pin, GPIO_PIN_SET)

// DHCP variables
#define DHCP_SOCKET    7      // Socket for DHCP
#define DNS_SOCKET     6      // Socket for DNS
static bool ip_assigned = false;
static uint8_t dhcp_buffer[548];
static uint8_t dns_buffer[512];

extern SPI_HandleTypeDef W5500_SPI;

// DHCP callback functions
void Callback_IPAssigned(void)
{
  ip_assigned = true;
  USART1_SendString("DHCP: IP Assigned!\r\n");
}

void Callback_IPConflict(void)
{
  ip_assigned = false;
  USART1_SendString("DHCP: IP Conflict detected!\r\n");
}

// SPI transmit/receive
void W5500_Select(void)
{
  W5500_CS_LOW();
}

void W5500_Unselect(void)
{
  W5500_CS_HIGH();
}

uint8_t W5500_ReadByte(void)
{
  uint8_t rx;
  uint8_t tx = 0xFF;
  HAL_SPI_TransmitReceive(&W5500_SPI, &tx, &rx, 1, HAL_MAX_DELAY);
  return rx;
}

void W5500_WriteByte(uint8_t byte)
{
  HAL_SPI_Transmit(&W5500_SPI, &byte, 1, HAL_MAX_DELAY);
}

int W5500_Init(void)
{
  uint8_t memsize[2][8] = { {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 2, 2, 2, 2, 2}};
  uint8_t retries;

  /***** Reset Sequence  *****/
  W5500_RST_LOW();
  HAL_Delay(50);
  W5500_RST_HIGH();
  HAL_Delay(200);

  /***** Register callback  *****/
  reg_wizchip_cs_cbfunc(W5500_Select, W5500_Unselect);
  reg_wizchip_spi_cbfunc(W5500_ReadByte, W5500_WriteByte);

  /***** Initialize the chip  *****/
  if(ctlwizchip(CW_INIT_WIZCHIP, (void*) memsize) == -1)
  {
    USART1_SendString("Error while initializing WIZCHIP\r\n");
    return W5500_ERR_INIT;
  }
  USART1_SendString("WIZCHIP Initialized\r\n");

  /***** Check communication by reading Version  *****/
  uint8_t ver = getVERSIONR();
  if(ver != 0x04)
  {
    USART1_SendString("Error Communicating with W5500 Version: ");
    USART1_SendHex(ver);
    USART1_SendString("\r\n");
    return W5500_ERR_COMM;
  }
  USART1_SendString("Checking Link Status..\r\n");

  /***** Check Link Status  *****/
  uint8_t link = PHY_LINK_OFF;
  retries = 10;
  while((link != PHY_LINK_ON) && (retries > 0))
  {
    ctlwizchip(CW_GET_PHYLINK, &link);
    if(link == PHY_LINK_ON)
      USART1_SendString("Link: UP\r\n");
    else
    {
      USART1_SendString("Link: DOWN Retrying: ");
      USART1_SendNumber(10 - retries);
      USART1_SendString("\r\n");
    }
    retries--;
    HAL_Delay(500);
  }
  if(link != PHY_LINK_ON)
  {
    USART1_SendString("Link is Down, please reconnect and retry\r\n");
    USART1_SendString("Exiting Setup..\r\n");
    return W5500_ERR_LINK_DOWN;
  }

  /***** Try DHCP first  *****/
  USART1_SendString("Attempting DHCP...\r\n");

  // Set MAC address
  setSHAR(netInfo.mac);

  // Initialize DHCP
  DHCP_init(DHCP_SOCKET, dhcp_buffer);

  // Register DHCP callbacks
  reg_dhcp_cbfunc(Callback_IPAssigned, Callback_IPAssigned, Callback_IPConflict);

  // Wait for DHCP to get IP (max 10 seconds)
  ip_assigned = false;
  retries = 20;  // 20 × 500ms = 10 seconds
  while((!ip_assigned) && (retries > 0))
  {
    DHCP_run();
    HAL_Delay(500);
    retries--;

    // Show progress every 2 seconds
    if(retries % 4 == 0)
    {
      USART1_SendString("DHCP: Waiting for IP...\r\n");
    }
  }

  if(ip_assigned)
  {
    // DHCP Success - Get the assigned network info
    USART1_SendString("DHCP: IP assigned successfully!\r\n");

    getIPfromDHCP(netInfo.ip);
    getGWfromDHCP(netInfo.gw);
    getSNfromDHCP(netInfo.sn);
    getDNSfromDHCP(netInfo.dns);

    USART1_SendString("Using DHCP Configuration\r\n");
  }
  else
  {
    // DHCP Failed - Use static IP as fallback
    USART1_SendString("DHCP Failed! Using Static IP as fallback...\r\n");
    // Keep the static IP already defined in netInfo
  }

  // Apply network configuration to W5500
  ctlnetwork(CN_SET_NETINFO, (void*) &netInfo);

  /***** Configure DNS  *****/
  USART1_SendString("Configuring DNS...\r\n");
  DNS_init(DNS_SOCKET, dns_buffer);

  /***** Print assigned IP on the console  *****/
  wiz_NetInfo tmpInfo;
  ctlnetwork(CN_GET_NETINFO, &tmpInfo);

  USART1_SendString("\r\n=== Network Configuration ===\r\n");
  USART1_SendString("IP: ");
  USART1_SendNumber(tmpInfo.ip[0]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.ip[1]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.ip[2]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.ip[3]);
  USART1_SendString("\r\n");

  USART1_SendString("SUBNET: ");
  USART1_SendNumber(tmpInfo.sn[0]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.sn[1]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.sn[2]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.sn[3]);
  USART1_SendString("\r\n");

  USART1_SendString("GATEWAY: ");
  USART1_SendNumber(tmpInfo.gw[0]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.gw[1]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.gw[2]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.gw[3]);
  USART1_SendString("\r\n");

  USART1_SendString("DNS: ");
  USART1_SendNumber(tmpInfo.dns[0]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.dns[1]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.dns[2]);
  USART1_SendString(".");
  USART1_SendNumber(tmpInfo.dns[3]);
  USART1_SendString("\r\n");
  USART1_SendString("===========================\r\n");

  return W5500_OK;
}
