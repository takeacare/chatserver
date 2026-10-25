import QtQuick 2.15

// 聊天消息气泡视图：仅负责展示，业务逻辑全部在 C++ 侧
Item {
    id: root

    // 依据发送者名称生成稳定的头像底色
    function avatarColor(name) {
        var colors = ["#2563eb", "#7c3aed", "#0ea5e9", "#059669", "#d97706", "#db2777"];
        var text = String(name || "");
        var hash = 0;
        for (var i = 0; i < text.length; ++i) {
            hash = (hash * 31 + text.charCodeAt(i)) % 1000000;
        }
        return colors[hash % colors.length];
    }

    function initialOf(name) {
        var text = String(name || "").trim();
        return text.length > 0 ? text.charAt(0).toUpperCase() : "?";
    }

    ListView {
        id: list
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 18
        anchors.topMargin: 14
        anchors.bottomMargin: 12
        clip: true
        spacing: 16
        model: messageModel
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 480

        // 注意：不要给 add 过渡加 opacity 动画。QQuickWidget 在窗口未激活/被遮挡时
        // 动画时钟可能停住，新消息 delegate 会停在 opacity=0，表现为“消息不显示”。
        // 新消息因此直接显示，仅保留列表重排的位移动画。

        // 列表重排时平滑位移
        displaced: Transition {
            NumberAnimation {
                properties: "y"
                duration: 160
                easing.type: Easing.OutCubic
            }
        }

        delegate: Item {
            id: row
            width: list.width
            height: Math.max(avatar.visible ? avatar.height : 0, messageColumn.height)

            // 对方头像
            Rectangle {
                id: avatar
                visible: !model.mine
                width: 36
                height: 36
                radius: 18
                color: root.avatarColor(model.senderName)
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.topMargin: 2

                Text {
                    anchors.centerIn: parent
                    text: root.initialOf(model.senderName)
                    color: "#ffffff"
                    font.pixelSize: 15
                    font.bold: true
                }
            }

            Column {
                id: messageColumn
                width: bubble.width
                spacing: 5
                anchors.top: parent.top
                x: model.mine ? row.width - width - 2 : 46

                // 对方名称与离线标记
                Text {
                    width: parent.width
                    leftPadding: 4
                    text: (model.offline ? "离线 · " : "") + model.senderName
                    visible: !model.mine
                    color: model.offline ? "#b07a3c" : "#7d8797"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Rectangle {
                    id: bubble
                    // 气泡至少容纳时间戳，避免短消息的时间被 elide 成 "2026-..."
                    width: Math.max(messageText.width + 24, timeText.implicitWidth + 24)
                    height: contentColumn.implicitHeight + 24
                    radius: 16
                    border.width: model.mine ? 0 : 1
                    border.color: "#e5eaf1"
                    gradient: model.mine ? ownGradient : otherGradient

                    Gradient {
                        id: ownGradient
                        GradientStop { position: 0.0; color: "#3b82f6" }
                        GradientStop { position: 1.0; color: "#2563eb" }
                    }

                    Gradient {
                        id: otherGradient
                        GradientStop { position: 0.0; color: "#ffffff" }
                        GradientStop { position: 1.0; color: "#fbfcfe" }
                    }

                    Column {
                        id: contentColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 12
                        spacing: 5

                        Text {
                            id: messageText
                            width: Math.min(Math.max(48, Math.min(500, row.width * 0.68) - 24),
                                            implicitWidth)
                            text: model.text
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            color: model.mine ? "#ffffff" : "#232a36"
                            font.pixelSize: 14
                            lineHeight: 1.35
                            lineHeightMode: Text.ProportionalHeight
                        }

                        Text {
                            id: timeText
                            width: contentColumn.width
                            text: model.time
                            color: model.mine ? "#dbeafe" : "#9aa1ad"
                            font.pixelSize: 11
                            horizontalAlignment: model.mine ? Text.AlignRight : Text.AlignLeft
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        // 新消息到达时自动滚动到底部
        onCountChanged: Qt.callLater(list.positionViewAtEnd)
        Component.onCompleted: positionViewAtEnd()
    }

    // 细滚动条：不依赖 QtQuick Controls，随内容自动显示
    Item {
        id: scrollTrack
        visible: list.contentHeight > list.height + 1
        anchors.right: parent.right
        anchors.rightMargin: 3
        anchors.top: list.top
        anchors.bottom: list.bottom
        width: 6

        Rectangle {
            id: scrollThumb
            width: 4
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 2
            color: list.dragging ? "#a9b6c8" : "#cdd8e6"
            height: Math.max(28, scrollTrack.height * list.height / Math.max(1, list.contentHeight))
            y: (scrollTrack.height - height)
               * Math.max(0, Math.min(1, list.contentY / Math.max(1, list.contentHeight - list.height)))
        }
    }

    // 空状态
    Item {
        anchors.centerIn: parent
        width: 260
        height: 148
        visible: list.count === 0

        Rectangle {
            id: emptyIcon
            width: 68
            height: 68
            radius: 34
            color: "#e8f0ff"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top

            Column {
                anchors.centerIn: parent
                spacing: 5

                Rectangle { width: 26; height: 4; radius: 2; color: "#7aa5f8" }
                Rectangle { width: 18; height: 4; radius: 2; color: "#9dbcfb" }
                Rectangle { width: 22; height: 4; radius: 2; color: "#c3d7fd" }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: emptyIcon.bottom
            anchors.topMargin: 18
            text: "选择左侧好友或群组"
            color: "#7d8797"
            font.pixelSize: 15
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: emptyIcon.bottom
            anchors.topMargin: 42
            text: "开始一段新的对话"
            color: "#aab2bf"
            font.pixelSize: 12
        }
    }
}
