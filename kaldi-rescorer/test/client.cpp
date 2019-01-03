//
// stream_client.cpp
// ~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2011 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <cstring>
#include <stdint.h>
#include <iostream>
#include <fstream>
#include <boost/asio.hpp>

#include "../rescore_message.hpp"

#if defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)

using boost::asio::local::stream_protocol;

//enum { max_length = 1024*1024*10 };

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 4)
    {
      std::cerr << "Usage: stream_client <socket_file> <in_lattice_file> <out_lattice_file>\n";
      return 1;
    }
    size_t max_length = 1024 * 1024 * 10;

    boost::asio::io_service io_service;

    stream_protocol::socket s(io_service);
    s.connect(stream_protocol::endpoint(argv[1]));
    std::cout << "connected to endpoint: " << argv[1] << std::endl;

    using namespace std; // For strlen.

    char *buffer = new char[max_length];
    FILE * filp = fopen(argv[2], "rb");

    size_t request_length = fread(buffer, sizeof(char), max_length, filp);
    fclose(filp);
    std::cout << "request_length: " << request_length << std::endl;

    RescoreMessage *out = new RescoreMessage();
    out->body_length(request_length);   
    out->encode_header();

    // send header
    boost::asio::write(s, boost::asio::buffer(out->data(), 4));
    // send buffer
    boost::asio::write(s, boost::asio::buffer(buffer, request_length));

    char *reply_header = new char[4];
    std::cout << "reading reply header..." << std::endl;
    boost::asio::read(s, boost::asio::buffer(reply_header, 4));
    size_t body_length = le32toh(*((uint32_t*)reply_header));
    std::cout << "body length: " << body_length << std::endl;

    char *reply = new char[max_length];
    std::cout << "reading reply body..." << std::endl;
    size_t reply_length = boost::asio::read(s, boost::asio::buffer(reply, body_length));
    std::cout << "Read " << reply_length << " bytes..." << std::endl;

    // write rescored lattice to disk
    std::ofstream output(argv[3], std::ofstream::binary);
    output.write(reply, reply_length);
    output.close();

    // cleanup, altho arguably useless at enf of program, is good to keep in mind
    delete [] buffer;
    delete [] reply;
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}

#else // defined(BOOST_ASIO_HAS_LOCAL_SOCKETS)
# error Local sockets not available on this platform.
#endif
