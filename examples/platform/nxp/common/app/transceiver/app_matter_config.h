/*
 *  Copyright 2020-2022 NXP
 *  All rights reserved.
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */


#define USB_HOST_CONFIG_EHCI 2
#define CONTROLLER_ID        kUSB_ControllerEhci0


#if defined(WIFI_IW416_BOARD_AW_AM457_USD) || defined(WIFI_IW416_BOARD_AW_AM510_USD) || defined(WIFI_88W8987_BOARD_AW_CM358_USD) || defined(WIFI_88W8801_BOARD_MURATA_2DS_USD)
#define WIFI_TRANSCEIVER_SUPPORT 1
#else
#define WIFI_TRANSCEIVER_SUPPORT 0
#endif

/* 
* Check if the transceiver is supported.
* Applies only for Matter over Wi-fi or Matter over Thread
*/
#if (CHIP_DEVICE_CONFIG_ENABLE_WPA || CHIP_DEVICE_CONFIG_ENABLE_THREAD)
#if !WIFI_TRANSCEIVER_SUPPORT && !defined(K32W061_TRANSCEIVER)
#error The transceiver module is unsupported
#endif
#endif /* CHIP_DEVICE_CONFIG_ENABLE_WPA || CHIP_DEVICE_CONFIG_ENABLE_THREAD */

#if WIFI_TRANSCEIVER_SUPPORT
#define NOT_DEFINE_DEFAULT_WIFI_MODULE
/* app_config.h comes from: <sdk>/components/wifi_bt_module/template/wifi_config.h */
#include "app_config.h"
#endif /* WIFI_TRANSCEIVER_SUPPORT */

#if CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE
#include "edgefast_bluetooth_config.h"
#endif /* CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE */
