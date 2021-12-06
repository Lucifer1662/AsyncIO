# CMakeList.txt : CMake project for AsyncIO, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

file (GLOB_RECURSE Foo_SOURCES CONFIGURE_DEPENDS "*.cpp")
file (GLOB_RECURSE Foo_HEADERS CONFIGURE_DEPENDS "*.h")

# Add source to this project's executable.
add_executable (AsyncIO ${Foo_SOURCES} ${Foo_HEADERS})
if(WIN32)
  target_link_libraries(AsyncIO wsock32 ws2_32)
 endif()
# TODO: Add tests and install targets if needed.
