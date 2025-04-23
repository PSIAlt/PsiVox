# PsiVox

PsiVox is a privacy-focused desktop application that transcribes and summarizes audio from meetings in real time. All processing happens locally on your device using advanced AI models, ensuring that your conversations and data never leave your computer.

## Features

- **Real-time Speech Transcription**: Capture and transcribe meeting audio on-the-fly.
- **Privacy-First**: All processing uses local AI models (whisper.cpp) — no cloud dependence.
- **Cross-Platform**: Native desktop support for **Windows**, **macOS**, and **Linux** via Qt.
- **Lightweight**: Minimal resource usage while delivering fast, accurate results.
- **Simple Interface**: Clean, intuitive UI built with Qt Widgets.

## How It Works

1. **Audio Capture**: Uses Qt Multimedia to record from the system microphone at 16 kHz, 16-bit PCM.
2. **Buffering & Chunking**: Audio is buffered and split into 5-second chunks (with a 1-second minimum) for incremental processing.
3. **Local Transcription**: Each chunk is converted to float samples and fed into `whisper.cpp` with greedy sampling to generate transcribed text.
4. **UI Update**: Transcribed segments are emitted back to the UI thread and displayed in the transcript view in real time.

## Requirements

- **Qt 5.x** (Widgets, Multimedia, Concurrent modules)
- **C++11** compatible compiler
- **CMake** & **Make** (or your preferred build tool)
- **whisper.cpp** submodule (included in this repository)
- Whisper model file: `ggml-large-v3-turbo-q5_0.bin` (recommended)

## Installation

### 1. Pre-built Binaries

Download the latest release for your OS from the [Releases](https://github.com/yourusername/PsiVox/releases) page. Unpack and run.

### 2. Building from Source

```bash
# Clone repository with submodules
git clone --recursive https://github.com/yourusername/PsiVox.git
cd PsiVox

# (Re)initialize submodules if needed
git submodule update --init --recursive

# Download the Whisper model into the correct directory
mkdir -p external/whisper.cpp/models
wget -P external/whisper.cpp/models \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin

# Build the application
mkdir build && cd build
cmake ..
make

# Run PsiVox
./PsiVox
```

## Usage

1. Launch **PsiVox**.
2. Click **Start Recording** to begin capturing audio.
3. Speak or play meeting audio; transcriptions appear live in the text area.
4. Click **Stop Recording** to end session; remaining audio will be processed.

## CMake Configuration

The following `CMakeLists.txt` sets up the PsiVox project, enabling automatic handling of Qt MOC/UIC/RCC, C++11 standard, and integrating the `whisper.cpp` submodule:

```cmake
cmake_minimum_required(VERSION 3.10)
project(PsiVox LANGUAGES CXX)

# Use C++11 and enable Qt automation
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt components
find_package(Qt5 REQUIRED COMPONENTS Widgets Multimedia Concurrent)

# Add whisper.cpp submodule
add_subdirectory(${CMAKE_SOURCE_DIR}/external/whisper.cpp build/whisper_build)

# Define the executable
add_executable(${PROJECT_NAME} src/main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/external/whisper.cpp)

# Link against whisper and Qt modules
target_link_libraries(${PROJECT_NAME} PRIVATE whisper Qt5::Widgets Qt5::Multimedia Qt5::Concurrent)
```

## Project Structure

```
PsiVox/
├── CMakeLists.txt         # Build configuration
├── src/
│   └── main.cpp           # Application entry point, UI setup, and transcription logic
├── external/
│   └── whisper.cpp/       # Submodule containing the Whisper C++ API
├── models/
│   └── ggml-large-v3-turbo-q5_0.bin  # Whisper model (downloaded at install)
├── README.md
└── LICENSE
```

## Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository.
2. Create a feature branch: `git checkout -b feature/YourFeature`.
3. Commit changes: `git commit -m "Add some feature"`.
4. Push to branch: `git push origin feature/YourFeature`.
5. Open a Pull Request.

Ensure that new features include appropriate documentation and, if possible, unit tests.

## License

This project is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Inspired by [meeting-minutes](https://github.com/Zackriya-Solutions/meeting-minutes)
- Powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp)

