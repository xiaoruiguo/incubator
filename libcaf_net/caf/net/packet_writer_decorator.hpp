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

#include "caf/net/packet_writer.hpp"

#include "caf/byte_buffer.hpp"
#include "caf/span.hpp"

namespace caf::net {

/// Implements the interface for transport and application policies and
/// dispatches member functions either to `object` or `parent`.
template <class Object, class Parent>
class packet_writer_decorator final : public packet_writer {
public:
  // -- member types -----------------------------------------------------------

  using transport_type = typename Parent::transport_type;

  using application_type = typename Parent::application_type;

  // -- constructors, destructors, and assignment operators --------------------

  packet_writer_decorator(Object& object, Parent& parent)
    : object_(object), parent_(parent) {
    // nop
  }

  // -- properties -------------------------------------------------------------

  actor_system& system() {
    return parent_.system();
  }

  transport_type& transport() {
    return parent_.transport();
  }

  endpoint_manager& manager() {
    return parent_.manager();
  }

  byte_buffer next_header_buffer() override {
    return transport().next_header_buffer();
  }

  byte_buffer next_payload_buffer() override {
    return transport().next_payload_buffer();
  }

  byte_buffer& write_buffer() override {
    return transport().write_buffer();
  }

  void start_writing() override {
    transport().start_writing();
  }

  // -- member functions -------------------------------------------------------

  void cancel_timeout(std::string tag, uint64_t id) {
    parent_.cancel_timeout(std::move(tag), id);
  }

  template <class... Ts>
  uint64_t set_timeout(timestamp tout, std::string tag, Ts&&... xs) {
    return parent_.set_timeout(tout, std::move(tag), std::forward<Ts>(xs)...);
  }

protected:
  void write_impl(span<byte_buffer*> buffers) override {
    parent_.write_packet(object_.id(), buffers);
  }

private:
  Object& object_;
  Parent& parent_;
};

template <class Object, class Parent>
packet_writer_decorator<Object, Parent>
make_packet_writer_decorator(Object& object, Parent& parent) {
  return {object, parent};
}

} // namespace caf::net
