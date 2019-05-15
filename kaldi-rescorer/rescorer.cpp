//
// rescorer.cpp
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2015 Askars Salimbajevs (SIA Tilde)
//
//

#include <algorithm>
#include <deque>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <boost/signals2.hpp>
#include <boost/asio/basic_stream_socket.hpp>
#include <boost/asio/basic_socket_acceptor.hpp>

#include "rescore_message.hpp"
#include "rescore_common.hpp"
#include "rescore_dispatch.hpp"

// the two protocols we support, we template over one of these
using boost::asio::ip::tcp;
using boost::asio::local::stream_protocol;

// global SIGTERM ignore counters and state
// we use atomic versions here just to be on the safe side from concurrency perspective
std::atomic_int message_counter{0};
std::atomic_bool termination_scheduled{false};

//----------------------------------------------------------------------
template<typename Protocol>
class RescoreSession
        : public RescoreJob,
          public boost::enable_shared_from_this<RescoreSession<Protocol> > {
public:
    RescoreSession(boost::asio::io_service &io_service,
                   RescoreDispatch *dispatcher)
            : socket_(io_service),
              dispatcher_(dispatcher) {
    }

    boost::asio::basic_stream_socket<Protocol> &socket() {
        return socket_;
    }

    void close() override {
        socket_.close();
    }

    void terminate_check() {
        if (message_counter == 0 && termination_scheduled) {
            // write_msgs are empty, and no new read messages have been added
            // and SIGTERM had been received in the past, time to go...
            KALDI_LOG << current_time()
                      << ": All messages processed. Termination was scheduled. Exiting...";
            exit(0);
        }
    }

    void start() {
        boost::asio::async_read(socket_,
                                boost::asio::buffer(read_msg_.data(),
                                                    RescoreMessage::header_length),
                                boost::bind(&RescoreSession::handle_read_header,
                                            this->shared_from_this(),
                                            boost::asio::placeholders::error));
    }

    void deliver(RescoreMessage *msg) override {
        bool write_in_progress = !write_msgs_.empty();
        write_msgs_.push_back(boost::shared_ptr<RescoreMessage>(msg));
        KALDI_LOG << current_time()
                  << ": sending rescored lattice back (write_in_progress = "
                  << write_in_progress << ")";
        if (!write_in_progress) {
            KALDI_LOG << current_time()
                      << ": will send buffer of size "
                      << write_msgs_.front()->length()
                      << ". message_counter = "
                      << message_counter;
            boost::asio::async_write(socket_,
                                     boost::asio::buffer(write_msgs_.front()->data(),
                                                         write_msgs_.front()->length()),
                                     boost::bind(&RescoreSession::handle_write,
                                                 this->shared_from_this(),
                                                 boost::asio::placeholders::error));
        }
    }

    void handle_read_header(const boost::system::error_code &error) {
        if (!error) {
            if (!read_msg_.decode_header()) {
                KALDI_WARN << "Failed to read lattice from client. Lattice too big?";
                // reply with error msg
                RescoreMessage *out = new RescoreMessage();
                out->body_length(3);
                out->body()[0] = 'E';
                out->body()[1] = 'E';
                out->body()[2] = 'R';
                out->encode_header();
                deliver(out);
                // disconnect
                close();
            } else {
                KALDI_LOG << current_time()
                          << ": starting to receive lattice of size "
                          << read_msg_.body_length();
                boost::asio::async_read(socket_,
                                        boost::asio::buffer(read_msg_.body(),
                                                            read_msg_.body_length()),
                                        boost::bind(&RescoreSession::handle_read_body,
                                                    this->shared_from_this(),
                                                    boost::asio::placeholders::error));
            }
        }
    }

    void handle_read_body(const boost::system::error_code &error) {
        if (!error) {
            // a new read message has been successfully received and will be processed
            // and will require a response, increment the counter
            message_counter += 1;
            KALDI_LOG << current_time()
                      << ": lattice of size "
                      << read_msg_.body_length()
                      << " received (message_counter = "
                      << message_counter
                      << " ). Rescoring...";
            // process read_msg_ and rescore
            dispatcher_->rescore(read_msg_, this->shared_from_this());
            // wait for next header
            start();
        } else {
            KALDI_WARN << current_time()
                       << ": failed to read lattice body from client. Error code: "
                       << error.message();
        }
    }

    void handle_write(const boost::system::error_code &error) {
        // remove message from queue
        write_msgs_.pop_front();
        message_counter -= 1;
        if (!error) {
            // continue sending
            if (!write_msgs_.empty()) {
                KALDI_LOG << current_time()
                          << ": will send buffer of size "
                          << write_msgs_.front()->length()
                          << ". message_counter = "
                          << message_counter;
                boost::asio::async_write(socket_,
                                         boost::asio::buffer(write_msgs_.front()->data(),
                                                             write_msgs_.front()->length()),
                                         boost::bind(&RescoreSession::handle_write,
                                                     this->shared_from_this(),
                                                     boost::asio::placeholders::error));
            } else {
                KALDI_LOG << current_time()
                          << ": All scheduled messages have been written. message_counter = "
                          << message_counter;
                // no more write messages remaining, a good time to check whether we need to DIE
                terminate_check();
            }
        } else {
            KALDI_WARN << current_time()
                       << ": failed to send lattice to client (message_counter = "
                       << message_counter
                       << " ). Error code: "
                       << error.message();
            // message failed sending, maybe there's no more messages remaining, let's see
            terminate_check();
        }
    }

private:
    // The socket used to communicate with the client.
    boost::asio::basic_stream_socket<Protocol> socket_;

    RescoreMessage read_msg_;
    RescoreMessageQueue write_msgs_;

    RescoreDispatch *dispatcher_;
};

