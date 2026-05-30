# Copyright (c) 2025-present The Byze developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit-license.php.

enable_language(C CXX)

function(add_randomx subdir)
  message("")
  message("Configuring RandomX subtree...")
  set(BUILD_SHARED_LIBS OFF)
  set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
  
  # RandomX build options
  set(RANDOMX_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(RANDOMX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  
  # Use native architecture for best performance
  if(NOT DEFINED ARCH)
    set(ARCH "native" CACHE STRING "" FORCE)
  endif()
  
  include(GetTargetInterface)
  # Pass sanitize flags to RandomX
  get_target_interface(RANDOMX_APPEND_CFLAGS "" sanitize_interface COMPILE_OPTIONS)
  string(STRIP "${RANDOMX_APPEND_CFLAGS} ${APPEND_CPPFLAGS}" RANDOMX_APPEND_CFLAGS)
  string(STRIP "${RANDOMX_APPEND_CFLAGS} ${APPEND_CFLAGS}" RANDOMX_APPEND_CFLAGS)
  set(RANDOMX_APPEND_CFLAGS ${RANDOMX_APPEND_CFLAGS} CACHE STRING "" FORCE)
  
  get_target_interface(RANDOMX_APPEND_CXXFLAGS "" sanitize_interface COMPILE_OPTIONS)
  string(STRIP "${RANDOMX_APPEND_CXXFLAGS} ${APPEND_CPPFLAGS}" RANDOMX_APPEND_CXXFLAGS)
  string(STRIP "${RANDOMX_APPEND_CXXFLAGS} ${APPEND_CFLAGS}" RANDOMX_APPEND_CXXFLAGS)
  set(RANDOMX_APPEND_CXXFLAGS ${RANDOMX_APPEND_CXXFLAGS} CACHE STRING "" FORCE)
  
  get_target_interface(RANDOMX_APPEND_LDFLAGS "" sanitize_interface LINK_OPTIONS)
  string(STRIP "${RANDOMX_APPEND_LDFLAGS} ${APPEND_LDFLAGS}" RANDOMX_APPEND_LDFLAGS)
  set(RANDOMX_APPEND_LDFLAGS ${RANDOMX_APPEND_LDFLAGS} CACHE STRING "" FORCE)
  
  # Use RelWithDebInfo configuration for RandomX
  foreach(config IN LISTS CMAKE_BUILD_TYPE CMAKE_CONFIGURATION_TYPES)
    if(config STREQUAL "")
      continue()
    endif()
    string(TOUPPER "${config}" config)
    set(CMAKE_C_FLAGS_${config} "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    set(CMAKE_CXX_FLAGS_${config} "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
  endforeach()
  
  # If CFLAGS/CXXFLAGS environment variable is defined, deduplicate
  if(DEFINED ENV{CFLAGS})
    deduplicate_flags(CMAKE_C_FLAGS)
  endif()
  if(DEFINED ENV{CXXFLAGS})
    deduplicate_flags(CMAKE_CXX_FLAGS)
  endif()

  add_subdirectory(${subdir})
  
  # RandomX creates a target called 'randomx'
  if(TARGET randomx)
    set_target_properties(randomx PROPERTIES
      EXCLUDE_FROM_ALL TRUE
    )
  endif()
endfunction()



