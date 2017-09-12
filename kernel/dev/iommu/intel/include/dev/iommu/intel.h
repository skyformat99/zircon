// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/iommu.h>
#include <zircon/syscalls/iommu.h>
#include <fbl/ref_ptr.h>

class IntelIommu {
public:
    static status_t Create(fbl::unique_ptr<const uint8_t[]> desc, uint32_t desc_len,
                           fbl::RefPtr<Iommu>* out);
};
