// Copyright 2018 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#if 0
#include "lwip/pbuf.h"
#include "lwip/ethip6.h"
#include "netif/etharp.h"
#include "esp_libc.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"


int8_t ieee80211_output_pbuf(uint8_t fd, uint8_t* dataptr, uint16_t datalen);
int8_t wifi_get_netif(uint8_t fd);
void wifi_station_set_default_hostname(uint8_t* hwaddr);

/**
 * In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif* netif)
{
    if (netif == NULL) {
        TCPIP_ATAPTER_LOG("ERROR netif is NULL\n");
        return;
    }

    /* set MAC hardware address length */
    netif->hwaddr_len = ETHARP_HWADDR_LEN;

    /* maximum transfer unit */
    netif->mtu = 1500;

    /* device capabilities */
    /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

#if LWIP_IGMP
    netif->flags |= NETIF_FLAG_IGMP;
#endif
    /* Do whatever else is needed to initialize interface. */
}

/**
 * This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an int8_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become availale since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static int8_t low_level_output(struct netif* netif, struct pbuf* p)
{
    int8_t err = ERR_OK;

    if (netif == NULL) {
        TCPIP_ATAPTER_LOG("ERROR netif is NULL\n");
        return ERR_ARG;
    }

#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif

    uint8_t* outputbuf = (uint8_t*)os_malloc(p->len + 36);
    if (outputbuf == NULL) {
        TCPIP_ATAPTER_LOG("ERROR no memory\n");
        return ERR_MEM;
    }

    outputbuf += 36;
    memcpy(outputbuf, p->payload, p->len);

    if (netif == esp_netif[TCPIP_ADAPTER_IF_STA]) {
        err = ieee80211_output_pbuf(TCPIP_ADAPTER_IF_STA, outputbuf, p->len);
    } else {
        err = ieee80211_output_pbuf(TCPIP_ADAPTER_IF_AP, outputbuf, p->len);
    }

    if (err == ERR_MEM) {
        err = ERR_OK;
    }

//  signal that packet should be sent();

#if ETH_PAD_SIZE
    pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif

#if LWIP_STATS
    LINK_STATS_INC(link.xmit);
#endif
    return err;
}

/**
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
void ethernetif_input(struct netif* netif, struct pbuf* p)
{
    struct eth_hdr* ethhdr;

    if (p == NULL) {
        TCPIP_ATAPTER_LOG("ERROR pbuf is NULL\n");
        goto _exit;
    }

    if (p->payload == NULL) {
        TCPIP_ATAPTER_LOG("ERROR payload is NULL\n");
        pbuf_free(p);
        goto _exit;
    }

    if (netif == NULL) {
        TCPIP_ATAPTER_LOG("ERROR netif is NULL\n");
        goto _exit;
    }

    if (!(netif->flags & NETIF_FLAG_LINK_UP)) {
        TCPIP_ATAPTER_LOG("ERROR netif is not up\n");
        pbuf_free(p);
        p = NULL;
        goto _exit;
    }

    /* points to packet payload, which starts with an Ethernet header */
    ethhdr = p->payload;

    switch (htons(ethhdr->type)) {
        /* IP or ARP packet? */
        case ETHTYPE_IP:
        case ETHTYPE_IPV6:
        case ETHTYPE_ARP:
#if PPPOE_SUPPORT

        /* PPPoE packet? */
        case ETHTYPE_PPPOEDISC:
        case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */

            /* full packet send to tcpip_thread to process */
            if (netif->input(p, netif) != ERR_OK) {
                TCPIP_ATAPTER_LOG("ERROR IP input error\n");
                pbuf_free(p);
                p = NULL;
            }

            break;

        default:
            pbuf_free(p);
            p = NULL;
            break;
    }

_exit:
    ;
}

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other int8_t on error
 */
int8_t ethernetif_init(struct netif* netif)
{
    uint8_t mac[NETIF_MAX_HWADDR_LEN];

    if (netif == NULL) {
        TCPIP_ATAPTER_LOG("ERROR netif is NULL\n");
    }

    /* set MAC hardware address */
    if (wifi_get_netif(TCPIP_ADAPTER_IF_STA) == TCPIP_ADAPTER_IF_STA) {
        wifi_get_macaddr(TCPIP_ADAPTER_IF_STA, mac);
    } else {
        wifi_get_macaddr(TCPIP_ADAPTER_IF_AP, mac);
    }

    memcpy(netif->hwaddr, mac, NETIF_MAX_HWADDR_LEN);

#if LWIP_NETIF_HOSTNAME

    if (wifi_get_netif(TCPIP_ADAPTER_IF_STA) == TCPIP_ADAPTER_IF_STA) {
        if (default_hostname == 1) {
            wifi_station_set_default_hostname(netif->hwaddr);
        }

        /* Initialize interface hostname */
        netif->hostname = hostname;
    } else {
        netif->hostname = NULL;
    }

#endif /* LWIP_NETIF_HOSTNAME */

    /*
     * Initialize the snmp variables and counters inside the struct netif.
     * The last argument should be replaced with your link speed, in units
     * of bits per second.
     */
    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;
    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    netif->output = etharp_output;
#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */
    netif->linkoutput = low_level_output;

    extern void wifi_station_dhcpc_event(void);
    netif->dhcp_event = wifi_station_dhcpc_event;

    /* initialize the hardware */
    low_level_init(netif);

    return ERR_OK;
}

#endif