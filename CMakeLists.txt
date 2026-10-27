cmake_minimum_required(VERSION 3.21)

project(m2n
  VERSION 0.1.0
  DESCRIPTION "Luxumbra's M2N - MIDI 到 Minecraft 音符盒红石音乐转换器"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(MSVC)
  add_compile_options(/utf-8 /W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# 依赖 (全部 FetchContent, GPLv3 兼容)
# ---------------------------------------------------------------------------
include(FetchContent)

# miniz 等旧项目声明 cmake_minimum_required < 3.5, CMake 4 需要此兼容开关
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# miniz: gzip (结构 NBT) + zip 打包 (数据包/资源包)
FetchContent_Declare(miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG 3.0.2)
FetchContent_MakeAvailable(miniz)

# stb (stb_vorbis): 解码本地 .minecraft 的原版音符盒音色
# stb 仓库根目录没有 CMakeLists.txt, MakeAvailable 只下载
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)  # master 2025-04
FetchContent_MakeAvailable(stb)

# doctest: 单元测试框架
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.12)
FetchContent_MakeAvailable(doctest)

# ---------------------------------------------------------------------------
# Ogg Vorbis 编码 (唱片资源包)
# 不用 xiph 自带的 CMakeLists (它的 find_package(Ogg) 对 FetchContent 不友好),
# 直接编译源码: ogg 只有两个 .c, vorbis 的 lib 目录也是自包含的。
# SOURCE_SUBDIR 指向无 CMakeLists.txt 的目录 => 只下载不 add_subdirectory。
# ---------------------------------------------------------------------------
set(M2N_ENABLE_AUDIO ON CACHE BOOL "启用音频渲染与 Ogg 编码 (唱片资源包)")

if(M2N_ENABLE_AUDIO)
  FetchContent_Declare(ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG v1.3.5
    SOURCE_SUBDIR doc)
  FetchContent_Declare(vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG v1.3.7
    SOURCE_SUBDIR doc)
  FetchContent_MakeAvailable(ogg vorbis)

  add_library(m2n_vorbis STATIC
    ${ogg_SOURCE_DIR}/src/bitwise.c
    ${ogg_SOURCE_DIR}/src/framing.c
    ${vorbis_SOURCE_DIR}/lib/analysis.c
    ${vorbis_SOURCE_DIR}/lib/bitrate.c
    ${vorbis_SOURCE_DIR}/lib/block.c
    ${vorbis_SOURCE_DIR}/lib/codebook.c
    ${vorbis_SOURCE_DIR}/lib/envelope.c
    ${vorbis_SOURCE_DIR}/lib/floor0.c
    ${vorbis_SOURCE_DIR}/lib/floor1.c
    ${vorbis_SOURCE_DIR}/lib/info.c
    ${vorbis_SOURCE_DIR}/lib/lookup.c
    ${vorbis_SOURCE_DIR}/lib/lpc.c
    ${vorbis_SOURCE_DIR}/lib/lsp.c
    ${vorbis_SOURCE_DIR}/lib/mapping0.c
    ${vorbis_SOURCE_DIR}/lib/mdct.c
    ${vorbis_SOURCE_DIR}/lib/misc.c
    ${vorbis_SOURCE_DIR}/lib/psy.c
    ${vorbis_SOURCE_DIR}/lib/registry.c
    ${vorbis_SOURCE_DIR}/lib/res0.c
    ${vorbis_SOURCE_DIR}/lib/sharedbook.c
    ${vorbis_SOURCE_DIR}/lib/smallft.c
    ${vorbis_SOURCE_DIR}/lib/synthesis.c
    ${vorbis_SOURCE_DIR}/lib/vorbisenc.c
    ${vorbis_SOURCE_DIR}/lib/window.c)
  target_include_directories(m2n_vorbis PUBLIC
    ${ogg_SOURCE_DIR}/include
    ${vorbis_SOURCE_DIR}/include
    ${vorbis_SOURCE_DIR}/lib)
  if(WIN32)
    target_compile_definitions(m2n_vorbis PRIVATE _USE_MATH_DEFINES)
  endif()
endif()

# ---------------------------------------------------------------------------
# 核心库 (main.cpp 之外的全部源码, 主程序与测试共用)
# ---------------------------------------------------------------------------
add_library(m2n_core STATIC
  src/app.cpp
  src/cli/cli.cpp
  src/util/fs.cpp
  src/util/json.cpp
  src/util/mc_versions.cpp
  src/util/zip.cpp
  src/midi/smf.cpp
  src/model/instruments.cpp
  src/model/convert.cpp
  src/nbt/nbt.cpp
  src/structure/layout.cpp
  src/structure/export_nbt.cpp
  src/datapack/export.cpp)

target_include_directories(m2n_core PUBLIC src ${miniz_SOURCE_DIR} ${stb_SOURCE_DIR})
target_link_libraries(m2n_core PUBLIC miniz)

if(M2N_ENABLE_AUDIO)
  target_sources(m2n_core PRIVATE
    src/audio/synth.cpp
    src/audio/ogg_encoder.cpp
    src/audio/vanilla_assets.cpp
    src/audio/stb_vorbis_impl.c
    src/resourcepack/disc.cpp)
  target_compile_definitions(m2n_core PUBLIC M2N_HAS_AUDIO=1)
  target_include_directories(m2n_core PUBLIC
    ${ogg_SOURCE_DIR}/include ${vorbis_SOURCE_DIR}/include ${vorbis_SOURCE_DIR}/lib)
  target_link_libraries(m2n_core PUBLIC m2n_vorbis)
endif()

# ---------------------------------------------------------------------------
# 主程序
# ---------------------------------------------------------------------------
add_executable(m2n src/main.cpp)
target_link_libraries(m2n PRIVATE m2n_core)

# ---------------------------------------------------------------------------
# 测试
# ---------------------------------------------------------------------------
enable_testing()

add_executable(m2n_tests
  tests/test_main.cpp
  tests/midi_tests.cpp
  tests/model_tests.cpp
  tests/nbt_tests.cpp
  tests/layout_tests.cpp
  tests/datapack_tests.cpp
  tests/e2e_tests.cpp)

target_link_libraries(m2n_tests PRIVATE m2n_core doctest::doctest)

if(M2N_ENABLE_AUDIO)
  target_sources(m2n_tests PRIVATE tests/audio_tests.cpp)
endif()

add_test(NAME m2n_tests COMMAND m2n_tests)
