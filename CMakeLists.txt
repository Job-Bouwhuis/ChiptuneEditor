cmake_minimum_required(VERSION 3.20)
project(ChiptuneStudio VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

include(FetchContent)
set(RAVEN_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(RAVEN_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(RAVEN_FETCH_VULKAN_HEADERS ON CACHE BOOL "" FORCE)
FetchContent_Declare(raven
    GIT_REPOSITORY https://github.com/oscar-soirey/Raven.git
    GIT_TAG 89197438a59edde8efc38c9b48223e6721ec1816
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(raven)

option(CHIPTUNE_BUILD_APP "Build the Chiptune Studio application" ON)
option(CHIPTUNE_BUILD_WAV_INFO "Build the command-line WAV inspection utility" ON)

add_library(chiptune_audio
    src/audio/envelope.cpp
    src/audio/engine.cpp
    src/audio/oscillator.cpp
    src/audio/wav.cpp
    src/audio/wav_player.cpp
)
add_library(ChiptuneStudio::Audio ALIAS chiptune_audio)
target_include_directories(chiptune_audio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(chiptune_audio PUBLIC cxx_std_20)
set_target_properties(chiptune_audio PROPERTIES CXX_EXTENSIONS OFF)

find_package(ALSA QUIET)
if(ALSA_FOUND)
    target_sources(chiptune_audio PRIVATE src/audio/alsa_output.cpp)
    target_link_libraries(chiptune_audio PUBLIC ALSA::ALSA)
    target_compile_definitions(chiptune_audio PUBLIC CHIPTUNE_HAS_ALSA=1)
endif()

if(MSVC)
    target_compile_options(chiptune_audio PRIVATE /W4 /permissive-)
else()
    target_compile_options(chiptune_audio PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(CHIPTUNE_BUILD_APP)
    add_library(chiptune_ui src/ui/main_window.cpp)
    target_include_directories(chiptune_ui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_compile_features(chiptune_ui PUBLIC cxx_std_20)
    target_link_libraries(chiptune_ui PRIVATE raven::raven ChiptuneStudio::Audio)

    add_executable(chiptune_studio src/main.cpp)
    target_link_libraries(chiptune_studio PRIVATE chiptune_ui)
    install(TARGETS chiptune_studio RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CHIPTUNE_BUILD_WAV_INFO)
    add_executable(chiptune_wav_info src/wav_info_main.cpp)
    target_link_libraries(chiptune_wav_info PRIVATE ChiptuneStudio::Audio)
    install(TARGETS chiptune_wav_info RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(FILES "${raven_SOURCE_DIR}/LICENSE" "${raven_SOURCE_DIR}/NOTICE" "${raven_SOURCE_DIR}/THIRD_PARTY.md"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/chiptune-studio/Raven)
if(CHIPTUNE_BUILD_APP)
    install(FILES packaging/chiptune-studio.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/chiptune-studio.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
endif()

install(TARGETS chiptune_audio
    EXPORT ChiptuneStudioTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ChiptuneStudioTargets
    FILE ChiptuneStudioTargets.cmake
    NAMESPACE ChiptuneStudio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChiptuneStudio
)
