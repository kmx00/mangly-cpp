cmake_minimum_required(VERSION 3.16)
project(mangly LANGUAGES CXX VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# The library is header-only (FetchContent/CPM friendly). Consumers just link
# `mangly::mangly` and #include <mangly/mangly.hpp>.
add_library(mangly INTERFACE)
add_library(mangly::mangly ALIAS mangly)
target_include_directories(mangly INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
target_compile_features(mangly INTERFACE cxx_std_17)

# No RTTI anywhere: the header carries no type_info/dynamic_cast, so disabling
# RTTI is a hard compile-time guarantee.
if(MSVC)
  add_compile_options(/GR- /W4 /permissive-)
else()
  add_compile_options(-fno-rtti -Wall -Wextra)
endif()

# Optional sanitizers. -DMANGLY_SANITIZE=address|undefined|all turns the whole
# build (tests included) into a memory/UB safety sweep -- the safety net for the
# hand-rolled Arena/OutputBuffer/Vec and the recursive parser before/after perf
# work. MSVC supports AddressSanitizer only; GCC/Clang support both.
set(MANGLY_SANITIZE "" CACHE STRING "Sanitizers: address|undefined|all (empty=off)")
if(MANGLY_SANITIZE)
  if(MSVC)
    if(MANGLY_SANITIZE MATCHES "undefined")
      message(FATAL_ERROR "MSVC has no UndefinedBehaviorSanitizer; use address")
    endif()
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/DEBUG)  # silence LNK4302; better ASan symbolization
  else()
    set(_mangly_san "")
    if(MANGLY_SANITIZE STREQUAL "all")
      set(_mangly_san "address,undefined")
    else()
      set(_mangly_san "${MANGLY_SANITIZE}")
    endif()
    add_compile_options(-fsanitize=${_mangly_san} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${_mangly_san})
  endif()
endif()

# CLI: the only shipped binary. It is compiled with exceptions DISABLED, which
# proves the header (parser/mangler/cli) is exception-free and stays lean.
add_executable(mangly_cli src/main.cpp)
set_target_properties(mangly_cli PROPERTIES OUTPUT_NAME mangly)
target_link_libraries(mangly_cli PRIVATE mangly)
# Prove the header is exception-free by compiling the CLI with exceptions off
# (GCC/Clang). MSVC keeps its default EH model to avoid mutating global flags
# that the test harness (which uses exceptions) also depends on.
if(NOT MSVC)
  target_compile_options(mangly_cli PRIVATE -fno-exceptions)
endif()

enable_testing()
add_subdirectory(tests)
