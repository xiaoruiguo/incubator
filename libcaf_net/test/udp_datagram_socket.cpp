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

#define CAF_SUITE udp_datagram_socket

#include "caf/net/udp_datagram_socket.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/binary_serializer.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/detail/net_syscall.hpp"
#include "caf/detail/socket_sys_includes.hpp"
#include "caf/ip_address.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/ipv4_address.hpp"
#include "caf/net/ip.hpp"

using namespace caf;
using namespace caf::net;
using namespace caf::net::ip;

namespace {

constexpr string_view hello_test = "Hello test!";

struct fixture : host_fixture {
  fixture() : host_fixture(), buf(1024) {
    addresses = local_addresses("localhost");
    CAF_CHECK(!addresses.empty());
    ep = ip_endpoint(*addresses.begin(), 0);
    auto send_pair = unbox(make_udp_datagram_socket(ep));
    send_socket = send_pair.first;
    auto receive_pair = unbox(make_udp_datagram_socket(ep));
    receive_socket = receive_pair.first;
    ep.port(receive_pair.second);
  }

  ~fixture() {
    close(send_socket);
    close(receive_socket);
  }

  std::vector<ip_address> addresses;
  actor_system_config cfg;
  actor_system sys{cfg};
  ip_endpoint ep;
  udp_datagram_socket send_socket;
  udp_datagram_socket receive_socket;
  byte_buffer buf;
};

error read_from_socket(udp_datagram_socket sock, byte_buffer& buf) {
  uint8_t receive_attempts = 0;
  variant<std::pair<size_t, ip_endpoint>, sec> read_ret;
  do {
    read_ret = read(sock, buf);
    if (auto read_res = get_if<std::pair<size_t, ip_endpoint>>(&read_ret)) {
      buf.resize(read_res->first);
    } else if (get<sec>(read_ret) != sec::unavailable_or_would_block) {
      return make_error(get<sec>(read_ret), "read failed");
    }
    if (++receive_attempts > 100)
      return make_error(sec::runtime_error,
                        "too many unavailable_or_would_blocks");
  } while (read_ret.index() != 0);
  return none;
}

struct header {
  header(size_t payload_size) : payload_size(payload_size) {
    // nop
  }

  header() : header(0) {
    // nop
  }

  template <class Inspector>
  friend typename Inspector::result_type inspect(Inspector& f, header& x) {
    return f(meta::type_name("header"), x.payload_size);
  }

  size_t payload_size;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(udp_datagram_socket_test, fixture)

CAF_TEST(socket creation) {
  ip_endpoint ep;
  CAF_CHECK_EQUAL(parse("0.0.0.0:0", ep), none);
  auto ret = make_udp_datagram_socket(ep);
  if (!ret)
    CAF_FAIL("socket creation failed: " << ret.error());
  CAF_CHECK_EQUAL(local_port(ret->first), ret->second);
}

CAF_TEST(read / write using span<byte>) {
  if (auto err = nonblocking(socket_cast<net::socket>(receive_socket), true))
    CAF_FAIL("setting socket to nonblocking failed: " << err);
  CAF_CHECK_EQUAL(read(receive_socket, buf), sec::unavailable_or_would_block);
  CAF_MESSAGE("sending data to " << to_string(ep));
  CAF_CHECK_EQUAL(write(send_socket, as_bytes(make_span(hello_test)), ep),
                  hello_test.size());
  CAF_CHECK_EQUAL(read_from_socket(receive_socket, buf), none);
  string_view received{reinterpret_cast<const char*>(buf.data()), buf.size()};
  CAF_CHECK_EQUAL(received, hello_test);
}

CAF_TEST(read / write using span<byte_buffer*>) {
  // generate header and payload in separate buffers
  header hdr{hello_test.size()};
  byte_buffer hdr_buf;
  binary_serializer sink(sys, hdr_buf);
  if (auto err = sink(hdr))
    CAF_FAIL("serializing payload failed" << err);
  auto bytes = as_bytes(make_span(hello_test));
  byte_buffer payload_buf(bytes.begin(), bytes.end());
  auto packet_size = hdr_buf.size() + payload_buf.size();
  std::vector<byte_buffer*> bufs{&hdr_buf, &payload_buf};
  CAF_CHECK_EQUAL(write(send_socket, bufs, ep), packet_size);
  // receive both as one single packet.
  buf.resize(packet_size);
  CAF_CHECK_EQUAL(read_from_socket(receive_socket, buf), none);
  CAF_CHECK_EQUAL(buf.size(), packet_size);
  binary_deserializer source(nullptr, buf);
  header recv_hdr;
  if (auto err = source(recv_hdr))
    CAF_FAIL("serializing failed: " << err);
  CAF_CHECK_EQUAL(hdr.payload_size, recv_hdr.payload_size);
  string_view received{reinterpret_cast<const char*>(buf.data())
                         + sizeof(header),
                       buf.size() - sizeof(header)};
  CAF_CHECK_EQUAL(received, hello_test);
}

CAF_TEST_FIXTURE_SCOPE_END()
