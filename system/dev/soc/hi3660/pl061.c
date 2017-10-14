// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <ddk/driver.h>
#include <hw/reg.h>
#include <zircon/syscalls.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "pl061.h"

// GPIO register offsets
#define GPIODATA(mask)  ((mask) << 2)   // Data registers, mask provided as index
#define GPIODIR         0x400           // Data direction register (0 = IN, 1 = OUT)
#define GPIOIS          0x404           // Interrupt sense register (0 = edge, 1 = level)
#define GPIOIBE         0x408           // Interrupt both edges register (1 = both)
#define GPIOIEV         0x40C           // Interrupt event register (0 = falling, 1 = rising)
#define GPIOIE          0x410           // Interrupt mask register (1 = interrupt masked)
#define GPIORIS         0x414           // Raw interrupt status register
#define GPIOMIS         0x418           // Masked interrupt status register
#define GPIOIC          0x41C           // Interrupt clear register
#define GPIOAFSEL       0x420           // Mode control select register

#define GPIOS_PER_PAGE  8

zx_status_t pl061_init(pl061_gpios_t* gpios, uint32_t gpio_start, uint32_t gpio_count,
                       const uint32_t* irqs, uint32_t irq_count,
                       zx_paddr_t mmio_base, size_t mmio_length, zx_handle_t resource) {
    zx_status_t status = io_buffer_init_physical(&gpios->buffer, mmio_base, mmio_length, resource,
                                                ZX_CACHE_POLICY_UNCACHED_DEVICE);
    if (status != ZX_OK) {
        dprintf(ERROR, "pl061_init: io_buffer_init_physical failed %d\n", status);
        pl061_free(gpios);
        return status;
    }

    gpios->event_handles = calloc(gpio_count, sizeof(*gpios->event_handles));
    if (!gpios->event_handles) {
        return ZX_ERR_NO_MEMORY;
    }
    gpios->irq_handles = calloc(irq_count, sizeof(*gpios->irq_handles));
    if (!gpios->irq_handles) {
        return ZX_ERR_NO_MEMORY;
    }
    gpios->irq_threads = calloc(irq_count, sizeof(*gpios->irq_threads));
    if (!gpios->irq_threads) {
        return ZX_ERR_NO_MEMORY;
    }

    mtx_init(&gpios->lock, mtx_plain);
    gpios->gpio_start = gpio_start;
    gpios->gpio_count = gpio_count;
    gpios->irqs = irqs;
    gpios->irq_count = irq_count;
    gpios->resource = resource;

    return ZX_OK;
}

void pl061_free(pl061_gpios_t* gpios) {
    // stop interrupt threads
    for (uint32_t i = 0; i < gpios->irq_count; i++) {
        zx_handle_t handle = gpios->irq_handles[i];
        if (handle) {
            zx_interrupt_signal(handle);
            zx_handle_close(gpios->irq_handles[i]);
            thrd_join(gpios->irq_threads[i], NULL);
        }
    }

    // close all event handles
    for (uint32_t i = 0; i < gpios->gpio_count; i++) {
        zx_handle_close(gpios->event_handles[i]);
    }

    io_buffer_release(&gpios->buffer);
    free(gpios);
}

static zx_status_t pl061_gpio_config(void* ctx, unsigned pin, gpio_config_flags_t flags) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    mtx_lock(&gpios->lock);
    uint8_t dir = readb(regs + GPIODIR);
    if ((flags & GPIO_DIR_MASK) == GPIO_DIR_OUT) {
        dir |= bit;
    } else {
        dir &= ~bit;
    }
    writeb(dir, regs + GPIODIR);

    uint8_t trigger = readb(regs + GPIOIS);
    if ((flags & GPIO_TRIGGER_MASK) == GPIO_TRIGGER_LEVEL) {
        trigger |= bit;
    } else {
        trigger &= ~bit;
    }
    writeb(trigger, regs + GPIOIS);

    uint8_t be = readb(regs + GPIOIBE);
    uint8_t iev = readb(regs + GPIOIEV);

    if ((flags & GPIO_TRIGGER_MASK) == GPIO_TRIGGER_EDGE && (flags & GPIO_TRIGGER_RISING)
        && (flags & GPIO_TRIGGER_FALLING)) {
        be |= bit;
     } else {
        be &= ~bit;
     }
    if ((flags & GPIO_TRIGGER_MASK) == GPIO_TRIGGER_EDGE && (flags & GPIO_TRIGGER_RISING)
        && !(flags & GPIO_TRIGGER_FALLING)) {
        iev |= bit;
     } else {
        iev &= ~bit;
     }

    writeb(be, regs + GPIOIBE);
    writeb(iev, regs + GPIOIEV);

    mtx_unlock(&gpios->lock);
    return ZX_OK;
}

