//
// rescore_common
// ~~~~~~~~~~~~~~~
//
// Copyright (c) 2015 Askars Salimbajevs (SIA Tilde)
//
//

#ifndef RESCORE_COMMON_HPP
#define RESCORE_COMMON_HPP

#include <boost/shared_ptr.hpp>

#include "rescore_message.hpp"

class rescore_job
{
public:
  virtual ~rescore_job() {}
  virtual void deliver(rescore_message* msg) = 0;
  virtual void close() = 0;
};

typedef boost::shared_ptr<rescore_job> rescore_job_ptr;

#endif // RESCORE_COMMON_HPP
