// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <httpd.h>

namespace geist_apache {
// child_init only: never start a thread in the parent that forks the MPM.
void start_index_warming(apr_pool_t* child_pool, server_rec* servers);
} // namespace geist_apache
