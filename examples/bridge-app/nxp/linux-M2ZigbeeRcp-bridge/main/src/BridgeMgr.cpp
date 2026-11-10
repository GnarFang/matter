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

#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/callback.h>
#include <cstdint>
#include <cinttypes>

#include <app-common/zap-generated/callback.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Commands.h>
#include <app/util/util.h>
#include <app/CommandHandler.h>
#include <app/InteractionModelEngine.h>
#include <lib/core/CHIPSafeCasts.h>
#include <lib/support/TypeTraits.h>

#include <app/util/MatterCallbacks.h>
#include "BridgeMgr.h"
#include "Bridge.h"
#include <syslog.h>
#include <stdio.h>
#include <cstring>
#include <memory>

#include "ZigbeeRcpImpl.h"
#include "ZigbeeRcpMessage.h"
#include <zap-generated/endpoint_config.h>
#ifdef GENERATED_FUNCTION_ARRAYS
    GENERATED_FUNCTION_ARRAYS
#else
    #error "GENERATED_FUNCTION_ARRAYS is NOT defined"
#endif

// If we have attributes that are more than 4 bytes, then
// we need this data block for the defaults
#if (defined(GENERATED_DEFAULTS) && GENERATED_DEFAULTS_COUNT)
constexpr const uint8_t generatedDefaults[] = GENERATED_DEFAULTS;
#define ZAP_LONG_DEFAULTS_INDEX(index)                                                                                             \
    {                                                                                                                              \
        &generatedDefaults[index]                                                                                                  \
    }
#endif // GENERATED_DEFAULTS

#if (defined(GENERATED_MIN_MAX_DEFAULTS) && GENERATED_MIN_MAX_DEFAULT_COUNT)
constexpr const EmberAfAttributeMinMaxValue minMaxDefaults[] = GENERATED_MIN_MAX_DEFAULTS;
#define ZAP_MIN_MAX_DEFAULTS_INDEX(index)                                                                                          \
    {                                                                                                                              \
        &minMaxDefaults[index]                                                                                                     \
    }
#endif // GENERATED_MIN_MAX_DEFAULTS

#if (defined(GENERATED_EVENTS) && (GENERATED_EVENT_COUNT > 0))
constexpr const chip::EventId generatedEvents[] = GENERATED_EVENTS;
#define ZAP_GENERATED_EVENTS_INDEX(index) (&generatedEvents[index])
#endif // GENERATED_EVENTS

constexpr const chip::CommandId generatedCommands[] = GENERATED_COMMANDS;
#define ZAP_GENERATED_COMMANDS_INDEX(index) (&generatedCommands[index])

constexpr const EmberAfAttributeMetadata generatedAttributes[] = GENERATED_ATTRIBUTES;
#define ZAP_ATTRIBUTE_INDEX(index) (&generatedAttributes[index])

constexpr const EmberAfCluster generatedClusters[] = GENERATED_CLUSTERS;
#define ZAP_CLUSTER_INDEX(index) (&generatedClusters[index])
#define DUMMY_ENDPOINT_2_FIRST_CLUSTER_INDEX 23

// define global variable
ZigbeeRcpMsg_t ZigbeeRcpMsg = {
    .AnnounceStart = false,
    .HandleMask    = true,
    .bridge_mutex  = PTHREAD_MUTEX_INITIALIZER,
    .bridge_cond   = PTHREAD_COND_INITIALIZER,
    .msg_type      = BRIDGE_UNKNOW,
};

pthread_t ZigbeeRcpComm_thread;
void send_message_to_comm_thread(int MsgType, m2z_device_params_t* ZigbeeRcp_dev, void* data);

Device * BridgeDevMgr::gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
int ZcbNodesNum = 0;

//namespace {
class BridgeCallbacks: public chip::DataModelCallbacks
{
public:
    CHIP_ERROR PreCommandReceived(const chip::app::ConcreteCommandPath & commandPath,
                            const chip::Access::SubjectDescriptor & subjectDescriptor, chip::app::CommandDataIB::Parser & aCommandElement) override
    {
        //(void) InteractiveServer::GetInstance().Command(commandPath);
        uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(commandPath.mEndpointId);
        Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;
        CHIP_ERROR TLVError = CHIP_NO_ERROR;


        if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
        {
            syslog(LOG_INFO, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mCommandId=0x%x\n", __FUNCTION__,
                            commandPath.mEndpointId ,endpointIndex ,commandPath.mClusterId ,commandPath.mCommandId);
            Device * dev = BridgeDevMgr::gDevices[endpointIndex];

            dev->dev_pending_command = true;
            dev->pendingCmdEndpointId = commandPath.mEndpointId;
            dev->pendingCmdClusterId = commandPath.mClusterId;
            dev->pendingCmdCommandId = commandPath.mCommandId;
            dev->cmdSize = 0;

            CommandPathIB::Parser cmdPath;
            ConcreteCommandPath concretePath(0, 0, 0);
            TLV::TLVReader DataReader;

            TLVError = aCommandElement.GetPath(&cmdPath);
            TLVError = cmdPath.GetConcreteCommandPath(concretePath);
            TLVError = aCommandElement.GetFields(&DataReader);
            chip::FabricIndex fabricIndex = subjectDescriptor.fabricIndex;

            switch (commandPath.mClusterId)
            {
                case chip::app::Clusters::OnOff::Id:
                {
                    // On/Off/Toggle are relayed 1:1 to the Zigbee OnOff cluster: the ZCL
                    // frame carries no payload, the command id is enough.
                    switch (commandPath.mCommandId)
                    {
                        case chip::app::Clusters::OnOff::Commands::On::Id:
                        case chip::app::Clusters::OnOff::Commands::Off::Id:
                        case chip::app::Clusters::OnOff::Commands::Toggle::Id:
                            break;
                        default:
                            syslog(LOG_WARNING, "%s: unsupported OnOff command 0x%02x", __FUNCTION__, commandPath.mCommandId);
                            return CHIP_ERROR_NOT_IMPLEMENTED;
                    }
                    break;
                }
                case chip::app::Clusters::LevelControl::Id:
                {
                    switch (commandPath.mCommandId)
                    {
                        case chip::app::Clusters::LevelControl::Commands::MoveToLevel::Id:
                        case chip::app::Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id:
                        {
                            chip::app::Clusters::LevelControl::Commands::MoveToLevel::DecodableType commandData;
                            CHIP_ERROR decodeErr = DataModel::Decode(DataReader, commandData);
                            if (decodeErr != CHIP_NO_ERROR)
                            {
                                syslog(LOG_ERR, "%s: cannot decode MoveToLevel: %s", __FUNCTION__, decodeErr.AsString());
                                return CHIP_ERROR_INVALID_ARGUMENT;
                            }
                            const uint16_t transitionTime =
                                commandData.transitionTime.IsNull() ? 0 : commandData.transitionTime.Value();
                            dev->cmdData[dev->cmdSize++] = commandData.level;
                            dev->cmdData[dev->cmdSize++] = (uint8_t)(transitionTime & 0xff);
                            dev->cmdData[dev->cmdSize++] = (uint8_t)((transitionTime >> 8) & 0xff);
                            dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.optionsMask.Raw() & 0xff);
                            dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.optionsOverride.Raw() & 0xff);
                            break;
                        }
                        default:
                            syslog(LOG_WARNING, "%s: unsupported LevelControl command 0x%02x", __FUNCTION__, commandPath.mCommandId);
                            return CHIP_ERROR_NOT_IMPLEMENTED;
                    }
                    break;
                }
                case chip::app::Clusters::Groups::Id:
                {
                    switch (commandPath.mCommandId)
                    {
                        case chip::app::Clusters::Groups::Commands::AddGroup::Id:
                        {
                            chip::app::Clusters::Groups::Commands::AddGroup::DecodableType commandData;
                            commandData.Decode(DataReader, fabricIndex);
                            dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.groupID >> 0) & 0xff;
                            dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.groupID >> 8) & 0xff;
                            memcpy(&(dev->cmdData[dev->cmdSize]), commandData.groupName.data(), commandData.groupName.size());
                            dev->cmdSize += commandData.groupName.size();

                            break;
                        }
                    }
                    break;
                }
            }
            if (dev->IsReachable())
            {
                ret = BridgeDevMgr::HandleSendCommand(static_cast<Device *>(dev));
            }
            if(ret != Protocols::InteractionModel::Status::Success)
            {
                syslog(LOG_ERR, "%s: Command failed: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                            dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
                return(CHIP_ERROR_INCORRECT_STATE);
            }
            syslog(LOG_INFO, "%s: Command successfully processed: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                        dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
        }
        return(CHIP_NO_ERROR);
    }

    void PostCommandReceived(const chip::app::ConcreteCommandPath & commandPath,
                             const chip::Access::SubjectDescriptor & subjectDescriptor, chip::app::CommandDataIB::Parser & aCommandElement) override
    {
        //(void) InteractiveServer::GetInstance().Command(commandPath);
        uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(commandPath.mEndpointId);
        Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;


        if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
        {
            Device * dev = BridgeDevMgr::gDevices[endpointIndex];

            syslog(LOG_INFO, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mCommandId=0x%x dev->dev_pending_command: %d\n", __FUNCTION__,
                            commandPath.mEndpointId ,endpointIndex ,commandPath.mClusterId ,commandPath.mCommandId,dev->dev_pending_command);

            if(dev->dev_pending_command == true)
            {
                syslog(LOG_DEBUG, "%s: Command is pending: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                                dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
                if (dev->IsReachable())
                {
                    //ret = gDevMgr->HandleSendCommand(static_cast<Device *>(dev));
                    dev->dev_pending_command = false;
                    //syslog(LOG_DEBUG, "%s: Command successfully processed: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                    //            dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
                }

                ////if the Cluster is OnOff, we need a workarround on the LevelControl cluster if the device has this cluster
                ////end the command to move to level = 0 in case of "Off" cmd
                ////send the command to move to level = 254 in case of "On" cmd
                //switch (commandPath.mClusterId)
                //{
                    //case chip::app::Clusters::OnOff::Id:
                    //{
                        //zb_uint16_t endpoint_idx=0;
                        //zb_uint16_t cluster_idx=0;

                        //if(dev->GetEndpointClusterIndexesForClusterId(dev->GetZigbeeRcp(), chip::app::Clusters::LevelControl::Id, &endpoint_idx, &cluster_idx))
                        //{
                            //dev->cmdSize = 0;
                            //dev->pendingCmdEndpointId = commandPath.mEndpointId;
                            //dev->pendingCmdClusterId = chip::app::Clusters::LevelControl::Id;
                            //dev->pendingCmdCommandId = chip::app::Clusters::LevelControl::Commands::MoveToLevel::Id;
                            //switch (commandPath.mCommandId)
                            //{
                                //case chip::app::Clusters::OnOff::Commands::On::Id:
                                //{
                                    //dev->cmdData[dev->cmdSize++] = 254;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //break;
                                //}
                                //case chip::app::Clusters::OnOff::Commands::Off::Id:
                                //{
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //break;
                                //}
                                //case chip::app::Clusters::OnOff::Commands::Toggle::Id:
                                //{
                                    //// Uggly workarround: get the current level to toggle it !
                                    ////uint8_t currentLevel = 0;
                                    ////ret = HandleReadAttribute(dev, chip::app::Clusters::LevelControl::Id, chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id, &currentLevel, sizeof(currentLevel));
                                    ////syslog(LOG_DEBUG, "%s currentLevel=%d\n", __FUNCTION__, currentLevel);
                                    ////if(currentLevel <=1)
                                    ////{
                                        ////dev->cmdData[dev->cmdSize++] = 254;
                                    ////}
                                    ////else
                                    ////{
                                        ////dev->cmdData[dev->cmdSize++] = 1;
                                    ////}
                                    ////dev->cmdData[dev->cmdSize++] = 0;
                                    ////dev->cmdData[dev->cmdSize++] = 0;
                                    ////dev->cmdData[dev->cmdSize++] = 0;
                                    ////dev->cmdData[dev->cmdSize++] = 0;
                                    
                                    //// to be redesigned completly, skip it for now on...
                                
                                    //break;
                                //}
                            //}
                            //if (dev->IsReachable())
                            //{
                                //ret = gDevMgr->HandleSendCommand(static_cast<Device *>(dev));
                            //}
                            //syslog(LOG_DEBUG, "%s: Command successfully processed: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                                //dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
                                //usleep(COMMAND_COMPLETED_TIME_MS * 1000);
                                
                        //}
                    //}
                //}
            }
        }
    }
};

