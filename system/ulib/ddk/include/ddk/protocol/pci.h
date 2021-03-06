// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/auxdata.h>
#include <hw/pci.h>
#include <zircon/compiler.h>
#include <zircon/syscalls/pci.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

/**
 * protocols/pci.h - PCI protocol definitions
 *
 * The PCI host driver publishes zx_device_t's with its config set to a pci_device_config_t.
 */

enum pci_resource_ids {
    PCI_RESOURCE_BAR_0 = 0,
    PCI_RESOURCE_BAR_1,
    PCI_RESOURCE_BAR_2,
    PCI_RESOURCE_BAR_3,
    PCI_RESOURCE_BAR_4,
    PCI_RESOURCE_BAR_5,
    PCI_RESOURCE_CONFIG,
    PCI_RESOURCE_COUNT,
};

enum pci_header_fields {
    kPciCfgVendorId = 0x00,
    kPciCfgDeviceId = 0x02,
    kPciCfgRevisionId = 0x08,
    kPciCfgClassCode = 0x09,
    kPciCfgSubsystemVendorId = 0x2C,
    kPciCfgSubsystemId = 0x2E,
    kPciCfgCapabilitiesPtr = 0x34,
};

enum pci_cap_types {
    kPciCapIdNull = 0x00,
    kPciCapIdPciPwrMgmt = 0x01,
    kPciCapIdAgp = 0x02,
    kPciCapIdVpd = 0x03,
    kPciCapIdMsi = 0x05,
    kPciCapIdPcix = 0x07,
    kPciCapIdHypertransport = 0x08,
    kPciCapIdVendor = 0x09,
    kPciCapIdDebugPort = 0x0A,
    kPciCapIdCompactPciCrc = 0x0B,
    kPciCapIdPciHotplug = 0x0C,
    kPciCapIdPciBridgeSubsystemVid = 0x0D,
    kPciCapIdAgp8x = 0x0E,
    kPciCapIdSecureDevice = 0x0F,
    kPciCapIdPciExpress = 0x10,
    kPciCapIdMsix = 0x11,
    kPciCapIdSataDataNdxCfg = 0x12,
    kPciCapIdAdvancedFeatures = 0x13,
    kPciCapIdEnhancedAllocation = 0x14,
};


typedef struct pci_protocol_ops {
    zx_status_t (*get_resource)(void* ctx, uint32_t res_id,  zx_pci_resource_t* out_res);
    zx_status_t (*map_resource)(void* ctx, uint32_t res_id, uint32_t cache_policy,
                                void** vaddr, size_t* size, zx_handle_t* out_handle);
    zx_status_t (*enable_bus_master)(void* ctx, bool enable);
    zx_status_t (*enable_pio)(void* ctx, bool enable);
    zx_status_t (*reset_device)(void* ctx);
    zx_status_t (*map_interrupt)(void* ctx, int which_irq, zx_handle_t* out_handle);
    zx_status_t (*query_irq_mode_caps)(void* ctx, zx_pci_irq_mode_t mode,
                                       uint32_t* out_max_irqs);
    zx_status_t (*set_irq_mode)(void* ctx, zx_pci_irq_mode_t mode,
                                uint32_t requested_irq_count);
    zx_status_t (*get_device_info)(void* ctx, zx_pcie_device_info_t* out_info);
    uint32_t    (*config_read)(void* ctx, uint8_t offset, size_t width);
    uint8_t     (*get_next_capability)(void* ctx, uint8_t type, uint8_t offset);
    zx_status_t (*get_auxdata)(void* ctx, const char* args,
                               void* data, uint32_t bytes, uint32_t* actual);
} pci_protocol_ops_t;
typedef struct pci_protocol {
    pci_protocol_ops_t* ops;
    void* ctx;
} pci_protocol_t;

static inline zx_status_t pci_get_resource(pci_protocol_t* pci, uint32_t res_id,
                                           zx_pci_resource_t* out_info) {
    return pci->ops->get_resource(pci->ctx, res_id, out_info);
}

static inline zx_status_t pci_map_resource(pci_protocol_t* pci, uint32_t res_id,
                                           uint32_t cache_policy, void** vaddr, size_t* size,
                                           zx_handle_t* out_handle) {
    return pci->ops->map_resource(pci->ctx, res_id, cache_policy, vaddr, size, out_handle);
}

static inline zx_status_t pci_enable_bus_master(pci_protocol_t* pci, bool enable) {
    return pci->ops->enable_bus_master(pci->ctx, enable);
}

static inline zx_status_t pci_enable_pio(pci_protocol_t* pci, bool enable) {
    return pci->ops->enable_pio(pci->ctx, enable);
}

static inline zx_status_t pci_reset_device(pci_protocol_t* pci) {
    return pci->ops->reset_device(pci->ctx);
}

static inline zx_status_t pci_map_interrupt(pci_protocol_t* pci, int which_irq,
                                            zx_handle_t* out_handle) {
    return pci->ops->map_interrupt(pci->ctx, which_irq, out_handle);
}

static inline zx_status_t pci_query_irq_mode_caps(pci_protocol_t* pci, zx_pci_irq_mode_t mode,
                                                  uint32_t* out_max_irqs) {
    return pci->ops->query_irq_mode_caps(pci->ctx, mode, out_max_irqs);
}

static inline zx_status_t pci_set_irq_mode(pci_protocol_t* pci, zx_pci_irq_mode_t mode,
                                           uint32_t requested_irq_count) {
    return pci->ops->set_irq_mode(pci->ctx, mode, requested_irq_count);
}

static inline zx_status_t pci_get_device_info(pci_protocol_t* pci,
                                              zx_pcie_device_info_t* out_info) {
    return pci->ops->get_device_info(pci->ctx, out_info);
}

static inline uint8_t pci_config_read8(pci_protocol_t* pci, uint8_t offset) {
    return (uint8_t)(pci->ops->config_read(pci->ctx, offset, 8u) & 0XFF);
}

static inline uint16_t pci_config_read16(pci_protocol_t* pci, uint8_t offset) {
    return (uint16_t)(pci->ops->config_read(pci->ctx, offset, 16u) & 0xFFFF);
}

static inline uint32_t pci_config_read32(pci_protocol_t* pci, uint8_t offset) {
    return pci->ops->config_read(pci->ctx, offset, 32u);
}

static uint8_t pci_get_next_capability(pci_protocol_t* pci, uint8_t type, uint8_t offset) {
    return pci->ops->get_next_capability(pci->ctx, type, offset);
}

static uint8_t pci_get_first_capability(pci_protocol_t* pci, uint8_t type) {
    // the next_capability method will always look at the second byte next
    // pointer to fetch the next capability. By offsetting the CapPtr field
    // by -1 we can pretend we're working with a normal capability entry
    return pci_get_next_capability(pci, kPciCfgCapabilitiesPtr - 1u, type);
}

static inline zx_status_t pci_get_auxdata(pci_protocol_t* pci,
                                          const char* args, void* data,
                                          uint32_t bytes, uint32_t* actual) {
    return pci->ops->get_auxdata(pci->ctx, args, data, bytes, actual);
}

__END_CDECLS;
