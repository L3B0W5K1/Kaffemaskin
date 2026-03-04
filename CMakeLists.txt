cmake_minimum_required(VERSION 3.12)
project(lwm2m_gpio_client C)

set(CMAKE_C_STANDARD 11)

# ---- Find Anjay ----
# If Anjay was installed system-wide via `make install`, find_package works.
# Otherwise, set ANJAY_DIR to the Anjay build directory.
find_package(anjay QUIET)

if(NOT anjay_FOUND)
    message(STATUS "anjay package not found via find_package, trying pkg-config...")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ANJAY REQUIRED libanjay)
endif()

# ---- Build the client ----
add_executable(lwm2m_gpio_client
    src/main.c
    src/gpio_object.c
)

target_include_directories(lwm2m_gpio_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ANJAY_INCLUDE_DIRS}
)

target_link_libraries(lwm2m_gpio_client PRIVATE
    anjay
    ${ANJAY_LIBRARIES}
)

# Install target (optional)
install(TARGETS lwm2m_gpio_client DESTINATION bin)