BridgeCallbacks gBridgeMatterCallbacks;

//} // namespace



BridgeDevMgr::BridgeDevMgr()
{
}

BridgeDevMgr::~BridgeDevMgr()
{
    RemoveAllDevice();
    syslog(LOG_INFO, "%s: free BridgeDevMgr ! \n", __FUNCTION__);
}

int BridgeDevMgr::AddNewZigbeeDevice(m2z_device_params_t* zigbee_node)
{
    if (zigbee_node == nullptr || zigbee_node->endpoint == 0)
    {
        syslog(LOG_WARNING, "%s: invalid zigbee node", __FUNCTION__);
        return -1;
    }

    // Device level discovery (name, location, device id) is done once per Zigbee node.
    Device * DiscoveredDevice = new Device("Dummy Zigbee Device", "Dummy Location");
    DiscoveredDevice->SetZigbeeRcp(zigbee_node);
    DiscoveredDevice->DiscoverNode(zigbee_node);
    DiscoveredDevice->SetClusters();
    std::string name              = DiscoveredDevice->GetName();
    std::string location          = DiscoveredDevice->GetLocation();
    std::string supported_clusters = DiscoveredDevice->GetClusters();
    std::string device_id         = DiscoveredDevice->GetEndpointDeviceId();

    syslog(LOG_INFO, "%s : Discovered Zigbee Device: Name %s Location: %s Device Id: %s SupportedClusters: %s", __FUNCTION__,
           name.c_str(), location.c_str(), device_id.c_str(), supported_clusters.c_str());

    delete DiscoveredDevice;

    // One Matter dynamic endpoint per Zigbee endpoint: a multi gang device exposes one Matter
    // endpoint per gang, which is required because a Matter endpoint can carry only one instance
    // of a given cluster.
    int bridged = 0;
    int failed  = 0;
    bool multi_ep = (zigbee_node->endpoint > 1);
    for (uint8_t ep_index = 0; ep_index < zigbee_node->endpoint; ep_index++)
    {
        zb_uint16_t profile_id = zigbee_node->endpoints[ep_index].profile_id;

        // Only bridge application endpoints (HA or ZLL profile). Endpoints on other
        // profiles (e.g. 0xA1E0 Green Power) do not expose clusters a Matter
        // controller can use, so they must not become Matter endpoints.
        if (profile_id != ZB_AF_HA_PROFILE_ID && profile_id != ZB_AF_ZLL_PROFILE_ID)
        {
            syslog(LOG_INFO, "%s: skip zigbee ep %d with non-application profile 0x%04x", __FUNCTION__,
                   zigbee_node->endpoints[ep_index].ep_id, profile_id);
            continue;
        }

        std::string ep_name = name;
        if (multi_ep)
        {
            ep_name += "-EP" + std::to_string(zigbee_node->endpoints[ep_index].ep_id);
        }
        if (AddNewZigbeeEndpoint(zigbee_node, ep_index, ep_name, location) >= 0)
        {
            bridged++;
        }
        else
        {
            failed++;
        }
    }

    syslog(LOG_INFO, "%s: bridged %d Matter endpoint(s) for Zigbee node 0x%04x (%d failed), already bridged ones are reused",
           __FUNCTION__, bridged, zigbee_node->short_addr, failed);
    return bridged;
}

