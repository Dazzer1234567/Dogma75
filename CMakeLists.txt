cmake_minimum_required(VERSION 3.15)
project(MinimalDAW VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Add external dependencies first
add_subdirectory(external)

set(SOURCES
    src/main.cpp
    src/audio/audio_engine.cpp
    src/audio/audio_engine.h
    src/audio/track.h
    src/controller/serial_controller.cpp
    src/controller/serial_controller.h
    src/gui/gui_manager.cpp
    src/gui/gui_manager.h
)

add_executable(MinimalDAW ${SOURCES})

# Include directories
target_include_directories(MinimalDAW PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Add dependency include directories if found
if(PORTAUDIO_FOUND)
    target_include_directories(MinimalDAW PRIVATE ${PORTAUDIO_INCLUDE_DIRS})
    target_compile_definitions(MinimalDAW PRIVATE PORTAUDIO_FOUND)
endif()

if(LIBSNDFILE_FOUND)
    target_include_directories(MinimalDAW PRIVATE ${LIBSNDFILE_INCLUDE_DIRS})
    target_compile_definitions(MinimalDAW PRIVATE LIBSNDFILE_FOUND)
endif()

if(RTMIDI_FOUND)
    target_include_directories(MinimalDAW PRIVATE ${RTMIDI_INCLUDE_DIRS})
    target_compile_definitions(MinimalDAW PRIVATE RTMIDI_FOUND)
endif()

if(IMGUI_FOUND)
    target_include_directories(MinimalDAW PRIVATE ${IMGUI_INCLUDE_DIRS})
    target_compile_definitions(MinimalDAW PRIVATE IMGUI_FOUND)
endif()

if(SDL2_FOUND)
    target_include_directories(MinimalDAW PRIVATE ${SDL2_INCLUDE_DIRS})
    target_compile_definitions(MinimalDAW PRIVATE SDL2_FOUND)
endif()

# Platform-specific libraries
if(WIN32)
    find_package(OpenGL REQUIRED)
    set(PLATFORM_LIBS
        winmm
        dsound
        ${OPENGL_LIBRARIES}
    )
    target_compile_definitions(MinimalDAW PRIVATE PLATFORM_WINDOWS)
elseif(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(COREAUDIOKIT_LIBRARY CoreAudioKit)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)

    set(PLATFORM_LIBS
        ${COREAUDIO_LIBRARY}
        ${COREAUDIOKIT_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
    target_compile_definitions(MinimalDAW PRIVATE PLATFORM_MAC)
endif()

# Link all libraries
set(ALL_LIBS ${PLATFORM_LIBS})

if(PORTAUDIO_FOUND)
    list(APPEND ALL_LIBS ${PORTAUDIO_LIBRARIES})
endif()

if(LIBSNDFILE_FOUND)
    list(APPEND ALL_LIBS ${LIBSNDFILE_LIBRARIES})
endif()

if(RTMIDI_FOUND)
    list(APPEND ALL_LIBS ${RTMIDI_LIBRARIES})
endif()

if(IMGUI_FOUND)
    list(APPEND ALL_LIBS ${IMGUI_LIBRARIES})
endif()

if(SDL2_FOUND)
    list(APPEND ALL_LIBS ${SDL2_LIBRARIES})
endif()

target_link_libraries(MinimalDAW PRIVATE ${ALL_LIBS})

# Print configuration summary
message(STATUS "")
message(STATUS "MinimalDAW Configuration Summary:")
message(STATUS "  PortAudio: ${PORTAUDIO_FOUND}")
message(STATUS "  libsndfile: ${LIBSNDFILE_FOUND}")
message(STATUS "  RtMidi: ${RTMIDI_FOUND}")
message(STATUS "  Dear ImGui: ${IMGUI_FOUND}")
message(STATUS "  SDL2: ${SDL2_FOUND}")
message(STATUS "")
