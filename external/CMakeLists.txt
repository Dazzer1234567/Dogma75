# PortAudio Configuration
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/portaudio/CMakeLists.txt)
    set(PA_BUILD_SHARED OFF CACHE BOOL "Build shared library")
    set(PA_BUILD_STATIC ON CACHE BOOL "Build static library")
    # Disable the WDM-KS backend. Pa_Initialize() enumerates every enabled
    # host API, and unlike MME/DirectSound/WASAPI (which query passively)
    # WDM-KS OPENS each endpoint to interrogate its pin formats. That was
    # breaking an unrelated dictation app holding the KLIM mic open through
    # MME: launching the DAW killed its audio stream mid-flight, while
    # launching the DAW first and the dictation app second worked fine.
    # Nothing here uses WDM-KS — the DAW runs on the Orion's ASIO driver.
    set(PA_USE_WDMKS      OFF CACHE BOOL "" FORCE)
    set(PA_USE_WDMKS_DEVICE_INFO OFF CACHE BOOL "" FORCE)
    add_subdirectory(portaudio)
    set(PORTAUDIO_FOUND TRUE PARENT_SCOPE)
    set(PORTAUDIO_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/portaudio/include PARENT_SCOPE)
    set(PORTAUDIO_LIBRARIES portaudio PARENT_SCOPE)
else()
    message(WARNING "PortAudio not found. Run setup_dependencies script first.")
    set(PORTAUDIO_FOUND FALSE PARENT_SCOPE)
endif()

# libsndfile Configuration
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/libsndfile/CMakeLists.txt)
    set(BUILD_PROGRAMS OFF CACHE BOOL "Build programs")
    set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples")
    set(BUILD_TESTING OFF CACHE BOOL "Build tests")
    set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "Enable external libs")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries")
    add_subdirectory(libsndfile)
    set(LIBSNDFILE_FOUND TRUE PARENT_SCOPE)
    set(LIBSNDFILE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/libsndfile/include PARENT_SCOPE)
    set(LIBSNDFILE_LIBRARIES sndfile PARENT_SCOPE)
else()
    message(WARNING "libsndfile not found. Run setup_dependencies script first.")
    set(LIBSNDFILE_FOUND FALSE PARENT_SCOPE)
endif()

# RtMidi Configuration
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi)
    set(RTMIDI_SOURCES
        rtmidi/RtMidi.cpp
        rtmidi/RtMidi.h
    )
    add_library(RtMidi STATIC ${RTMIDI_SOURCES})
    target_include_directories(RtMidi PUBLIC rtmidi)

    if(WIN32)
        target_compile_definitions(RtMidi PRIVATE __WINDOWS_MM__)
        target_link_libraries(RtMidi winmm)
    elseif(APPLE)
        target_compile_definitions(RtMidi PRIVATE __MACOSX_CORE__)
        find_library(COREMIDI_LIBRARY CoreMIDI)
        find_library(COREAUDIO_LIBRARY CoreAudio)
        find_library(COREFOUNDATION_LIBRARY CoreFoundation)
        target_link_libraries(RtMidi ${COREMIDI_LIBRARY} ${COREAUDIO_LIBRARY} ${COREFOUNDATION_LIBRARY})
    endif()

    set(RTMIDI_FOUND TRUE PARENT_SCOPE)
    set(RTMIDI_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/rtmidi PARENT_SCOPE)
    set(RTMIDI_LIBRARIES RtMidi PARENT_SCOPE)
else()
    message(WARNING "RtMidi not found. Run setup_dependencies script first.")
    set(RTMIDI_FOUND FALSE PARENT_SCOPE)
endif()

# SDL2 Configuration
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/sdl2/CMakeLists.txt)
    set(SDL_SHARED OFF CACHE BOOL "Build shared library")
    set(SDL_STATIC ON CACHE BOOL "Build static library")
    set(SDL_TEST OFF CACHE BOOL "Build tests")
    add_subdirectory(sdl2)
    set(SDL2_FOUND TRUE PARENT_SCOPE)
    set(SDL2_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/sdl2/include PARENT_SCOPE)
    set(SDL2_LIBRARIES SDL2-static SDL2main PARENT_SCOPE)
else()
    message(WARNING "SDL2 not found.")
    set(SDL2_FOUND FALSE PARENT_SCOPE)
endif()

# Dear ImGui Configuration (SDL2 + OpenGL3)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
    set(IMGUI_SOURCES
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui/backends/imgui_impl_sdl2.cpp
        imgui/backends/imgui_impl_opengl3.cpp
    )

    add_library(ImGui STATIC ${IMGUI_SOURCES})
    target_include_directories(ImGui PUBLIC
        imgui
        imgui/backends
        ${CMAKE_CURRENT_SOURCE_DIR}/sdl2/include
    )

    # OpenGL and platform libraries
    if(WIN32)
        find_package(OpenGL REQUIRED)
        target_link_libraries(ImGui ${OPENGL_LIBRARIES} imm32)
    elseif(APPLE)
        find_library(OPENGL_LIBRARY OpenGL)
        find_library(COCOA_LIBRARY Cocoa)
        target_link_libraries(ImGui ${OPENGL_LIBRARY} ${COCOA_LIBRARY})
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(ImGui ${OPENGL_LIBRARIES})
    endif()

    set(IMGUI_FOUND TRUE PARENT_SCOPE)
    set(IMGUI_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/imgui ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends PARENT_SCOPE)
    set(IMGUI_LIBRARIES ImGui PARENT_SCOPE)
else()
    message(WARNING "Dear ImGui not found. Run setup_dependencies script first.")
    set(IMGUI_FOUND FALSE PARENT_SCOPE)
endif()
