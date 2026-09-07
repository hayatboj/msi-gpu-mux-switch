cmake_minimum_required(VERSION 3.24)
project(msi-mux VERSION 0.5.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "The KDE tray and privileged helper are Linux-only. Build the Windows CLI with Cargo.")
endif()
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX /usr CACHE PATH "Installation prefix" FORCE)
endif()
if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
  message(FATAL_ERROR "Use -DCMAKE_INSTALL_PREFIX=/usr; the privileged interface uses fixed trusted paths. Stage with DESTDIR.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
include(CTest)
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network DBus Test)
find_program(CARGO_EXECUTABLE cargo REQUIRED)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo-target")
file(GLOB_RECURSE RUST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.rs")
add_custom_command(
  OUTPUT "${RUST_TARGET_DIR}/release/msi-mux-switch"
  COMMAND "${CARGO_EXECUTABLE}" build --locked --release --target-dir "${RUST_TARGET_DIR}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  DEPENDS ${RUST_SOURCES} Cargo.toml Cargo.lock build.rs
  VERBATIM)
add_custom_target(msi-mux-backend ALL DEPENDS "${RUST_TARGET_DIR}/release/msi-mux-switch")
install(PROGRAMS "${RUST_TARGET_DIR}/release/msi-mux-switch" DESTINATION bin)

add_executable(msi-mux-helper packaging/helper.cpp)
target_compile_options(msi-mux-helper PRIVATE -Wall -Wextra -Wpedantic -Werror -fstack-protector-strong -fPIE)
target_link_options(msi-mux-helper PRIVATE -pie -Wl,-z,relro,-z,now)
install(TARGETS msi-mux-helper RUNTIME DESTINATION lib/msi-mux)
install(FILES packaging/org.hayatboj.msimux.policy DESTINATION share/polkit-1/actions)
install(FILES packaging/org.hayatboj.msimux.desktop DESTINATION share/applications)
install(FILES packaging/org.hayatboj.msimux.metainfo.xml DESTINATION share/metainfo)
install(FILES README.md README.tr.md CHANGELOG.md LICENSE SECURITY.md DESTINATION share/doc/msi-mux)
install(FILES docs/RECOVERY.md docs/VALIDATION.md DESTINATION share/doc/msi-mux/docs)
add_subdirectory(gui)

install(FILES docs/images/kde-en.png docs/images/kde-tr.png DESTINATION share/doc/msi-mux/docs/images)

if(BUILD_TESTING)
  add_test(NAME helper-rejects-invalid-input
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/packaging/tests/test_helper.py" "$<TARGET_FILE:msi-mux-helper>")
  add_test(NAME bundle-installer-tests
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/packaging/tests/test_installer.py")
endif()