//----------------------------------------------------------------------

template<typename Protocol>
class Server {
public:
    Server(boost::asio::io_service &io_service,
           const typename Protocol::endpoint &endpoint,
           RescoreDispatch *dispatcher)
            : io_service_(io_service),
              acceptor_(io_service, endpoint),
              dispatcher_(dispatcher),
              signals_(io_service, SIGINT, SIGTERM) {
        boost::shared_ptr<RescoreSession<Protocol> > new_session(
                new RescoreSession<Protocol>(io_service_, dispatcher_));
        acceptor_.async_accept(new_session->socket(),
                               boost::bind(&Server::handle_accept,
                                           this,
                                           new_session,
                                           boost::asio::placeholders::error));

        // handle signals
        signals_.async_wait(boost::bind(&Server::handle_signals,
                                        this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::signal_number));
    }

    void handle_signals(const boost::system::error_code &error,
                        int signal_number) {
        if (!error) {
            // A signal occurred.
            KALDI_LOG << current_time() << ": signal " << signal_number << " received";
            if (message_counter == 0) {
                exit(0);
            } else {
                KALDI_LOG << current_time()
                          << ": Message counter is not 0 ( = " << message_counter
                          << "). Scheduling a future termination.";
                termination_scheduled = true;
            }
        }

        KALDI_WARN << current_time() << " error in signal handler: " << error.message();
    }

    void handle_accept(boost::shared_ptr<RescoreSession<Protocol> > new_session,
                       const boost::system::error_code &error) {
        if (!error) {
            new_session->start();
            new_session.reset(new RescoreSession<Protocol>(io_service_, dispatcher_));
            acceptor_.async_accept(new_session->socket(),
                                   boost::bind(&Server::handle_accept,
                                               this,
                                               new_session,
                                               boost::asio::placeholders::error));
        }

    }

private:
    boost::asio::io_service &io_service_;
    boost::asio::basic_socket_acceptor<Protocol> acceptor_;
    RescoreDispatch *dispatcher_;
    boost::asio::signal_set signals_;
};

//----------------------------------------------------------------------