int BridgeDevMgr::AddNewZigbeeEndpoint(m2z_device_params_t* zigbee_node, uint8_t ep_index, const std::string & name,
                                       const std::string & location, chip::EndpointId preferredMatterEndpointId,
                                       bool markReachable)
{
    if (zigbee_node == nullptr || ep_index >= zigbee_node->endpoint)
    {
        return -1;
    }

    zb_uint16_t ep_id = zigbee_node->endpoints[ep_index].ep_id;

    // A Zigbee endpoint is bridged exactly once. The same node may be announced again
    // (rejoin, or boot where RestoreBridgedDevices() already recreated its endpoints), and
    // the restored devices are not reachable yet, so reachability cannot be used as the
    // "already bridged" indication.
    for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
    {
        Device * dev = gDevices[i];
        // (short address, endpoint id) is the Zigbee identity of an endpoint, and it survives a
        // reboot, unlike the m2z_device_params_t pointer which depends on the device table slot.
        if (dev != nullptr && dev->GetZigbeeRcpSaddr() == zigbee_node->short_addr && dev->GetZigbeeEp() == ep_id)
        {
            syslog(LOG_INFO, "%s: Zigbee node 0x%04x ep %d is already bridged to Matter endpoint %u (index=%u), skip",
                   __FUNCTION__, zigbee_node->short_addr, ep_id, dev->GetEndpointId(), i);
            return i;
        }
    }

    std::unique_ptr<Device> newDevice(new Device(name, location));
    newDevice->SetZigbeeRcp(zigbee_node);
    newDevice->SetZigbeeEp(ep_id);
    newDevice->SetMatterDeviceTypes(zigbee_node, ep_index);
    newDevice->SetChangeCallback(&HandleZigbeeDeviceStatusChanged);
    newDevice->InitState(false);

    zb_uint16_t mapped_clusters_number = 0;

    // Descriptor and Bridged Device Basic Information are mandatory on every bridged endpoint.
    for ( zb_uint16_t attr_idx = DUMMY_ENDPOINT_2_FIRST_CLUSTER_INDEX; attr_idx < GENERATED_CLUSTER_COUNT; attr_idx++)
    {
        if (mapped_clusters_number >= MATTER_ZIGBEERCP_BRIDGE_MAX_CLUSTERS_PER_EP)
        {
            syslog(LOG_WARNING, "%s: cluster list is full (%u), stopping at cluster 0x%04x", __FUNCTION__,
                   (unsigned) mapped_clusters_number, generatedClusters[attr_idx].clusterId);
            break;
        }
        if (generatedClusters[attr_idx].clusterId == Descriptor::Id)
        {
            syslog(LOG_DEBUG, "\t%s : Found GENERATED_CLUSTERS clusterId: %d ",__FUNCTION__, generatedClusters[attr_idx].clusterId);
            newDevice->clusterList[mapped_clusters_number++] = generatedClusters[attr_idx];
        }
        if (generatedClusters[attr_idx].clusterId == BridgedDeviceBasicInformation::Id)
        {
            syslog(LOG_DEBUG, "\t%s : Found GENERATED_CLUSTERS clusterId: %d ",__FUNCTION__, generatedClusters[attr_idx].clusterId);
            newDevice->clusterList[mapped_clusters_number++] = generatedClusters[attr_idx];
        }
    }

    // Map the clusters of this Zigbee endpoint to their Matter counterparts.
    for ( zb_uint16_t j = 0; j < (zigbee_node->endpoints[ep_index].num_in_clusters + zigbee_node->endpoints[ep_index].num_out_clusters); j++)
    {
        if (!zigbee_node->endpoints[ep_index].ep_cluster[j].in_cluster)
        {
            syslog(LOG_DEBUG, "\t%s: ClusterId: %d is OUT Zigbee cluster, skip it",__FUNCTION__, zigbee_node->endpoints[ep_index].ep_cluster[j].cluster_id);
            continue;
        }

        ClusterId clusterId = newDevice->GetMatterClusterId(zigbee_node->endpoints[ep_index].ep_cluster[j].cluster_id);
        for ( zb_uint16_t attr_idx = DUMMY_ENDPOINT_2_FIRST_CLUSTER_INDEX; attr_idx < GENERATED_CLUSTER_COUNT; attr_idx++)
        {
            if (generatedClusters[attr_idx].clusterId == clusterId)
            {
                if (mapped_clusters_number >= MATTER_ZIGBEERCP_BRIDGE_MAX_CLUSTERS_PER_EP)
                {
                    syslog(LOG_WARNING, "%s: cluster list is full (%u), drop Zigbee cluster 0x%04x of ep %d",
                           __FUNCTION__, (unsigned) mapped_clusters_number, zigbee_node->endpoints[ep_index].ep_cluster[j].cluster_id,
                           ep_id);
                    break;
                }
                syslog(LOG_INFO, "\t%s : Found GENERATED_CLUSTERS clusterId: %d == zigbee_node->endpoints[%d].ep_cluster[%d].cluster_id: %d",__FUNCTION__, generatedClusters[attr_idx].clusterId,
                       ep_index, j, zigbee_node->endpoints[ep_index].ep_cluster[j].cluster_id);
                newDevice->clusterList[mapped_clusters_number++] = generatedClusters[attr_idx];
                break;
            }
        }
    }

    newDevice->endpointType.cluster = (const EmberAfCluster*)(newDevice->clusterList);
    newDevice->endpointType.clusterCount = (zb_uint8_t)mapped_clusters_number;
    newDevice->endpointType.endpointSize = 0;
    newDevice->SetEndpointClustersCount((int)mapped_clusters_number);

    // SetDynamicEndpoint() takes ownership of the device on success (it stores the pointer in
    // gDevices[]), so release the unique_ptr only once the endpoint has been created.
    int rc = SetDynamicEndpoint(newDevice.get(), &(newDevice->endpointType), newDevice->deviceTypes,
                                newDevice->dataVersions, 1, preferredMatterEndpointId, markReachable);
    if (rc >= 0)
    {
        newDevice.release();
    }
    return rc;
}

void BridgeDevMgr::init()
{
    m2z_impl_init();
}


void BridgeDevMgr::start()
{
    mFirstDynamicEndpointId = static_cast<chip::EndpointId>(
    static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);

    chip::DataModelCallbacks::SetInstance(&gBridgeMatterCallbacks);

    RestoreBridgedDevices();

    // start monitor
    m2z_impl_start_threads();
    start_ZigbeeRcp_comm_thread();
    m2z_register_message_callback(send_message_to_comm_thread);
}

void BridgeDevMgr::RemoveAllDevice()
{
    for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
    {
        if (gDevices[i] != NULL)
        {
            RemoveDevice(gDevices[i]);
        }
    }

    memset(gDevices, 0, sizeof(Device *) * CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT);
}

void BridgeDevMgr::RemoveDevice(Device * dev)
{
    ClearDynamicEndpoint(dev);
    //if (dev->GetZigbee()->zcb.uSupportedClusters.sClusterBitmap.hasOnOff)
    //{
        //RemoveOnOffNode(static_cast<DeviceOnOff *>(dev));
    //}

    //if (dev->GetZigbee()->zcb.uSupportedClusters.sClusterBitmap.hasTemperatureSensing)
    //{
        //RemoveTempMeasurementNode(static_cast<DeviceTempSensor *>(dev));
    //}
}

// -----------------------------------------------------------------------------------------
// Device Management
// -----------------------------------------------------------------------------------------

// Publish one Matter dynamic endpoint for an already fully described Device.
int BridgeDevMgr::SetDynamicEndpoint(Device * dev, EmberAfEndpointType *ep, EmberAfDeviceType *deviceTypeList,
                                     DataVersion * dataVersionStorage,
                                     chip::EndpointId parentEndpointId,
                                     chip::EndpointId preferredEndpointId,
                                     bool markReachable)
{
    uint8_t index = 0;

    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (nullptr == gDevices[index])
        {
            gDevices[index] = dev;
            CHIP_ERROR ret;

            /* Reuse the endpoint id that this device had before the reboot when
             * possible: controllers cache endpoint ids. */
            chip::EndpointId endpointId = preferredEndpointId;
            if (endpointId == chip::kInvalidEndpointId || EndpointIdInUse(endpointId))
            {
                endpointId = AllocateEndpointId();
            }

            /* Retry with another id when the data model reports the wanted id as taken. */
            while (endpointId != chip::kInvalidEndpointId)
            {
                DeviceLayer::StackLock lock;
                dev->SetEndpointId(endpointId);
                dev->SetParentEndpointId(parentEndpointId);

                chip::Span<EmberAfDeviceType> device_types(deviceTypeList, dev->GetDeviceTypeCount());
                chip::Span<chip::DataVersion> data_versions(dataVersionStorage, dev->GetEndpointClustersCount());

                ret = emberAfSetDynamicEndpoint(index, endpointId, ep, data_versions, device_types, parentEndpointId);
                if (ret == CHIP_NO_ERROR)
                {
                    if (markReachable)
                    {
                        dev->SetReachable(true);
                    }
                    dev->SetMatterIndex(index);
                    syslog(LOG_INFO, "%s: Set device %s addr %x on dynamic endpoint %d (index=%d)", __FUNCTION__,
                                    (dev->GetName()).c_str(), dev->GetZigbeeRcpSaddr(), endpointId, index);
                    return index;
                }
                if (ret != CHIP_ERROR_ENDPOINT_EXISTS)
                {
                    syslog(LOG_ERR, "%s: emberAfSetDynamicEndpoint() failed: %s", __FUNCTION__, chip::ErrorStr(ret));
                    break;
                }
                endpointId = AllocateEndpointId();
            }

            syslog(LOG_ERR, "%s: failed to set a dynamic endpoint for %s", __FUNCTION__, (dev->GetName()).c_str());
            gDevices[index] = nullptr;
            return -1;
        }
        index++;
    }
    syslog(LOG_ERR, "%s: Failed to add dynamic endpoint: No endpoints available!", __FUNCTION__);
    return -1;
}

bool BridgeDevMgr::EndpointIdInUse(chip::EndpointId endpointId)
{
    for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
    {
        Device * dev = gDevices[i];
        if (dev != nullptr && dev->GetEndpointId() == endpointId)
        {
            return true;
        }
    }
    return false;
}

chip::EndpointId BridgeDevMgr::AllocateEndpointId()
{
    for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
    {
        chip::EndpointId candidate = static_cast<chip::EndpointId>(mFirstDynamicEndpointId + i);
        if (!EndpointIdInUse(candidate))
        {
            return candidate;
        }
    }
    /* Should not happen: one endpoint id per dynamic slot. */
    syslog(LOG_WARNING, "%s: no free endpoint id left", __FUNCTION__);
    return chip::kInvalidEndpointId;
}

