#include <Arduino.h>
#include <Ethernet.h>
#include "myNetworkConfig.h"
#include "Parameter_Config.h"
#include "myShowMsg.h"

bool Network_DHCP_Init()
{
    ShowMsg("Starting DHCP...", true);

    if (Ethernet.begin(myPar.mac) == 0)
    {
        ShowMsg("DHCP Failed!", true);
        return false;
    }

    localIP = Ethernet.localIP();
    subnetMask = Ethernet.subnetMask();
    gatewayIP = Ethernet.gatewayIP();
    dnsServerIP = Ethernet.dnsServerIP();

    ShowMsg("DHCP Success!", true);
    ShowMsg("IP: " + String(localIP[0]) + "." + String(localIP[1]) + "." + String(localIP[2]) + "." + String(localIP[3]), true);
    ShowMsg("Mask: " + String(subnetMask[0]) + "." + String(subnetMask[1]) + "." + String(subnetMask[2]) + "." + String(subnetMask[3]), true);
    ShowMsg("GW: " + String(gatewayIP[0]) + "." + String(gatewayIP[1]) + "." + String(gatewayIP[2]) + "." + String(gatewayIP[3]), true);

    myPar.ip = localIP;

    return true;
}

bool Network_Static_Init()
{
    ShowMsg("Using Static IP...", true);

    Ethernet.begin(myPar.mac, myPar.ip);

    localIP = myPar.ip;
    subnetMask = Ethernet.subnetMask();
    gatewayIP = Ethernet.gatewayIP();

    ShowMsg("Static IP: " + String(localIP[0]) + "." + String(localIP[1]) + "." + String(localIP[2]) + "." + String(localIP[3]), true);

    return true;
}

bool Network_Init()
{
    ShowMsg("Network Initializing...", true);

    Ethernet.init(10);

    #if USE_DHCP
        uint32_t dhcpStartTime = millis();
        bool dhcpSuccess = false;

        while (millis() - dhcpStartTime < DHCP_TIMEOUT_MS)
        {
            if (Network_DHCP_Init())
            {
                dhcpSuccess = true;
                break;
            }
            ShowMsg("DHCP retry...", true);
            delay(DHCP_RETRY_DELAY_MS);
        }

        if (!dhcpSuccess)
        {
            ShowMsg("DHCP timeout, fallback to static IP", true);
            Network_Static_Init();
        }
    #else
        Network_Static_Init();
    #endif

    networkInitialized = true;
    ShowMsg("Network Initialized", true);
    return true;
}

void Network_Maintain()
{
    if (!networkInitialized) return;

    #if USE_DHCP
        switch (Ethernet.maintain())
        {
            case 1:
                ShowMsg("DHCP lease renewed", true);
                break;
            case 2:
                ShowMsg("DHCP lease rebind", true);
                break;
            case 3:
                ShowMsg("DHCP lease failed", true);
                break;
            default:
                break;
        }
    #endif
}
