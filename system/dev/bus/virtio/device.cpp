// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <hw/inout.h>
#include <zircon/status.h>
#include <pretty/hexdump.h>
#include <ddk/debug.h>
// #include <fbl/auto_lock.h>

#include "trace.h"

#define LOCAL_TRACE 1

namespace virtio {

Device::Device(zx_device_t* bus_device, ::fbl::unique_ptr<Backend>&& backend)
    : backend_(fbl::move(backend)), bus_device_(bus_device) {
    LTRACE_ENTRY;
    device_ops_.version = DEVICE_OPS_VERSION;
}

Device::~Device() {
    LTRACE_ENTRY;
}

void Device::Unbind() {
    device_remove(device_);
}

void Device::Release() {
    irq_handle_.reset();
    backend_.reset();
}


void Device::IrqWorker() {
    zx_status_t rc;
    dprintf(TRACE, "%s: starting irq worker\n", Tag());

    while (backend_->irq_handle()) {
        if ((rc = zx_interrupt_wait(backend_->irq_handle())) != ZX_OK) {
            dprintf(SPEW, "%s: error while waiting for interrupt: %s\n",
                Tag(), zx_status_get_string(rc));
            continue;
        }

        // Read the status before completing the interrupt in case
        // another interrupt fires and changes the status.
        uint32_t irq_status = IsrStatus();

        LTRACEF_LEVEL(2, "irq_status %#x\n", irq_status);

        if ((rc = zx_interrupt_complete(backend_->irq_handle())) != ZX_OK) {
            dprintf(SPEW, "virtio: error while completing interrupt: %s\n",
                zx_status_get_string(rc));
            continue;
        }

        // Since we handle both interrupt types here it's possible for a
        // spurious interrupt if they come in sequence and we check IsrStatus
        // after both have been triggered.
        if (irq_status == 0)
            continue;

        // XXX: Unnecessary? grab the mutex for the duration of the irq handlers
        // fbl::AutoLock lock(&lock_);

        if (irq_status & VIRTIO_ISR_QUEUE_INT) { /* used ring update */
            IrqRingUpdate();
        }
        if (irq_status & VIRTIO_ISR_DEV_CFG_INT) { /* config change */
            IrqConfigChange();
        }
    }
}

int Device::IrqThreadEntry(void* arg) {
    Device* d = static_cast<Device*>(arg);

    d->IrqWorker();

    return 0;
}

void Device::StartIrqThread() {
    thrd_create_with_name(&irq_thread_, IrqThreadEntry, this, "virtio-irq-thread");
    thrd_detach(irq_thread_);
}

zx_status_t Device::CopyDeviceConfig(void* _buf, size_t len) {
    assert(_buf);

    for (uint16_t i = 0; i < len; i++) {
        backend_->DeviceConfigRead(i, static_cast<uint8_t*>(_buf) + i);
    }

    return ZX_OK;
}

// Get the Ring size for the particular device / backend.
// This has to be proxied to a backend method because we can't
// simply do config reads to determine the information. Modern
// devices have queue selects to worry about, whereas legacy do
// not.
uint16_t Device::GetRingSize(uint16_t index) {
    return backend_->GetRingSize(index);
}

// Set up ring descriptors with the backend.
void Device::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail, zx_paddr_t pa_used) {
    backend_->SetRing(index, count, pa_desc, pa_avail, pa_used);
}

// Another method that has to be proxied to the backend due to differences
// in how Legacy vs Modern systems are laid out.
void Device::RingKick(uint16_t ring_index) {
    backend_->RingKick(ring_index);
}

void Device::DeviceReset(void) {
    backend_->DeviceReset();
}

void Device::DriverStatusAck(void) {
    backend_->DriverStatusAck();
}

void Device::DriverStatusOk(void) {
    backend_->DriverStatusOk();
}

uint32_t Device::IsrStatus(void) {
    return backend_->IsrStatus();
}

} // namespace virtio
