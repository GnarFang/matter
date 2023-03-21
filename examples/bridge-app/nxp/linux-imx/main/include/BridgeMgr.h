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

#include "BridgeConfig.h"

class BridgeDevMgr
{
public:
    BridgeDevMgr();
    virtual ~BridgeDevMgr();

    void start();
    static void HandleDeviceOnOffStatusChanged(DeviceOnOff * dev, DeviceOnOff::Changed_t itemChangedMask);

    friend EmberAfStatus emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                   const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
                                                   uint16_t maxReadLength);
    friend EmberAfStatus emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                    const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer);
private:
    static void* ZcbMonitor(void *context);

    void AddOnOffNode(zbdev_t *ZigbeeDev);
    void DestoryOnOffNode(DeviceOnOff* dev);
    void MapZcbNodes();
    void AddNewZcbNode(int saddr);
    int  AddZcbNode(zbdev_t* ZigbeeDev);
    int  start_threads();
    
    int AddDeviceEndpoint(Device * dev, EmberAfEndpointType * ep, const Span<const EmberAfDeviceType> & deviceTypeList,
                      const Span<DataVersion> & dataVersionStorage, chip::EndpointId parentEndpointId);

    int RemoveDeviceEndpoint(Device * dev);


    BridgeDevMgr (const BridgeDevMgr&) = delete;
    BridgeDevMgr &operator=(const BridgeDevMgr&) = delete;

protected:
    static Device* gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
    zbdev_t* ZigbeeDevList[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
    
    EndpointId mCurrentEndpointId;
    EndpointId mFirstDynamicEndpointId;
    pthread_t ZcbMonitor_thread;

    int thread_exit;
};
