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

#include <arpa/nameser.h>
#include <deque>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/pem.h>
#include <resolv.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>

extern "C" {
#include "quicly.h"
#include "quicly/defaults.h"
#include "quicly/streambuf.h"
}

#include "caf/byte.hpp"
#include "caf/detail/convert_ip_endpoint.hpp"
#include "caf/detail/quicly_cb.hpp"
#include "caf/detail/quicly_util.hpp"
#include "caf/detail/socket_sys_aliases.hpp"
#include "caf/detail/socket_sys_includes.hpp"
#include "caf/error.hpp"
#include "caf/fwd.hpp"
#include "caf/logger.hpp"
#include "caf/net/endpoint_manager.hpp"
#include "caf/net/fwd.hpp"
#include "caf/net/receive_policy.hpp"
#include "caf/net/transport_worker_dispatcher.hpp"
#include "caf/net/udp_datagram_socket.hpp"
#include "caf/sec.hpp"
#include "caf/span.hpp"
#include "caf/variant.hpp"
#include "picotls/openssl.h"
#include "picotls/pembase64.h"

namespace caf {
namespace net {

struct received_data {
  received_data(detail::quicly_conn_ptr conn, span<byte> data)
    : conn(std::move(conn)) {
    received.resize(data.size());
    received.insert(received.begin(), data.begin(), data.end());
  }

  detail::quicly_conn_ptr conn;
  std::vector<byte> received;
};

template <class Factory>
struct quicly_stream_open : public quicly_stream_open_t {
  quic_transport<Factory>* transport;
};

template <class Factory>
struct quicly_closed_by_peer : public quicly_closed_by_peer_t {
  quic_transport<Factory>* transport;
};

struct transport_streambuf : public quicly_streambuf_t {
  std::shared_ptr<std::vector<received_data>> buf;
};

/// Implements a quic transport policy that manages a datagram socket.
template <class Factory>
class quic_transport {
public:
  // -- member types -----------------------------------------------------------

  using factory_type = Factory;

  using application_type = typename Factory::application_type;

  using dispatcher_type = transport_worker_dispatcher<factory_type,
                                                      detail::quicly_conn_ptr>;

  // -- constructors, destructors, and assignment operators --------------------

  quic_transport(udp_datagram_socket handle, factory_type factory)
    : dispatcher_(std::move(factory)),
      handle_(handle),
      read_buf_(std::make_shared<std::vector<received_data>>()),
      max_consecutive_reads_(0),
      read_threshold_(1024),
      collected_(0),
      max_(1024),
      rd_flag_(receive_policy_flag::exactly),
      stream_callbacks{
        quicly_streambuf_destroy, quicly_streambuf_egress_shift,
        quicly_streambuf_egress_emit, ::detail::on_stop_sending,
        // on_receive()
        [](quicly_stream_t* stream, size_t off, const void* src,
           size_t len) -> int {
          if (auto ret = quicly_streambuf_ingress_receive(stream, off, src,
                                                          len))
            return ret;
          ptls_iovec_t input;
          if ((input = quicly_streambuf_ingress_get(stream)).len) {
            CAF_LOG_TRACE("quicly received: " << CAF_ARG(input.len) << "bytes");
            auto buf = reinterpret_cast<transport_streambuf*>(stream->data)
                         ->buf;
            buf->emplace_back(std::shared_ptr<quicly_conn_t>(stream->conn,
                                                             [](
                                                               quicly_conn_t*) {
                                                             }),
                              as_writable_bytes(
                                make_span(input.base, input.len)));
            quicly_streambuf_ingress_shift(stream, input.len);
          }
          return 0;
        },
        // on_receive_reset()
        [](quicly_stream_t* stream, int err) -> int {
          CAF_LOG_TRACE("quicly_received reset-stream: "
                        << CAF_ARG(QUICLY_ERROR_GET_ERROR_CODE(err)));
          return quicly_close(stream->conn,
                              QUICLY_ERROR_FROM_APPLICATION_ERROR_CODE(0),
                              "received reset");
        }} {
    stream_open_.transport = this;
    stream_open_.cb = [](quicly_stream_open_t* self,
                         quicly_stream_t* stream) -> int {
      auto tmp = static_cast<quicly_stream_open<factory_type>*>(self);
      return tmp->transport->on_stream_open(self, stream);
    };
    closed_by_peer_.transport = this;
    closed_by_peer_.cb = [](quicly_closed_by_peer_t* self, quicly_conn_t* conn,
                            int, uint64_t, const char*, size_t) {
      auto tmp = static_cast<quicly_closed_by_peer<factory_type>*>(self);
      tmp->transport->on_closed_by_peer(conn);
    };
  }

