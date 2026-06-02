#ifndef _my_NETWORK_CONFIG_H_
#define _my_NETWORK_CONFIG_H_

#include <Arduino.h>
#include <Ethernet.h>
#include "myShowMsg.h"
#include "Parameter_Config.h"

/* 网络配置选项 */
#define USE_DHCP 1  // 1=使用DHCP, 0=使用静态IP

/* DHCP超时时间 */
#define DHCP_TIMEOUT_MS 15000
#define DHCP_RETRY_DELAY_MS 5000

/* 网络初始化状态 - 定义在globals.cpp中 */
extern volatile bool networkInitialized;
extern IPAddress localIP;
extern IPAddress subnetMask;
extern IPAddress gatewayIP;
extern IPAddress dnsServerIP;

/* 函数声明 */
bool Network_DHCP_Init();
bool Network_Static_Init();
bool Network_Init();
void Network_Maintain();

#endif
