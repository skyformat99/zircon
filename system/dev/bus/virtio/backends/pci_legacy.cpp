#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>
#include <hw/inout.h>

#include "pci.h"

namespace virtio {

zx_status_t PciLegacyBackend::Init(void) {
    zx_pci_resource_t bar0;
    zx_status_t status = pci_get_resource(&pci_, PCI_RESOURCE_BAR_0, &bar0);
    if (status != ZX_OK) {
        dprintf(ERROR, "%s: Couldn't get IO bar for device: %d\n", Tag(), status);
        return status;
    }

    if (bar0.type != PCI_RESOURCE_TYPE_PIO) {
        return ZX_ERR_WRONG_TYPE;
    }

    bar0_base_ = static_cast<uint16_t>(bar0.pio_addr & 0xffff);
    // TODO(cja): When MSI support is added we need to dynamically add
    // the extra two fields here that offset the device config.
    // Virtio 1.0 section 4.1.4.8
    device_cfg_offset_ = static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_CONFIG_OFFSET_NOMSIX);
    dprintf(INFO, "%s: %02x:%02x.%01x using legacy backend (io base %#04x, device base %#04x\n",
            Tag(), info_.bus_id, info_.dev_id, info_.func_id, bar0_base_, device_cfg_offset_);
    return ZX_OK;
}

PciLegacyBackend::~PciLegacyBackend(void) {
    fbl::AutoLock lock(&backend_lock_);
    bar0_base_ = 0;
    device_cfg_offset_ = 0;
}

// value pointers are used to maintain type safety with field width
void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint8_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    *value = inp(static_cast<uint16_t>(device_cfg_offset_ + offset));
}

void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint16_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    *value = inpw(static_cast<uint16_t>(device_cfg_offset_ + offset));
}

void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint32_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    *value = inpd(static_cast<uint16_t>(device_cfg_offset_ + offset));
}

void PciLegacyBackend::DeviceConfigRead(uint16_t offset, uint64_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    auto _val = reinterpret_cast<uint32_t*>(value);

    _val[0] = inpd(static_cast<uint16_t>(device_cfg_offset_ + offset));
    _val[1] = inpd(static_cast<uint16_t>(device_cfg_offset_ + offset + sizeof(uint32_t)));
}

void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint8_t value) {
    fbl::AutoLock lock(&backend_lock_);
    outp(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint16_t value) {
    fbl::AutoLock lock(&backend_lock_);
    outpw(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

void PciLegacyBackend::DeviceConfigWrite(uint16_t offset, uint32_t value) {
    fbl::AutoLock lock(&backend_lock_);
    outpd(static_cast<uint16_t>(device_cfg_offset_ + offset), value);
}

// Get the ring size of a specific index
uint16_t PciLegacyBackend::GetRingSize(uint16_t index) {
    fbl::AutoLock lock(&backend_lock_);
    return inpw(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_QUEUE_SIZE));
}

// Set up ring descriptors with the backend.
void PciLegacyBackend::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail, zx_paddr_t pa_used) {
    fbl::AutoLock lock(&backend_lock_);
    // Virtio 1.0 section 2.4.2
    outpw(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_QUEUE_SELECT), index);
    outpw(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_QUEUE_SIZE), count);
    outpd(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_QUEUE_PFN), static_cast<uint32_t>(pa_desc / 4096));
}

void PciLegacyBackend::RingKick(uint16_t ring_index) {
    fbl::AutoLock lock(&backend_lock_);
    outpw(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_QUEUE_NOTIFY), ring_index);
}

void PciLegacyBackend::DeviceReset(void) {
    fbl::AutoLock lock(&backend_lock_);
    outp(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS), 0u);
}

void PciLegacyBackend::DriverStatusAck(void) {
    fbl::AutoLock lock(&backend_lock_);
    uint16_t addr = static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS);
    outp(addr, inp(addr) | VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
}

void PciLegacyBackend::DriverStatusOk(void) {
    fbl::AutoLock lock(&backend_lock_);
    uint16_t addr = static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_DEVICE_STATUS);
    outp(addr, inp(addr) | VIRTIO_STATUS_DRIVER_OK);
}

uint32_t PciLegacyBackend::IsrStatus(void) {
    fbl::AutoLock lock(&backend_lock_);
    return inp(static_cast<uint16_t>(bar0_base_ + VIRTIO_PCI_ISR_STATUS)
            & (VIRTIO_ISR_QUEUE_INT | VIRTIO_ISR_DEV_CFG_INT));
}

} // namespace virtio
