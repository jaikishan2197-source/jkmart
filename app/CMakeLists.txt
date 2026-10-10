cmake_minimum_required(VERSION 3.20)
project(jkmart_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JKMART_DEPS "C:/Users/Home/Desktop/jkmart/jkmart/install")
set(PGSQL_ROOT "C:/Users/Home/Desktop/jkmart/jkmart/pgsql/pgsql")

set(CMAKE_PREFIX_PATH "${JKMART_DEPS}" ${CMAKE_PREFIX_PATH})

find_package(Drogon REQUIRED CONFIG)
find_package(Trantor REQUIRED CONFIG)
find_package(jsoncpp REQUIRED CONFIG)

add_executable(jkmart_backend
  src/main.cpp
  src/Db.cpp
  src/Crypto.cpp
  src/Auth.cpp
  src/Products.cpp
  src/Commerce.cpp
  src/Reviews.cpp
  src/Admin.cpp
  src/Assistant.cpp
  src/Handlers.cpp
)

target_include_directories(jkmart_backend PRIVATE
  src
  ${PGSQL_ROOT}/include
)

target_link_libraries(jkmart_backend PRIVATE
  Drogon::Drogon
  Trantor::Trantor
  jsoncpp_lib
  ${PGSQL_ROOT}/bin/libpq.dll
)

target_compile_definitions(jkmart_backend PRIVATE
  PGSQL_ROOT="${PGSQL_ROOT}"
)