static const char * kBridgeDeviceStorePath = "/var/local/zboss/M2ZigbeeRcp-bridge.devices";

void BridgeDevMgr::SaveBridgedDevices()
{
    if (!m2z_persist_enabled)
    {
        return;
    }

    char tmpPath[160];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", kBridgeDeviceStorePath);

    FILE * f = fopen(tmpPath, "wb");
    if (f == nullptr)
    {
        syslog(LOG_ERR, "%s: cannot open %s for writing", __FUNCTION__, tmpPath);
        return;
    }

    uint32_t magic = M2Z_STORE_MAGIC;
    uint16_t version = M2Z_STORE_VERSION;
    uint16_t nodeCount = 0;

    m2z_device_params_t * nodes[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT];
    for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
    {
        Device * dev = gDevices[i];
        if (dev == nullptr || dev->GetZigbeeRcp() == nullptr)
        {
            continue;
        }
        bool seen = false;
        for (uint16_t k = 0; k < nodeCount; k++)
        {
            if (nodes[k] == dev->GetZigbeeRcp())
            {
                seen = true;
                break;
            }
        }
        if (!seen)
        {
            nodes[nodeCount++] = dev->GetZigbeeRcp();
        }
    }

    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&version, sizeof(version), 1, f);
    fwrite(&nodeCount, sizeof(nodeCount), 1, f);

    for (uint16_t k = 0; k < nodeCount; k++)
    {
        m2z_device_params_t * node = nodes[k];
        m2z_stored_device_t rec;
        memset(&rec, 0, sizeof(rec));

        memcpy(rec.ieee_addr, node->ieee_addr, sizeof(rec.ieee_addr));
        rec.short_addr = node->short_addr;
        rec.ep_count = node->endpoint;
        if (rec.ep_count > MATTER_ZIGBEERCP_BRIDGE_MAX_EP_PER_DEV)
        {
            rec.ep_count = MATTER_ZIGBEERCP_BRIDGE_MAX_EP_PER_DEV;
        }

        for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
        {
            if (gDevices[i] != nullptr && gDevices[i]->GetZigbeeRcp() == node)
            {
                snprintf(rec.location, sizeof(rec.location), "%s", gDevices[i]->GetLocation().c_str());
                break;
            }
        }

        uint8_t stored_eps = 0;
        for (uint8_t e = 0; e < node->endpoint && stored_eps < MATTER_ZIGBEERCP_BRIDGE_MAX_EP_PER_DEV; e++)
        {
            m2z_device_ep_t * ep = &node->endpoints[e];
            m2z_stored_ep_t * sep = &rec.eps[stored_eps];

            // Only remember endpoints that were actually bridged to a Matter endpoint.
            chip::EndpointId matter_ep_id = chip::kInvalidEndpointId;
            for (uint16_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
            {
                Device * dev = gDevices[i];
                if (dev != nullptr && dev->GetZigbeeRcp() == node && dev->GetZigbeeEp() == ep->ep_id)
                {
                    matter_ep_id = dev->GetEndpointId();
                    snprintf(sep->name, sizeof(sep->name), "%s", dev->GetName().c_str());
                    break;
                }
            }
            if (matter_ep_id == chip::kInvalidEndpointId)
            {
                continue;
            }

            sep->ep_id = ep->ep_id;
            sep->profile_id = ep->profile_id;
            sep->device_id = ep->device_id;
            sep->num_out_clusters = ep->num_out_clusters;
            sep->matter_ep_id = matter_ep_id;
            sep->num_in_clusters = 0;

            for (uint8_t c = 0; c < MATTER_ZIGBEERCP_BRIDGE_MAX_CLUSTERS_PER_EP; c++)
            {
                if (ep->ep_cluster[c].in_cluster && sep->num_in_clusters < MATTER_ZIGBEERCP_BRIDGE_MAX_CLUSTERS_PER_EP)
                {
                    sep->in_clusters[sep->num_in_clusters++] = ep->ep_cluster[c].cluster_id;
                }
            }
            stored_eps++;
        }
        rec.ep_count = stored_eps;

        fwrite(&rec, sizeof(rec), 1, f);
    }

    fclose(f);
    rename(tmpPath, kBridgeDeviceStorePath);
    syslog(LOG_INFO, "%s: saved %d bridged device(s)", __FUNCTION__, nodeCount);
}

void BridgeDevMgr::RestoreBridgedDevices()
{
    if (!m2z_persist_enabled)
    {
        return;
    }

    FILE * f = fopen(kBridgeDeviceStorePath, "rb");
    if (f == nullptr)
    {
        return;
    }

    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t count = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || fread(&version, sizeof(version), 1, f) != 1 ||
        fread(&count, sizeof(count), 1, f) != 1 || magic != M2Z_STORE_MAGIC || version != M2Z_STORE_VERSION)
    {
        syslog(LOG_ERR, "%s: invalid bridged device store, ignoring it", __FUNCTION__);
        fclose(f);
        return;
    }

    syslog(LOG_INFO, "%s: restoring %d bridged device(s)", __FUNCTION__, count);

    for (uint16_t k = 0; k < count; k++)
    {
        m2z_stored_device_t rec;
        if (fread(&rec, sizeof(rec), 1, f) != 1)
        {
            break;
        }

        zb_uint16_t devIdx = m2z_store_restore_device(&rec);
        if (devIdx == MATTER_ZIGBEERCP_BRIDGE_INVALID_DEV_INDEX)
        {
            syslog(LOG_ERR, "%s: cannot restore device short 0x%04x (no slot)", __FUNCTION__, rec.short_addr);
            continue;
        }
        syslog(LOG_INFO, "%s: device short 0x%04x ep_count %d -> zigbee dev index %d", __FUNCTION__, rec.short_addr,
               rec.ep_count, devIdx);

        m2z_device_params_t * node = m2z_get_device_params(devIdx);
        if (node == nullptr)
        {
            continue;
        }

        for (uint8_t e = 0; e < rec.ep_count; e++)
        {
            std::string name = rec.eps[e].name;
            if (name.empty())
            {
                name = "Zigbee Device";
            }
            int rc = AddNewZigbeeEndpoint(node, e, name, rec.location, rec.eps[e].matter_ep_id, false);
            syslog(LOG_INFO, "%s: restore device short 0x%04x zigbee ep %d -> %s (rc=%d)", __FUNCTION__, rec.short_addr,
                   rec.eps[e].ep_id, name.c_str(), rc);
        }
    }

    fclose(f);
}

// Remove the Matter dynamic endpoint of a device and free its gDevices[] slot. The Device object
// stays alive: bridging is only undone when the whole bridge goes down, so it is freed with the
// process instead of here (deleting it while Zigbee reports may still arrive would be unsafe).
int BridgeDevMgr::ClearDynamicEndpoint(Device * dev)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (gDevices[index] == dev)
        {
            // Todo: Update this to schedule the work rather than use this lock
            DeviceLayer::StackLock lock;
            // Silence complaints about unused ep when progress logging
            // disabled.
            [[maybe_unused]] EndpointId ep   = emberAfClearDynamicEndpoint(index);
            gDevices[index] = nullptr;
            syslog(LOG_INFO, "%s: Cleared dynamic endpoint %d (index=%d) of device %s", __FUNCTION__, ep, index,
                            (dev->GetName()).c_str());

            SaveBridgedDevices();
            return index;
        }
        index++;
    }
    return -1;
}

