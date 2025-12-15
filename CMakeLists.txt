cmake_minimum_required(VERSION 3.20)
project(itgmania_reference_harness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(ITGMANIA_ROOT "${CMAKE_CURRENT_LIST_DIR}/src/extern/itgmania" CACHE PATH "Path to the ITGmania source tree (defaults to the vendored submodule)")
option(USE_ITGMANIA_SOURCES "Attempt to compile against ITGmania parsing sources" ON)
option(USE_ITGMANIA_PREBUILT "Link against a prebuilt ITGmania static/shared library" OFF)
set(ITGMANIA_BUILD_DIR "" CACHE PATH "Path to an existing ITGmania build directory (for generated headers like config.hpp)")
set(ITGMANIA_LIB "" CACHE FILEPATH "Path to a prebuilt ITGmania library (if USE_ITGMANIA_PREBUILT=ON)")
set(ITGMANIA_EXTERN_DIR "" CACHE PATH "Path to ITGmania extern build products (auto-detected when possible)")

set(SL_CHART_PARSER_PATH "${ITGMANIA_ROOT}/Themes/Simply Love/Scripts/SL-ChartParser.lua")
set(SL_CHART_PARSER_HELPERS_PATH "${ITGMANIA_ROOT}/Themes/Simply Love/Scripts/SL-ChartParserHelpers.lua")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${SL_CHART_PARSER_PATH}"
  "${SL_CHART_PARSER_HELPERS_PATH}"
)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(EMBEDDED_LUA_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_lua.h")
set(SL_CHART_PARSER_LUA_RAW "")
set(SL_CHART_PARSER_HELPERS_RAW "")

if(EXISTS "${SL_CHART_PARSER_PATH}" AND EXISTS "${SL_CHART_PARSER_HELPERS_PATH}")
  file(READ "${SL_CHART_PARSER_PATH}" SL_CHART_PARSER_LUA_RAW)
  file(READ "${SL_CHART_PARSER_HELPERS_PATH}" SL_CHART_PARSER_HELPERS_RAW)
else()
  message(WARNING "Simply Love chart parser Lua not found; embedded copy will be empty. Expected at ${SL_CHART_PARSER_PATH}")
endif()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/embedded_lua.h.in" "${EMBEDDED_LUA_HEADER}" @ONLY)

if(NOT USE_ITGMANIA_SOURCES AND NOT USE_ITGMANIA_PREBUILT)
  message(WARNING "Building in stub-only mode; set USE_ITGMANIA_SOURCES=ON (default) for real parsing output.")
endif()

if(NOT ITGMANIA_BUILD_DIR)
  if(EXISTS "${ITGMANIA_ROOT}/Build")
    set(ITGMANIA_BUILD_DIR "${ITGMANIA_ROOT}/Build" CACHE PATH "Path to an existing ITGmania build directory (for generated headers like config.hpp)" FORCE)
  elseif(EXISTS "${ITGMANIA_ROOT}/build")
    set(ITGMANIA_BUILD_DIR "${ITGMANIA_ROOT}/build" CACHE PATH "Path to an existing ITGmania build directory (for generated headers like config.hpp)" FORCE)
  endif()
endif()

if(NOT ITGMANIA_EXTERN_DIR AND ITGMANIA_BUILD_DIR)
  set(ITGMANIA_EXTERN_DIR "${ITGMANIA_BUILD_DIR}/extern" CACHE PATH "Path to ITGmania extern build products (auto-detected when possible)" FORCE)
endif()

if(NOT EXISTS "${ITGMANIA_ROOT}/src")
  message(WARNING "ITGMANIA_ROOT does not look valid: ${ITGMANIA_ROOT}")
endif()

set(ITGMANIA_EXTERN_HINT_DIRS
  "${ITGMANIA_EXTERN_DIR}"
  "${ITGMANIA_ROOT}/Build/extern"
  "${ITGMANIA_ROOT}/build/extern"
)

find_library(TOMCRYPT_LIB NAMES tomcrypt
  HINTS ${ITGMANIA_EXTERN_HINT_DIRS} "${ITGMANIA_ROOT}/extern/libtomcrypt" "${ITGMANIA_ROOT}/extern/libtomcrypt/lib")
find_library(TOMMATH_LIB NAMES tommath
  HINTS ${ITGMANIA_EXTERN_HINT_DIRS} "${ITGMANIA_ROOT}/extern/libtommath" "${ITGMANIA_ROOT}/extern/libtommath/lib")
find_library(PCRE_LIB NAMES pcre
  HINTS ${ITGMANIA_EXTERN_HINT_DIRS} "${ITGMANIA_ROOT}/extern/pcre" "${ITGMANIA_ROOT}/extern/pcre/lib")
find_library(LUA_LIB NAMES lua5.1 lua51 lua
  HINTS ${ITGMANIA_EXTERN_HINT_DIRS} "${ITGMANIA_ROOT}/extern/lua-5.1" "${ITGMANIA_ROOT}/extern/lua-5.1/lib")
find_library(JSONCPP_LIB NAMES jsoncpp)

set(HARNESS_SOURCES
  src/main.cpp
  src/itgmania_adapter.cpp
)

if(NOT USE_ITGMANIA_PREBUILT)
  list(APPEND HARNESS_SOURCES src/itgmania_stubs.cpp)
endif()

add_executable(itgmania-reference-harness ${HARNESS_SOURCES})

target_include_directories(itgmania-reference-harness PRIVATE
  "${ITGMANIA_ROOT}/src"
  "${ITGMANIA_ROOT}/extern/lua-5.1/src"
  "${ITGMANIA_ROOT}/extern/jsoncpp"
  "${ITGMANIA_ROOT}/extern/libtomcrypt/src/headers"
  "${ITGMANIA_ROOT}/extern/pcre"
  "${ITGMANIA_ROOT}/extern/miniz"
  ${ITGMANIA_BUILD_DIR}/generated/src
  ${ITGMANIA_BUILD_DIR}
  ${ITGMANIA_ROOT}/Build/generated/src
  ${ITGMANIA_ROOT}/Build
  ${ITGMANIA_ROOT}/build/generated/src
  ${ITGMANIA_ROOT}/build
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

if(USE_ITGMANIA_PREBUILT)
  if(NOT ITGMANIA_LIB OR NOT EXISTS "${ITGMANIA_LIB}")
    message(FATAL_ERROR "USE_ITGMANIA_PREBUILT=ON but ITGMANIA_LIB is not set or not found")
  endif()
  if(NOT ITGMANIA_BUILD_DIR)
    message(FATAL_ERROR "Set ITGMANIA_BUILD_DIR to the directory that contains ITGmania's extern/ outputs (e.g. <itgmania>/Build)")
  endif()

  target_compile_definitions(itgmania-reference-harness PRIVATE ITGMANIA_ROOT_DIR="${ITGMANIA_ROOT}")

  if(NOT ITGMANIA_EXTERN_DIR)
    set(ITGMANIA_EXTERN_DIR "${ITGMANIA_BUILD_DIR}/extern")
  endif()
  set(ITGMANIA_FFMPEG_DIR "${ITGMANIA_BUILD_DIR}/ffmpeg-prefix/src/ffmpeg-build/dest/lib")
  set(ITGMANIA_JPEG_DIR "${ITGMANIA_BUILD_DIR}/libjpeg-turbo-install/lib")
  set(ITGMANIA_LIBUSB_DIR "${ITGMANIA_BUILD_DIR}/extern/libusb-prefix/src/libusb-build")
  set(ITGMANIA_LOADING_GTK_OBJ "${ITGMANIA_BUILD_DIR}/src/CMakeFiles/LoadingWindowGtk.dir/arch/LoadingWindow/LoadingWindow_Gtk.cpp.o")

  if(NOT EXISTS "${ITGMANIA_LOADING_GTK_OBJ}")
    message(FATAL_ERROR "Expected LoadingWindow_Gtk object at ${ITGMANIA_LOADING_GTK_OBJ}. Build ITGmania first (cmake --build <itgmania>/Build).")
  endif()

  set(ITGMANIA_PREBUILT_LIBS
    ${ITGMANIA_BUILD_DIR}/src/CMakeFiles/ITGmania.dir/RageFileDriverDirect.cpp.o
    ${ITGMANIA_LIB}
    ${ITGMANIA_LOADING_GTK_OBJ}
    ${ITGMANIA_EXTERN_DIR}/liblua-5.1.a
    ${ITGMANIA_EXTERN_DIR}/libminiz.a
    ${ITGMANIA_ROOT}/extern/libmmmagic/linux-64bit/libmmmagic.a
    ${ITGMANIA_EXTERN_DIR}/libmad.a
    ${ITGMANIA_EXTERN_DIR}/libpcre.a
    ${ITGMANIA_EXTERN_DIR}/libglew.a
    ${ITGMANIA_EXTERN_DIR}/libtomcrypt.a
    ${ITGMANIA_EXTERN_DIR}/libtommath.a
    ${ITGMANIA_JPEG_DIR}/libturbojpeg.a
    ${ITGMANIA_EXTERN_DIR}/libpng.a
    ${ITGMANIA_EXTERN_DIR}/libjsoncpp.a
    ${ITGMANIA_EXTERN_DIR}/libvorbisfile.a
    ${ITGMANIA_EXTERN_DIR}/libvorbis.a
    ${ITGMANIA_EXTERN_DIR}/libogg.a
    ${ITGMANIA_EXTERN_DIR}/libixwebsocket.a
    ${ITGMANIA_EXTERN_DIR}/libhidapi.a
    ${ITGMANIA_FFMPEG_DIR}/libavformat.a
    ${ITGMANIA_FFMPEG_DIR}/libavcodec.a
    ${ITGMANIA_FFMPEG_DIR}/libswscale.a
    ${ITGMANIA_FFMPEG_DIR}/libavutil.a
    ${ITGMANIA_EXTERN_DIR}/libzlib.a
    ${ITGMANIA_LIBUSB_DIR}/libusb-1.0.a
    ${ITGMANIA_EXTERN_DIR}/libmbedtls.a
    ${ITGMANIA_EXTERN_DIR}/libmbedx509.a
    ${ITGMANIA_EXTERN_DIR}/libmbedcrypto.a
    ${ITGMANIA_EXTERN_DIR}/libeverest.a
    ${ITGMANIA_EXTERN_DIR}/libp256m.a
  )

  target_link_libraries(itgmania-reference-harness PRIVATE
    ${ITGMANIA_PREBUILT_LIBS}
    udev
    dl
    OpenGL
    GLX
    GLU
    gtk-3
    gdk-3
    z
    pangocairo-1.0
    pango-1.0
    harfbuzz
    atk-1.0
    cairo-gobject
    cairo
    gdk_pixbuf-2.0
    gio-2.0
    gobject-2.0
    glib-2.0
    asound
    pulse
    SM
    ICE
    X11
    Xext
    Xtst
    Xrandr
    Xinerama
    pthread
    m
  )
  target_compile_definitions(itgmania-reference-harness PRIVATE ITGMANIA_HARNESS=1)
elseif(USE_ITGMANIA_SOURCES)
  # Minimal subset of ITGmania sources needed for simfile parsing and tech counting.
  set(ITGMANIA_SOURCES
    ${ITGMANIA_ROOT}/src/StdString.cpp
    ${ITGMANIA_ROOT}/src/RageUtil.cpp
    ${ITGMANIA_ROOT}/src/RageException.cpp
    ${ITGMANIA_ROOT}/src/RageThreads.cpp
    ${ITGMANIA_ROOT}/src/RageTimer.cpp
    ${ITGMANIA_ROOT}/src/GameConstantsAndTypes.cpp
    ${ITGMANIA_ROOT}/src/NoteTypes.cpp
    ${ITGMANIA_ROOT}/src/NoteData.cpp
    ${ITGMANIA_ROOT}/src/NoteDataUtil.cpp
    ${ITGMANIA_ROOT}/src/TimingData.cpp
    ${ITGMANIA_ROOT}/src/TimingSegments.cpp
    ${ITGMANIA_ROOT}/src/Steps.cpp
    ${ITGMANIA_ROOT}/src/StepParityGenerator.cpp
    ${ITGMANIA_ROOT}/src/StepParityCost.cpp
    ${ITGMANIA_ROOT}/src/StepParityDatastructs.cpp
    ${ITGMANIA_ROOT}/src/TechCounts.cpp
    ${ITGMANIA_ROOT}/src/ScreenMessage.cpp
    ${ITGMANIA_ROOT}/src/NotesLoader.cpp
    ${ITGMANIA_ROOT}/src/NotesLoaderSMA.cpp
    ${ITGMANIA_ROOT}/src/NotesLoaderSM.cpp
    ${ITGMANIA_ROOT}/src/NotesLoaderSSC.cpp
    ${ITGMANIA_ROOT}/src/ColumnCues.cpp
    ${ITGMANIA_ROOT}/src/MsdFile.cpp
    ${ITGMANIA_ROOT}/src/MeasureInfo.cpp
    ${ITGMANIA_ROOT}/src/XmlFile.cpp
    ${ITGMANIA_ROOT}/src/global.cpp
  )

  target_sources(itgmania-reference-harness PRIVATE ${ITGMANIA_SOURCES})
  target_compile_definitions(itgmania-reference-harness PRIVATE ITGMANIA_HARNESS=1 LTM_DESC ITGMANIA_HARNESS_SOURCE=1)
  if(ITGMANIA_BUILD_DIR)
    target_include_directories(itgmania-reference-harness PRIVATE "${ITGMANIA_BUILD_DIR}")
  endif()
endif()

if(MSVC)
  target_compile_options(itgmania-reference-harness PRIVATE /W4 /permissive-)
else()
  target_compile_options(itgmania-reference-harness PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NOT USE_ITGMANIA_PREBUILT)
  if(NOT TOMCRYPT_LIB)
    message(FATAL_ERROR "libtomcrypt not found. Set TOMCRYPT_LIB or install libtomcrypt-dev.")
  endif()
  if(NOT TOMMATH_LIB)
    message(FATAL_ERROR "libtommath not found. Set TOMMATH_LIB or install libtommath-dev.")
  endif()
  if(NOT PCRE_LIB)
    message(FATAL_ERROR "libpcre not found. Set PCRE_LIB or install libpcre3-dev.")
  endif()
  if(NOT LUA_LIB)
    message(FATAL_ERROR "Lua 5.1 library not found. Set LUA_LIB or install liblua5.1-0-dev (or equivalent).")
  endif()
  if(NOT JSONCPP_LIB)
    message(FATAL_ERROR "libjsoncpp not found. Install libjsoncpp-dev or set JSONCPP_LIB.")
  endif()

  target_link_libraries(itgmania-reference-harness PRIVATE
    ${TOMCRYPT_LIB}
    ${TOMMATH_LIB}
    ${PCRE_LIB}
    ${LUA_LIB}
    ${JSONCPP_LIB}
    pthread
  )
endif()
