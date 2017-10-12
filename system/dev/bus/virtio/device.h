// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#pragma once

#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/pci.h>
#include <zx/handle.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <threads.h>
#include <virtio/virtio.h>
#include "backends/backend.h"

// Virtio devices are represented by a derived class specific to their type (eg
// gpu) with a virtio::Device base. The device class handles general work around
// IRQ handling and contains a backend that is instantiated at creation time
// that implements a virtio backend. This allows a single device driver to work
// on both Virtio legacy or transistional without needing to special case the
// device interaction.
namespace virtio {

class Device {
public:
    Device(zx_device_t* bus_device, fbl::unique_ptr<Backend>&& backend);
    virtual ~Device();

    virtual zx_status_t Init() = 0;
    virtual void Release();
    virtual void Unbind();

    void StartIrqThread();
    // interrupt cases that devices may override
    virtual void IrqRingUpdate() = 0;
    virtual void IrqConfigChange() = 0;

    // used by Ring class to manipulate config registers
    void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc,
                 zx_paddr_t pa_avail, zx_paddr_t pa_used);
    uint16_t GetRingSize(uint16_t index);
    void RingKick(uint16_t ring_index);
    // for logging purposes
    virtual const char* Tag(void) const = 0;
    zx_device_t* device(void) { return device_; }

protected:
    void DeviceReset();
    void DriverStatusAck();
    void DriverStatusOk();
    zx_device_t* bus_device() { return bus_device_; }
    zx_status_t GetFeatures(uint64_t& features);
    zx_status_t RequestFeatures(uint64_t& features);

    zx_status_t CopyDeviceConfig(void* _buf, size_t len);

    virtual uint32_t IsrStatus(void);
    static int IrqThreadEntry(void* arg);
    void IrqWorker();

    // backend responsible for hardware io. Will be released when device goes out of scope
    fbl::unique_ptr<Backend> backend_;
    // irq thread object
    thrd_t irq_thread_ = {};
    zx::handle irq_handle_ = {};
    // Bus device is the parent device on the bus, device is this driver's device node.
    zx_device_t* bus_device_ = nullptr;
    zx_device_t* device_ = nullptr;

    // DDK device
    // TODO: It might make sense for the base device class to be the one
    // to handle device_add() calls rather than delegating it to the derived
    // instances of devices.
    zx_protocol_device_t device_ops_ = {};

    // This lock exists for devices to synchronize themselves, it should not be used by the base
    // device class.
    fbl::Mutex lock_;
};

} // namespace virtio