static zx_status_t pl061_gpio_read(void* ctx, unsigned pin, unsigned* out_value) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    *out_value = !!(readb(regs + GPIODATA(bit)) & bit);
    return ZX_OK;
}

static zx_status_t pl061_gpio_write(void* ctx, unsigned pin, unsigned value) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (pin / GPIOS_PER_PAGE);
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    writeb((value ? bit : 0), regs + GPIODATA(bit));
    return ZX_OK;
}

static void pl061_gpio_int_enable_locked(volatile uint8_t* regs, unsigned pin, bool enable) {
    uint8_t bit = 1 << (pin % GPIOS_PER_PAGE);

    uint8_t ie = readb(regs + GPIOIE);
    if (enable) {
        ie |= bit;
    } else {
        ie &= ~bit;
    }
    writeb(ie, regs + GPIOIE);
}

typedef struct {
    volatile uint8_t* regs;
    zx_handle_t event_handle;
    zx_handle_t irq_handle;
} pl061_irq_thread_args_t;

static int pl061_irq_thread(void* arg) {
    pl061_irq_thread_args_t* args = arg;
    volatile uint8_t* regs = args->regs;
    zx_handle_t event_handle = args->event_handle;
    zx_handle_t irq_handle = args->irq_handle;
    free(args);

    while (1) {
        zx_status_t status = zx_interrupt_wait(irq_handle);
        zx_interrupt_complete(irq_handle);
        if (status != ZX_OK) {
            if (status != ZX_ERR_CANCELED) {
                dprintf(ERROR, "dwc3_irq_thread: zx_interrupt_wait returned %d\n", status);
            }
            break;
        }

        uint8_t mis = readb(regs + GPIOMIS);
        uint8_t ic = 0; // ?? readb(regs + GPIOIC);

        for (int i = 0; i < GPIOS_PER_PAGE; i++) {
            uint8_t bit = 1 << (i % GPIOS_PER_PAGE);
            if (mis & bit) {
                // signal state change
                if (readb(regs + GPIODATA(bit)) & bit) {
                    zx_object_signal(event_handle, GPIO_SIGNAL_LOW, GPIO_SIGNAL_HIGH);
                } else {
                    zx_object_signal(event_handle, GPIO_SIGNAL_HIGH, GPIO_SIGNAL_LOW);
                }

                // clear the interrupt
                ic |= bit;
            }
            writeb(ic, regs + GPIOIC);
        }
    }

    return 0;
}

static zx_status_t pl061_gpio_get_event_handle(void* ctx, unsigned pin, zx_handle_t* out_handle) {
    pl061_gpios_t* gpios = ctx;
    pin -= gpios->gpio_start;
    volatile uint8_t* regs = io_buffer_virt(&gpios->buffer) + PAGE_SIZE * (pin / GPIOS_PER_PAGE);

    mtx_lock(&gpios->lock);

    zx_handle_t event_handle = gpios->event_handles[pin];

    if (!event_handle) {
        zx_status_t status = zx_event_create(0, &event_handle);
        if (status != ZX_OK) {
            mtx_unlock(&gpios->lock);
            return status;
        }
        gpios->event_handles[pin] = event_handle;
    }

    uint32_t irq_index = pin / GPIOS_PER_PAGE;
    if (!gpios->irq_handles[irq_index]) {
        zx_handle_t irq_handle;
        zx_status_t status = zx_interrupt_create(gpios->resource, gpios->irqs[irq_index],
                                                 ZX_INTERRUPT_REMAP_IRQ, &irq_handle);
        if (status != ZX_OK) {
            dprintf(ERROR, "pl061_gpio_get_event_handle: zx_interrupt_create failed for irq %u: %d\n",
                    gpios->irqs[irq_index], status);
            mtx_unlock(&gpios->lock);
            return status;
        }

        pl061_irq_thread_args_t* args = calloc(1, sizeof(pl061_irq_thread_args_t));
        if (!args) {
            zx_handle_close(irq_handle);
            mtx_unlock(&gpios->lock);
            return ZX_ERR_NO_MEMORY;
        }
        args->regs = regs;
        args->event_handle = event_handle;
        args->irq_handle = irq_handle;

        thrd_create_with_name(&gpios->irq_threads[irq_index], pl061_irq_thread, args, "pl061_irq_thread");
        gpios->irq_handles[irq_index] = irq_handle;
    }

    pl061_gpio_int_enable_locked(regs, pin, true);
    mtx_unlock(&gpios->lock);

    return zx_handle_duplicate(event_handle, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

gpio_protocol_ops_t pl061_proto_ops = {
    .config = pl061_gpio_config,
    .read = pl061_gpio_read,
    .write = pl061_gpio_write,
    .get_event_handle = pl061_gpio_get_event_handle,
};
