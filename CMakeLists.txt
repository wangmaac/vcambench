cmake_minimum_required(VERSION 3.20)
project(vcambench CXX)

if(NOT MSVC)
  message(FATAL_ERROR "vcambench targets MSVC / Windows SDK only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT. vcamsource.dll is loaded into the Windows Frame Server
# (svchost.exe); depending on the VC++ redistributable being present there is an
# avoidable failure mode. It also keeps the download to a single self-contained
# installer.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
add_compile_options(/W4 /permissive- /EHsc /utf-8)

# ---------------------------------------------------------------------------
# C++/WinRT headers ship in the SDK under Include/<ver>/cppwinrt, which is not
# on the default include path for CMake-generated projects. Pick the newest.
# ---------------------------------------------------------------------------
if(NOT VCAM_CPPWINRT_INCLUDE_DIR)
  set(_kits "$ENV{WindowsSdkDir}")
  if(NOT _kits)
    set(_kits "C:/Program Files (x86)/Windows Kits/10")
  endif()
  file(GLOB _cppwinrt_dirs "${_kits}/Include/*/cppwinrt")
  if(_cppwinrt_dirs)
    list(SORT _cppwinrt_dirs)
    list(REVERSE _cppwinrt_dirs)
    list(GET _cppwinrt_dirs 0 VCAM_CPPWINRT_INCLUDE_DIR)
  endif()
endif()

# ---------------------------------------------------------------------------
# vcamcore - frame generation. No dependencies, no side effects, unit testable.
# ---------------------------------------------------------------------------
add_library(vcamcore STATIC
  src/vcamcore/frame_pattern.cpp
  src/vcamcore/font5x7.cpp
)
target_include_directories(vcamcore PUBLIC include)

add_executable(vcamcore_tests tests/test_frame_pattern.cpp)
target_link_libraries(vcamcore_tests PRIVATE vcamcore)

enable_testing()
add_test(NAME vcamcore_tests COMMAND vcamcore_tests)

# ---------------------------------------------------------------------------
# vcamcommon - helpers shared by the diagnostic tools, kept out of vcamcore so
# that library stays free of Windows dependencies.
# ---------------------------------------------------------------------------
add_library(vcamcommon STATIC src/common/nv12_bmp.cpp)
target_include_directories(vcamcommon PUBLIC src)

# ---------------------------------------------------------------------------
# vcamsource.dll - the COM in-proc media source the Frame Server loads.
# ---------------------------------------------------------------------------
add_library(vcamsource SHARED
  src/vcamsource/dllmain.cpp
  src/vcamsource/logging.cpp
  src/vcamsource/media_source.cpp
  src/vcamsource/media_stream.cpp
  src/vcamsource/vcamsource.def
)
target_link_libraries(vcamsource PRIVATE vcamcore mfplat mfuuid mf ole32 oleaut32 advapi32 shlwapi)
target_include_directories(vcamsource PRIVATE src)
# C4458: parameters named `value` (the names the IMFAttributes contract uses)
# shadow a member of WRL's RuntimeClassFlags.
target_compile_options(vcamsource PRIVATE /wd4458)

# ---------------------------------------------------------------------------
# vcamctl - console front end for CameraManager. The GUI replaces this later;
# the manager underneath is the part that ships.
# ---------------------------------------------------------------------------
add_executable(vcamctl
  src/vcamapp/main.cpp
  src/vcamapp/camera_manager.cpp
)
target_link_libraries(vcamctl PRIVATE mfsensorgroup mfplat mfuuid mf ole32 oleaut32)
target_include_directories(vcamctl PRIVATE src include)

# ---------------------------------------------------------------------------
# Diagnostics kept from the prototype: they are how every claim about this
# project has been checked.
# ---------------------------------------------------------------------------
add_executable(camlist src/camlist/main.cpp)
target_link_libraries(camlist PRIVATE mf mfplat mfuuid strmiids ole32 oleaut32)
if(VCAM_CPPWINRT_INCLUDE_DIR)
  target_include_directories(camlist PRIVATE "${VCAM_CPPWINRT_INCLUDE_DIR}")
  target_compile_definitions(camlist PRIVATE VCAM_HAVE_CPPWINRT=1)
  target_link_libraries(camlist PRIVATE windowsapp)
endif()

add_executable(camcapture src/camcapture/main.cpp)
target_link_libraries(camcapture PRIVATE vcamcommon mfplat mfuuid mf mfreadwrite ole32 oleaut32)
target_include_directories(camcapture PRIVATE src)
