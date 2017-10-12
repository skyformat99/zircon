// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/handle.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <ddk/protocol/pci.h>
#include <ddk/debug.h>
#include <virtio/virtio.h>
#include <assert.h>

#include "pci.h"

// For reading the virtio specific vendor capabilities that can be in PIO or MMIO space
#define cap_field(offset, field) static_cast<uint8_t>(offset + offsetof(virtio_pci_cap_t, field))
static void ReadVirtioCap(pci_protocol_t* pci, uint8_t offset, virtio_pci_cap& cap) {
    cap.cap_vndr = pci_config_read8(pci, cap_field(offset, cap_vndr));
    cap.cap_next = pci_config_read8(pci, cap_field(offset, cap_next));
    cap.cap_len  = pci_config_read8(pci, cap_field(offset, cap_len));
    cap.cfg_type = pci_config_read8(pci, cap_field(offset, cfg_type));
    cap.bar = pci_config_read8(pci, cap_field(offset, bar));
    cap.offset = pci_config_read32(pci, cap_field(offset, offset));
    cap.length = pci_config_read32(pci, cap_field(offset, length));
}
#undef cap_field

namespace virtio {

zx_status_t PciBackend::Bind(void) {
    fbl::AutoLock lock(&backend_lock_);
    zx_handle_t tmp_handle;

    dprintf(TRACE, "virtio[%p] binding via PCI\n", this);

    // enable bus mastering
    zx_status_t r;
    if ((r = pci_enable_bus_master(&pci_, true)) != ZX_OK) {
        dprintf(ERROR, "cannot enable bus master %d\n", r);
        return r;
    }

    // try to set up our IRQ mode
    if (pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_MSI, 1)) {
        if (pci_set_irq_mode(&pci_, ZX_PCIE_IRQ_MODE_LEGACY, 1)) {
            dprintf(ERROR, "failed to set irq mode\n");
            return -1;
        } else {
            dprintf(SPEW, "using legacy irq mode\n");
        }
    }

    r = pci_map_interrupt(&pci_, 0, &tmp_handle);
    if (r != ZX_OK) {
        dprintf(ERROR, "failed to map irq %d\n", r);
        return r;
    }
    irq_handle_.reset(tmp_handle);

    dprintf(SPEW, "irq handle %u\n", irq_handle_.get());

    return Init();

}

} // namespace virtio


