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

#include "BridgeMgr.h"
#include "ZcbMessage.h"

// define global variable 
ZcbMsg_t ZcbMsg = {
    .AnnounceStart = false,
    .HandleMask = true,
    .bridge_mutex = PTHREAD_MUTEX_INITIALIZER,
    .bridge_cond =  PTHREAD_COND_INITIALIZER,
    .saddr = 0,
    .msg_type = BRIDGE_UNKNOW,
};

Device* BridgeDevMgr::gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
int ZcbNodesNum = 0;

BridgeDevMgr::BridgeDevMgr() 
{
    thread_exit = 0;
}


BridgeDevMgr::~BridgeDevMgr() 
{
    thread_exit = 1;

    for (int i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++ ) {
        if ( gDevices[i] != NULL) { 
            if ( gDevices[i]->GetZB()->zcb->uSupportedClusters.sClusterBitmap.hasOnOff ) {
                DestoryOnOffNode(static_cast<DeviceOnOff *>(gDevices[i]));
            }
        }
    }

    newDbfreeZigbeeDeviceList(ZigbeeDevList, ZcbNodesNum);
    printf("free BridgeDevMgr ! \n");
}


void BridgeDevMgr::AddOnOffNode(zbdev_t *ZBNode)
{
    std::string onoff_label = "Zigbee Light ";
    Light_t *gLith = new Light_t;

    gLith->OnOffDev.Init((onoff_label + std::to_string(ZBNode->zcb->id)).c_str(), "Office");
    gLith->OnOffDev.SetZBdev(ZBNode);
    gLith->OnOffDev.SetChangeCallback(HandleDeviceOnOffStatusChanged);

    AddDeviceEndpoint(&gLith->OnOffDev, &bridgedLightEndpoint,
                Span<const EmberAfDeviceType>(gBridgedOnOffDeviceTypes),
                Span<DataVersion>(gLith->DataVersions), 1);
}

void BridgeDevMgr::DestoryOnOffNode(DeviceOnOff* dev)
{
    Light_t* light = (Light_t*)dev;
    delete light;
    light = NULL;
}

void BridgeDevMgr::AddNewZcbNode(int saddr)
{
    newdb_zcb_t zcb;
    newdb_dev_t dev;
    zbdev_t ZigbeeDevice;

    ZigbeeDevice.zcb = &zcb;
    ZigbeeDevice.dev = &dev;

    newDbGetZcbSaddr( saddr, ZigbeeDevice.zcb );
    newDbGetDevice( zcb.mac, ZigbeeDevice.dev );

    AddZcbNode(&ZigbeeDevice);
}

int BridgeDevMgr::AddZcbNode(zbdev_t* ZigbeeDev)
{
    if (ZigbeeDev->zcb->uSupportedClusters.sClusterBitmap.hasOnOff) {
        AddOnOffNode(ZigbeeDev);
        return 1;
    }

    return 0;
}

void BridgeDevMgr::MapZcbNodes()
{
    newDbGetZigbeeDeviceList(ZigbeeDevList, CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT, &ZcbNodesNum);

    for(int i = 0; i < ZcbNodesNum; i++ ) {
        printf("device mac %s \n", ZigbeeDevList[i]->dev->mac);
        AddZcbNode(ZigbeeDevList[i]);
    }

}

void BridgeDevMgr::start()
{
    mFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);
    mCurrentEndpointId = mFirstDynamicEndpointId;
    MapZcbNodes();
    
    // start monitor
    start_threads();
}


int BridgeDevMgr::start_threads()
{
    int res = pthread_create(&ZcbMonitor_thread, nullptr, ZcbMonitor, (void*)this);
    if (res)
    {
        printf("Error creating polling thread: %d\n", res);
        return -1;
    }

    return 0;
}

void* BridgeDevMgr::ZcbMonitor(void *context)
{
    BridgeDevMgr *ThisMgr = (BridgeDevMgr *)context;
    while( 1 )
    {
        pthread_mutex_lock(&ZcbMsg.bridge_mutex);
        while(ZcbMsg.HandleMask) {
            if (pthread_cond_wait(&ZcbMsg.bridge_cond, &ZcbMsg.bridge_mutex) == 0) {                
                switch (ZcbMsg.msg_type)
                {
                    case BRIDGE_ADD_DEV: {
                        ThisMgr->AddNewZcbNode(ZcbMsg.saddr);
                        ZcbMsg.saddr = 0;
                        ZcbMsg.msg_type = BRIDGE_UNKNOW;
                        }
                        break;
                
                    default:
                        break;
                }
            }

            if (ThisMgr->thread_exit)
                return NULL;
        }

        ZcbMsg.HandleMask = true;
        pthread_mutex_unlock(&ZcbMsg.bridge_mutex);
    }
}

