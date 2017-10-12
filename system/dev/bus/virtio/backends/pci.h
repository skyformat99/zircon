// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once
#include "backend.h"
#include <zircon/thread_annotations.h>

namespace virtio {

class PciBackend : public Backend {
public:
    PciBackend(pci_protocol_t pci, zx_pcie_device_info_t info)
        : pci_(pci), info_(info) {}
    zx_status_t Bind() override;
    virtual zx_status_t Init(void) = 0;
protected:
    pci_protocol_t pci_ = { nullptr, nullptr };
    zx_pcie_device_info_t info_;
};

class PciLegacyBackend : public PciBackend {
public:
    PciLegacyBackend(pci_protocol_t pci, zx_pcie_device_info_t info)
        : PciBackend(pci, info) {}
    virtual ~PciLegacyBackend();
    zx_status_t Init() override;

    void DriverStatusOk(void);
    void DriverStatusAck(void);
    void DeviceReset(void);
    uint32_t IsrStatus(void);

    // These handle writing to/from a device's device config to allow derived
    // virtio devices to work with fields only they know about.
    void DeviceConfigRead(uint16_t offset, uint8_t* value);
    void DeviceConfigRead(uint16_t offset, uint16_t* value);
    void DeviceConfigRead(uint16_t offset, uint32_t* value);
    void DeviceConfigRead(uint16_t offset, uint64_t* value);
    void DeviceConfigWrite(uint16_t offset, uint8_t value);
    void DeviceConfigWrite(uint16_t offset, uint16_t value);
    void DeviceConfigWrite(uint16_t offset, uint32_t value);
    void DeviceConfigWrite(uint16_t offset, uint64_t value);

    // Handle the virtio queues for the device. Due to configuration layouts changing
    // depending on backend this has to be handled by the backend itself.
    uint16_t GetRingSize(uint16_t index);
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                 zx_paddr_t pa_used);
    void RingKick(uint16_t ring_index);
private:
    uint16_t bar0_base_;
    zx_handle_t bar0_handle_;
    uint16_t device_cfg_offset_;
};

class PciModernBackend : public PciBackend {
public:
    PciModernBackend(pci_protocol_t pci, zx_pcie_device_info_t info)
        : PciBackend(pci, info) {}
    // The dtor handles cleanup of allocated bars because we cannot tear down
    // the mappings safely while the virtio device is being used by a driver.
    virtual ~PciModernBackend();
    zx_status_t Init() override;

    void DriverStatusOk(void);
    void DriverStatusAck(void);
    void DeviceReset(void);
    uint32_t IsrStatus(void);

    // These handle writing to/from a device's device config to allow derived
    // virtio devices to work with fields only they know about.
    void DeviceConfigRead(uint16_t offset, uint8_t* value);
    void DeviceConfigRead(uint16_t offset, uint16_t* value);
    void DeviceConfigRead(uint16_t offset, uint32_t* value);
    void DeviceConfigRead(uint16_t offset, uint64_t* value);
    void DeviceConfigWrite(uint16_t offset, uint8_t value);
    void DeviceConfigWrite(uint16_t offset, uint16_t value);
    void DeviceConfigWrite(uint16_t offset, uint32_t value);
    void DeviceConfigWrite(uint16_t offset, uint64_t value);

    // Callbacks called during PciBackend's parsing of capabilities in Bind()
    void CommonCfgCallback(const virtio_pci_cap_t& cap) TA_REQ(backend_lock_);
    void NotifyCfgCallback(const virtio_pci_cap_t& cap) TA_REQ(backend_lock_);
    void IsrCfgCallback(const virtio_pci_cap_t& cap) TA_REQ(backend_lock_);
    void DeviceCfgCallback(const virtio_pci_cap_t& cap) TA_REQ(backend_lock_);
    void PciCfgCallback(const virtio_pci_cap_t& cap) TA_REQ(backend_lock_);

    // Handle the virtio queues for the device. Due to configuration layouts changing
    // depending on backend this has to be handled by the backend itself.
    uint16_t GetRingSize(uint16_t index);
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                 zx_paddr_t pa_used);
    void RingKick(uint16_t ring_index);

private:
    zx_status_t MapBar(uint8_t bar);

    struct bar {
        uintptr_t mmio_base;
        zx::handle mmio_handle;
    } bar_[6] = { {0, {}} };

    uintptr_t notify_base_ = 0;
    volatile uint32_t* isr_status_ = nullptr;
    uintptr_t device_cfg_ = 0 TA_GUARDED(backend_lock_);
    volatile virtio_pci_common_cfg_t* common_cfg_ = nullptr TA_GUARDED(backend_lock_);
    uint32_t notify_off_mul_;
};

} // namespace virtio
