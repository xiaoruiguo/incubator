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

#include <map>

#include "caf/detail/net_export.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/middleman_backend.hpp"
#include "caf/net/stream_socket.hpp"
#include "caf/node_id.hpp"

namespace caf::net::backend {

/// Minimal backend for unit testing.
/// @warning this backend is *not* thread safe.
class CAF_NET_EXPORT test : public middleman_backend {
public:
  // -- member types -----------------------------------------------------------

  using peer_entry = std::pair<stream_socket, endpoint_manager_ptr>;

  // -- constructors, destructors, and assignment operators --------------------

  test(middleman& mm);

  ~test() override;

  // -- interface functions ----------------------------------------------------

  error init() override;

  void stop() override;

  endpoint_manager_ptr peer(const node_id& id) override;

  expected<endpoint_manager_ptr> get_or_connect(const uri& locator) override;

  void resolve(const uri& locator, const actor& listener) override;

  strong_actor_ptr make_proxy(node_id nid, actor_id aid) override;

  void set_last_hop(node_id*) override;

  // -- properties -------------------------------------------------------------

  stream_socket socket(const node_id& peer_id) {
    return get_peer(peer_id).first;
  }

  uint16_t port() const noexcept override;

  peer_entry& emplace(const node_id& peer_id, stream_socket first,
                      stream_socket second);

private:
  peer_entry& get_peer(const node_id& id);

  middleman& mm_;

  std::map<node_id, peer_entry> peers_;

  proxy_registry proxies_;
};

} // namespace caf::net::backend
