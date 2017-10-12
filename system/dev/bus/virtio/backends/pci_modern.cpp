#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <inttypes.h>

#include "pci.h"

namespace {

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

// MMIO reads and writes are abstracted out into template methods that
// ensure fields are only accessed with the right size.
template <typename T> void MmioWrite(volatile T* addr, T value) {
    *addr = value;
}

template<typename T> void MmioRead(volatile T* addr, T* value) {
    *value = *addr;
}

// Virtio 1.0 Section 4.1.3:
// 64-bit fields are to be treated as two 32-bit fields, with low 32 bit
// part followed by the high 32 bit part.
void MmioWrite(volatile uint64_t* addr, uint64_t value) {
    auto words = reinterpret_cast<volatile uint32_t*>(addr);
    words[0] = static_cast<uint32_t>(value & UINT32_MAX);
    words[1] = static_cast<uint32_t>(value >> 32);
}

void MmioRead(volatile uint64_t* addr, uint64_t* value) {
    auto words = reinterpret_cast<volatile uint32_t*>(addr);
    auto val = reinterpret_cast<uint32_t*>(value);

    val[0] = words[0];
    val[1] = words[1];
}

} // anonymous namespace

namespace virtio {

zx_status_t PciModernBackend::Init(void) {
    // try to parse capabilities
    for (uint8_t off = pci_get_first_capability(&pci_, kPciCapIdVendor);
            off != 0;
            off = pci_get_next_capability(&pci_, off, kPciCapIdVendor)) {
        virtio_pci_cap_t cap;

        ReadVirtioCap(&pci_, off, cap);
        switch (cap.cfg_type) {
        case VIRTIO_PCI_CAP_COMMON_CFG:
            CommonCfgCallback(cap);
            break;
        case VIRTIO_PCI_CAP_NOTIFY_CFG:
            // Virtio 1.0 section 4.1.4.4
            // notify_off_multiplier is a 32bit field following this capability
            notify_off_mul_ = pci_config_read32(&pci_,
                                    static_cast<uint8_t>(off + sizeof(virtio_pci_cap_t)));
            NotifyCfgCallback(cap);
            break;
        case VIRTIO_PCI_CAP_ISR_CFG:
            IsrCfgCallback(cap);
            break;
        case VIRTIO_PCI_CAP_DEVICE_CFG:
            DeviceCfgCallback(cap);
            break;
        case VIRTIO_PCI_CAP_PCI_CFG:
            PciCfgCallback(cap);
            break;
        }
    }

    // Ensure we found needed capabilities during parsing
    if (common_cfg_ == nullptr || isr_status_ == nullptr
            || device_cfg_ == 0 || notify_base_ == 0) {
        dprintf(ERROR, "%s: failed to bind, missing capabilities\n", Tag());
        return ZX_ERR_BAD_STATE;
    }

    return ZX_OK;
}

PciModernBackend::~PciModernBackend(void) {
    fbl::AutoLock lock(&backend_lock_);
    for (size_t i = 0; i < countof(bar_); i++) {
        bar_[i].mmio_handle.reset();
        bar_[i].mmio_base = 0;
    }
}

// value pointers are used to maintain type safety with field width
void PciModernBackend::DeviceConfigRead(uint16_t offset, uint8_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioRead(reinterpret_cast<volatile uint8_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigRead(uint16_t offset, uint16_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioRead(reinterpret_cast<volatile uint16_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigRead(uint16_t offset, uint32_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioRead(reinterpret_cast<volatile uint32_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigRead(uint16_t offset, uint64_t* value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioRead(reinterpret_cast<volatile uint64_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint8_t value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioWrite(reinterpret_cast<volatile uint8_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint16_t value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioWrite(reinterpret_cast<volatile uint16_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint32_t value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioWrite(reinterpret_cast<volatile uint32_t*>(device_cfg_ + offset), value);
}

void PciModernBackend::DeviceConfigWrite(uint16_t offset, uint64_t value) {
    fbl::AutoLock lock(&backend_lock_);
    MmioWrite(reinterpret_cast<volatile uint64_t*>(device_cfg_ + offset), value);
}

// Attempt to map a bar found in a capability structure. If it has already been
// mapped and we have stored a valid handle in the structure then just return
// ZX_OK.
zx_status_t PciModernBackend::MapBar(uint8_t bar) {
    if (bar >= countof(bar_)) {
        return ZX_ERR_INVALID_ARGS;
    }

    if (bar_[bar].mmio_handle != ZX_HANDLE_INVALID) {
        return ZX_OK;
    }

    size_t size;
    zx_handle_t handle;
    void* base;
    zx_status_t s = pci_map_resource(&pci_, PCI_RESOURCE_BAR_0 + bar,
                                     ZX_CACHE_POLICY_UNCACHED_DEVICE, &base, &size, &handle);
    if (s != ZX_OK) {
        dprintf(ERROR, "%s: Failed to map bar %u: %d\n", Tag(), bar, s);
        return s;
    }

    // Store the base as a uintptr_t due to the amount of math done on it later
    bar_[bar].mmio_base = reinterpret_cast<uintptr_t>(base);
    bar_[bar].mmio_handle.reset(handle);
    return ZX_OK;
}

void PciModernBackend::CommonCfgCallback(const virtio_pci_cap_t& cap) {
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    // Common config is a structure of type virtio_pci_common_cfg_t located at an
    // the bar and offset specified by the capability.
    auto addr = bar_[cap.bar].mmio_base + cap.offset;
    common_cfg_ = reinterpret_cast<volatile virtio_pci_common_cfg_t*>(addr);

	// Cache this when we find the config for kicking the queues later
}

void PciModernBackend::NotifyCfgCallback(const virtio_pci_cap_t& cap) {
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    notify_base_ = bar_[cap.bar].mmio_base + cap.offset;
}

void PciModernBackend::IsrCfgCallback(const virtio_pci_cap_t& cap) {
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    // interrupt status is directly read from the register at this address
    isr_status_ = reinterpret_cast<volatile uint32_t*>(bar_[cap.bar].mmio_base + cap.offset);
}

void PciModernBackend::DeviceCfgCallback(const virtio_pci_cap_t& cap) {
    if (MapBar(cap.bar) != ZX_OK) {
        return;
    }

    device_cfg_ = bar_[cap.bar].mmio_base + cap.offset;
}

void PciModernBackend::PciCfgCallback(const virtio_pci_cap_t& cap) {
    // We are not using this capability presently since we can map the
    // bars for direct memory access.
}
// Get the ring size of a specific index
uint16_t PciModernBackend::GetRingSize(uint16_t index) {
    fbl::AutoLock lock(&backend_lock_);

	uint16_t queue_size = 0;
	MmioWrite(&common_cfg_->queue_select, index);
	MmioRead(&common_cfg_->queue_size, &queue_size);
	return queue_size;
}

// Set up ring descriptors with the backend.
void PciModernBackend::SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail, zx_paddr_t pa_used) {
    fbl::AutoLock lock(&backend_lock_);

    // These offsets are wrong and this should be changed
    MmioWrite(&common_cfg_->queue_select, index);
    MmioWrite(&common_cfg_->queue_size, count);
    MmioWrite(&common_cfg_->queue_desc, pa_desc);
    MmioWrite(&common_cfg_->queue_avail, pa_avail);
    MmioWrite(&common_cfg_->queue_used, pa_used);
    MmioWrite(&common_cfg_->queue_desc, pa_desc);
    MmioWrite<uint16_t>(&common_cfg_->queue_enable, 1);
}

void PciModernBackend::RingKick(uint16_t ring_index) {
    fbl::AutoLock lock(&backend_lock_);
    uint16_t queue_notify_off;
    MmioRead(&common_cfg_->queue_notify_off, &queue_notify_off);

    // Virtio 1.0 Section 4.1.4.4
    // The address to notify for a queue is calculated using information from
    // the notify_off_multiplier, the capability's base + offset, and the
    // selected queue's offset.
    auto addr = notify_base_ + queue_notify_off * notify_off_mul_;
    auto ptr = reinterpret_cast<volatile uint16_t*>(addr);
	dprintf(SPEW, "%s: kick %u addr %p\n", Tag(), ring_index, ptr);
    *ptr = ring_index;
}

void PciModernBackend::DeviceReset(void) {
    fbl::AutoLock lock(&backend_lock_);

    MmioWrite<uint8_t>(&common_cfg_->device_status, 0u);
}

void PciModernBackend::DriverStatusOk(void) {
    fbl::AutoLock lock(&backend_lock_);

	uint8_t device_status;
	MmioRead(&common_cfg_->device_status, &device_status);
	device_status |= VIRTIO_STATUS_DRIVER_OK;
	MmioWrite(&common_cfg_->device_status, device_status);
}

void PciModernBackend::DriverStatusAck(void) {
    fbl::AutoLock lock(&backend_lock_);

	uint8_t device_status;
	MmioRead(&common_cfg_->device_status, &device_status);
	device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
	MmioWrite(&common_cfg_->device_status, device_status);
}

uint32_t PciModernBackend::IsrStatus(void) {
    return (*isr_status_ & (VIRTIO_ISR_QUEUE_INT | VIRTIO_ISR_DEV_CFG_INT));
}

} // namespace virtio
