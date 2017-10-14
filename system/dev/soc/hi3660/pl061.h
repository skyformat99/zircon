// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/io-buffer.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform-device.h>
#include <zircon/listnode.h>
#include <zircon/types.h>
#include <threads.h>

typedef struct {
    list_node_t node;
    mtx_t lock;
    io_buffer_t buffer;
    uint32_t gpio_start;
    uint32_t gpio_count;
    zx_handle_t resource;
    const uint32_t* irqs;
    uint32_t irq_count;
    zx_handle_t* event_handles;
    zx_handle_t* irq_handles;
    thrd_t* irq_threads;
} pl061_gpios_t;

// PL061 GPIO protocol ops uses pl061_gpios_t* for ctx
extern gpio_protocol_ops_t pl061_proto_ops;

zx_status_t pl061_init(pl061_gpios_t* gpios, uint32_t gpio_start, uint32_t gpio_count,
                       const uint32_t* irqs, uint32_t irq_count,
                       zx_paddr_t mmio_base, size_t mmio_length, zx_handle_t resource);
void pl061_free(pl061_gpios_t* gpios);
