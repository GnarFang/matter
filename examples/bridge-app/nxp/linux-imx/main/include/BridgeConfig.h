/*
 *    Copyright (c) 2023 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <AppMain.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <app-common/zap-generated/af-structs.h>
#include <app-common/zap-generated/attribute-id.h>
#include <app-common/zap-generated/cluster-id.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/EventLogging.h>
#include <app/chip-zcl-zpro-codec.h>
#include <app/reporting/reporting.h>
#include <app/util/af-types.h>
#include <app/util/af.h>
#include <app/util/attribute-storage.h>
#include <app/util/util.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/ZclString.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>
#include <platform/Linux/NetworkCommissioningDriver.h>

#include "CommissionableInit.h"
#include "Device.h"
#include <app/server/Server.h>

using namespace chip;
using namespace chip::app;
using namespace chip::Credentials;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

namespace {

#define  LIGHT_CLUSTER_LIST  bridgedLightClusters
// #define  TEMP_CLUSTER_LIST   bridgedTempSensorEndpoint


// Device types for dynamic endpoints: TODO Need a generated file from ZAP to define these!
// (taken from matter-devices.xml)
#define DEVICE_TYPE_BRIDGED_NODE 0x0013
// (taken from lo-devices.xml)
#define DEVICE_TYPE_LO_ON_OFF_LIGHT 0x0100
// (taken from matter-devices.xml)
#define DEVICE_TYPE_POWER_SOURCE 0x0011
// (taken from matter-devices.xml)
#define DEVICE_TYPE_TEMP_SENSOR 0x0302

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1



// REVISION DEFINITIONS:
// =================================================================================
#define ZCL_DESCRIPTOR_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION (1u)
#define ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP (0u)
#define ZCL_FIXED_LABEL_CLUSTER_REVISION (1u)
#define ZCL_ON_OFF_CLUSTER_REVISION (4u)
#define ZCL_TEMPERATURE_SENSOR_CLUSTER_REVISION (1u)
#define ZCL_TEMPERATURE_SENSOR_FEATURE_MAP (0u)
#define ZCL_POWER_SOURCE_CLUSTER_REVISION (1u)


// ---------------------------------------------------------------------------
//
// Bridge basic params 
const int kNodeLabelSize = 32;
const int kDescriptorAttributeArraySize = 254;

// ---------------------------------------------------------------------------
//
// LIGHT ENDPOINT: contains the following clusters:
//   - On/Off
//   - Descriptor
//   - Bridged Device Basic

// Declare On/Off cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ZCL_ON_OFF_ATTRIBUTE_ID, BOOLEAN, 1, 0), /* on/off */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ZCL_DEVICE_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0),     /* device list */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_SERVER_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0), /* server list */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_CLIENT_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0), /* client list */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_PARTS_LIST_ATTRIBUTE_ID, ARRAY, kDescriptorAttributeArraySize, 0),  /* parts list */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Bridged Device Basic information cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(bridgedDeviceBasicAttrs)
DECLARE_DYNAMIC_ATTRIBUTE(ZCL_NODE_LABEL_ATTRIBUTE_ID, CHAR_STRING, kNodeLabelSize, 0), /* NodeLabel */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_REACHABLE_ATTRIBUTE_ID, BOOLEAN, 1, 0),               /* Reachable */
    DECLARE_DYNAMIC_ATTRIBUTE(ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID, BITMAP32, 4, 0),     /* feature map */
    DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Cluster List for Bridged Light endpoint
// TODO: It's not clear whether it would be better to get the command lists from
// the ZAP config on our last fixed endpoint instead.
constexpr CommandId onOffIncomingCommands[] = {
    app::Clusters::OnOff::Commands::Off::Id,
    app::Clusters::OnOff::Commands::On::Id,
    app::Clusters::OnOff::Commands::Toggle::Id,
    app::Clusters::OnOff::Commands::OffWithEffect::Id,
    app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id,
    app::Clusters::OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(LIGHT_CLUSTER_LIST)
DECLARE_DYNAMIC_CLUSTER(ZCL_ON_OFF_CLUSTER_ID, onOffAttrs, onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_DESCRIPTOR_CLUSTER_ID, descriptorAttrs, nullptr, nullptr),
    DECLARE_DYNAMIC_CLUSTER(ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, bridgedDeviceBasicAttrs, nullptr,
                            nullptr) DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Declare Bridged Light endpoint
DECLARE_DYNAMIC_ENDPOINT(bridgedLightEndpoint, LIGHT_CLUSTER_LIST);
const EmberAfDeviceType gBridgedOnOffDeviceTypes[] = { { DEVICE_TYPE_LO_ON_OFF_LIGHT, DEVICE_VERSION_DEFAULT },
                                                       { DEVICE_TYPE_BRIDGED_NODE, DEVICE_VERSION_DEFAULT } };

typedef struct {
    DeviceOnOff OnOffDev;
    DataVersion DataVersions[ArraySize(LIGHT_CLUSTER_LIST)];
} Light_t;


// // ---------------------------------------------------------------------------
// //
// // TEMPERATURE SENSOR ENDPOINT: contains the following clusters:
// //   - Temperature measurement
// //   - Descriptor
// //   - Bridged Device Basic
// DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(tempSensorAttrs)
// DECLARE_DYNAMIC_ATTRIBUTE(ZCL_TEMP_MEASURED_VALUE_ATTRIBUTE_ID, INT16S, 2, 0),         /* Measured Value */
//     DECLARE_DYNAMIC_ATTRIBUTE(ZCL_TEMP_MIN_MEASURED_VALUE_ATTRIBUTE_ID, INT16S, 2, 0), /* Min Measured Value */
//     DECLARE_DYNAMIC_ATTRIBUTE(ZCL_TEMP_MAX_MEASURED_VALUE_ATTRIBUTE_ID, INT16S, 2, 0), /* Max Measured Value */
//     DECLARE_DYNAMIC_ATTRIBUTE(ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID, BITMAP32, 4, 0),    /* FeatureMap */
//     DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(TEMP_CLUSTER_LIST)
// DECLARE_DYNAMIC_CLUSTER(ZCL_TEMP_MEASUREMENT_CLUSTER_ID, tempSensorAttrs, nullptr, nullptr),
//     DECLARE_DYNAMIC_CLUSTER(ZCL_DESCRIPTOR_CLUSTER_ID, descriptorAttrs, nullptr, nullptr),
//     DECLARE_DYNAMIC_CLUSTER(ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID, bridgedDeviceBasicAttrs, nullptr, nullptr),
//     DECLARE_DYNAMIC_CLUSTER_LIST_END;

// // Declare Bridged Light endpoint
// DECLARE_DYNAMIC_ENDPOINT(bridgedTempSensorEndpoint, TEMP_CLUSTER_LIST);

// typedef struct {
//     DeviceTempSensor *TempSensorDev;
//     DataVersion DataVersions[ArraySize(TEMP_CLUSTER_LIST)];
// } TempSensor;

} // namespace


