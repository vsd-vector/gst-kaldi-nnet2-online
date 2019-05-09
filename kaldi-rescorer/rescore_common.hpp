//
// rescore_common
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2017 Askars Salimbajevs (SIA Tilde)
//
//

#ifndef RESCORE_COMMON_HPP
#define RESCORE_COMMON_HPP

#include <boost/shared_ptr.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <string>
#include <stdexcept>
#include "rescore_message.hpp"


inline std::string current_time() {
    return boost::posix_time::to_simple_string(boost::posix_time::second_clock::local_time());
}

class RescoreJob
{
public:
  virtual ~RescoreJob() {}
  virtual void deliver(RescoreMessage* msg) = 0;
  virtual void close() = 0;
};

typedef boost::shared_ptr<RescoreJob> RescoreJobPtr;

typedef std::deque<boost::shared_ptr<RescoreMessage> > RescoreMessageQueue;

#endif // RESCORE_COMMON_HPP
