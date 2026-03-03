cmake_minimum_required(VERSION 3.16)
project(FileTinder VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6 packages
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Sql Network)

# Source files
set(SOURCES
    app/lib/main.cpp
    app/lib/DatabaseManager.cpp
    app/lib/StandaloneFileTinderDialog.cpp
    app/lib/AdvancedFileTinderDialog.cpp
    app/lib/FolderTreeModel.cpp
    app/lib/FileTinderExecutor.cpp
    app/lib/MindMapView.cpp
    app/lib/AppLogger.cpp
    app/lib/DiagnosticTool.cpp
    app/lib/FilterWidget.cpp
    app/lib/ImagePreviewWindow.cpp
    app/lib/AiFileTinderDialog.cpp
    app/lib/FileListWindow.cpp
    app/lib/DuplicateDetectionWindow.cpp
)

# Header files
set(HEADERS
    app/include/DatabaseManager.hpp
    app/include/StandaloneFileTinderDialog.hpp
    app/include/AdvancedFileTinderDialog.hpp
    app/include/FolderTreeModel.hpp
    app/include/FileTinderExecutor.hpp
    app/include/MindMapView.hpp
    app/include/ui_constants.hpp
    app/include/AppLogger.hpp
    app/include/DiagnosticTool.hpp
    app/include/FilterWidget.hpp
    app/include/ImagePreviewWindow.hpp
    app/include/AiFileTinderDialog.hpp
    app/include/FileListWindow.hpp
    app/include/DuplicateDetectionWindow.hpp
)

# Resources
set(RESOURCES
    app/resources/resources.qrc
)

# Create executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/app/include
)

# Link Qt6 libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Sql
    Qt6::Network
)

# Installation
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Platform specific settings
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
elseif(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
    )
endif()
