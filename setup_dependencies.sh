#!/bin/bash

echo "Setting up Minimal DAW dependencies..."
echo ""

cd external || exit

echo "Cloning PortAudio..."
if [ ! -d "portaudio" ]; then
    git clone https://github.com/PortAudio/portaudio.git
else
    echo "PortAudio already exists, skipping..."
fi

echo ""
echo "Cloning libsndfile..."
if [ ! -d "libsndfile" ]; then
    git clone https://github.com/libsndfile/libsndfile.git
else
    echo "libsndfile already exists, skipping..."
fi

echo ""
echo "Cloning RtMidi..."
if [ ! -d "rtmidi" ]; then
    git clone https://github.com/thestk/rtmidi.git
else
    echo "RtMidi already exists, skipping..."
fi

echo ""
echo "Cloning Dear ImGui..."
if [ ! -d "imgui" ]; then
    git clone https://github.com/ocornut/imgui.git
else
    echo "Dear ImGui already exists, skipping..."
fi

cd ..

echo ""
echo "Dependencies setup complete!"
echo ""
echo "Next steps:"
echo "1. Open Xcode or your preferred IDE"
echo "2. Build the project with: mkdir build && cd build && cmake .. && cmake --build ."
echo ""
