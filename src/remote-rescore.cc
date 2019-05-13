// remote_rescore.cc

// Copyright 2016  Askars Salimbajevs

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.


#include "remote-rescore.h"
#include <sstream>
#include <istream>
#include <sys/socket.h>
#include <boost/make_shared.hpp>

namespace kaldi {

    RemoteRescore::RemoteRescore(std::string address, void (*error_log_func)(std::string msg)) {
        this->error_log_func = error_log_func;
        if (address[0] == 'u') {
            this->rescore_socket = new UnixSocket(address, error_log_func);
        } else if (address[0] == 't') {
            this->rescore_socket = new TcpSocket(address, error_log_func);
        } else {
            error_log_func("Unable to create rescore socket. Protocol not implemented");
            // TODO error state... in constructor?
        }
    }

    RemoteRescore::RemoteRescore(std::string address) :
            RemoteRescore::RemoteRescore(std::move(address), &empty_log_func) {
    }

    bool RemoteRescore::send_lattice(CompactLattice &lat) {
        std::ostringstream str;
        WriteCompactLattice(str, true, lat);
        ssize_t size_of_lattice = str.tellp();
        char header[4];
        *((uint32_t *) header) = htole32(size_of_lattice);
        int size_of_header = sizeof(header);

        if (size_of_lattice > max_lattice_size) {
            error_log_func("Failed to write lattice to rescore socket. Lattice too big.");
            return false;
        }

        if (!rescore_socket->send_bytes(header, size_of_header)) {
            error_log_func("Failed to write header to rescore socket");
            return false;
        }

        if (!rescore_socket->send_bytes(str.str().c_str(), size_of_lattice)) {
            error_log_func("Failed to write lattice to rescore socket");
            return false;
        }
        return true;
    }

    CompactLattice *RemoteRescore::rcv_lattice() {
        ssize_t size_of_lattice = 0;
        char header[4];

        // read header
        if (!rescore_socket->receive_bytes(header, 4)) {
            error_log_func("Failed to read header from rescore socket");
            return nullptr;
        }

        // get body size from header
        size_of_lattice = le32toh(*((uint32_t *) header));

        // allocate buffer for body
        char *buffer = new char[size_of_lattice];

        if (!rescore_socket->receive_bytes(buffer, size_of_lattice)) {
            error_log_func("Failed to read lattice from rescore socket");
            return nullptr;
        }

        // create a stream to read CompactLattice from
        membuf sbuf(buffer, buffer + size_of_lattice);
        std::istream in(&sbuf);
        CompactLattice *tmp_lat = nullptr;
        if (ReadCompactLattice(in, true, &tmp_lat)) {
            return tmp_lat;
        } else {
            error_log_func("Failed to parse lattice");
            return nullptr;
        }
    }

    bool RemoteRescore::rescore(CompactLattice &lat, CompactLattice &rescored_lat) {
        // connect to rescorer
        if (!rescore_socket->connect_socket()) {
            return false;
        }

        // send lattice to remote rescorer
        if (!send_lattice(lat)) {
            rescore_socket->close_socket();
            return false;
        }

        // read rescored lattice
        CompactLattice *tmp = rcv_lattice();
        if (tmp == nullptr) {
            rescore_socket->close_socket();
            return false;
        }

        rescored_lat = *tmp;
        delete tmp;

        // close socket
        rescore_socket->close_socket();

        return true;
    }

    RemoteRescore::~RemoteRescore() {
        delete rescore_socket;
    }

    // base virtual destructor is required for some weird linking reason...
    RemoteRescore::RescoreSocket::~RescoreSocket() {
        // noop
    }

    // unix socket
    RemoteRescore::UnixSocket::UnixSocket(const std::string &address,
                                          void (*error_log_func)(std::string msg)) {
        this->error_log_func = error_log_func;
        this->fd = -1;
        // TODO is this really the best way to init addr?
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        const char *c_addr = address.c_str() + 2; // skip "u:"
        strncpy(addr.sun_path, c_addr, sizeof(addr.sun_path) - 1);
    }

