//
// rescore_message.hpp
// ~~~~~~~~~~~~~~~~
//
//

#ifndef RESCORE_MESSAGE_HPP
#define RESCORE_MESSAGE_HPP

#include <cstdio>
#include <cstdlib>
#include <stdint.h>
#include <cstring>
#include <sstream>
#include <stdexcept>

class rescore_message
{
public:
  enum { header_length = 4 }; // 4 bytes
  enum { max_body_length = 1024*1024*100 }; // size limit for sanity (100MB)

  rescore_message()
    : body_length_(0)
  {
      data_ = new char[header_length + max_body_length];
  }

  ~rescore_message() 
  {
      delete[] data_;
  }

  const char* data() const
  {
    return data_;
  }

  char* data()
  {
    return data_;
  }

  size_t length() const
  {
    return header_length + body_length_;
  }

  const char* body() const
  {
    return data_ + header_length;
  }

  char* body()
  {
    return data_ + header_length;
  }

  size_t body_length() const
  {
    return body_length_;
  }

  void body_length(size_t new_length)
  {   
    if (new_length > max_body_length) {
      new_length = max_body_length;
      throw std::runtime_error("Can not allocate buffer for message body. Body size > MAX");
    }

    body_length_ = new_length;
  }

  bool decode_header()
  {
    body_length_ = le32toh(*((uint32_t*)data_));
    if (body_length_ > max_body_length)
    {
      body_length_ = 0;
      return false;
    }

    delete[] data_;
    data_ = new char[header_length + body_length_];
    *((uint32_t*)data_) = htole32(body_length_);

    return true;
  }

  void encode_header()
  {
      *((uint32_t*)data_) = htole32(body_length_);
  }

  void set_body_from_stream(std::ostringstream str) {
      memcpy(body(), str.str().c_str(), body_length_);
  }

private:
  char* data_;
  size_t body_length_;
};

#endif // RESCORE_MESSAGE_HPP
