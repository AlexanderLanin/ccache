find_package(Git)
execute_process(COMMAND ${GIT_EXECUTABLE} describe
  OUTPUT_VARIABLE git_output
  ERROR_VARIABLE git_error
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
if (NOT git_error)
  string(REGEX MATCH "[0-9.]+" CCACHE_VERSION ${git_output})
else()
  set(CCACHE_VERSION 3.7.7)
endif()

file(WRITE ${CMAKE_BINARY_DIR}/version.cpp.in [=[
  extern const char CCACHE_VERSION[];
  const char CCACHE_VERSION[] = "@CCACHE_VERSION@";
]=])
configure_file(
  ${CMAKE_BINARY_DIR}/version.cpp.in
  ${CMAKE_BINARY_DIR}/version.cpp @ONLY)
