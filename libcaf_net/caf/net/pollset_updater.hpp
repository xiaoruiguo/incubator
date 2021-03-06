/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2019 Dominik Charousset                                     *
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

#include <array>
#include <cstdint>
#include <mutex>

#include "caf/byte.hpp"
#include "caf/net/pipe_socket.hpp"
#include "caf/net/socket_manager.hpp"

namespace caf::net {

class pollset_updater : public socket_manager {
public:
  // -- member types -----------------------------------------------------------

  using super = socket_manager;

  using msg_buf = std::array<byte, sizeof(intptr_t) + 1>;

  // -- constructors, destructors, and assignment operators --------------------

  pollset_updater(pipe_socket read_handle, const multiplexer_ptr& parent);

  ~pollset_updater() override;

  // -- properties -------------------------------------------------------------

  /// Returns the managed socket.
  pipe_socket handle() const noexcept {
    return socket_cast<pipe_socket>(handle_);
  }

  // -- interface functions ----------------------------------------------------

  bool handle_read_event() override;

  bool handle_write_event() override;

  void handle_error(sec code) override;

private:
  msg_buf buf_;
  size_t buf_size_;
};

} // namespace caf::net
