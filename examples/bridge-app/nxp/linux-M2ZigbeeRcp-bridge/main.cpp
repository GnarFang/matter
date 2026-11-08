/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include <AppMain.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <app/EventLogging.h>
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <app/reporting/reporting.h>
#include <app/util/af-types.h>
#include <app/util/attribute-storage.h>
#include <app/util/util.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/ZclString.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>
#include <platform/Linux/NetworkCommissioningDriver.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <app/server/Server.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <iostream>
#include <vector>

#include "CommissionableInit.h"
#include "Device.h"
#include "main.h"
#include "ZigbeeRcpImpl.h"
#include "Bridge.h"

using namespace chip;
using namespace chip::app;
using namespace chip::Credentials;
using namespace chip::Inet;
using namespace chip::Transport;
using namespace chip::DeviceLayer;
using namespace chip::app::Clusters;

#if CHIP_DEVICE_CONFIG_ENABLE_WPA
DeviceLayer::NetworkCommissioning::LinuxWiFiDriver sLinuxWiFiDriver;
Clusters::NetworkCommissioning::Instance sWiFiNetworkCommissioningInstance(0, &sLinuxWiFiDriver);
#endif

void ApplicationInit()
{
#if CHIP_DEVICE_CONFIG_ENABLE_WPA
    sWiFiNetworkCommissioningInstance.Init();
#endif
}


#define POLL_INTERVAL_MS (100)
uint8_t poll_prescale = 0;

bool kbhit()
{
    int byteswaiting;
    ioctl(0, FIONREAD, &byteswaiting);
    return byteswaiting > 0;
}

void * bridge_polling_thread(void * context)
{
    Bridge* bridge = (Bridge *)context;

    while (1)
    {
        if (kbhit())
        {
            int ch = getchar();

            switch (ch)
            {
                case '1': {
                    //ePermitJoinOn(0xFF);
                    printf("permit join done ! \n");
                }
                break;

                case '2': {
                    //newDbPrintDevices();
                }
                break;

                case '3': {
                    //eFactoryNew();
                    // bridge->test();
                }
                break;

                case 'q': {
                    delete bridge;
                    chip::Server::GetInstance().Shutdown();
                    chip::DeviceLayer::PlatformMgr().StopEventLoopTask();
                    chip::DeviceLayer::PlatformMgr().Shutdown();
                    return NULL;
                }

                default:
                    break;
            }
        }

        // Sleep to avoid tight loop reading commands
        usleep(POLL_INTERVAL_MS * 1000);
    }

    return NULL;
}

int main(int argc, char * argv[])
{
    // Optional device persistence (stable Matter endpoint ids + restore of the
    // bridged device list after a reboot) is enabled with --persist or the
    // M2Z_PERSIST=1 environment variable (handy for systemd units that cannot
    // pass extra arguments). The flag is removed from argv so that
    // ChipLinuxAppInit does not reject it.
    {
        const char * persist_env = getenv("M2Z_PERSIST");
        if (persist_env != nullptr && (strcmp(persist_env, "1") == 0 || strcasecmp(persist_env, "true") == 0))
        {
            m2z_set_persist_enabled(ZB_TRUE);
        }
    }
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--persist") == 0)
        {
            m2z_set_persist_enabled(ZB_TRUE);
            for (int j = i; j < argc - 1; j++)
            {
                argv[j] = argv[j + 1];
            }
            argc--;
            i--;
        }
    }

    Bridge* bridge = new Bridge();
    if ( bridge->Init() != CHIP_NO_ERROR)
        return -1;

    if (ChipLinuxAppInit(argc, argv) != 0)
    {
        return -1;
    }

    // Init Data Model and CHIP App Server
    static chip::CommonCaseDeviceServerInitParams initParams;
    (void) initParams.InitializeStaticResourcesBeforeServerInit();
    initParams.dataModelProvider = CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);

#if CHIP_DEVICE_ENABLE_PORT_PARAMS
    // use a different service port to make testing possible with other sample devices running on same host
    initParams.operationalServicePort = LinuxDeviceOptions::GetInstance().securedDevicePort;
#endif

    initParams.interfaceId = LinuxDeviceOptions::GetInstance().interfaceId;

    // Initialize device attestation config. This must be set before server init,
    // because the Operational Credentials cluster snapshots the provider during
    // cluster construction.
    SetDeviceAttestationCredentialsProvider(Examples::GetExampleDACProvider());

    chip::Server::GetInstance().Init(initParams);

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);
    bridge->ServiceStart();

    //{
        //pthread_t poll_thread;
        //int res = pthread_create(&poll_thread, nullptr, bridge_polling_thread, bridge);
        //if (res)
        //{
            //printf("Error creating polling thread: %d\n", res);
            //exit(1);
        //}
    //}

    // Run CHIP
    ApplicationInit();
    chip::DeviceLayer::PlatformMgr().RunEventLoop();

    return 0;
}
