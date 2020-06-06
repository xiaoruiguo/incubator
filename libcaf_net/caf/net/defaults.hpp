/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2018 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "caf/detail/net_export.hpp"

// -- hard-coded default values for various CAF options ------------------------

CAF_NET_EXPORT extern std::atomic<size_t> num_heap_allocs;

namespace caf::defaults::middleman {

/// Maximum number of cached buffers for sending payloads.
CAF_NET_EXPORT extern const size_t max_payload_buffers;

/// Maximum number of cached buffers for sending headers.
CAF_NET_EXPORT extern const size_t max_header_buffers;

/// Port to listen on for tcp.
CAF_NET_EXPORT extern const uint16_t tcp_port;

} // namespace caf::defaults::middleman
