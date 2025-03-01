/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
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

#include <credentials/DeviceAttestationCredsProvider.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.h>

#include "CHIPPlatformConfig.h"
#include "K32W0Config.h"
#include <platform/internal/GenericConfigurationManagerImpl.h>

namespace chip {
namespace DeviceLayer {

/**
 * @brief This class provides Commissionable data, Device Attestation Credentials,
 *        and Device Instance Info.
 */

class K32W0FactoryDataProvider :
    public DeviceInstanceInfoProvider,
    public CommissionableDataProvider,
    public Credentials::DeviceAttestationCredentialsProvider
{
public:
    // Default factory data IDs
    enum FactoryDataId
    {
        kVerifierId = 1,
        kSaltId,
        kIcId,
        kDacPrivateKeyId,
        kDacCertificateId,
        kPaiCertificateId,
        kDiscriminatorId,
        kSetupPasscodeId,
        kVidId,
        kPidId,
        kCertDeclarationId,
        kVendorNameId,
        kProductNameId,
        kSerialNumberId,
        kManufacturingDateId,
        kHardwareVersionId,
        kHardwareVersionStrId,
        kUniqueId,
        kMaxId
    };

#if CHIP_DEVICE_CONFIG_USE_CUSTOM_PROVIDER
#if !CHIP_DEVICE_CONFIG_CUSTOM_PROVIDER_NUMBER_IDS
#error "CHIP_DEVICE_CONFIG_CUSTOM_PROVIDER_NUMBER_IDS must be > 0 if custom provider is enabled."
#endif
    static constexpr uint16_t kNumberOfIds = 
        FactoryDataId::kMaxId + CHIP_DEVICE_CONFIG_CUSTOM_PROVIDER_NUMBER_IDS;
#else
    static constexpr uint16_t kNumberOfIds = FactoryDataId::kMaxId;
#endif

    static K32W0FactoryDataProvider & GetDefaultInstance();

	K32W0FactoryDataProvider();

    CHIP_ERROR Init();
    CHIP_ERROR SearchForId(uint8_t searchedType, uint8_t *pBuf, size_t bufLength, uint16_t & length);

    // Custom factory data providers must implement this method in order to define
    // their own custom IDs.
    virtual CHIP_ERROR SetCustomIds();

    // ===== Members functions that implement the CommissionableDataProvider
    CHIP_ERROR GetSetupDiscriminator(uint16_t & setupDiscriminator) override;
    CHIP_ERROR SetSetupDiscriminator(uint16_t setupDiscriminator) override;
    CHIP_ERROR GetSpake2pIterationCount(uint32_t & iterationCount) override;
    CHIP_ERROR GetSpake2pSalt(MutableByteSpan & saltBuf) override;
    CHIP_ERROR GetSpake2pVerifier(MutableByteSpan & verifierBuf, size_t & verifierLen) override;
    CHIP_ERROR GetSetupPasscode(uint32_t & setupPasscode) override;
    CHIP_ERROR SetSetupPasscode(uint32_t setupPasscode) override;

    // ===== Members functions that implement the DeviceAttestationCredentialsProvider
    CHIP_ERROR GetCertificationDeclaration(MutableByteSpan & outBuffer) override;
    CHIP_ERROR GetFirmwareInformation(MutableByteSpan & out_firmware_info_buffer) override;
    CHIP_ERROR GetDeviceAttestationCert(MutableByteSpan & outBuffer) override;
    CHIP_ERROR GetProductAttestationIntermediateCert(MutableByteSpan & outBuffer) override;
    CHIP_ERROR SignWithDeviceAttestationKey(const ByteSpan & messageToSign, MutableByteSpan & outSignBuffer) override;

    // ===== Members functions that implement the GenericDeviceInstanceInfoProvider
    CHIP_ERROR GetVendorName(char * buf, size_t bufSize) override;
    CHIP_ERROR GetVendorId(uint16_t & vendorId) override;
    CHIP_ERROR GetProductName(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductId(uint16_t & productId) override;
    CHIP_ERROR GetHardwareVersionString(char * buf, size_t bufSize) override;
    CHIP_ERROR GetSerialNumber(char * buf, size_t bufSize) override;
    CHIP_ERROR GetManufacturingDate(uint16_t & year, uint8_t & month, uint8_t & day) override;
    CHIP_ERROR GetHardwareVersion(uint16_t & hardwareVersion) override;
    CHIP_ERROR GetRotatingDeviceIdUniqueId(MutableByteSpan & uniqueIdSpan) override;

protected:
    uint16_t maxLengths[kNumberOfIds];
};

} // namespace DeviceLayer
} // namespace chip
