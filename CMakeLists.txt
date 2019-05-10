# fake CMakeLists file for parsing both rescorer and decoder subprojects
cmake_minimum_required(VERSION 3.5.1)
project(root)

add_subdirectory(src)
add_subdirectory(kaldi-rescorer)