int main(int argc, char *argv[]) {
    try {
        const char *usage =
                "Multithreaded server for remote lattice rescoring.\n"
                "Usage: rescorer [options] <address> <lm-fst-rspecifier>\n";
        ParseOptions po(usage);
        TaskSequencerConfig sequencer_config;
        sequencer_config.Register(&po);

        std::string rescore_const_arpa_lm;
        std::string rescore_rnnlm_dir;
        // "carpa", "rnnlm" or "both"
        std::string rescore_mode = "carpa";
        kaldi::int32 max_ngram_order = 4;
        po.Register("mode", &rescore_mode, "defines how the rescorer operates. \"carpa\" uses "
                                           "just a const-arpa model to perform rescoring. "
                                           "\"rnnlm\" uses just the rnnlm model to perform rescoring, while"
                                           "\"both\" performs rescoring with carpa, and then with rnnlm afterwards.");
        po.Register("const-arpa", &rescore_const_arpa_lm, "ConstArpa LM rspecifier, required if the mode is "
                                                          "\"carpa\" or \"both\"");
        po.Register("rnnlm-dir", &rescore_rnnlm_dir, "path to directory with required kaldi-RNNLM model, "
                                                     "required if the mode is \"rnnlm\" or \"both\". "
                                                     "Directory should contain \"word_embedding.final.mat\"(or "
                                                     "\"feat_embedding.final.mat\"), \"final.raw\" "
                                                     "and \"special_symbol_opts.txt\".");
        po.Register("max-ngram-order", &max_ngram_order,
                    "If positive, allow RNNLM histories longer than this to be identified "
                    "with each other for rescoring purposes (an approximation that "
                    "saves time and reduces output lattice size).");


        po.Read(argc, argv);

        if (po.NumArgs() != 2) {
            po.PrintUsage();
            return 1;
        }
        // validate named args...
        bool do_carpa_rescore = rescore_mode == "carpa" || rescore_mode == "both";
        bool do_rnnlm_rescore = rescore_mode == "rnnlm" || rescore_mode == "both";
        if (do_carpa_rescore && rescore_const_arpa_lm.empty()) {
            po.PrintUsage();
            return 1;
        }
        if (do_rnnlm_rescore && rescore_rnnlm_dir.empty()) {
            po.PrintUsage();
            return 1;
        }

        std::string address = po.GetArg(1),
                lm_fst = po.GetArg(2);

        bool do_tcp = address[0] == 't'; // unix sockets by default
        // still, verify that address has been specified correctly
        if ((address[0] != 'u' && address[0] != 't') || address[1] != ':') {
            KALDI_WARN << "Unsupported address type: "
                       << address[0];
            po.PrintUsage();
            return 1;
        }

        address = address.substr(2, address.length());

        // load dispatcher
        // dispatcher trusts that arguments have been validated above...
        KALDI_LOG << current_time()
                  << ": Loading requested models";
        RescoreDispatch *dispatch = new RescoreDispatch(sequencer_config,
                                                        rescore_mode,
                                                        lm_fst,
                                                        rescore_const_arpa_lm,
                                                        rescore_rnnlm_dir,
                                                        max_ngram_order,
                                                        do_carpa_rescore,
                                                        do_rnnlm_rescore);

        boost::asio::io_service io_service;
        if (do_tcp) {
            // server is allocated on stack, and for some reason, gets thrown out, when we exit
            // from branch, so we block with io_service.run() inside branches, not afterwards
            // which would've been prettier...
            KALDI_LOG << current_time()
                      << ": Starting rescorer in tcp mode on port: "
                      << address;
            tcp::endpoint endpoint(tcp::v4(), std::atoi(address.c_str()));
            Server<tcp> s(io_service, endpoint, dispatch);
            io_service.run();
        } else {
            KALDI_LOG << current_time()
                      << ": Starting rescorer on unix socket at: "
                      << address;
            // unbind file at address
            unlink(address.c_str());
            stream_protocol::endpoint endpoint(address);
            Server<stream_protocol> s(io_service, endpoint, dispatch);
            io_service.run();
        }

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
