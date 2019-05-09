cmake_minimum_required (VERSION 3.5.1)
project (rescorer)

set(KALDI_ROOT /home/andris/repos/kaldi)

include_directories( ${KALDI_ROOT}/src )
include_directories( ${KALDI_ROOT}/tools/openfst/include )

find_package(Boost 1.57.0 COMPONENTS system date_time REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

add_executable(rescorer_unix rescore_dispatch.cpp rescorer_unix.cpp)
add_executable(rescorer_tcp rescore_dispatch.cpp rescorer_tcp.cpp)