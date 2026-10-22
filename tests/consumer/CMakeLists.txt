cmake_minimum_required(VERSION 3.20)
project(uxid_consumer LANGUAGES C)

# Only the installed package may satisfy this: the test passes the scratch
# prefix as CMAKE_PREFIX_PATH and turns the package registries off.
find_package(uxid ${UXID_EXPECTED_VERSION} EXACT CONFIG REQUIRED)

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE uxid::uxid)
