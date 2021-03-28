// Copyright 2021 Mukaev Rinat <rinamuka4@gmail.com>

#ifndef INCLUDE_HEADER_HPP_
#define INCLUDE_HEADER_HPP_
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include "prepareSuggests.hpp"
namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>



namespace my_program_state {
std::size_t request_count() {
  static std::size_t count = 0;
  return ++count;
}


std::time_t now() { return std::time(0); }
}  // namespace my_program_state

class http_connection : public std::enable_shared_from_this<http_connection> {
 public:
  http_connection(tcp::socket socket, preparerSug& sugObj) : socket_(std::move(socket)), sugObj_(sugObj){}
  // Initiate the asynchronous operations associated with the connection.
  void start() {
    read_request();
    check_deadline();
  }

 private:
  // The socket for the currently connected client.
  tcp::socket socket_;
  preparerSug sugObj_;
  // The buffer for performing reads.
  beast::flat_buffer buffer_{8192};

  // The request message.
  http::request<http::string_body> request_;

  // The response message.
  http::response<http::string_body> response_;

  // The timer for putting a deadline on connection processing.
  net::steady_timer deadline_{socket_.get_executor(), std::chrono::seconds(60)};

  // Asynchronously receive a complete request message.
  void read_request() {
    auto self = shared_from_this();

    http::async_read(
        socket_, buffer_, request_,
        [self](beast::error_code ec, std::size_t bytes_transferred) {
          boost::ignore_unused(bytes_transferred);
          if (!ec) self->process_request();
        });
  }

  // Determine what needs to be done with the request message.
  void process_request() {
    response_.version(request_.version());
    response_.keep_alive(false);

    switch (request_.method()) {
      case http::verb::post:
        response_.result(http::status::ok);
        response_.set(http::field::server, "Beast");
        std::cout << request_.body().data()<<std::endl;
        create_response();
        break;

      default:
        // We return responses indicating an error if
        // we do not recognize the request method.
        response_.result(http::status::bad_request);
        response_.set(http::field::content_type, "text/plain");
        response_.body() = "Invalid request-method '" + std::string(
            request_.method_string()) + "'";
        break;
    }

    write_response();
  }

  // Construct a response message based on the program state.
  void create_response() {
    if (request_.target() == "/v1/api/suggest") {
      response_.set(http::field::content_type, "application/json");

      response_.body() = sugObj.getSuggestions(request_.body());

    } else {
      response_.result(http::status::not_found);
      response_.set(http::field::content_type, "text/plain");
      /*beast::ostream(response_.body()) << "File not found\r\n";*/
    }
  }

  // Asynchronously transmit the response message.
  void write_response() {
    auto self = shared_from_this();

    response_.content_length(response_.body().size());

    http::async_write(socket_, response_,
                      [self](beast::error_code ec, std::size_t) {
                        self->socket_.shutdown(tcp::socket::shutdown_send, ec);
                        self->deadline_.cancel();
                      });
  }

  // Check whether we have spent enough time on this connection.
  void check_deadline() {
    auto self = shared_from_this();

    deadline_.async_wait([self](beast::error_code ec) {
      if (!ec) {
        // Close socket to cancel any outstanding operation.
        self->socket_.close(ec);
      }
    });
  }
};

// "Loop" forever accepting new connections.
void http_server(tcp::acceptor& acceptor, tcp::socket& socket, preparerSug& sugObj) {//,sugPreferer* pointer to sugPreferer sugObj
  acceptor.async_accept(socket, [&](beast::error_code ec) {
    if (!ec) std::make_shared<http_connection>(std::move(socket), sugObj)->start();
    http_server(acceptor, socket, sugObj);
  });
}
#endif  // INCLUDE_HEADER_HPP_