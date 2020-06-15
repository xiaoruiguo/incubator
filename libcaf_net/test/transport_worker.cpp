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

#define CAF_SUITE transport_worker

#include "caf/net/transport_worker.hpp"

#include "caf/net/test/host_fixture.hpp"
#include "caf/test/dsl.hpp"

#include "caf/binary_serializer.hpp"
#include "caf/byte.hpp"
#include "caf/byte_buffer.hpp"
#include "caf/detail/scope_guard.hpp"
#include "caf/ip_endpoint.hpp"
#include "caf/make_actor.hpp"
#include "caf/net/actor_proxy_impl.hpp"
#include "caf/net/multiplexer.hpp"
#include "caf/span.hpp"

using namespace caf;
using namespace caf::net;

namespace {

constexpr string_view hello_test = "hello test!";

struct application_result {
  bool initialized;
  byte_buffer data_buffer;
  std::string resolve_path;
  actor resolve_listener;
  std::string timeout_value;
  uint64_t timeout_id;
  sec err;
};

struct transport_result {
  byte_buffer packet_buffer;
  ip_endpoint ep;
};

class dummy_application {
public:
  dummy_application(std::shared_ptr<application_result> res)
    : res_(std::move(res)){
      // nop
    };

  ~dummy_application() = default;

  template <class Parent>
  error init(Parent&) {
    res_->initialized = true;
    return none;
  }

  template <class Parent>
  error write_message(Parent& parent,
                      std::unique_ptr<endpoint_manager_queue::message> ptr) {
    auto payload_buf = parent.next_payload_buffer();
    binary_serializer sink(parent.system(), payload_buf);
    if (auto err = sink(ptr->msg->content()))
      CAF_FAIL("serializing failed: " << err);
    CAF_MESSAGE("before sending: " << CAF_ARG(ptr->msg->content()));
    parent.write_packet(payload_buf);
    return none;
  }

  template <class Parent>
  error handle_data(Parent&, span<const byte> data) {
    auto& buf = res_->data_buffer;
    buf.clear();
    buf.insert(buf.begin(), data.begin(), data.end());
    return none;
  }

  template <class Parent>
  void resolve(Parent&, string_view path, const actor& listener) {
    res_->resolve_path.assign(path.begin(), path.end());
    res_->resolve_listener = listener;
  }

  template <class Parent>
  void timeout(Parent&, std::string value, uint64_t id) {
    res_->timeout_value = std::move(value);
    res_->timeout_id = id;
  }

  void handle_error(sec err) {
    res_->err = err;
  }

private:
  std::shared_ptr<application_result> res_;
};

class dummy_transport {
public:
  using transport_type = dummy_transport;

  using application_type = dummy_application;

  dummy_transport(actor_system& sys, std::shared_ptr<transport_result> res)
    : sys_(sys), res_(std::move(res)) {
    // nop
  }

  void write_packet(ip_endpoint ep, span<byte_buffer*> buffers) {
    res_->ep = ep;
    auto& packet_buf = res_->packet_buffer;
    packet_buf.clear();
    for (auto buf : buffers)
      packet_buf.insert(packet_buf.end(), buf->begin(), buf->end());
  }

  actor_system& system() {
    return sys_;
  }

  transport_type& transport() {
    return *this;
  }

  byte_buffer next_header_buffer() {
    return {};
  }

  byte_buffer next_payload_buffer() {
    return {};
  }

  void register_writing() {
    // nop
  }

private:
  actor_system& sys_;
  std::shared_ptr<transport_result> res_;
};

struct fixture : test_coordinator_fixture<>, host_fixture {
  using worker_type = transport_worker<dummy_application, ip_endpoint>;

  fixture()
    : transport_results{std::make_shared<transport_result>()},
      application_results{std::make_shared<application_result>()},
      transport(sys, transport_results),
      worker{dummy_application{application_results}} {
    mpx = std::make_shared<multiplexer>();
    if (auto err = mpx->init())
      CAF_FAIL("mpx->init failed: " << err);
    if (auto err = parse("[::1]:12345", ep))
      CAF_FAIL("parse returned an error: " << err);
    worker = worker_type{dummy_application{application_results}, ep};
  }

  bool handle_io_event() override {
    return mpx->poll_once(false);
  }

  multiplexer_ptr mpx;
  std::shared_ptr<transport_result> transport_results;
  std::shared_ptr<application_result> application_results;
  dummy_transport transport;
  worker_type worker;
  ip_endpoint ep;
};

} // namespace

CAF_TEST_FIXTURE_SCOPE(endpoint_manager_tests, fixture)

CAF_TEST(construction and initialization) {
  CAF_CHECK_EQUAL(worker.init(transport), none);
  CAF_CHECK_EQUAL(application_results->initialized, true);
}

CAF_TEST(handle_data) {
  auto test_span = as_bytes(make_span(hello_test));
  CAF_CHECK_EQUAL(worker.handle_data(transport, test_span), none);
  auto& buf = application_results->data_buffer;
  string_view result{reinterpret_cast<char*>(buf.data()), buf.size()};
  CAF_CHECK_EQUAL(result, hello_test);
}

CAF_TEST(write_message) {
  std::string hello_test{"hello world!"};
  actor act;
  auto strong_actor = actor_cast<strong_actor_ptr>(act);
  mailbox_element::forwarding_stack stack;
  auto msg = make_message(hello_test);
  auto elem = make_mailbox_element(strong_actor, make_message_id(12345), stack,
                                   msg);
  using message_type = endpoint_manager_queue::message;
  auto message = detail::make_unique<message_type>(std::move(elem), nullptr);
  worker.write_message(transport, std::move(message));
  auto& buf = transport_results->packet_buffer;
  binary_deserializer source{sys, buf};
  caf::message received_msg;
  CAF_CHECK_EQUAL(source(received_msg), none);
  CAF_MESSAGE(CAF_ARG(received_msg));
  auto received_str = received_msg.get_as<std::string>(0);
  string_view result{received_str};
  CAF_CHECK_EQUAL(result, hello_test);
  CAF_CHECK_EQUAL(transport_results->ep, ep);
}

CAF_TEST(resolve) {
  worker.resolve(transport, "foo", self);
  CAF_CHECK_EQUAL(application_results->resolve_path, "foo");
  CAF_CHECK_EQUAL(application_results->resolve_listener, self);
}

CAF_TEST(timeout) {
  worker.timeout(transport, "bar", 42u);
  CAF_CHECK_EQUAL(application_results->timeout_value, "bar");
  CAF_CHECK_EQUAL(application_results->timeout_id, 42u);
}

CAF_TEST(handle_error) {
  worker.handle_error(sec::feature_disabled);
  CAF_CHECK_EQUAL(application_results->err, sec::feature_disabled);
}

CAF_TEST_FIXTURE_SCOPE_END()
