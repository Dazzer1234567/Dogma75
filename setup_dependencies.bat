@echo off
echo Setting up Minimal DAW dependencies...
echo.

cd external

echo Cloning PortAudio...
if not exist portaudio (
    git clone https://github.com/PortAudio/portaudio.git
) else (
    echo PortAudio already exists, skipping...
)

echo.
echo Cloning libsndfile...
if not exist libsndfile (
    git clone https://github.com/libsndfile/libsndfile.git
) else (
    echo libsndfile already exists, skipping...
)

echo.
echo Cloning RtMidi...
if not exist rtmidi (
    git clone https://github.com/thestk/rtmidi.git
) else (
    echo RtMidi already exists, skipping...
)

echo.
echo Cloning Dear ImGui...
if not exist imgui (
    git clone https://github.com/ocornut/imgui.git
) else (
    echo Dear ImGui already exists, skipping...
)

cd ..

echo.
echo Dependencies setup complete!
echo.
echo Next steps:
echo 1. Open Visual Studio
echo 2. Open this folder as a CMake project, or
echo 3. Run: mkdir build ^&^& cd build ^&^& cmake .. ^&^& cmake --build .
echo.
pause
