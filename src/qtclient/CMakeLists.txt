# Qt 图形化聊天客户端（QWidget + QML 混合）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Qml Quick QuickWidgets)

add_executable(ChatGui
    main.cpp
    chatclient.cpp
    logindialog.cpp
    mainwindow.cpp
    messagemodel.cpp
    resources.qrc
)

target_link_libraries(ChatGui PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickWidgets
)
