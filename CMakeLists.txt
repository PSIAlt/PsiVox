cmake_minimum_required(VERSION 3.10)
project(PsiVox LANGUAGES CXX)

# Устанавливаем стандарт C++ и включаем автоматическую обработку MOC, UIC и RCC
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Находим компоненты Qt (Widgets, Multimedia и Concurrent)
find_package(Qt5 REQUIRED COMPONENTS Widgets Multimedia Concurrent)

# Подключаем whisper.cpp как поддиректорию.
# Путь к whisper.cpp нужно указать относительно корня проекта.
add_subdirectory(${CMAKE_SOURCE_DIR}/external/whisper.cpp build/whisper_build)

# Добавляем исполняемый файл (код в src/main.cpp)
add_executable(${PROJECT_NAME} src/main.cpp)

# Указываем путь к заголовочным файлам whisper.cpp
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/external/whisper.cpp)

# Линкуем с библиотекой whisper и нужными модулями Qt
target_link_libraries(${PROJECT_NAME} PRIVATE whisper Qt5::Widgets Qt5::Multimedia Qt5::Concurrent)
