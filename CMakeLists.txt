cmake_minimum_required(VERSION 3.20)

# The version lives in one file that both this build and the Inno Setup script
# read. They disagreed once - the v0.1.1 tag was cut while the installer still
# declared 0.1.0 - and a single source is the only fix that cannot drift.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" VCAM_VERSION)
string(STRIP "${VCAM_VERSION}" VCAM_VERSION)

# RC is a language here, not an afterthought: the version resource below is
# compiled into every shipped binary.
project(vcambench VERSION ${VCAM_VERSION} LANGUAGES CXX RC)

if(NOT MSVC)
  message(FATAL_ERROR "vcambench targets MSVC / Windows SDK only.")
endif()

# The resource template wants the parts separately; FILEVERSION is a
# comma-separated quad, not a string.
set(VCAM_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VCAM_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VCAM_VERSION_PATCH ${PROJECT_VERSION_PATCH})
message(STATUS "VCamBench version ${VCAM_VERSION}")

# ---------------------------------------------------------------------------
# vcam_add_version_resource(<target> <APP|DLL> <description>)
#
# Gives a binary its Details tab: product name, version, copyright. Windows
# reads it for the UAC prompt and Task Manager, and SignPath checks ProductName
# and ProductVersion against the registered project before it will sign.
# ---------------------------------------------------------------------------
function(vcam_add_version_resource target kind description)
  if(kind STREQUAL "DLL")
    set(VCAM_RC_FILETYPE "VFT_DLL")
    set(VCAM_RC_FILENAME "${target}.dll")
  else()
    set(VCAM_RC_FILETYPE "VFT_APP")
    set(VCAM_RC_FILENAME "${target}.exe")
  endif()
  set(VCAM_RC_NAME "${target}")
  set(VCAM_RC_DESCRIPTION "${description}")

  set(_out "${CMAKE_CURRENT_BINARY_DIR}/version_${target}.rc")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in" "${_out}" @ONLY)
  target_sources(${target} PRIVATE "${_out}")
endfunction()

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
  src/vcamsource/attr_dump.cpp
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
vcam_add_version_resource(vcamsource DLL "VCamBench virtual camera media source")

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
vcam_add_version_resource(vcamctl APP "VCamBench command line control")

# ---------------------------------------------------------------------------
# vcambench - the GUI users run. WIN32 subsystem so launching it does not flash
# a console window.
# ---------------------------------------------------------------------------
add_executable(vcambench WIN32
  src/vcamapp/gui_main.cpp
  src/vcamapp/camera_manager.cpp
)
target_link_libraries(vcambench PRIVATE mfsensorgroup mfplat mfuuid mf ole32 oleaut32)
target_include_directories(vcambench PRIVATE src include)
vcam_add_version_resource(vcambench APP "VCamBench virtual camera")

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
vcam_add_version_resource(camlist APP "VCamBench camera enumeration diagnostic")

add_executable(camcapture src/camcapture/main.cpp)
target_link_libraries(camcapture PRIVATE vcamcommon mfplat mfuuid mf mfreadwrite ole32 oleaut32)
target_include_directories(camcapture PRIVATE src)
vcam_add_version_resource(camcapture APP "VCamBench frame capture diagnostic")

# ---------------------------------------------------------------------------
# vcamprobe - loads vcamsource.dll directly, with no registry and no Frame
# Server, and saves what the media source actually renders. When the pipeline
# and the source disagree, this is what tells them apart.
# ---------------------------------------------------------------------------
add_executable(vcamprobe src/vcamprobe/main.cpp)
target_link_libraries(vcamprobe PRIVATE vcamcommon mfplat mfuuid mf ole32 oleaut32)
target_include_directories(vcamprobe PRIVATE src include)
add_dependencies(vcamprobe vcamsource)
vcam_add_version_resource(vcamprobe APP "VCamBench media source probe")
