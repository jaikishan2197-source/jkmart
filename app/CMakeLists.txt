
cmake_minimum_required(VERSION 3.20)
project(jkmart_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Drogon REQUIRED CONFIG)
find_package(Trantor REQUIRED CONFIG)
find_package(jsoncpp REQUIRED CONFIG)
find_package(PostgreSQL REQUIRED)

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
)

target_link_libraries(jkmart_backend PRIVATE
  Drogon::Drogon
  Trantor::Trantor
  jsoncpp_lib
  PostgreSQL::PostgreSQL
)

target_compile_definitions(jkmart_backend PRIVATE
  PGSQL_ROOT=""
)
