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
#include "lat/lattice-functions.h"

namespace kaldi {


// Lattice rescoring in separate "remote" process
class RemoteRescore {
    public:

    RemoteRescore(std::string address);
    RemoteRescore(std::string address, void (*error_log_func)(std::string msg));

    void (*error_log_func)(std::string msg);

    bool rescore(CompactLattice &lat, CompactLattice &rescored_lat);

    virtual ~RemoteRescore();

    protected:

    int fd;
    struct sockaddr* addr = NULL;

    bool process_address (std::string address);
    bool connect_socket ();
    void close_socket ();
    bool rcv_bytes (char* buffer, ssize_t bytes);
    bool send_bytes (const char* buffer, ssize_t bytes);

    virtual CompactLattice* rcv_lattice();
    virtual bool send_lattice(CompactLattice &lat);

    private:

    struct membuf : std::streambuf
    {
        membuf(char* begin, char* end) {
            this->setg(begin, begin, end);
        }
    };

    static void empty_log_func(std::string msg) {};

};

}  // namespace kaldi

#endif  // 
