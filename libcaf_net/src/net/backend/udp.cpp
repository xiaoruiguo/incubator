/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright 2011-2020 Dominik Charousset                                     *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/net/backend/udp.hpp"

#include <string>

#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/basp/application.hpp"
#include "caf/net/basp/application_factory.hpp"
#include "caf/net/basp/ec.hpp"
#include "caf/net/datagram_transport.hpp"
#include "caf/net/ip.hpp"
#include "caf/net/make_endpoint_manager.hpp"
#include "caf/net/middleman.hpp"
#include "caf/net/socket_guard.hpp"
#include "caf/net/stream_transport.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/send.hpp"

namespace caf::net::backend {

udp::udp(middleman& mm)
  : middleman_backend("udp"), mm_(mm), proxies_(mm.system(), *this) {
  // nop
}

udp::~udp() {
  // nop
}

error udp::init() {
  uint16_t conf_port = get_or<uint16_t>(
    mm_.system().config(), "middleman.udp-port", defaults::middleman::udp_port);
  ip_endpoint ep;
  auto local_address = std::string("[::]:") + std::to_string(conf_port);
  if (auto err = detail::parse(local_address, ep))
    return err;
  auto sock = make_udp_datagram_socket(ep, true);
  if (!sock)
    return sock.error();
  auto guard = make_socket_guard(sock->first);
  if (auto err = nonblocking(guard.socket(), true))
    return err;
  listening_port_ = sock->second;
  CAF_LOG_INFO("udp socket spawned on " << CAF_ARG(listening_port_));
  auto& mpx = mm_.mpx();
  ep_manager_ = make_endpoint_manager(
    mpx, mm_.system(),
    datagram_transport{guard.release(), basp::application_factory{proxies_}});
  if (auto err = ep_manager_->init()) {
    CAF_LOG_ERROR("mgr->init() failed: " << err);
    return err;
  }
  return none;
}

void udp::stop() {
  for (const auto& id : node_ids_)
    proxies_.erase(id);
  ep_manager_.reset();
}

expected<endpoint_manager_ptr> udp::connect(const uri&) {
  return make_error(sec::runtime_error, "connect called on udp backend");
}

endpoint_manager_ptr udp::peer(const node_id&) {
  return ep_manager_;
}

void udp::resolve(const uri& locator, const actor& listener) {
  ep_manager_->resolve(locator, listener);
}

strong_actor_ptr udp::make_proxy(node_id nid, actor_id aid) {
  using impl_type = actor_proxy_impl;
  using hdl_type = strong_actor_ptr;
  actor_config cfg;
  return make_actor<impl_type, hdl_type>(aid, nid, &mm_.system(), cfg,
                                         peer(nid));
}

void udp::set_last_hop(node_id*) {
  // nop
}

} // namespace caf::net::backend