    bool RemoteRescore::UnixSocket::connect_socket() {
        if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            error_log_func("Unable to create rescore socket");
            return false;
        }
        if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
            error_log_func("Failed to connect to rescore socket");
            return false;
        }
        return true;
    }

    void RemoteRescore::UnixSocket::close_socket() {
        close(fd);
    }

    bool RemoteRescore::UnixSocket::send_bytes(const char *buffer, ssize_t bytes) {
        return write(fd, buffer, bytes) == bytes;
    }

    bool RemoteRescore::UnixSocket::receive_bytes(char *buffer, ssize_t bytes) {
        ssize_t bytes_read = 0;
        ssize_t bytes_left = bytes;

        // read bytes from socket
        do {
            bytes_read = read(fd, buffer + (bytes - bytes_left), bytes_left);
            bytes_left -= bytes_read;
        } while (bytes_read >= 0 && bytes_left > 0);

        return bytes_read != -1;
    }

    RemoteRescore::UnixSocket::~UnixSocket() {
        // destructor
    }

    // tcp socket
    RemoteRescore::TcpSocket::TcpSocket(const std::string &address,
                                        void (*error_log_func)(std::string msg)) {
        this->error_log_func = error_log_func;
        this->error_log_func("trying to parse address and port!");
        // parse address
        size_t pos = address.find(':');
        // dbg
        std::stringstream ss;
        ss << "first pos at: " << pos;
        this->error_log_func(ss.str());

        std::string host_and_port = address.substr(pos + 1, address.length());
        //dbg
        ss.str("");
        ss << "host_and_port: " << host_and_port;
        this->error_log_func(ss.str());

        pos = host_and_port.find(':');
        //dbg
        ss.str("");
        ss << "second pos at : " << pos;
        this->error_log_func(ss.str());

        std::string host = host_and_port.substr(0, pos);
        std::string port = host_and_port.substr(pos + 1, host_and_port.length());
        //dbg
        ss.str("");
        ss << "host: " << host << ", port: " << port;
        this->error_log_func(ss.str());

        // TODO this only creates ip addresses, right? what about names?
        boost::asio::ip::address ip_addr = boost::asio::ip::address::from_string(host);
        unsigned short port_num = std::atoi(port.c_str());
        this->error_log_func("address and port_num parsed!");
        // TODO not sure if move needed here...
        endpoint = std::move(boost::asio::ip::tcp::endpoint(ip_addr, port_num));
        this->error_log_func("endpoint created");

        // set up socket and associated plumbing
        ctx = boost::make_shared<boost::asio::io_context>();
        socket = boost::make_shared<boost::asio::ip::tcp::socket>(*ctx);
        this->error_log_func("io plumbing created");
    }

    bool RemoteRescore::TcpSocket::connect_socket() {
        // TODO error handling, catch exception, log, return false
        this->error_log_func("connect_socket");
        socket->connect(endpoint);
        return true;
    }

    void RemoteRescore::TcpSocket::close_socket() {
        this->error_log_func("close_socket");
        socket->close();
    }

    bool RemoteRescore::TcpSocket::send_bytes(const char *buffer, ssize_t bytes) {
        this->error_log_func("send_bytes");
        // TODO handle errors...
        return socket->write_some(boost::asio::buffer(buffer, bytes)) == bytes;
//        return true;
    }

    bool RemoteRescore::TcpSocket::receive_bytes(char *buffer, ssize_t bytes) {
        this->error_log_func("receive_bytes");
        // TODO error handling
        return socket->read_some(boost::asio::buffer(buffer, bytes)) != -1;
    }

    RemoteRescore::TcpSocket::~TcpSocket() {
        // destructor
    }

}  // namespace kaldi

