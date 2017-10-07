// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power.h"

#include <acpica/acpi.h>

void poweroff(void) {
    ACPI_STATUS status = AcpiEnterSleepStatePrep(5);
    if (status == AE_OK) {
        AcpiEnterSleepState(5);
    }
}

void reboot(void) {
    AcpiReset();
}

extern zx_handle_t get_root_resource(void);
zx_status_t suspend_to_ram(void) {
    printf("HERE\n");
    // TODO: Get the number of CPUs from the system
    for (size_t i = 1; i < 4; ++i) {
        // TODO: Check status
        zx_system_cpu_ctl(get_root_resource(), i, ZX_SYS_CPU_CTL_STOP, 0);
    }

    ACPI_STATUS acpi_status = AcpiEnterSleepStatePrep(3);
    if (acpi_status != AE_OK) {
        printf("Failed to enter sleep state prep: %x\n", acpi_status);
        // TODO: I think we need to do LeaveSleepState{Prep,} on failure
        return ZX_ERR_INTERNAL;
    }

    acpi_status = AcpiEnterSleepState(3);
    if (acpi_status != AE_OK) {
        printf("Failed to enter sleep state: %x\n", acpi_status);
        // TODO: I think we need to do LeaveSleepState{Prep,} on failure
        return ZX_ERR_INTERNAL;
    }

    acpi_status = AcpiLeaveSleepStatePrep(3);
    if (acpi_status != AE_OK) {
        // TODO: figure ut what to do here
        //return ERR_INTERNAL;
    }

    acpi_status = AcpiLeaveSleepState(3);
    if (acpi_status != AE_OK) {
        // TODO: figure ut what to do here
        //return ERR_INTERNAL;
    }

    for (size_t i = 1; i < 4; ++i) {
        // TODO: Check status
        zx_system_cpu_ctl(get_root_resource(), i, ZX_SYS_CPU_CTL_START, 0);
    }
    return ZX_OK;
}
