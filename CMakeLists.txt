cmake_minimum_required(VERSION 3.15)
project(VCP VERSION 1.01 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_FILES
    VCP.cpp
    FTP.cpp
)

set(SERVER_SRC
  Server/server_main.cpp
  Server/protocol.cpp
  Server/storage.cpp
  Server/logging.cpp
  Server/auth.cpp
)

# Try the modern FindOpenSSL module first
find_package(OpenSSL)

# Fallback to pkg-config if OpenSSL wasn't found by CMake
find_package(PkgConfig QUIET)
if(NOT OpenSSL_FOUND AND PKG_CONFIG_FOUND)
  pkg_check_modules(OPENSSL_PKG QUIET openssl)
endif()

add_executable(vcp ${SRC_FILES})

if(OpenSSL_FOUND)
  target_link_libraries(vcp PRIVATE OpenSSL::SSL OpenSSL::Crypto)
elseif(OPENSSL_PKG_FOUND)
  target_include_directories(vcp PRIVATE ${OPENSSL_PKG_INCLUDE_DIRS})
  target_link_libraries(vcp PRIVATE ${OPENSSL_PKG_LIBRARIES})
else()
  message(WARNING "OpenSSL not found. You may need to set -DOPENSSL_ROOT_DIR or use pkg-config/vcpkg to provide OpenSSL.")
endif()

add_executable(vcpserver ${SERVER_SRC})

if(OpenSSL_FOUND)
  target_link_libraries(vcpserver PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  target_include_directories(vcpserver PRIVATE ${OPENSSL_INCLUDE_DIR})
elseif(OPENSSL_PKG_FOUND)
  target_include_directories(vcpserver PRIVATE ${OPENSSL_PKG_INCLUDE_DIRS})
  target_link_libraries(vcpserver PRIVATE ${OPENSSL_PKG_LIBRARIES})
else()
  message(WARNING "OpenSSL not found - server may lack password hashing support")
endif()

# Find and link SQLite3 for server (used for auth storage)
find_package(SQLite3 REQUIRED)
if(SQLite3_FOUND)
  target_link_libraries(vcpserver PRIVATE SQLite::SQLite3)
else()
  message(WARNING "SQLite3 not found - server auth will not build correctly if missing")
endif()
