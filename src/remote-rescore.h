// remote_rescore.h

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

#ifndef KALDI_SRC_REMOTE_RESCORE_H_
#define KALDI_SRC_REMOTE_RESCORE_H_

#include <streambuf>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include "lat/lattice-functions.h"
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

namespace kaldi {


// Lattice rescoring in separate "remote" process
    class RemoteRescore {
    public:
        enum {
            max_lattice_size = 1024 * 1024 * 100
        }; // size limit for sanity (100MB)

        RemoteRescore(std::string address);

        RemoteRescore(std::string address, void (*error_log_func)(std::string msg));

        void (*error_log_func)(std::string msg);

        bool rescore(CompactLattice &lat, CompactLattice &rescored_lat);

        void wait_for_rescorer();

        ~RemoteRescore();

    private:

        class RescoreSocket {
        public:
            virtual bool connect_socket() = 0;
            virtual void close_socket() = 0;
            virtual bool send_bytes(const char* buffer, ssize_t bytes) = 0;
            virtual bool receive_bytes(char* buffer, ssize_t bytes) = 0;

            virtual ~RescoreSocket();
        };

        RescoreSocket *rescore_socket;

        CompactLattice *rcv_lattice();

        bool send_lattice(CompactLattice &lat);

        struct membuf : std::streambuf {
            membuf(char *begin, char *end) {
                this->setg(begin, begin, end);
            }
        };

        static void empty_log_func(std::string msg) {};

        class UnixSocket : public RescoreSocket {
        public:
            UnixSocket(const std::string& address, void (*error_log_func)(std::string msg));

            bool connect_socket() override;
            void close_socket() override;
            bool send_bytes(const char* buffer, ssize_t bytes) override;
            bool receive_bytes(char* buffer, ssize_t bytes) override;
            ~UnixSocket() override;
        private:
            void (*error_log_func)(std::string msg);
            int fd;
            struct sockaddr_un addr;
        };

        class TcpSocket : public RescoreSocket {
        public:
            TcpSocket(const std::string& address, void (*error_log_func)(std::string msg));

            bool connect_socket() override;
            void close_socket() override;
            bool send_bytes(const char* buffer, ssize_t bytes) override;
            bool receive_bytes(char* buffer, ssize_t bytes) override;
            ~TcpSocket() override;
        private:
            void (*error_log_func)(std::string msg);
            boost::asio::ip::tcp::endpoint endpoint;
            // context is the new name of service
            boost::shared_ptr<boost::asio::io_service> ctx;
            boost::shared_ptr<boost::asio::ip::tcp::socket> socket;
        };

    };



}  // namespace kaldi

#endif  // 