void * BridgeDevMgr::ZigbeeRcpComm_handler(void * context)
{
    BridgeDevMgr * ThisMgr = (BridgeDevMgr *) context;
    m2z_device_params_t *ZigbeeRcp_device;
    syslog(LOG_INFO, "%s running !!!\n", __FUNCTION__);
    while (1)
    {
        pthread_mutex_lock(&ZigbeeRcpMsg.bridge_mutex);
        while (ZigbeeRcpMsg.HandleMask)
        {
            if (pthread_cond_wait(&ZigbeeRcpMsg.bridge_cond, &ZigbeeRcpMsg.bridge_mutex) == 0)
            {
                syslog(LOG_INFO, " %s RECEIVED A MESSAGE\n", __FUNCTION__);
                ZigbeeRcp_device = (m2z_device_params_t *)ZigbeeRcpMsg.ZigbeeRcp_dev;
                
                switch (ZigbeeRcpMsg.msg_type)
                {
                    case BRIDGE_ADD_DEV: {
                        ZigbeeRcp_device->dev_state = ANNOUNCE_COMPLETED;
                        ThisMgr->AddNewZigbeeDevice(ZigbeeRcp_device);
                        ThisMgr->SaveBridgedDevices();
                        ZigbeeRcpMsg.ZigbeeRcp_dev      = { 0 };
                        ZigbeeRcpMsg.msg_type = BRIDGE_UNKNOW;
                        while(ZigbeeRcp_device->dev_state == ANNOUNCE_COMPLETED)
                        {
                            sleep(1);
                        }
                        syslog(LOG_INFO, "%s: all attributes were read !!!\n", __FUNCTION__);
                        ZigbeeRcp_device->dev_state = BRIDGED_REACHABLE;
                    }
                    break;

                case BRIDGE_SET_DEV_UNSUPPORTED: {
                    // A Zigbee node may own several Matter endpoints: disable all of them.
                    uint8_t index = 0;
                    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
                    {
                        Device * dev = gDevices[index];
                        if (dev != nullptr && dev->GetZigbeeRcp() == ZigbeeRcp_device)
                        {
                            syslog(LOG_DEBUG, "%s: BRIDGE_SET_DEV_UNSUPPORTED: dev->GetZigbeeRcp(): 0x%x ZigbeeRcp_device: %x \n", __FUNCTION__,dev->GetZigbeeRcp(), ZigbeeRcp_device);
                            syslog(LOG_INFO, "%s: BRIDGE_SET_DEV_UNSUPPORTED: ---> UNSUPPORTED Dynamic Endpoint: %d !!!\n", __FUNCTION__, dev->GetEndpointId());
                            DeviceLayer::StackLock lock;
                            emberAfEndpointEnableDisable(emberAfEndpointFromIndex(dev->GetEndpointId()), false);
                            dev->SetReachable(false);
                        }
                        index++;
                    }
                    ZigbeeRcp_device->dev_state = BRIDGED_LEFT_NETWORK;
                }
                break;
                case BRIDGE_SET_DEV_ACTIVE: {
                    // A Zigbee node may own several Matter endpoints: enable all of them.
                    uint8_t index = 0;
                    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
                    {
                        Device * dev = gDevices[index];
                        if (dev != nullptr && dev->GetZigbeeRcp() == ZigbeeRcp_device)
                        {
                            syslog(LOG_DEBUG, "%s: BRIDGE_SET_DEV_ACTIVE: dev->GetZigbeeRcp(): 0x%x ZigbeeRcp_device: %x \n", __FUNCTION__,dev->GetZigbeeRcp(), ZigbeeRcp_device);
                            syslog(LOG_INFO, "%s: BRIDGE_SET_DEV_ACTIVE: ---> Activate Dynamic Endpoint: %d !!!\n", __FUNCTION__, dev->GetEndpointId());
                            DeviceLayer::StackLock lock;
                            emberAfEndpointEnableDisable(emberAfEndpointFromIndex(dev->GetEndpointId()), true);
                            dev->SetReachable(true);
                        }
                        index++;
                    }
                    ZigbeeRcp_device->dev_state = BRIDGED_REACHABLE;
                }
                break;
                //case BRIDGE_REMOVE_DEV: {
                    //ThisMgr->RemoveDevice(gDevices[ZcbMsg.zcb.matterIndex]);
                    //ZcbMsg.zcb      = { 0 };
                    //ZcbMsg.msg_type = BRIDGE_UNKNOW;
                //}
                //break;

                //case BRIDGE_FACTORY_RESET: {
                    //sleep(1);
                    //ThisMgr->RemoveAllDevice();
                    //ZcbMsg.zcb      = { 0 };
                    //ZcbMsg.msg_type = BRIDGE_UNKNOW;
                //}
                //break;

                //case BRIDGE_WRITE_ATTRIBUTE: {
                    //ZcbAttribute_t * Data = (ZcbAttribute_t *) ZcbMsg.msg_data;
                    //syslog(LOG_DEBUG, "WriteAttributeToDynamicEndpoint: u16ClusterID: %d "
                                    //"u16AttributeID: %d u64Data: %ld\n",
                                    //Data->u16ClusterID, Data->u16AttributeID, Data->u64Data);
                    //ThisMgr->WriteAttributeToDynamicEndpoint(ZcbMsg.zcb, Data->u16ClusterID, Data->u16AttributeID, Data->u64Data,
                                                             //ZCL_INT16S_ATTRIBUTE_TYPE);
                    //free(Data);
                    //ZcbMsg.zcb      = { 0 };
                    //ZcbMsg.msg_type = BRIDGE_UNKNOW;
                //}
                //break;

                    default:
                        break;
                }
            }

            //if (ThisMgr->thread_exit)
            //    return NULL;
        }

        ZigbeeRcpMsg.HandleMask = true;
        pthread_mutex_unlock(&ZigbeeRcpMsg.bridge_mutex);
    }
    return NULL;
}

int BridgeDevMgr::start_ZigbeeRcp_comm_thread()
{
    int res = pthread_create(&ZigbeeRcpComm_thread, NULL, ZigbeeRcpComm_handler, (void *) this);
    if (res)
    {
        syslog(LOG_DEBUG, "%s: Error creating polling thread: %d\n", __FUNCTION__, res);
        return -1;
    }

    return 0;
}

void send_message_to_comm_thread(int MsgType, m2z_device_params_t* ZigbeeRcp_dev, void* data)
{
    pthread_mutex_lock(&ZigbeeRcpMsg.bridge_mutex);

    ZigbeeRcpMsg.HandleMask = false;
    pthread_cond_signal(&ZigbeeRcpMsg.bridge_cond);

    if( ZigbeeRcp_dev != NULL ) {
        ZigbeeRcpMsg.ZigbeeRcp_dev = ZigbeeRcp_dev;
        ZigbeeRcpMsg.msg_type = MsgType;
        ZigbeeRcpMsg.msg_data = data;
        syslog(LOG_DEBUG, "%s: type: %d", __FUNCTION__, ZigbeeRcpMsg.msg_type);
    } else {
        ZigbeeRcpMsg.msg_type = MsgType;
    }

    pthread_mutex_unlock(&ZigbeeRcpMsg.bridge_mutex);
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
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::Reachable::Id);
    }

    if (itemChangedMask & Device::kChanged_Name)
    {
        ScheduleReportingCallback(dev, BridgedDeviceBasicInformation::Id, BridgedDeviceBasicInformation::Attributes::NodeLabel::Id);
    }
}