// -----------------------------------------------------------------------------------------
// Device Management
// -----------------------------------------------------------------------------------------

int BridgeDevMgr::AddDeviceEndpoint(Device * dev, EmberAfEndpointType * ep, const Span<const EmberAfDeviceType> & deviceTypeList,
                      const Span<DataVersion> & dataVersionStorage, chip::EndpointId parentEndpointId = chip::kInvalidEndpointId)
{
    uint8_t index = 0;

    if (dev->IsReachable() == true)
    {
        ChipLogProgress(DeviceLayer, "The endpoints has been added!");
        return -1;
    }

    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (nullptr == gDevices[index] )
        {
            gDevices[index] = dev;
            EmberAfStatus ret;
            while (1)
            {
                // Todo: Update this to schedule the work rather than use this lock
                DeviceLayer::StackLock lock;
                dev->SetEndpointId(mCurrentEndpointId);
                dev->SetParentEndpointId(parentEndpointId);
                ret =
                    emberAfSetDynamicEndpoint(index, mCurrentEndpointId, ep, dataVersionStorage, deviceTypeList, parentEndpointId);
                if (ret == EMBER_ZCL_STATUS_SUCCESS)
                {
                    dev->SetReachable(true);
                    ChipLogProgress(DeviceLayer, "Added device %s Saddr %x to dynamic endpoint %d (index=%d)", dev->GetName(),
                                    dev->GetZBSaddr(), mCurrentEndpointId, index);
                    return index;
                }
                if (ret != EMBER_ZCL_STATUS_DUPLICATE_EXISTS)
                {
                    return -1;
                }
                // Handle wrap condition
                if (++mCurrentEndpointId < mFirstDynamicEndpointId)
                {
                    mCurrentEndpointId = mFirstDynamicEndpointId;
                }
            }
        }
        index++;
    }
    ChipLogProgress(DeviceLayer, "Failed to add dynamic endpoint: No endpoints available!");
    return -1;
}

int BridgeDevMgr::RemoveDeviceEndpoint(Device * dev)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (gDevices[index] == dev)
        {
            // Todo: Update this to schedule the work rather than use this lock
            DeviceLayer::StackLock lock;
            EndpointId ep   = emberAfClearDynamicEndpoint(index);
            gDevices[index] = nullptr;
            ChipLogProgress(DeviceLayer, "Removed device %s from dynamic endpoint %d (index=%d)", dev->GetName(), ep, index);
            // Silence complaints about unused ep when progress logging
            // disabled.
            UNUSED_VAR(ep);
            return index;
        }
        index++;
    }
    return -1;
}

// -----------------------------------------------------------------------------------------
// Device callback
// -----------------------------------------------------------------------------------------
void CallReportingCallback(intptr_t closure)
{
    auto path = reinterpret_cast<app::ConcreteAttributePath *>(closure);
    MatterReportingAttributeChangeCallback(*path);
    Platform::Delete(path);
}

void ScheduleReportingCallback(Device * dev, ClusterId cluster, AttributeId attribute)
{
    auto * path = Platform::New<app::ConcreteAttributePath>(dev->GetEndpointId(), cluster, attribute);
    PlatformMgr().ScheduleWork(CallReportingCallback, reinterpret_cast<intptr_t>(path));
}

void HandleDeviceStatusChanged(Device * dev, Device::Changed_t itemChangedMask)
{
    if (itemChangedMask & Device::kChanged_Reachable)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasic::Id, BridgedDeviceBasic::Attributes::Reachable::Id);
    }

    if (itemChangedMask & Device::kChanged_Name)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasic::Id, BridgedDeviceBasic::Attributes::NodeLabel::Id);
    }
}