  // -- public member functions ------------------------------------------------

  template <class Parent>
  error init(Parent& parent) {
    if (auto err = dispatcher_.init(parent))
      return err;
    memset(&tlsctx_, 0, sizeof(ptls_context_t));
    tlsctx_.random_bytes = ptls_openssl_random_bytes;
    tlsctx_.get_time = &ptls_get_time;
    tlsctx_.key_exchanges = key_exchanges_;
    tlsctx_.cipher_suites = ptls_openssl_cipher_suites;
    tlsctx_.require_dhe_on_psk = 1;
    tlsctx_.save_ticket = &save_ticket_;
    ctx_ = quicly_spec_context;
    ctx_.tls = &tlsctx_;
    ctx_.stream_open = &stream_open_;
    ctx_.closed_by_peer = &closed_by_peer_;
    detail::setup_session_cache(ctx_.tls);
    quicly_amend_ptls_context(ctx_.tls);
    std::string path_to_certs;
    char* path = getenv("QUICLY_CERTS");
    if (path) {
      path_to_certs = path;
    } else {
      // try to load default certs
      path_to_certs = "/home/jakob/code/quicly/t/assets/";
    }
    detail::load_certificate_chain(ctx_.tls,
                                   (path_to_certs + "server.crt").c_str());
    detail::load_private_key(ctx_.tls, (path_to_certs + "server.key").c_str());
    key_exchanges_[0] = &ptls_openssl_secp256r1;
    char random_key[17];
    tlsctx_.random_bytes(random_key, sizeof(random_key) - 1);
    memcpy(cid_key_, random_key, sizeof(random_key)); // save cid_key
    ctx_.cid_encryptor = quicly_new_default_cid_encryptor(
      &ptls_openssl_bfecb, &ptls_openssl_sha256,
      ptls_iovec_init(cid_key_, strlen(cid_key_)));
    parent.mask_add(operation::read);
    return none;
  }