Protocols::InteractionModel::Status HandleReadBridgedDeviceBasicAttribute(Device * dev, chip::AttributeId attributeId, uint8_t * buffer,
                                                    uint16_t maxReadLength)
{
    using namespace BridgedDeviceBasicInformation::Attributes;

    syslog(LOG_DEBUG, "%s: attrId=%d, maxReadLength=%d",__FUNCTION__,
                    attributeId, maxReadLength);

    //if ((attributeId == Reachable::Id) && (maxReadLength == 1))
    //{
        //*buffer = dev->IsReachable() ? 1 : 0;
    //}
    //else if ((attributeId == NodeLabel::Id) && (maxReadLength == 32))
    //{
        //MutableByteSpan zclNameSpan(buffer, maxReadLength);
        //MakeZclCharString(zclNameSpan, (dev->GetName()).c_str());
    //}
    //else if ((attributeId == ClusterRevision::Id) && (maxReadLength == 2))
    //{
        //uint16_t rev = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_CLUSTER_REVISION;
        //memcpy(buffer, &rev, sizeof(rev));
    //}
    //else if ((attributeId == FeatureMap::Id) && (maxReadLength == 4))
    //{
        //uint32_t featureMap = ZCL_BRIDGED_DEVICE_BASIC_INFORMATION_FEATURE_MAP;
        //memcpy(buffer, &featureMap, sizeof(featureMap));
    //}
    //else
    //{
        //return Protocols::InteractionModel::Status::Failure;
    //}

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status BridgeDevMgr::HandleReadAttribute(Device * dev, ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength)
{
    chip::AttributeId translated_attributeId = attributeId;
    bool is_bridged_basic = (clusterId == BridgedDeviceBasicInformation::Id);
    
    syslog(LOG_DEBUG, "%s: attrId=%d, maxReadLength=%d",__FUNCTION__, translated_attributeId,
                    maxReadLength);

    // if clusterId is BridgedDeviceBasicInformation::Id
    if(is_bridged_basic)
    {
        // Reachable is a Matter-only attribute (no Zigbee equivalent): answer it
        // directly from the bridge's link state so the controller can gate
        // control on the bridged device's real reachability.
        if (attributeId == BridgedDeviceBasicInformation::Attributes::Reachable::Id)
        {
            *buffer = dev->IsReachable() ? 1 : 0;
            return Protocols::InteractionModel::Status::Success;
        }
        // Zigbee clusterId should be 0x0 (the Basic Zigbee cluster)
        clusterId = 0;
        // a translation is also required from Matter to Zigbee attribute ID
        switch(attributeId)
        {
            case chip::app::Clusters::BasicInformation::Attributes::DataModelRevision::Id:         translated_attributeId = ZB_ZCL_ATTR_BASIC_ZCL_VERSION_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::VendorName::Id:             translated_attributeId = ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::VendorID::Id:                 translated_attributeId = 0xFF;break;
            case chip::app::Clusters::BasicInformation::Attributes::ProductName::Id:             translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::ProductID::Id:                 translated_attributeId = ZB_ZCL_ATTR_BASIC_PRODUCT_CODE_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::NodeLabel::Id:                 translated_attributeId = ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::Location::Id:                 translated_attributeId = ZB_ZCL_ATTR_BASIC_LOCATION_DESCRIPTION_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::HardwareVersion::Id:         translated_attributeId = ZB_ZCL_ATTR_BASIC_HW_VERSION_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::HardwareVersionString::Id:     translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::SoftwareVersion::Id:         translated_attributeId = ZB_ZCL_ATTR_BASIC_SW_BUILD_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::SoftwareVersionString::Id:    translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::ManufacturingDate::Id:         translated_attributeId = ZB_ZCL_ATTR_BASIC_DATE_CODE_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::PartNumber::Id:             translated_attributeId = ZB_ZCL_ATTR_BASIC_MANUFACTURER_VERSION_DETAILS_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::ProductURL::Id:             translated_attributeId = ZB_ZCL_ATTR_BASIC_PRODUCT_URL_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::ProductLabel::Id:             translated_attributeId = ZB_ZCL_ATTR_BASIC_PRODUCT_LABEL_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::SerialNumber::Id:             translated_attributeId = ZB_ZCL_ATTR_BASIC_SERIAL_NUMBER_ID;break;
            case chip::app::Clusters::BasicInformation::Attributes::LocalConfigDisabled::Id:     translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::Reachable::Id:                 translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::UniqueID::Id:                 translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::CapabilityMinima::Id:         translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::ProductAppearance::Id:         translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::SpecificationVersion::Id:     translated_attributeId = 0xff;break;
            case chip::app::Clusters::BasicInformation::Attributes::MaxPathsPerInvoke::Id:         translated_attributeId = 0xff;break;
            //case chip::app::Clusters::BasicInformation::Attributes::DeviceLocation::Id:         translated_attributeId = 0xff;break;
        }
        
    }
    if (is_bridged_basic)
    {
        // The Zigbee Basic cluster is a node level cluster: read it from whichever endpoint carries it.
        m2z_schedule_read_attribute(dev->GetZigbeeRcp(), MATTER_ZIGBEERCP_BRIDGE_INVALID_EP_INDEX, (uint16_t)clusterId, (uint16_t)translated_attributeId);
    }
    else
    {
        dev->readAttribute(dev, (uint16_t)clusterId, (uint16_t)translated_attributeId);
    }
    dev->NodeReadAttributeIdFromClusterId(dev->GetZigbeeRcp(), (uint16_t)clusterId, (uint16_t)translated_attributeId, buffer, maxReadLength);
    //if ((attributeId == OnOff::Attributes::OnOff::Id) && (maxReadLength == 1))
    //{
        //*buffer = dev->IsOn() ? 1 : 0;
    //}
    //else if ((attributeId == OnOff::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    //{
        //uint16_t rev = ZCL_ON_OFF_CLUSTER_REVISION;
        //memcpy(buffer, &rev, sizeof(rev));
    //}
    //else
    //{
        //return Protocols::InteractionModel::Status::Failure;
    //}

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status BridgeDevMgr::HandleWriteAttribute(Device * dev, ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer)
{
    syslog(LOG_DEBUG, "%s: clusterId=%d attrId=%d",__FUNCTION__, clusterId, attributeId);
    dev->writeAttribute(dev, (uint16_t)clusterId, (uint16_t)attributeId, buffer);

    //if ((attributeId == OnOff::Attributes::OnOff::Id) && (dev->IsReachable()))
    //{
        //if (*buffer)
        //{
            //dev->SetOnOff(true);
        //}
        //else
        //{
            //dev->SetOnOff(false);
        //}
    //}
    //else
    //{
        //return Protocols::InteractionModel::Status::Failure;
    //}

    return Protocols::InteractionModel::Status::Success;
}

void GetAttributeReadOnlyStatus(ClusterId clusterId, chip::AttributeId attributeId, Protocols::InteractionModel::Status *attr_zb_status)
{
    switch (clusterId)
    {   case chip::app::Clusters::OnOff::Id:
        {
            switch(attributeId)
            {
                case chip::app::Clusters::OnOff::Attributes::OnOff::Id:
                case chip::app::Clusters::OnOff::Attributes::GlobalSceneControl::Id:
                {
                    *attr_zb_status = Protocols::InteractionModel::Status::UnsupportedWrite;
                }
                break;
            }
        }
        break;
        case chip::app::Clusters::LevelControl::Id:
        {
            switch(attributeId)
            {
                case chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id:
                case chip::app::Clusters::LevelControl::Attributes::RemainingTime::Id:
                case chip::app::Clusters::LevelControl::Attributes::MinLevel::Id:
                case chip::app::Clusters::LevelControl::Attributes::MaxLevel::Id:
                case chip::app::Clusters::LevelControl::Attributes::CurrentFrequency::Id:
                case chip::app::Clusters::LevelControl::Attributes::MinFrequency::Id:
                case chip::app::Clusters::LevelControl::Attributes::MaxFrequency::Id:
                {
                    *attr_zb_status = Protocols::InteractionModel::Status::UnsupportedWrite;
                }
                break;
            }
        }
        break;
    }
    
    syslog(LOG_DEBUG, "<< %s attribute_zb_status: %d ", __FUNCTION__, *attr_zb_status);
    
}

Protocols::InteractionModel::Status CanWriteAttribute(Device * dev, ClusterId clusterId, chip::AttributeId attributeId, bool check_from_standard_spec)
{
    Protocols::InteractionModel::Status zcl_attr_status = Protocols::InteractionModel::Status::Success;
    syslog(LOG_DEBUG, "%s: clusterId=%d attrId=%d", __FUNCTION__, clusterId, attributeId);
    if(check_from_standard_spec)
    {
        GetAttributeReadOnlyStatus((uint16_t)clusterId, (uint16_t)attributeId, (Protocols::InteractionModel::Status *)&zcl_attr_status);
    }
    else
    {
        dev->canWriteAttribute(dev, (uint16_t)clusterId, (uint16_t)attributeId, &zcl_attr_status);
    }
    
    return(zcl_attr_status);
}

Protocols::InteractionModel::Status BridgeDevMgr::HandleSendCommand(Device* dev)
{
    syslog(LOG_DEBUG, "%s:",__FUNCTION__);
    uint8_t ret = 0;
    ret = dev->sendCommand(dev);
    if (ret != 0)
    {
        return Protocols::InteractionModel::Status::Failure;
    }
    return Protocols::InteractionModel::Status::Success;
}


void BridgeDevMgr::HandleZigbeeDeviceStatusChanged(Device * dev, Device::Changed_t itemChangedMask)
{
    if (itemChangedMask & (Device::kChanged_Reachable | Device::kChanged_Name | Device::kChanged_Location))
    {
        HandleDeviceStatusChanged(static_cast<Device *>(dev), (Device::Changed_t) itemChangedMask);
    }

    if (itemChangedMask & Device::kChanged_Reachable)
    {
        // Re-report OnOff only when the device comes online, so the controller can
        // clear its offline flag on a fresh value. Reporting on the offline
        // transition would carry a stale value and wrongly mark the device online.
        if (dev->IsReachable())
        {
            ScheduleReportingCallback(dev, OnOff::Id, OnOff::Attributes::OnOff::Id);
        }
    }
}

// Static endpoints (e.g. the dummy endpoint 2) defer every attribute to external
// storage, but there is no dynamic Device backing them. Code-driven clusters read
// mandatory attributes (FeatureMap, SceneTableSize, Tolerance, ...) during
// initialization through the external attribute read path and VerifyOrDie when the
// read fails. For those endpoints, return the attribute's default value so the
// reads succeed. This mirrors emAfApplyDefaults() in src/app/util/attribute-storage.cpp.
static Protocols::InteractionModel::Status ReadStaticAttributeDefault(const EmberAfAttributeMetadata * attributeMetadata,
                                                                      uint8_t * buffer)
{
    const uint8_t * defaultValue                   = nullptr;
    size_t defaultValueSizeForBigEndianNudger      = 0;

    if (attributeMetadata->HasMinMax())
    {
        if (attributeMetadata->size <= 2)
        {
            defaultValue = reinterpret_cast<const uint8_t *>(
                &(attributeMetadata->defaultValue.ptrToMinMaxValue->defaultValue.defaultValue));
            defaultValueSizeForBigEndianNudger =
                sizeof(attributeMetadata->defaultValue.ptrToMinMaxValue->defaultValue.defaultValue);
        }
        else
        {
            defaultValue = attributeMetadata->defaultValue.ptrToMinMaxValue->defaultValue.ptrToDefaultValue;
        }
    }
    else if ((attributeMetadata->size <= 4) && !emberAfIsStringAttributeType(attributeMetadata->attributeType))
    {
        defaultValue                      = reinterpret_cast<const uint8_t *>(&(attributeMetadata->defaultValue.defaultValue));
        defaultValueSizeForBigEndianNudger = sizeof(attributeMetadata->defaultValue.defaultValue);
    }
    else
    {
        defaultValue = attributeMetadata->defaultValue.ptrToDefaultValue;
    }

    if (defaultValue == nullptr)
    {
        memset(buffer, 0, attributeMetadata->size);
    }
    else
    {
#if CHIP_CONFIG_BIG_ENDIAN_TARGET
        if (attributeMetadata->size < defaultValueSizeForBigEndianNudger)
        {
            defaultValue += (defaultValueSizeForBigEndianNudger - attributeMetadata->size);
        }
#endif // CHIP_CONFIG_BIG_ENDIAN_TARGET
        memcpy(buffer, defaultValue, attributeMetadata->size);
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
                                                   const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer,
                                                   uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;
    syslog(LOG_INFO, "%s: ep=%d endpointIndex=%d clusterId=%d attributeId=%d  maxReadLength=%d\n",__FUNCTION__,
                    endpoint, endpointIndex, clusterId, attributeMetadata->attributeId, maxReadLength);

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (BridgeDevMgr::gDevices[endpointIndex] != nullptr))
    {
        Device * dev = BridgeDevMgr::gDevices[endpointIndex];

        if (dev->IsReachable())
        {
            ret = BridgeDevMgr::HandleReadAttribute(dev, clusterId, attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else
        {
            // Just-bridged devices are not reachable until their first report/web
            // comes in, yet code-driven clusters (e.g. Scenes) validate mandatory
            // attributes while the endpoint is registered. Serve the default value
            // so that initialization does not abort.
            ret = ReadStaticAttributeDefault(attributeMetadata, buffer);
        }
    }
    else
    {
        // Not backed by a dynamic Device (a static endpoint). Serve the default
        // value so code-driven cluster initialization on static endpoints succeeds.
        ret = ReadStaticAttributeDefault(attributeMetadata, buffer);
    }

    return ret;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
                                                    const EmberAfAttributeMetadata * attributeMetadata, uint8_t * buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;
    bool do_send_command = false;

    syslog(LOG_INFO, "%s: ep=%d endpointIndex=%d clusterId=%d attributeId=%d\n", __FUNCTION__,
                    endpoint, endpointIndex, clusterId, attributeMetadata->attributeId);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        Device * dev = BridgeDevMgr::gDevices[endpointIndex];

        if (dev->IsReachable())
        {
            ret = CanWriteAttribute(static_cast<Device *>(dev), clusterId, attributeMetadata->attributeId, true);
            if(ret == Protocols::InteractionModel::Status::Success)
            {
                ret = BridgeDevMgr::HandleWriteAttribute(static_cast<Device *>(dev), clusterId, attributeMetadata->attributeId, buffer);
            }
            if(ret == Protocols::InteractionModel::Status::UnsupportedWrite)
            {
                zb_uint16_t endpoint_idx=0;
                zb_uint16_t cluster_idx=0;
                syslog(LOG_INFO, "%s: UNSUPPORTED_WRITE for ep=%d endpointIndex=%d clusterId=%d attributeId=%d\n", __FUNCTION__,
                                endpoint, endpointIndex, clusterId, attributeMetadata->attributeId);
                // Matter wants to write a Zigbee bridged node attribute which is ReadOnly attribute
                // A translation to a Command may be needed here, but not implemented yet...
                // Store the value into the Zigbee attribute shadow so the ZCL server
                // (e.g. the LevelControl transition) converges instead of looping forever,
                // and later reads/reports reflect the new value.
                dev->NodeWriteAttributeIdToClusterId(dev->GetZigbeeRcp(), (uint16_t)clusterId,
                                                     (uint16_t)attributeMetadata->attributeId, buffer,
                                                     attributeMetadata->size);
                
                //if(dev->GetEndpointClusterIndexesForClusterId(dev->GetZigbeeRcp(), clusterId, &endpoint_idx, &cluster_idx))
                //{
                    //dev->cmdSize = 0;
                    //dev->pendingCmdEndpointId = endpoint;
                    //dev->pendingCmdClusterId = clusterId;
                    //switch (clusterId)
                    //{
                        //case chip::app::Clusters::OnOff::Id:
                        //{
                            //switch(attributeMetadata->attributeId)
                            //{
                                //case chip::app::Clusters::OnOff::Attributes::OnOff::Id:
                                //{
                                    //if(*buffer == 0)
                                    //{
                                        //dev->pendingCmdCommandId = chip::app::Clusters::OnOff::Commands::Off::Id;
                                    //}
                                    //else
                                    //{
                                        //dev->pendingCmdCommandId = chip::app::Clusters::OnOff::Commands::On::Id;
                                    //}
                                    //do_send_command = true;
                                //}
                                //break;
                            //}
                        //}
                        //break;
                        //case chip::app::Clusters::LevelControl::Id:
                        //{
                            //switch(attributeMetadata->attributeId)
                            //{
                                //case chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id:
                                //{
                                    //dev->pendingCmdCommandId = chip::app::Clusters::LevelControl::Commands::MoveToLevel::Id;
                                    //memcpy(&(dev->cmdData[dev->cmdSize]), buffer, attributeMetadata->size);
                                    //dev->cmdSize += attributeMetadata->size;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //dev->cmdData[dev->cmdSize++] = 0;
                                    //do_send_command = true;
                                //}
                                //break;
                            //}
                        //}
                        //break;
                    //}
                    //if (dev->IsReachable() && do_send_command)
                    //{
                        //syslog(LOG_DEBUG, "%s: HandleSendCommand for ep=%d endpointIndex=%d clusterId=%d attributeId=%d\n", __FUNCTION__,
                            //endpoint, endpointIndex, clusterId, attributeMetadata->attributeId);
                        ////ret = gDevMgr->HandleSendCommand(static_cast<Device *>(dev));
                        //syslog(LOG_DEBUG, "%s: Command successfully processed for ep=%d endpointIndex=%d clusterId=%d attributeId=%d\n", __FUNCTION__,
                            //endpoint, endpointIndex, clusterId, attributeMetadata->attributeId);
                        
                    //}
                //}
            }
            if(ret == Protocols::InteractionModel::Status::UnsupportedAttribute)
            {
                syslog(LOG_INFO, "%s: UNSUPPORTED_ATTRIBUTE for ep=%d endpointIndex=%d clusterId=%d attributeId=%d\n", __FUNCTION__,
                                endpoint, endpointIndex, clusterId, attributeMetadata->attributeId);
            }
        }
    }

    //return ret;
    return(Protocols::InteractionModel::Status::Success);
}

#include <lib/core/TLVData.h>
#include <lib/core/TLVTags.h>
#include <lib/core/TLVUtilities.h>
//constexpr uint8_t sNoFields[] = {
    //CHIP_TLV_STRUCTURE(CHIP_TLV_TAG_ANONYMOUS),
    //CHIP_TLV_END_OF_CONTAINER,
//};

//CHIP_ERROR __attribute__((weak)) MatterPreCommandReceivedCallback(const chip::app::ConcreteCommandPath & commandPath,
                                                                  //const chip::Access::SubjectDescriptor & subjectDescriptor, chip::app::CommandDataIB::Parser & aCommandElement)
//chip::DataModelCallbacks::PreCommandReceived(const chip::app::ConcreteCommandPath & commandPath,
                                                                  //const chip::Access::SubjectDescriptor & subjectDescriptor, chip::app::CommandDataIB::Parser & aCommandElement)
//{
    //uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(commandPath.mEndpointId);
    //Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;
    //CHIP_ERROR TLVError = CHIP_NO_ERROR;

    //syslog(LOG_DEBUG, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mCommandId=0x%x\n", __FUNCTION__,
                    //commandPath.mEndpointId ,endpointIndex ,commandPath.mClusterId ,commandPath.mCommandId);

    //if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    //{
        //Device * dev = BridgeDevMgr::gDevices[endpointIndex];
        //dev->dev_pending_command = true;
        //dev->pendingCmdEndpointId = commandPath.mEndpointId;
        //dev->pendingCmdClusterId = commandPath.mClusterId;
        //dev->pendingCmdCommandId = commandPath.mCommandId;
        //dev->cmdSize = 0;

        //CommandPathIB::Parser cmdPath;
        //ConcreteCommandPath concretePath(0, 0, 0);
        //TLV::TLVReader DataReader;

        //TLVError = aCommandElement.GetPath(&cmdPath);
        //TLVError = cmdPath.GetConcreteCommandPath(concretePath);
        //TLVError = aCommandElement.GetFields(&DataReader);

        
        //switch (commandPath.mClusterId)
        //{
            //case chip::app::Clusters::LevelControl::Id:
            //{
                //switch (commandPath.mCommandId)
                //{
                    //case chip::app::Clusters::LevelControl::Commands::MoveToLevel::Id:
                    //{
                        //chip::app::Clusters::LevelControl::Commands::MoveToLevel::DecodableType commandData;
                        //DataModel::Decode(DataReader, commandData);
                        //dev->cmdData[dev->cmdSize++] = commandData.level;
                        //dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.transitionTime.Value() >> 0) & 0xff;
                        //dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.transitionTime.Value() >> 8) & 0xff;
                        //dev->cmdData[dev->cmdSize++] = (commandData.optionsMask.Raw() >> 0) & 0xff;
                        //dev->cmdData[dev->cmdSize++] = (commandData.optionsOverride.Raw() >> 0) & 0xff;
                        
                        //break;
                    //}
                //}
            //}
            //case chip::app::Clusters::Groups::Id:
            //{
                //switch (commandPath.mCommandId)
                //{
                    //case chip::app::Clusters::Groups::Commands::AddGroup::Id:
                    //{
                        //chip::app::Clusters::Groups::Commands::AddGroup::DecodableType commandData;
                        //DataModel::Decode(DataReader, commandData);
                        //dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.groupID >> 0) & 0xff;
                        //dev->cmdData[dev->cmdSize++] = (uint8_t)(commandData.groupID >> 8) & 0xff;
                        //memcpy(&(dev->cmdData[dev->cmdSize]), commandData.groupName.data(), commandData.groupName.size());
                        //dev->cmdSize += commandData.groupName.size();
                        
                        //break;
                    //}
                //}
            //}
        //}
    //}
    //return(CHIP_NO_ERROR);
//}
//void __attribute__((weak)) MatterPostCommandReceivedCallback(const chip::app::ConcreteCommandPath & commandPath,
                                                             //const chip::Access::SubjectDescriptor & subjectDescriptor, chip::app::CommandDataIB::Parser & aCommandElement)
//chip::DataModelCallbacks::PostCommandReceived(const chip::app::ConcreteCommandPath & commandPath,
                                                             //const chip::Access::SubjectDescriptor & subjectDescriptor, chip::app::CommandDataIB::Parser & aCommandElement)
//{
    //uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(commandPath.mEndpointId);
    //Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    //if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    //{
        //Device * dev = BridgeDevMgr::gDevices[endpointIndex];
        //if(dev->dev_pending_command == true)
        //{
            //syslog(LOG_DEBUG, "%s Command is pending: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                            //dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
            //if (dev->IsReachable())
            //{
                //ret = HandleSendCommand(static_cast<Device *>(dev));
                //dev->dev_pending_command = false;
                //syslog(LOG_DEBUG, "%s Command successfully processed: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                            //dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);            }

            ////if the Cluster is OnOff, we need a workarround on the LevelControl cluster if the device has this cluster
            ////end the command to move to level = 0 in case of "Off" cmd
            ////send the command to move to level = 254 in case of "On" cmd
            //switch (commandPath.mClusterId)
            //{
                //case chip::app::Clusters::OnOff::Id:
                //{
                    //zb_uint16_t endpoint_idx=0;
                    //zb_uint16_t cluster_idx=0;

                    //if(dev->GetEndpointClusterIndexesForClusterId(dev->GetZigbeeRcp(), chip::app::Clusters::LevelControl::Id, &endpoint_idx, &cluster_idx))
                    //{
                        //dev->cmdSize = 0;
                        //dev->pendingCmdEndpointId = commandPath.mEndpointId;
                        //dev->pendingCmdClusterId = chip::app::Clusters::LevelControl::Id;
                        //dev->pendingCmdCommandId = chip::app::Clusters::LevelControl::Commands::MoveToLevel::Id;
                        //switch (commandPath.mCommandId)
                        //{
                            //case chip::app::Clusters::OnOff::Commands::On::Id:
                            //{
                                //dev->cmdData[dev->cmdSize++] = 254;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //break;
                            //}
                            //case chip::app::Clusters::OnOff::Commands::Off::Id:
                            //{
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //dev->cmdData[dev->cmdSize++] = 0;
                                //break;
                            //}
                            //case chip::app::Clusters::OnOff::Commands::Toggle::Id:
                            //{
                                //// Uggly workarround: get the current level to toggle it !
                                ////uint8_t currentLevel = 0;
                                ////ret = HandleReadAttribute(dev, chip::app::Clusters::LevelControl::Id, chip::app::Clusters::LevelControl::Attributes::CurrentLevel::Id, &currentLevel, sizeof(currentLevel));
                                ////syslog(LOG_DEBUG, "%s currentLevel=%d\n", __FUNCTION__, currentLevel);
                                ////if(currentLevel <=1)
                                ////{
                                    ////dev->cmdData[dev->cmdSize++] = 254;
                                ////}
                                ////else
                                ////{
                                    ////dev->cmdData[dev->cmdSize++] = 1;
                                ////}
                                ////dev->cmdData[dev->cmdSize++] = 0;
                                ////dev->cmdData[dev->cmdSize++] = 0;
                                ////dev->cmdData[dev->cmdSize++] = 0;
                                ////dev->cmdData[dev->cmdSize++] = 0;
                                
                                //// to be redesigned completly, skip it for now on...
                            
                                //break;
                            //}
                        //}
                        //if (dev->IsReachable())
                        //{
                            //ret = HandleSendCommand(static_cast<Device *>(dev));
                        //}
                        //syslog(LOG_DEBUG, "%s Command successfully processed: mEndpointId=%d endpointIndex=%d clusterId=0x%x commandId=0x%x\n", __FUNCTION__,
                            //dev->pendingCmdEndpointId ,commandPath.mEndpointId ,dev->pendingCmdClusterId ,dev->pendingCmdCommandId);
                            
                    //}
                //}
            //}
        //}
    //}
//}

//void MatterPreAttributeReadCallback(const chip::app::ConcreteAttributePath & attributePath)
//{
    //uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(attributePath.mEndpointId);
    //Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    //if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    //{
        //Device * dev = BridgeDevMgr::gDevices[endpointIndex];
        //syslog(LOG_DEBUG, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mAttributeId=0x%x\n", __FUNCTION__,
                        //dev->pendingCmdEndpointId ,attributePath.mEndpointId ,attributePath.mClusterId, attributePath.mAttributeId );
    //}
//}

//void MatterPostAttributeReadCallback(const chip::app::ConcreteAttributePath & attributePath)
//{
    //uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(attributePath.mEndpointId);
    //Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    //if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    //{
        //Device * dev = BridgeDevMgr::gDevices[endpointIndex];
        //syslog(LOG_DEBUG, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mAttributeId=0x%x\n", __FUNCTION__,
                        //dev->pendingCmdEndpointId ,attributePath.mEndpointId ,attributePath.mClusterId, attributePath.mAttributeId );
    //}
//}

//void MatterPreAttributeWriteCallback(const chip::app::ConcreteAttributePath & attributePath)
//{
    //uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(attributePath.mEndpointId);
    //Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    //if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    //{
        //Device * dev = BridgeDevMgr::gDevices[endpointIndex];
        //syslog(LOG_DEBUG, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mAttributeId=0x%x\n", __FUNCTION__,
                        //dev->pendingCmdEndpointId ,attributePath.mEndpointId ,attributePath.mClusterId, attributePath.mAttributeId );
    //}
//}

//void MatterPostAttributeWriteCallback(const chip::app::ConcreteAttributePath & attributePath)
//{
    //uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(attributePath.mEndpointId);
    //Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    //if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    //{
        //Device * dev = BridgeDevMgr::gDevices[endpointIndex];
        //syslog(LOG_DEBUG, "%s: mEndpointId=%d endpointIndex=%d mClusterId=0x%x mAttributeId=0x%x\n", __FUNCTION__,
                        //dev->pendingCmdEndpointId ,attributePath.mEndpointId ,attributePath.mClusterId, attributePath.mAttributeId );
    //}
//}



// -----------------------------------------------------------------------------------------
//  zcb node report attribute to Bridge
// -----------------------------------------------------------------------------------------
//bool BridgeDevMgr::WriteAttributeToDynamicEndpoint(newdb_zcb_t zcb, uint16_t u16ClusterID, uint16_t u16AttributeID,
                                                   //uint64_t u64Data, uint8_t u8DataType)
//{
    //if (gDevices[zcb.matterIndex] == NULL)
    //{
        //return false;
    //}

    //if (u8DataType == ZCL_INT16S_ATTRIBUTE_TYPE)
    //{
        //DeviceTempSensor * TempSensor = static_cast<DeviceTempSensor *>(gDevices[zcb.matterIndex]);
        //TempSensor->SetMeasuredValue(static_cast<int16_t>(u64Data));
        //return true;
    //}

    //return false;
//}
