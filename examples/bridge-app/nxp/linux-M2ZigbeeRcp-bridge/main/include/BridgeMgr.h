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
#include <app/util/MatterCallbacks.h>

#include "Device.h"
#include "ZigbeeRcpImpl.h"
#include "ZigbeeRcpMessage.h"

class BridgeDevMgr
{
public:
    BridgeDevMgr();
    virtual ~BridgeDevMgr();

    void init();
    void start();
    void RemoveAllDevice();

    static void HandleZigbeeDeviceStatusChanged(Device * dev, Device::Changed_t itemChangedMask);
    static Protocols::InteractionModel::Status HandleSendCommand(Device * dev);
    static Protocols::InteractionModel::Status HandleReadAttribute(Device * dev, ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);
    static Protocols::InteractionModel::Status HandleWriteAttribute(Device * dev, ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer);

    static Device* gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
    //chip::BridgeCallbacks gBridgeMatterCallbacks;
    
private:
    static void* ZigbeeRcpComm_handler(void *context);

    int AddNewZigbeeDevice(m2z_device_params_t* zigbee_node);
    int AddNewZigbeeEndpoint(m2z_device_params_t* zigbee_node, uint8_t ep_index, const std::string & name,
                             const std::string & location,
                             chip::EndpointId preferredMatterEndpointId = chip::kInvalidEndpointId,
                             bool markReachable = true);

    // Persist/restore the bridged device table so that the Matter endpoints
    void SaveBridgedDevices();
    void RestoreBridgedDevices();
    bool EndpointIdInUse(chip::EndpointId endpointId);
    chip::EndpointId AllocateEndpointId();

    void RemoveDevice(Device *dev);
    int  start_ZigbeeRcp_comm_thread();

    // Register Matter dynamic endpoint for bridged Zigbee device, attr read via external callback on demand
    int SetDynamicEndpoint(Device * dev, EmberAfEndpointType *ep, EmberAfDeviceType *deviceTypeList,
                                    DataVersion * dataVersionStorage, chip::EndpointId parentEndpointId,
                                    chip::EndpointId preferredEndpointId = chip::kInvalidEndpointId,
                                    bool markReachable = true);

    int ClearDynamicEndpoint(Device * dev);

    BridgeDevMgr (const BridgeDevMgr&) = delete;
    BridgeDevMgr &operator=(const BridgeDevMgr&) = delete;

protected:
    EndpointId mFirstDynamicEndpointId;
};

