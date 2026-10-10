@echo off
setlocal
set ROOT=%~dp0
set TC=C:\tools\jkmart
set GCC=C:\Users\Ayesha Amren\Downloads\gcc-16.2.0-gdb-17.2.90.20260510-binutils-2.46.1-mingw-w64-v14.0.0-ucrt\bin
set PATH=%GCC%;%TC%\install\bin;%TC%\pgsql\pgsql\bin;%TC%\ninja;C:\Program Files\CMake\bin;%PATH%

set PGHOST=127.0.0.1
set PGPORT=5432
set PGDATABASE=jkmart
set PGUSER=postgres

if not exist "%ROOT%build" mkdir "%ROOT%build"
cmake -S "%ROOT%" -B "%ROOT%build" -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build "%ROOT%build" || exit /b 1
"%ROOT%build\jkmart_backend.exe"