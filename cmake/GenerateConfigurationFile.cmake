include(CheckIncludeFile)
foreach(include_file IN ITEMS pwd.h sys/mman.h sys/time.h sys/wait.h termios.h)
  string(TOUPPER ${include_file} include_var)
  string(REGEX REPLACE "[/.]" "_" include_var ${include_var})
  set(include_var HAVE_${include_var})
  check_include_file(${include_file} ${include_var})
endforeach()

include(CheckFunctionExists)
foreach(
  func IN
  ITEMS geteuid
        GetFinalPathNameByHandleW
        getopt_long
        getpwuid
        gettimeofday
        localtime_r
        mkstemp
        posix_fallocate
        realpath
        strndup
        strtok_r
        unsetenv
        utimes)
  string(TOUPPER ${func} func_var)
  set(func_var HAVE_${func_var})
  check_function_exists(${func} ${func_var})
endforeach()

list(APPEND CMAKE_REQUIRED_LIBRARIES ws2_32)
list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES ws2_32)

include(CheckTypeSize)
check_type_size("long long" HAVE_LONG_LONG)

if(WIN32)
  set(_WIN32_WINNT 0x0600)
endif()

if(CMAKE_SYSTEM MATCHES "Darwin")
  set(_DARWIN_C_SOURCE 1)
endif()

# alias
set(MTR_ENABLED "${ENABLE_TRACING}")

configure_file(${CMAKE_SOURCE_DIR}/cmake/config.h.in
               ${CMAKE_BINARY_DIR}/config.h @ONLY)
