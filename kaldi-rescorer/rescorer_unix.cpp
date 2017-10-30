//
// rescore_server.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2015 Askars Salimbajevs (SIA Tilde)
//
//

#include <cstdio>
#include <deque>
#include <iostream>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/asio.hpp>

#include "rescore_common.hpp"
#include "rescore_message.hpp"
#include "rescore_dispatch.hpp"

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

using boost::asio::local::stream_protocol;


//----------------------------------------------------------------------

typedef std::deque<boost::shared_ptr<rescore_message> > rescore_message_queue;

//----------------------------------------------------------------------

class rescore_session
  : public rescore_job,
    public boost::enable_shared_from_this<rescore_session>
{
public:
  rescore_session(boost::asio::io_service& io_service, rescore_dispatch* dispatcher)
    : socket_(io_service), 
      dispatcher_(dispatcher)
  {
  };

  stream_protocol::socket& socket()
  {
    return socket_;
  }

  void close() {
      socket_.close();
  }

  void start()
  {   
    boost::asio::async_read(socket_,
        boost::asio::buffer(read_msg_.data(), rescore_message::header_length),
        boost::bind(
          &rescore_session::handle_read_header, shared_from_this(),
          boost::asio::placeholders::error));
  }

  void deliver(rescore_message* msg)
  {
    bool write_in_progress = !write_msgs_.empty();
    write_msgs_.push_back(boost::shared_ptr<rescore_message>(msg));
    KALDI_LOG << current_time() << ": sending rescored lattice back (write_in_progress = " << write_in_progress << ")";
    if (!write_in_progress)
    {
      KALDI_LOG << current_time() << ": will send buffer of size " << write_msgs_.front()->length();
      boost::asio::async_write(socket_,
          boost::asio::buffer(write_msgs_.front()->data(),
            write_msgs_.front()->length()),
          boost::bind(&rescore_session::handle_write, shared_from_this(),
            boost::asio::placeholders::error));
    }
  }

  void handle_read_header(const boost::system::error_code& error)
  {
    if (!error)
    {
      if (! read_msg_.decode_header()) {
        KALDI_WARN << "Failed to read lattice from client. Lattice too big?";
        // reply with error msg
        rescore_message *out = new rescore_message();
        out->body_length(3);
        out->body()[0] = 'E';
        out->body()[1] = 'E';
        out->body()[2] = 'R';
        out->encode_header();
        deliver(out);
        // disconnect
        close();
      } else {
        KALDI_LOG << current_time() << ": starting to receive lattice of size " << read_msg_.body_length();
        boost::asio::async_read(socket_,
          boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
          boost::bind(&rescore_session::handle_read_body, shared_from_this(),
            boost::asio::placeholders::error));
      }
    }
  }

  void handle_read_body(const boost::system::error_code& error)
  {
    if (!error)
    {
      KALDI_LOG << current_time() << ": lattice of size " << read_msg_.body_length() << " received. Rescoring...";
      // process read_msg_ and rescore
      dispatcher_->rescore(read_msg_, shared_from_this());
      // wait for next header
      start();
    }
  }

  void handle_write(const boost::system::error_code& error)
  {

    // remove message from queue
    write_msgs_.pop_front();

    if (!error)
    {
      // continue sending
      if (!write_msgs_.empty())
      {
        KALDI_LOG << current_time() << ": will send buffer of size " << write_msgs_.front()->length();
        boost::asio::async_write(socket_,
            boost::asio::buffer(write_msgs_.front()->data(),
              write_msgs_.front()->length()),
            boost::bind(&rescore_session::handle_write, shared_from_this(),
              boost::asio::placeholders::error));
      }
    } else {
        KALDI_WARN << current_time() << ": failed to send lattice to client. Error code: " << error.message();
    }
  }

private:
  // The socket used to communicate with the client.
  stream_protocol::socket socket_;

  rescore_message read_msg_;
  rescore_message_queue write_msgs_;

  rescore_dispatch* dispatcher_;
};

typedef boost::shared_ptr<rescore_session> session_ptr;


class server
{
public:
  server(boost::asio::io_service& io_service, const std::string& file, rescore_dispatch* dispatcher)
    : io_service_(io_service),
      acceptor_(io_service, stream_protocol::endpoint(file)),
      dispatcher_(dispatcher)
  { 
    session_ptr new_session(new rescore_session(io_service_, dispatcher_));
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&server::handle_accept, this, new_session,
          boost::asio::placeholders::error));
  }

  void handle_accept(session_ptr new_session,
      const boost::system::error_code& error)
  {
    if (!error)
    {
      new_session->start();
      new_session.reset(new rescore_session(io_service_, dispatcher_));
      acceptor_.async_accept(new_session->socket(),
          boost::bind(&server::handle_accept, this, new_session,
            boost::asio::placeholders::error));
    }
  }

private:
  boost::asio::io_service& io_service_;
  stream_protocol::acceptor acceptor_;
  rescore_dispatch* dispatcher_;
};


int main(int argc, char* argv[])
{
  try
  {
    const char *usage =
        "Multithreaded server for remote lattice rescoring.\n"
        "Usage: rescorer_unix [options] <socket> <rescore-lm-rspecifier> <lm-fst-rspecifier>\n";
    ParseOptions po(usage);
    TaskSequencerConfig sequencer_config;
    sequencer_config.Register(&po);
    po.Read(argc, argv);

    if (po.NumArgs() < 3 || po.NumArgs() > 3) {
        po.PrintUsage();
        return 1;
    }

    std::string socket = po.GetArg(1),
        rescore_lm = po.GetArg(2),
        lm_fst = po.GetArg(3);

    // unbind address
    unlink(socket.c_str());

    // load dispatcher
    rescore_dispatch* dispatch = new rescore_dispatch(sequencer_config, rescore_lm, lm_fst);

    boost::asio::io_service io_service;

    server s(io_service, socket.c_str(), dispatch);

    io_service.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

#else // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
# error Local sockets not available on this platform.
#endif // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

