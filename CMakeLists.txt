cmake_minimum_required(VERSION 3.20)

project(claimgate
        VERSION 0.1.0
        DESCRIPTION "Turn a library's documented accuracy claims into machine-checkable rows"
        LANGUAGES CXX)

option(CLAIMGATE_BUILD_TESTS "Build the test suite" ON)
option(CLAIMGATE_BUILD_EXAMPLES "Build the examples" ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------

add_library(claimgate src/claimgate.cpp)
add_library(claimgate::claimgate ALIAS claimgate)

target_compile_features(claimgate PUBLIC cxx_std_17)
set_target_properties(claimgate PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

target_include_directories(claimgate PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# The warnings every target in this project is built with.
add_library(claimgate-warnings INTERFACE)

if(MSVC)
    target_compile_options(claimgate-warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(claimgate-warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# Build-interface only: the warnings belong to this project's build, not to a
# consumer's, so the target stays out of the installed export set.
target_link_libraries(claimgate PRIVATE $<BUILD_INTERFACE:claimgate-warnings>)

# ---------------------------------------------------------------------------
# The test suite
# ---------------------------------------------------------------------------

if(CLAIMGATE_BUILD_TESTS)
    enable_testing()

    add_executable(claimgate-tests tests/claimgate_tests.cpp)
    target_link_libraries(claimgate-tests PRIVATE claimgate::claimgate claimgate-warnings)
    add_test(NAME claimgate-tests COMMAND claimgate-tests)
endif()

# ---------------------------------------------------------------------------
# The examples
# ---------------------------------------------------------------------------

if(CLAIMGATE_BUILD_EXAMPLES)
    add_executable(claimgate-example examples/toy_accuracy.cpp)
    target_link_libraries(claimgate-example PRIVATE claimgate::claimgate claimgate-warnings)

    if(CLAIMGATE_BUILD_TESTS)
        # The sample run is green, and the same sample pointed at a bound its
        # lane does not meet is red: the exit status is the gate's verdict.
        add_test(NAME claimgate-example COMMAND claimgate-example)
        add_test(NAME claimgate-example-exceeded COMMAND claimgate-example --exceeded)
        set_tests_properties(claimgate-example-exceeded PROPERTIES WILL_FAIL TRUE)
    endif()
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

install(TARGETS claimgate
        EXPORT claimgate-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT claimgate-targets
        FILE claimgate-targets.cmake
        NAMESPACE claimgate::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/claimgate)

configure_package_config_file(
    cmake/claimgate-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/claimgate-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/claimgate)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/claimgate-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/claimgate-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/claimgate-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/claimgate)
