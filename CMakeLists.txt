cmake_minimum_required(VERSION 3.20)
project(bambu_networking VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Version define ----------------------------------------------------------
set(BAMBU_PLUGIN_VERSION "02.06.00.00" CACHE STRING "Bambu plugin version string")
add_compile_definitions(BAMBU_PLUGIN_VERSION="${BAMBU_PLUGIN_VERSION}")

# ---- Find dependencies -------------------------------------------------------
find_package(OpenSSL REQUIRED)
find_package(CURL    REQUIRED)
find_package(Threads REQUIRED)

find_library(MOSQUITTO_LIB mosquitto REQUIRED
    HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
if(NOT MOSQUITTO_LIB)
    message(FATAL_ERROR "libmosquitto not found. Install mosquitto development package.")
endif()
message(STATUS "Found libmosquitto: ${MOSQUITTO_LIB}")

# ---- nlohmann/json via FetchContent -----------------------------------------
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(nlohmann_json)

# ---- Source files -----------------------------------------------------------
set(BAMBU_SOURCES
    src/MqttClient.cpp
    src/FtpClient.cpp
    src/CloudClient.cpp
    src/NetworkAgent.cpp
    src/exports.cpp
)

# ---- Shared library target --------------------------------------------------
add_library(bambu_networking SHARED ${BAMBU_SOURCES})

target_include_directories(bambu_networking
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CURL_INCLUDE_DIRS}
        ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(bambu_networking
    PRIVATE
        ${MOSQUITTO_LIB}
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        nlohmann_json::nlohmann_json
)

# ---- Symbol visibility (hide everything except BAMBU_EXPORT) ----------------
set_target_properties(bambu_networking PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    SOVERSION 1
    VERSION   1.0.0
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(bambu_networking PRIVATE -fvisibility=hidden)
endif()

# ---- Optional debug build flag ----------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(bambu_networking PRIVATE BAMBU_DEBUG=1)
endif()

# ---- Install rules ----------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS bambu_networking
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(FILES include/bambu_networking.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ---- Summary ----------------------------------------------------------------
message(STATUS "bambu_networking: version=${BAMBU_PLUGIN_VERSION}")
message(STATUS "  OpenSSL : ${OPENSSL_VERSION}")
message(STATUS "  CURL    : ${CURL_VERSION_STRING}")
message(STATUS "  mosquitto: ${MOSQUITTO_LIB}")
