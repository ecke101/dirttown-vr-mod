set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
# Debian/Ubuntu provide separate thread models. Prefer POSIX for std::mutex
# and std::thread; distributions with a single POSIX compiler use the fallback.
find_program(DTVR_MINGW_C NAMES x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-gcc REQUIRED)
find_program(DTVR_MINGW_CXX NAMES x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++ REQUIRED)
set(CMAKE_C_COMPILER "${DTVR_MINGW_C}")
set(CMAKE_CXX_COMPILER "${DTVR_MINGW_CXX}")
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
