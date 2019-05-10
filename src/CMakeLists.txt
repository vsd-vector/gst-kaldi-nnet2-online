# fake CMakeLists.txt for CLion to parse project, at least somewhat
cmake_minimum_required(VERSION 3.5.1)
project(online-decoder)

set(KALDI_ROOT /home/andris/repos/kaldi)

include_directories( ${KALDI_ROOT}/src )
include_directories( ${KALDI_ROOT}/tools/openfst/include )

find_package(Boost 1.57.0 COMPONENTS system date_time REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

add_library(libgstkaldinnet2onlinedecoder.so gstkaldinnet2onlinedecoder.cc remote-rescore.cc)
