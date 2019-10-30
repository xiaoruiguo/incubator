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
 ******************************************************************************
 *
 * This header is partly based on work of others. Previous header:
 *
 * Copyright (c) 2016,2017 DeNA Co., Ltd., Kazuho Oku, Fastly
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#pragma once

#include "caf/net/fwd.hpp"
#include "caf/net/quic/types.hpp"

namespace caf {
namespace net {
namespace quic {

// -- init / sending / receiving functions -------------------------------------

error make_server_context(state& state, callbacks callbacks);

error make_client_context(state& state, callbacks callbacks);

// -- helper functions ---------------------------------------------------------

/// makes a `quicly_conn_ptr` aka `std::shared_ptr<quicly_conn_t>` from given
/// `quicly_conn_t*`
conn_ptr make_conn_ptr(quicly_conn_t* conn);

/// converts a `quicly_conn_t*` to `size_t` for use as key/id_type.
size_t convert(quicly_conn_t* ptr) noexcept;

/// converts a `quicly_conn_ptr` to `size_t` for use as key/id_type.
size_t convert(const conn_ptr& ptr) noexcept;

// -- quicly send functions ----------------------------------------------------

/// sends a single `quicly_datagram_t` to its endpoint.
variant<size_t, sec> send_datagram(net::udp_datagram_socket handle,
                                   quicly_datagram_t* p);

/// sends pending `quicly_datagram_t` for given connection to their endpoint.
error send_pending_datagrams(net::udp_datagram_socket handle, conn_ptr conn);

// -- quicly default callbacks -------------------------------------------------

/// When `stop_sending` is received this cb is called.
int on_stop_sending(quicly_stream_t* stream, int err);

// -- general quicly routines --------------------------------------------------

///
int validate_token(sockaddr* remote, ptls_iovec_t client_cid,
                   ptls_iovec_t server_cid,
                   quicly_address_token_plaintext_t* token,
                   quicly_context_t* ctx);

///
int load_session(quicly_transport_parameters_t* params,
                 ptls_iovec_t& resumption_token,
                 ptls_handshake_properties_t& hs_properties,
                 const std::string& path);

///
int save_session(const quicly_transport_parameters_t* transport_params,
                 const std::string& session_file_path, session_info info);

} // namespace quic
} // namespace net
} // namespace caf