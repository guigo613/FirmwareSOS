#ifndef __ETHERNET___H
#define __ETHERNET___H 

/**
 * Lib C
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/**
 * FreeRTOS
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

/**
 * Logs;
 */
#include "esp_system.h"
#include "esp_log.h"

/**
 * Callbacks 
 */
#include "esp_event_loop.h"
#include "esp_event.h"

/**
 * Ethernet lib
 */
#include "esp_eth.h"

#include "esp_netif.h"

/**
 * Drivers;
 */
#include "rom/gpio.h"
#include "driver/gpio.h"
#include "driver/periph_ctrl.h"
#include "driver/spi_master.h"

/**
 * NVS
 */
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#define SIP_URI     CONFIG_SIP_URI


void ethernet_init(void); 
void ethernet_stop(void);

#endif //<-- __ETHERNET___H -->