  template <class Parent>
  bool handle_read_event(Parent& parent) {
    CAF_LOG_TRACE(CAF_ARG(handle_.id));
    uint8_t buf[4096];
    auto ret = read(handle_, as_writable_bytes(make_span(buf, sizeof(buf))));
    if (auto err = get_if<sec>(&ret)) {
      CAF_LOG_DEBUG("read failed" << CAF_ARG(*err));
      dispatcher_.handle_error(*err);
      return false;
    }
    auto read_pair = get<std::pair<size_t, ip_endpoint>>(ret);
    auto read_res = read_pair.first;
    auto ep = read_pair.second;
    sockaddr_storage sa = {};
    detail::convert(ep, sa);
    size_t off = 0;
    while (off != read_res) {
      quicly_decoded_packet_t packet;
      size_t plen = 0;
      if (quicly_decode_packet(&ctx_, &packet, buf + off, read_pair.first - off)
          == SIZE_MAX)
        break;
      if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        if (packet.version != QUICLY_PROTOCOL_VERSION) {
          auto rp = quicly_send_version_negotiation(&ctx_,
                                                    reinterpret_cast<sockaddr*>(
                                                      &sa),
                                                    packet.cid.src, nullptr,
                                                    packet.cid.dest.encrypted);
          CAF_ASSERT(rp != nullptr);
          if (::detail::send_one(handle_.id, rp) == -1)
            CAF_LOG_ERROR("send_one failed");
          break;
        }
      }
      auto conn_it = std::
        find_if(known_conns_.begin(), known_conns_.end(),
                [&](const detail::quicly_conn_ptr& conn) {
                  return quicly_is_destination(conn.get(), nullptr,
                                               reinterpret_cast<sockaddr*>(&sa),
                                               &packet);
                });
      if (conn_it != known_conns_.end()) {
        // already accepted connection
        quicly_receive(conn_it->get(), nullptr,
                       reinterpret_cast<sockaddr*>(&sa), &packet);
        for (auto& data : *read_buf_)
          dispatcher_.handle_data(parent, make_span(data.received), data.conn);
        read_buf_->clear();
      } else if (QUICLY_PACKET_IS_LONG_HEADER(packet.octets.base[0])) {
        // new connection
        quicly_address_token_plaintext_t* token = nullptr;
        quicly_conn_t* conn = nullptr;
        int accept_res = quicly_accept(&conn, &ctx_, nullptr,
                                       reinterpret_cast<sockaddr*>(&sa),
                                       &packet, token, &next_cid_, nullptr);
        if (accept_res == 0 && conn) {
          auto conn_ptr = detail::make_quicly_conn_ptr(conn);
          known_conns_.insert(conn_ptr);
          ++next_cid_.master_id;
          dispatcher_.add_new_worker(parent, node_id{}, conn_ptr);
        } else {
          CAF_LOG_ERROR("could not accept new connection");
        }
      } else {
        /* short header packet; potentially a dead connection. No need to check
         * the length of the incoming packet, because loop is prevented by
         * authenticating the CID (by checking node_id and thread_id). If the
         * peer is also sending a reset, then the next CID is highly likely to
         * contain a non-authenticating CID, ... */
        if (packet.cid.dest.plaintext.node_id == 0
            && packet.cid.dest.plaintext.thread_id == 0) {
          auto dgram = quicly_send_stateless_reset(&ctx_,
                                                   reinterpret_cast<sockaddr*>(
                                                     &sa),
                                                   nullptr,
                                                   packet.cid.dest.encrypted
                                                     .base);
          if (::detail::send_one(handle_.id, dgram) == -1)
            CAF_LOG_ERROR("could not send stateless reset");
        }
      }
      off += plen;
    }
    return true;
  }

  template <class Parent>
  bool handle_write_event(Parent& parent) {
    CAF_LOG_TRACE(CAF_ARG(handle_.id)
                  << CAF_ARG2("queue-size", packet_queue_.size()));
    // Try to write leftover data.
    write_some();
    // Get new data from parent.
    for (auto msg = parent.next_message(); msg != nullptr;
         msg = parent.next_message()) {
      auto decorator = make_write_packet_decorator(*this, parent);
      dispatcher_.write_message(decorator, std::move(msg));
    }
    // Write prepared data.
    return write_some();
  }

  template <class Parent>
  void resolve(Parent& parent, const std::string& path, actor listener) {
    dispatcher_.resolve(parent, path, listener);
  }

  template <class Parent>
  void timeout(Parent& parent, atom_value value, uint64_t id) {
    auto decorator = make_write_packet_decorator(*this, parent);
    dispatcher_.timeout(decorator, value, id);
  }

  void set_timeout(uint64_t timeout_id, detail::quicly_conn_ptr ep) {
    dispatcher_.set_timeout(timeout_id, ep);
  }

  void handle_error(sec code) {
    dispatcher_.handle_error(code);
  }

  udp_datagram_socket handle() const noexcept {
    return handle_;
  }