EmberAfStatus HandleReadBridgedDeviceBasicAttribute(Device * dev, chip::AttributeId attributeId, uint8_t * buffer,
                                                    uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadBridgedDeviceBasicAttribute: attrId=%d, maxReadLength=%d", attributeId, maxReadLength);

    if ((attributeId == ZCL_REACHABLE_ATTRIBUTE_ID) && (maxReadLength == 1))
    {
        *buffer = dev->IsReachable() ? 1 : 0;
    }
    else if ((attributeId == ZCL_NODE_LABEL_ATTRIBUTE_ID) && (maxReadLength == 32))
    {
        MutableByteSpan zclNameSpan(buffer, maxReadLength);
        MakeZclCharString(zclNameSpan, dev->GetName());
    }
    else if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_REVISION;
    }
    else if ((attributeId == ZCL_FEATURE_MAP_SERVER_ATTRIBUTE_ID) && (maxReadLength == 4))
    {
        *buffer = (uint32_t) ZCL_BRIDGED_DEVICE_BASIC_FEATURE_MAP;
    }
    else
    {
        return EMBER_ZCL_STATUS_FAILURE;
    }

    return EMBER_ZCL_STATUS_SUCCESS;
}

EmberAfStatus HandleReadOnOffAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadOnOffAttribute: attrId=%d, maxReadLength=%d", attributeId, maxReadLength);

    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && (maxReadLength == 1))
    {
        *buffer = dev->IsOn() ? 1 : 0;
    }
    else if ((attributeId == ZCL_CLUSTER_REVISION_SERVER_ATTRIBUTE_ID) && (maxReadLength == 2))
    {
        *buffer = (uint16_t) ZCL_ON_OFF_CLUSTER_REVISION;
    }
    else
    {
        return EMBER_ZCL_STATUS_FAILURE;
    }

    return EMBER_ZCL_STATUS_SUCCESS;
}

void BridgeDevMgr::HandleDeviceOnOffStatusChanged(DeviceOnOff * dev, DeviceOnOff::Changed_t itemChangedMask)
{
    if (itemChangedMask & (DeviceOnOff::kChanged_Reachable | DeviceOnOff::kChanged_Name | DeviceOnOff::kChanged_Location))
    {
        HandleDeviceStatusChanged(static_cast<Device *>(dev), (Device::Changed_t) itemChangedMask);
    }

    if (itemChangedMask & DeviceOnOff::kChanged_OnOff)
    {
        ScheduleReportingCallback(dev, OnOff::Id, OnOff::Attributes::OnOff::Id);
    }
}

EmberAfStatus HandleWriteOnOffAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteOnOffAttribute: attrId=%d", attributeId);

    if ((attributeId == ZCL_ON_OFF_ATTRIBUTE_ID) && (dev->IsReachable()))
    {
        if (*buffer)
        {
            dev->SetOnOff(true);
        }
        else
        {
            dev->SetOnOff(false);
        }
    }
    else
    {
        return EMBER_ZCL_STATUS_FAILURE;
    }

    return EMBER_ZCL_STATUS_SUCCESS;
}

EmberAfStatus emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                   const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
                                                   uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (BridgeDevMgr::gDevices[endpointIndex] != nullptr))
    {
        Device * dev = BridgeDevMgr::gDevices[endpointIndex];

        if (clusterId == ZCL_BRIDGED_DEVICE_BASIC_CLUSTER_ID)
        {
            ret = HandleReadBridgedDeviceBasicAttribute(dev, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == ZCL_ON_OFF_CLUSTER_ID)
        {
            ret = HandleReadOnOffAttribute(static_cast<DeviceOnOff *>(dev), attributeMetadata->attributeId, buffer, maxReadLength);
        }
    }

    return ret;
}

EmberAfStatus emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                    const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    EmberAfStatus ret = EMBER_ZCL_STATUS_FAILURE;

    // ChipLogProgress(DeviceLayer, "emberAfExternalAttributeWriteCallback: ep=%d", endpoint);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        Device * dev = BridgeDevMgr::gDevices[endpointIndex];

        if ((dev->IsReachable()) && (clusterId == ZCL_ON_OFF_CLUSTER_ID))
        {
            ret = HandleWriteOnOffAttribute(static_cast<DeviceOnOff *>(dev), attributeMetadata->attributeId, buffer);
        }
    }

    return ret;
}