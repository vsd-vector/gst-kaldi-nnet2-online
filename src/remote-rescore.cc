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

namespace kaldi {

    RemoteRescore::RemoteRescore(std::string address, void (*error_log_func)(std::string msg)) {
        this->error_log_func = error_log_func;
        process_address(address);
    }

    RemoteRescore::RemoteRescore(std::string address) {
        error_log_func = &empty_log_func;
        process_address(address);
    };

    bool RemoteRescore::send_lattice(CompactLattice &lat) {
        std::ostringstream str;
        WriteCompactLattice(str, true, lat);
        ssize_t size_of_lattice = str.tellp();
        char header[4];
        *((uint32_t*)header) = htole32(size_of_lattice);
        int size_of_header = sizeof(header);

        if (size_of_lattice > max_lattice_size) {
            error_log_func("Failed to write lattice to rescore socket. Lattice too big.");
            return false;
        }

        if (send_bytes(header, size_of_header) == false) {
            error_log_func("Failed to write header to rescore socket");
            return false;
        }

        if (send_bytes(str.str().c_str(), size_of_lattice) == false) {
            error_log_func("Failed to write lattice to rescore socket");
            return false;
        }
        return true;
    }

    CompactLattice* RemoteRescore::rcv_lattice() {
        ssize_t size_of_lattice = 0;
        char header[4];

        // read header
        if (rcv_bytes(header, 4) == false) {
            error_log_func("Failed to read header from rescore socket");
            return NULL;
        }

        // get body size from header
        size_of_lattice = le32toh(*((uint32_t*)header));

        // allocate buffer for body
        char* buffer = new char[size_of_lattice];

        if (rcv_bytes(buffer, size_of_lattice) == false) {
            error_log_func("Failed to read lattice from rescore socket");
            return NULL;
        }

       // create a stream to read CompactLattice from
       membuf sbuf(buffer, buffer + size_of_lattice);
       std::istream in(&sbuf);
       CompactLattice* tmp_lat = NULL;
       if (ReadCompactLattice(in, true, &tmp_lat)) {
           return tmp_lat;
       } else {
           error_log_func("Failed to parse lattice");
           return NULL;
       }
    }

    bool RemoteRescore::rescore(CompactLattice &lat, CompactLattice &rescored_lat) {
        // connect to rescorer
        if (! connect_socket()) {
            return false;
        }

        // send lattice to remote rescorer
        if (! send_lattice(lat) ) {
            close_socket();
            return false;
        }

        // read rescored lattice
        CompactLattice* tmp = rcv_lattice();
        if (tmp == NULL ) {
            close_socket();
            return false;
        }

        rescored_lat = *tmp;
        delete tmp;

        // close socket
        close_socket();

        return true;
    }

    RemoteRescore::~RemoteRescore() {
        // destructor
    }


    bool RemoteRescore::process_address (std::string address) {
        const char* c_addr = address.c_str() + 2; // skip "u:" or "t:"

        if (address[0] == 'u') {
            memset(&addr, 0, sizeof(addr));            
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, c_addr, sizeof(addr.sun_path)-1);
        } else {
            error_log_func("Unable to create rescore socket. Protocol not implemented");
        }

        return true;
    }

    bool RemoteRescore::connect_socket () {
        if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            error_log_func("Unable to create rescore socket");
            return false;
        }
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
            error_log_func("Failed to connect to rescore socket");
            return false;
        }
        return true;
    }

    void RemoteRescore::close_socket () {
        close(fd);
    }

    bool RemoteRescore::rcv_bytes (char* buffer, ssize_t bytes) {
        ssize_t bytes_read = 0;
        ssize_t bytes_left = bytes;      
    
        // read bytes from socket
        do {
            bytes_read = read(fd, buffer+(bytes-bytes_left), bytes_left);
            bytes_left -= bytes_read;        
        }
        while (bytes_read >= 0 && bytes_left > 0);

        if (bytes_read == -1) {
            return false;
        }

        return true;
    }

    bool RemoteRescore::send_bytes (const char* buffer, ssize_t bytes) {
        if (write(fd, buffer, bytes) != bytes) {
            return false;
        }
        return true;
    }

}  // namespace kaldi