  void prepare_next_read() {
    read_buf_->clear();
    collected_ = 0;
    // This cast does nothing, but prevents a weird compiler error on GCC
    // <= 4.9.
    // TODO: remove cast when dropping support for GCC 4.9.
    switch (static_cast<receive_policy_flag>(rd_flag_)) {
      case receive_policy_flag::exactly:
        if (read_buf_->size() != max_)
          read_buf_->resize(max_);
        read_threshold_ = max_;
        break;
      case receive_policy_flag::at_most:
        if (read_buf_->size() != max_)
          read_buf_->resize(max_);
        read_threshold_ = 1;
        break;
      case receive_policy_flag::at_least: {
        // read up to 10% more, but at least allow 100 bytes more
        auto max_size = max_ + std::max<size_t>(100, max_ / 10);
        if (read_buf_->size() != max_size)
          read_buf_->resize(max_size);
        read_threshold_ = max_;
        break;
      }
    }
  }

  void configure_read(receive_policy::config cfg) {
    rd_flag_ = cfg.first;
    max_ = cfg.second;
  }

  template <class Parent>
  void write_packet(Parent&, span<const byte> header, span<const byte> payload,
                    detail::quicly_conn_ptr conn) {
    std::vector<byte> buf;
    buf.reserve(header.size() + payload.size());
    buf.insert(buf.end(), header.begin(), header.end());
    buf.insert(buf.end(), payload.begin(), payload.end());
    packet_queue_.emplace_back(std::move(conn), std::move(buf));
  }

  struct packet {
    detail::quicly_conn_ptr destination;
    std::vector<byte> bytes;

    packet(detail::quicly_conn_ptr destination, std::vector<byte> bytes)
      : destination(std::move(destination)), bytes(std::move(bytes)) {
      // nop
    }
  };

private:
  bool write_some() {
    return false;
    /*if (packet_queue_.empty())
      return false;
    auto& next_packet = packet_queue_.front();
    auto send_res = write(handle_, as_bytes(make_span(next_packet.bytes)),
                          next_packet.destination);
    if (auto num_bytes = get_if<size_t>(&send_res)) {
      CAF_LOG_DEBUG(CAF_ARG(handle_.id) << CAF_ARG(*num_bytes));
      packet_queue_.pop_front();
      return true;
    }
    auto err = get<sec>(send_res);
    CAF_LOG_DEBUG("send failed" << CAF_ARG(err));
    dispatcher_.handle_error(err);
    return false;*/
  }

  int on_stream_open(struct st_quicly_stream_open_t*,
                     struct st_quicly_stream_t* stream) {
    CAF_LOG_TRACE("new quic stream opened");
    if (auto ret = quicly_streambuf_create(stream, sizeof(transport_streambuf)))
      return ret;
    stream->callbacks = &stream_callbacks;
    reinterpret_cast<transport_streambuf*>(stream->data)->buf = read_buf_;
    return 0;
  }

  void on_closed_by_peer(quicly_conn_t* conn) {
    known_conns_.erase(
      std::shared_ptr<quicly_conn_t>(conn, [](quicly_conn_t*) {}));
    // TODO: delete worker that handles this connection.
  }

  dispatcher_type dispatcher_;
  udp_datagram_socket handle_;

  std::shared_ptr<std::vector<received_data>> read_buf_;
  std::deque<packet> packet_queue_;

  size_t max_consecutive_reads_;
  size_t read_threshold_;
  size_t collected_;
  size_t max_;
  receive_policy_flag rd_flag_;

  // -- quicly state -----------------------------------------------------------
  char cid_key_[17];
  quicly_cid_plaintext_t next_cid_;
  ptls_handshake_properties_t hs_properties_;
  ptls_save_ticket_t save_ticket_;
  ptls_key_exchange_algorithm_t* key_exchanges_[128];
  ptls_context_t tlsctx_;
  quicly_context_t ctx_;

  quicly_stream_callbacks_t stream_callbacks;
  std::set<detail::quicly_conn_ptr> known_conns_;

  quicly_stream_open<factory_type> stream_open_;
  quicly_closed_by_peer<factory_type> closed_by_peer_;
}; // namespace net

} // namespace net
} // namespace caf