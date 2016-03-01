//
// rescore_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2015 Askars Salimbajevs (SIA Tilde)
//
//

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <set>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include "rescore_message.hpp"

using boost::asio::ip::tcp;

//----------------------------------------------------------------------

typedef std::deque<rescore_message> rescore_message_queue;

//----------------------------------------------------------------------

class rescore_job
{
public:
  virtual ~rescore_job() {}
  virtual void deliver(const rescore_message& msg) = 0;
};

typedef boost::shared_ptr<rescore_job> rescore_job_ptr;


//----------------------------------------------------------------------

class rescore_session
  : public rescore_job,
    public boost::enable_shared_from_this<rescore_session>
{
public:
  rescore_session(boost::asio::io_service& io_service)
    : socket_(io_service)
  {
  }

  tcp::socket& socket()
  {
    return socket_;
  }

  void start()
  {
    //room_.join(shared_from_this());
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), rescore_message::header_length),
        boost::bind(
          &rescore_session::handle_read_header, shared_from_this(),
          boost::asio::placeholders::error));
  }

  void deliver(const rescore_message& msg)
  {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(msg);
    if (!write_in_progress)
    {
      boost::asio::async_write(socket_,
          boost::asio::buffer(write_msgs_.front().data(),
            write_msgs_.front().length()),
          boost::bind(&rescore_session::handle_write, shared_from_this(),
            boost::asio::placeholders::error));
    }
  }

  void handle_read_header(const boost::system::error_code& error)
  {
    if (!error && read_msg_.decode_header())
    {
      boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
          boost::bind(&rescore_session::handle_read_body, shared_from_this(),
            boost::asio::placeholders::error));
    }
  }

  void handle_read_body(const boost::system::error_code& error)
  {
    if (!error)
    {
      // TODO: process read_msg_ and rescore   
      boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_.data(), rescore_message::header_length),
          boost::bind(&rescore_session::handle_read_header, shared_from_this(),
            boost::asio::placeholders::error));
    }
  }

  void handle_write(const boost::system::error_code& error)
  {
    if (!error)
    {
      write_msgs_.pop_front();
      if (!write_msgs_.empty())
      {
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front().data(),
              write_msgs_.front().length()),
            boost::bind(&rescore_session::handle_write, shared_from_this(),
              boost::asio::placeholders::error));
      }
    }
  }

private:
  tcp::socket socket_;
  rescore_message read_msg_;
  rescore_message_queue write_msgs_;
};

typedef boost::shared_ptr<rescore_session> rescore_session_ptr;

//----------------------------------------------------------------------

class rescore_server
{
public:
  rescore_server(boost::asio::io_service& io_service,
      const tcp::endpoint& endpoint)
    : io_service_(io_service),
      acceptor_(io_service, endpoint)
  {
    start_accept();
  }

  void start_accept()
  {
    rescore_session_ptr new_session(new rescore_session(io_service_));
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&rescore_server::handle_accept, this, new_session,
          boost::asio::placeholders::error));
  }

  void handle_accept(rescore_session_ptr session,
      const boost::system::error_code& error)
  {
    if (!error)
    {
      session->start();
    }

    start_accept();
  }

private:
  boost::asio::io_service& io_service_;
  tcp::acceptor acceptor_;
};

typedef boost::shared_ptr<rescore_server> rescore_server_ptr;
typedef std::list<rescore_server_ptr> rescore_server_list;

//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
  try
  {
    if (argc < 2)
    {
      std::cerr << "Usage: rescore_server <port> [<port> ...]\n";
      return 1;
    }

    boost::asio::io_service io_service;

    rescore_server_list servers;
    for (int i = 1; i < argc; ++i)
    {
      using namespace std; // For atoi.
      tcp::endpoint endpoint(tcp::v4(), atoi(argv[i]));
      rescore_server_ptr server(new rescore_server(io_service, endpoint));
      servers.push_back(server);
    }

    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
