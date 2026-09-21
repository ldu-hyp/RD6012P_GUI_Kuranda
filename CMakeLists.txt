cmake_minimum_required(VERSION 3.21)

project(RD6012P_GUI_Kuranda
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    SerialPort
)

qt_standard_project_setup()

qt_add_executable(RD6012P_GUI_Kuranda
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/DeviceData.h
    src/RidenProtocol.h
    src/RidenProtocol.cpp
    src/SerialWorker.h
    src/SerialWorker.cpp
    src/RealtimePlotWidget.h
    src/RealtimePlotWidget.cpp
)

target_link_libraries(RD6012P_GUI_Kuranda PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::SerialPort
)

if(MSVC)
    target_compile_options(RD6012P_GUI_Kuranda PRIVATE /W4 /permissive-)
endif()

set_target_properties(RD6012P_GUI_Kuranda PROPERTIES
    WIN32_EXECUTABLE TRUE
)
