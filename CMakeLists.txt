cmake_minimum_required(VERSION 3.14)
project(tx81z_emu CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(YMFM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ymfm/src)

# core library: CPU + OPZ + machine (reused later by the Pd external / Bela)
add_library(tx81z_core STATIC
  src/cpu/hd6303x.cpp
  src/tx81z.cpp
  ${YMFM_DIR}/ymfm_opz.cpp
  ${YMFM_DIR}/ymfm_misc.cpp
  ${YMFM_DIR}/ymfm_adpcm.cpp
  ${YMFM_DIR}/ymfm_ssg.cpp
)
target_include_directories(tx81z_core PUBLIC src ${YMFM_DIR})

add_executable(tx81z-emu src/main.cpp)
target_link_libraries(tx81z-emu tx81z_core)
