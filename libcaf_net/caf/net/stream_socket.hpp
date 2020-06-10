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

#include "caf/detail/net_export.hpp"
#include "caf/fwd.hpp"
#include "caf/net/network_socket.hpp"

namespace caf::net {

/// A connection-oriented network communication endpoint for bidirectional byte
/// streams.
struct CAF_NET_EXPORT stream_socket : network_socket {
  using super = network_socket;

  using super::super;
};

/// Creates two connected sockets to mimic network communication (usually for
/// testing purposes).
/// @relates stream_socket
expected<std::pair<stream_socket, stream_socket>>
  CAF_NET_EXPORT make_stream_socket_pair();

/// Enables or disables keepalive on `x`.
/// @relates network_socket
error CAF_NET_EXPORT keepalive(stream_socket x, bool new_value);

/// Enables or disables Nagle's algorithm on `x`.
/// @relates stream_socket
error CAF_NET_EXPORT nodelay(stream_socket x, bool new_value);

/// Receives data from `x`.
/// @param x Connected endpoint.
/// @param buf Points to destination buffer.
/// @returns The number of received bytes on success, an error code otherwise.
/// @relates stream_socket
/// @post either the result is a `sec` or a positive (non-zero) integer
variant<size_t, sec> CAF_NET_EXPORT read(stream_socket x, span<byte> buf);

/// Transmits data from `x` to its peer.
/// @param x Connected endpoint.
/// @param buf Points to the message to send.
/// @returns The number of written bytes on success, otherwise an error code.
/// @relates stream_socket
/// @post either the result is a `sec` or a positive (non-zero) integer
variant<size_t, sec> CAF_NET_EXPORT write(stream_socket x,
                                          span<const byte> buf);

/// Transmits data from `x` to its peer.
/// @param x Connected endpoint.
/// @param bufs Points to the message to send, scattered across up to 10
///             buffers.
/// @returns The number of written bytes on success, otherwise an error code.
/// @relates stream_socket
/// @post either the result is a `sec` or a positive (non-zero) integer
/// @pre `bufs.size() < 10`
variant<size_t, sec>
  CAF_NET_EXPORT write(stream_socket x,
                       std::initializer_list<span<const byte>> bufs);

/// Transmits data from `x` to its peer.
/// @param x Connected endpoint.
/// @param bufs Points to the message to send, scattered across up to 10
///             buffers.
/// @param offset the number of bytes of the packet that have already been
///               written.
/// @returns The number of written bytes on success, otherwise an error code.
/// @relates stream_socket
/// @post either the result is a `sec` or a positive (non-zero) integer
/// @pre `bufs.size() < 10`
variant<size_t, sec> CAF_NET_EXPORT write(stream_socket x,
                                          span<byte_buffer> bufs,
                                          size_t offset = 0);

/// Converts the result from I/O operation on a ::stream_socket to either an
/// error code or a non-zero positive integer.
/// @relates stream_socket
variant<size_t, sec>
  CAF_NET_EXPORT check_stream_socket_io_res(std::make_signed<size_t>::type res);

} // namespace caf::net
