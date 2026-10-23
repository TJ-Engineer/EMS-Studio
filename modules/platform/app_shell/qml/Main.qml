// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Oep.Shell 1.0
import "Theme.js" as Theme

ApplicationWindow {
    id: root

    required property ShellController shellController
    readonly property bool cadWorkspaceActive: root.shellController.currentWorkspace === "cad"
    readonly property bool navigationContractSatisfied:
        cadWorkspaceButton.objectName === "cadWorkspaceButton"
        && emWorkspaceButton.objectName === "emWorkspaceButton"
        && commandButton.objectName === "commandPaletteButton"
        && engineeringViewport.objectName === "engineeringViewport"
        && commandPalette.objectName === "commandPalette"
    readonly property bool accessibilityContractSatisfied:
        cadWorkspaceButton.Accessible.name.length > 0
        && emWorkspaceButton.Accessible.name.length > 0
        && commandButton.Accessible.name.length > 0
        && engineeringViewport.Accessible.name.length > 0
        && browserDock.Accessible.name.length > 0
        && inspectorDock.Accessible.name.length > 0
        && commandPaletteContent.Accessible.name.length > 0
    readonly property bool commandPaletteVisible: commandPalette.visible
    readonly property bool keyboardContractSatisfied:
        cadShortcut.sequence.toString().length > 0
        && emShortcut.sequence.toString().length > 0
        && commandShortcut.sequence.toString().length > 0
        && diagnosticsShortcut.sequence.toString().length > 0
        && escapeShortcut.sequence.toString().length > 0

    property bool gridVisible: true
    property bool overlayVisible: true
    property int outputTab: 0
    property int selectedStudyRow: -1
    onCadWorkspaceActiveChanged: {
        // A query belongs to the current browser vocabulary, not both studios.
        browserFilter.clear()
        selectedStudyRow = -1
        outputTab = 0
    }
    readonly property Flickable studyInspectorFlickable: studyInspector.contentItem as Flickable

    // Browser row indices belong to the compiled study projection. Filtering
    // changes visibility only, so it must not renumber section navigation.
    function navigateStudyRow(row: int) {
        selectedStudyRow = row
        root.shellController.setPanelVisible("right", true)
        const sections = [frequencyEditor, frequencyEditor, materialEditor,
                          roleSection, portSection, boundarySection, monitorSection]
        const section = sections[row]
        if (!section) return
        section.reveal()
        Qt.callLater(function() {
            if (!root.studyInspectorFlickable) return
            const flick = root.studyInspectorFlickable
            const position = section.mapToItem(flick.contentItem, 0, 0)
            flick.contentY = Math.max(0, Math.min(position.y, flick.contentHeight - flick.height))
        })
    }


    visible: true
    width: 1280
    height: 800
    minimumWidth: 920
    minimumHeight: 620
    title: root.shellController.productName + " — Open Engineering Platform"
    color: Theme.background

    onClosing: function() {
        root.shellController.saveLayout(
            browserDock.width,
            inspectorDock.width,
            outputDock.height,
            browserDock.visible,
            inspectorDock.visible,
            outputDock.visible)
    }

    Shortcut {
        id: cadShortcut
        objectName: "cadWorkspaceShortcut"
        sequence: "Ctrl+1"
        context: Qt.ApplicationShortcut
        onActivated: root.shellController.selectWorkspace("cad")
    }
    Shortcut {
        id: emShortcut
        objectName: "emWorkspaceShortcut"
        sequence: "Ctrl+2"
        context: Qt.ApplicationShortcut
        onActivated: root.shellController.selectWorkspace("em")
    }
    Shortcut {
        id: commandShortcut
        objectName: "commandPaletteShortcut"
        sequence: "Ctrl+K"
        context: Qt.ApplicationShortcut
        onActivated: commandPalette.open()
    }
    Shortcut {
        id: diagnosticsShortcut
        objectName: "diagnosticsShortcut"
        sequence: "Ctrl+J"
        context: Qt.ApplicationShortcut
        onActivated: root.shellController.setPanelVisible(
            "bottom", !root.shellController.bottomPanelVisible)
    }
    Shortcut {
        id: escapeShortcut
        objectName: "escapeShortcut"
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: commandPalette.visible
        onActivated: commandPalette.close()
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("&File")
            MenuItem {
                objectName: "newCadDocumentAction"
                text: qsTr("New CAD example…")
                enabled: root.shellController.cadAvailable
                onTriggered: { documentDialog.operation = "new"; documentDialog.open() }
            }
            MenuItem {
                text: qsTr("Open CAD checkpoint…")
                enabled: root.shellController.cadAvailable
                onTriggered: { documentDialog.operation = "open"; documentDialog.open() }
            }
            MenuItem {
                text: qsTr("Save CAD checkpoint…")
                enabled: root.shellController.documentRevision.length > 0
                onTriggered: { documentDialog.operation = "save"; documentDialog.open() }
            }
            MenuItem {
                objectName: "openProjectAction"
                text: qsTr("Open CAD + EM project…")
                enabled: root.shellController.cadAvailable
                onTriggered: { documentDialog.operation = "open-project"; documentDialog.open() }
            }
            MenuItem {
                objectName: "saveProjectAction"
                text: qsTr("Save CAD + EM project…")
                enabled: root.shellController.documentRevision.length > 0
                onTriggered: { documentDialog.operation = "save-project"; documentDialog.open() }
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("E&xit")
                Accessible.name: qsTr("Exit application")
                onTriggered: Qt.quit()
            }
        }
        Menu {
            title: qsTr("&Edit")
            MenuItem {
                text: qsTr("Undo")
                enabled: root.shellController.canUndo
                onTriggered: root.shellController.undoDocument()
            }
            MenuItem {
                text: qsTr("Redo")
                enabled: root.shellController.canRedo
                onTriggered: root.shellController.redoDocument()
            }
        }
        Menu {
            title: qsTr("&Workspace")
            MenuItem {
                text: qsTr("CAD Studio\tCtrl+1")
                checkable: true
                checked: root.cadWorkspaceActive
                Accessible.name: qsTr("Switch to CAD Studio")
                onTriggered: root.shellController.selectWorkspace("cad")
            }
            MenuItem {
                text: qsTr("EM Studio\tCtrl+2")
                checkable: true
                checked: !root.cadWorkspaceActive
                Accessible.name: qsTr("Switch to EM Studio")
                onTriggered: root.shellController.selectWorkspace("em")
            }
        }
        Menu {
            title: qsTr("&View")
            MenuItem {
                text: qsTr("Project browser")
                checkable: true
                checked: root.shellController.leftPanelVisible
                Accessible.name: qsTr("Show project browser")
                onTriggered: root.shellController.setPanelVisible("left", checked)
            }
            MenuItem {
                text: qsTr("Inspector")
                checkable: true
                checked: root.shellController.rightPanelVisible
                Accessible.name: qsTr("Show inspector")
                onTriggered: root.shellController.setPanelVisible("right", checked)
            }
            MenuItem {
                text: qsTr("Diagnostics\tCtrl+J")
                checkable: true
                checked: root.shellController.bottomPanelVisible
                Accessible.name: qsTr("Show diagnostics")
                onTriggered: root.shellController.setPanelVisible("bottom", checked)
            }
            MenuSeparator {}
            MenuItem {
                text: qsTr("Reset panel layout")
                Accessible.name: qsTr("Reset panel layout")
                onTriggered: root.shellController.resetLayout()
            }
        }
        Menu {
            title: qsTr("&Help")
            MenuItem {
                text: qsTr("Keyboard shortcuts")
                Accessible.name: qsTr("Show keyboard shortcuts")
                onTriggered: commandPalette.open()
            }
        }
    }

    header: Rectangle {
        implicitHeight: 62
        color: Theme.chrome
        border.color: Theme.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 18

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 34
                radius: Theme.radius
                color: Theme.surfaceRaised
                border.color: Theme.lineStrong
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Open Engineering Platform")

                Text {
                    anchors.centerIn: parent
                    text: "OE<span style='color:" + Theme.cyan + "'>P</span>"
                    textFormat: Text.RichText
                    color: Theme.text
                    font.pixelSize: 11
                    font.bold: true
                }
            }

            ColumnLayout {
                Layout.minimumWidth: 230
                Layout.fillWidth: true
                spacing: 2

                Text {
                    text: qsTr("Open Engineering Platform")
                    color: Theme.text
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: root.shellController.documentStatus
                    color: root.shellController.documentStatus.startsWith("Evaluated") ? Theme.green : Theme.amber
                    font.pixelSize: 10
                }
            }

            Rectangle {
                Layout.preferredWidth: workspaceButtons.implicitWidth + 8
                Layout.preferredHeight: 40
                radius: Theme.radius + 2
                color: Theme.background
                border.color: Theme.line

                RowLayout {
                    id: workspaceButtons
                    anchors.centerIn: parent
                    spacing: 4

                    ShellButton {
                        id: cadWorkspaceButton
                        objectName: "cadWorkspaceButton"
                        text: qsTr("CAD Studio")
                        selected: root.cadWorkspaceActive
                        Accessible.description: qsTr("Open the mechanical design workspace. Shortcut Control 1.")
                        onClicked: root.shellController.selectWorkspace("cad")
                    }
                    ShellButton {
                        id: emWorkspaceButton
                        objectName: "emWorkspaceButton"
                        text: qsTr("EM Studio")
                        selected: !root.cadWorkspaceActive
                        Accessible.description: qsTr("Open the electromagnetic study workspace. Shortcut Control 2.")
                        onClicked: root.shellController.selectWorkspace("em")
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ShellButton {
                id: commandButton
                objectName: "commandPaletteButton"
                text: qsTr("Commands   Ctrl K")
                Accessible.description: qsTr("Open the command palette. Shortcut Control K.")
                onClicked: commandPalette.open()
            }

            RowLayout {
                spacing: 6
                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: 4
                    color: Theme.green
                }
                Text {
                    text: qsTr("Engine ready")
                    color: Theme.muted
                    font.pixelSize: 10
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 25
        color: Theme.chrome
        border.color: Theme.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Text {
                text: root.cadWorkspaceActive
                      ? root.shellController.documentStatus
                      : root.shellController.studyStatus
                color: Theme.muted
                font.pixelSize: 9
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("Open Engineering Platform %1").arg(root.shellController.version)
                color: Theme.quiet
                font.pixelSize: 9
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            id: activityRail
            Layout.preferredWidth: 50
            Layout.fillHeight: true
            color: Theme.chrome
            border.color: Theme.line
            Accessible.role: Accessible.ToolBar
            Accessible.name: qsTr("Primary tools")

            ColumnLayout {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.topMargin: 8
                spacing: 4

                Repeater {
                    model: [qsTr("Project browser"), qsTr("Viewport"), qsTr("Studies"), qsTr("Diagnostics")]
                    delegate: ToolButton {
                        id: railButton

                        required property string modelData
                        required property int index
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        text: ["⌗", "◇", "λ", "!"][index]
                        onClicked: {
                            if (index === 0) root.shellController.setPanelVisible("left", !browserDock.visible)
                            else if (index === 1) engineeringViewport.forceActiveFocus()
                            else if (index === 2) root.shellController.selectWorkspace("em")
                            else root.shellController.setPanelVisible("bottom", !outputDock.visible)
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData
                        hoverEnabled: true
                        Accessible.name: modelData
                        Accessible.description: qsTr("Open %1").arg(modelData)
                        background: Rectangle {
                            color: railButton.hovered ? Theme.surfaceRaised : "transparent"
                            border.color: railButton.activeFocus ? Theme.cyan : "transparent"
                            border.width: railButton.activeFocus ? 2 : 0
                        }
                        contentItem: Text {
                            text: railButton.text
                            color: railButton.index === 0 ? Theme.cyan : Theme.muted
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            font.bold: true
                        }
                    }
                }
            }
        }

        SplitView {
            id: horizontalDocks
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                id: browserDock
                objectName: "projectBrowserDock"
                visible: root.shellController.leftPanelVisible
                SplitView.preferredWidth: root.shellController.leftPanelWidth
                SplitView.minimumWidth: 180
                SplitView.maximumWidth: 520
                color: Theme.surface
                border.color: Theme.line
                Accessible.role: Accessible.Pane
                Accessible.name: root.cadWorkspaceActive
                                 ? qsTr("CAD model browser")
                                 : qsTr("EM study browser")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: Theme.panelPadding
                        spacing: 4
                        Text {
                            text: root.cadWorkspaceActive ? qsTr("MODEL BROWSER") : qsTr("STUDY BROWSER")
                            color: Theme.quiet
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1.2
                        }
                        Text {
                            text: root.cadWorkspaceActive ? qsTr("Document features") : qsTr("EM study draft")
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }

                    TextField {
                        id: browserFilter
                        objectName: "browserFilter"
                        Layout.fillWidth: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        placeholderText: qsTr("Filter project")
                        Accessible.name: qsTr("Filter project browser")
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        ColumnLayout {
                            width: Math.max(implicitWidth, browserDock.width - 20)
                            spacing: 2

                            Repeater {
                                model: root.cadWorkspaceActive
                                       ? root.shellController.featureRows
                                       : root.shellController.studyRows
                                delegate: ItemDelegate {
                                    id: treeItem

                                    required property string modelData
                                    required property int index
                                    Layout.fillWidth: true
                                    visible: browserFilter.text.length === 0 || modelData.toLowerCase().indexOf(browserFilter.text.toLowerCase()) >= 0
                                    Layout.preferredHeight: visible ? 34 : 0
                                    objectName: "browserRow_" + index
                                    text: modelData
                                    onClicked: {
                                        if (root.cadWorkspaceActive) root.shellController.selectFeature(index)
                                        else root.navigateStudyRow(index)
                                    }
                                    Accessible.name: modelData
                                    highlighted: root.cadWorkspaceActive
                                                 ? treeItem.modelData.startsWith(root.shellController.selectedFeature + " · ")
                                                 : treeItem.index === root.selectedStudyRow
                                    contentItem: Text {
                                        text: treeItem.text
                                        textFormat: Text.PlainText
                                        color: treeItem.highlighted ? Theme.text : Theme.muted
                                        elide: Text.ElideRight
                                        leftPadding: 24
                                        font.pixelSize: 11
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                    background: Rectangle {
                                        radius: 4
                                        color: treeItem.highlighted ? Theme.cyanSoft : "transparent"
                                        border.color: treeItem.activeFocus ? Theme.cyan : "transparent"
                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 3; height: 20
                                            color: treeItem.highlighted ? Theme.cyan : Theme.lineStrong
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            SplitView {
                id: centerDocks
                SplitView.fillWidth: true
                orientation: Qt.Vertical

                Rectangle {
                    id: engineeringViewport
                    objectName: "engineeringViewport"
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 260
                    color: "#0c171d"
                    Accessible.role: Accessible.Pane
                    Accessible.name: root.cadWorkspaceActive
                                     ? qsTr("CAD engineering viewport")
                                     : qsTr("EM engineering viewport")
                    focus: true
                    Accessible.description: qsTr("Native evaluated-geometry viewport. Drag to orbit, middle-drag to pan, wheel to zoom, and click to select a persistent face.")

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 43
                            color: Theme.surface
                            border.color: Theme.line

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 6
                                Repeater {
                                    model: root.cadWorkspaceActive
                                           ? [qsTr("Solid"), qsTr("Sketch"), qsTr("Inspect"), qsTr("Create"), qsTr("Modify")]
                                           : [qsTr("Setup"), qsTr("Mesh"), qsTr("Solve"), qsTr("Results")]
                                    delegate: ShellButton {
                                        required property string modelData
                                        required property int index
                                        text: modelData
                                        selected: index === 0
                                        enabled: index === 0
                                        implicitWidth: Math.max(48, contentItem.implicitWidth + 16)
                                        implicitHeight: 28
                                        Accessible.description: qsTr("Select %1 tools").arg(modelData)
                                    }
                                }
                                Item { Layout.fillWidth: true }
                                ShellButton {
                                    objectName: "viewportFitButton"
                                    text: qsTr("Fit")
                                    implicitWidth: 42
                                    onClicked: root.shellController.fitViewport()
                                }
                                Text {
                                    text: qsTr("Clip")
                                    color: Theme.muted
                                    font.pixelSize: 10
                                }
                                Slider {
                                    id: clipSlider
                                    objectName: "viewportClipSlider"
                                    from: 0
                                    to: 1
                                    value: root.shellController.viewportClipFraction
                                    implicitWidth: 90
                                    Accessible.name: qsTr("Viewport section clip")
                                    onMoved: root.shellController.setViewportClip(value)
                                }
                                ShellButton {
                                    objectName: "viewportGridButton"
                                    text: qsTr("Grid")
                                    selected: root.gridVisible
                                    implicitWidth: 48
                                    onClicked: root.gridVisible = !root.gridVisible
                                }
                                ShellButton {
                                    objectName: "viewportOverlayButton"
                                    text: qsTr("Overlay")
                                    selected: root.overlayVisible
                                    implicitWidth: 60
                                    onClicked: root.overlayVisible = !root.overlayVisible
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Rectangle {
                                anchors.fill: parent
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "#20323b" }
                                    GradientStop { position: 1; color: "#0b151a" }
                                }
                            }
                            Repeater {
                                model: root.gridVisible ? Math.ceil(parent.width / 28) : 0
                                delegate: Rectangle {
                                    required property int index
                                    x: index * 28
                                    width: 1; height: parent.height
                                    color: "#263b46"; opacity: 0.45
                                }
                            }
                            Repeater {
                                model: root.gridVisible ? Math.ceil(parent.height / 28) : 0
                                delegate: Rectangle {
                                    required property int index
                                    y: index * 28
                                    height: 1; width: parent.width
                                    color: "#263b46"; opacity: 0.45
                                }
                            }
                            Rectangle {
                                visible: !root.shellController.hasViewportScene
                                anchors.centerIn: parent
                                width: Math.min(parent.width - 40, 400)
                                height: 180
                                radius: 8
                                color: "#111f27"
                                border.color: Theme.lineStrong
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 20
                                    spacing: 12
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: "◇"
                                        color: Theme.cyan
                                        font.pixelSize: 32
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignHCenter
                                        text: qsTr("No current model to display")
                                        color: Theme.text
                                        font.pixelSize: 15
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        horizontalAlignment: Text.AlignHCenter
                                        text: qsTr("Create or open an evaluated CAD document. Failed regeneration intentionally clears the current scene.")
                                        color: Theme.muted
                                        font.pixelSize: 11
                                    }
                                }
                            }
                            Canvas {
                                id: viewportCanvas
                                objectName: "nativeViewportCanvas"
                                anchors.fill: parent
                                visible: root.shellController.hasViewportScene
                                renderStrategy: Canvas.Cooperative

                                function faceColor(shade, persistentId, material) {
                                    if (persistentId === root.shellController.viewportSelection)
                                        return "#e0a33a"
                                    if (persistentId === root.shellController.viewportPreselection)
                                        return "#58d9ff"
                                    return "rgb(" + Math.round(material[0] * shade * 255)
                                            + "," + Math.round(material[1] * shade * 255)
                                            + "," + Math.round(material[2] * shade * 255) + ")"
                                }

                                onPaint: {
                                    var context = getContext("2d")
                                    context.clearRect(0, 0, width, height)
                                    var frame = root.shellController.viewportFrame(width, height)
                                    var triangles = frame["triangles"]
                                    var edges = frame["edges"]
                                    for (var index = 0; index < triangles.length; ++index) {
                                        var triangle = triangles[index]
                                        context.beginPath()
                                        context.moveTo(triangle.points[0].x, triangle.points[0].y)
                                        context.lineTo(triangle.points[1].x, triangle.points[1].y)
                                        context.lineTo(triangle.points[2].x, triangle.points[2].y)
                                        context.closePath()
                                        context.fillStyle = faceColor(
                                            triangle.shade, triangle.id, triangle.material)
                                        context.fill()
                                    }
                                    context.lineWidth = 1
                                    context.strokeStyle = "#192a32"
                                    for (var edgeIndex = 0; edgeIndex < edges.length; ++edgeIndex) {
                                        var edge = edges[edgeIndex]
                                        if (edge.points.length < 2) continue
                                        context.beginPath()
                                        context.moveTo(edge.points[0].x, edge.points[0].y)
                                        for (var pointIndex = 1; pointIndex < edge.points.length; ++pointIndex)
                                            context.lineTo(edge.points[pointIndex].x, edge.points[pointIndex].y)
                                        context.stroke()
                                    }
                                }

                                Connections {
                                    target: root.shellController
                                    function onViewport_changed() { viewportCanvas.requestPaint() }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
                                    property real previousX: 0
                                    property real previousY: 0
                                    property bool dragged: false
                                    onPressed: function(mouse) {
                                        previousX = mouse.x
                                        previousY = mouse.y
                                        dragged = false
                                    }
                                    onPositionChanged: function(mouse) {
                                        var deltaX = mouse.x - previousX
                                        var deltaY = mouse.y - previousY
                                        if (pressedButtons & Qt.LeftButton) {
                                            dragged = dragged || Math.abs(deltaX) + Math.abs(deltaY) > 1
                                            root.shellController.orbitViewport(deltaX, deltaY)
                                        } else if (pressedButtons & (Qt.MiddleButton | Qt.RightButton)) {
                                            dragged = dragged || Math.abs(deltaX) + Math.abs(deltaY) > 1
                                            root.shellController.panViewport(deltaX, deltaY)
                                        } else {
                                            root.shellController.preselectViewport(mouse.x, mouse.y, width, height)
                                        }
                                        previousX = mouse.x
                                        previousY = mouse.y
                                    }
                                    onReleased: function(mouse) {
                                        if (!dragged && mouse.button === Qt.LeftButton)
                                            root.shellController.selectViewport(mouse.x, mouse.y, width, height)
                                    }
                                    onExited: root.shellController.preselectViewport(-1, -1, width, height)
                                    onWheel: function(wheel) {
                                        root.shellController.zoomViewport(wheel.angleDelta.y)
                                        wheel.accepted = true
                                    }
                                }
                            }
                            Text {
                                visible: root.overlayVisible
                                anchors.left: parent.left
                                anchors.bottom: parent.bottom
                                anchors.margins: 16
                                text: "X   Y   Z   ·   " + root.shellController.viewportStatus
                                      + (root.shellController.viewportSelection.length > 0
                                         ? "   ·   selected " + root.shellController.viewportSelection : "")
                                color: Theme.muted
                                font.pixelSize: 10
                                width: parent.width - 32
                                elide: Text.ElideRight
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 15
                                text: root.cadWorkspaceActive ? qsTr("DESIGN · ORTHOGRAPHIC") : qsTr("SETUP · ORTHOGRAPHIC")
                                color: Theme.quiet
                                font.pixelSize: 9
                                font.bold: true
                            }
                        }
                    }
                }

                Rectangle {
                    id: outputDock
                    objectName: "diagnosticsDock"
                    visible: root.shellController.bottomPanelVisible
                    SplitView.preferredHeight: root.shellController.bottomPanelHeight
                    SplitView.minimumHeight: 110
                    SplitView.maximumHeight: 420
                    color: Theme.surface
                    border.color: Theme.line
                    Accessible.role: Accessible.Pane
                    Accessible.name: qsTr("Diagnostics and timeline")

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 36
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Repeater {
                                model: root.cadWorkspaceActive
                                       ? [qsTr("Feature history"), qsTr("Diagnostics")]
                                       : [qsTr("Preflight"), qsTr("Worker"), qsTr("Diagnostics")]
                                delegate: ShellButton {
                                    required property string modelData
                                    required property int index
                                    text: modelData
                                    selected: root.outputTab === index
                                    implicitHeight: 28
                                    onClicked: root.outputTab = index
                                }
                            }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: root.cadWorkspaceActive ? root.shellController.documentStatus : root.shellController.studyStatus
                                color: Theme.amber
                                font.pixelSize: 10
                            }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
                        ScrollView {
                            visible: root.cadWorkspaceActive && root.outputTab === 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            RowLayout {
                                spacing: 10
                                Repeater {
                                    model: root.shellController.featureRows
                                    delegate: ShellButton {
                                        required property string modelData
                                        required property int index
                                        text: modelData.replace(" · ", "\n")
                                        implicitHeight: 56
                                        selected: modelData.startsWith(root.shellController.selectedFeature + " · ")
                                        onClicked: root.shellController.selectFeature(index)
                                    }
                                }
                                Text {
                                    visible: root.shellController.featureRows.length === 0
                                    text: qsTr("Create or open a CAD document to inspect its feature sequence.")
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                            }
                        }
                        ScrollView {
                            visible: (root.cadWorkspaceActive && root.outputTab === 1)
                                     || (!root.cadWorkspaceActive
                                         && (root.outputTab === 0 || root.outputTab === 2))
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.margins: 12
                            clip: true
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                textFormat: Text.PlainText
                                color: Theme.muted
                                font.pixelSize: 11
                                text: root.cadWorkspaceActive
                                      ? root.shellController.diagnosticText
                                      : root.shellController.studyDiagnostics
                            }
                        }
                        ScrollView {
                            visible: !root.cadWorkspaceActive && root.outputTab === 1
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            ColumnLayout {
                                width: parent.width
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: root.shellController.workerEstimate.length > 0
                                          ? root.shellController.workerEstimate
                                          : qsTr("Prepare a preflight-valid study to calculate the bounded reference job.")
                                    wrapMode: Text.WordWrap
                                    color: Theme.text
                                    font.pixelSize: 11
                                }
                                ProgressBar {
                                    objectName: "workerProgressBar"
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 1
                                    value: root.shellController.workerProgress
                                    Accessible.name: qsTr("Simulation progress")
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.shellController.workerStatus
                                    wrapMode: Text.WordWrap
                                    color: Theme.muted
                                    font.pixelSize: 10
                                }
                                RowLayout {
                                    ShellButton {
                                        objectName: "prepareWorkerButton"
                                        text: qsTr("Estimate")
                                        enabled: root.shellController.canPrepareWorker
                                        onClicked: root.shellController.prepareWorkerJob()
                                    }
                                    ShellButton {
                                        objectName: "launchWorkerButton"
                                        text: qsTr("Launch")
                                        enabled: root.shellController.canLaunchWorker
                                        onClicked: root.shellController.launchWorker()
                                    }
                                    ShellButton {
                                        objectName: "cancelWorkerButton"
                                        text: qsTr("Cancel")
                                        enabled: root.shellController.workerRunning
                                        onClicked: root.shellController.cancelWorker()
                                    }
                                    ShellButton {
                                        objectName: "checkpointWorkerButton"
                                        text: qsTr("Checkpoint")
                                        enabled: root.shellController.canCheckpointWorker
                                        onClicked: root.shellController.checkpointWorker()
                                    }
                                    ShellButton {
                                        objectName: "restartWorkerButton"
                                        text: qsTr("Restart")
                                        enabled: root.shellController.hasWorkerCheckpoint
                                        onClicked: root.shellController.restartWorkerFromCheckpoint()
                                    }
                                    ShellButton {
                                        objectName: "collectWorkerButton"
                                        text: qsTr("Collect")
                                        enabled: root.shellController.canCollectWorker
                                        onClicked: root.shellController.collectWorkerArtifacts()
                                    }
                                }
                                Timer {
                                    interval: 45
                                    repeat: true
                                    running: root.shellController.workerRunning
                                    onTriggered: root.shellController.advanceWorker()
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: inspectorDock
                objectName: "inspectorDock"
                visible: root.shellController.rightPanelVisible
                SplitView.preferredWidth: root.shellController.rightPanelWidth
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 560
                color: Theme.surface
                border.color: Theme.line
                Accessible.role: Accessible.Pane
                Accessible.name: root.cadWorkspaceActive
                                 ? qsTr("Feature inspector")
                                 : qsTr("Study inspector")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: Theme.panelPadding
                        spacing: 4
                        Text {
                            text: root.cadWorkspaceActive ? qsTr("FEATURE INSPECTOR") : qsTr("STUDY INSPECTOR")
                            color: Theme.quiet
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1.2
                        }
                        Text {
                            text: root.cadWorkspaceActive ? (root.shellController.selectedFeature || qsTr("No feature selected")) : qsTr("EM setup draft")
                            color: Theme.text
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.line }
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        visible: root.cadWorkspaceActive
                        clip: true
                        ColumnLayout {
                            width: inspectorDock.width - 24
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: selectionIdentity.implicitHeight + 20
                                color: Theme.background
                                border.color: Theme.line
                                radius: 4
                                Text {
                                    id: selectionIdentity
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    text: qsTr("FEATURE  %1\nREVISION  %2").arg(root.shellController.selectedFeature || "—").arg(root.shellController.documentRevision || "—")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WrapAnywhere
                                    color: Theme.muted
                                    font.family: "monospace"
                                    font.pixelSize: 10
                                }
                            }
                            Repeater {
                                model: root.shellController.parameterRows
                                delegate: ColumnLayout {
                                    id: parameterEditor
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 5
                                    // Keep the engine's expected revision with the editor value.
                                    // Enter and Apply share the same validated presentation guard;
                                    // Document::apply remains authoritative for stale/invalid commands.
                                    readonly property bool hasChange: parameterValue.text.trim().length > 0
                                        && isFinite(Number(parameterValue.text))
                                        && Number(parameterValue.text) !== Number(modelData.value)
                                    function commitValue() {
                                        if (hasChange) root.shellController.setParameter(
                                            String(modelData.id), Number(parameterValue.text), String(modelData.revision))
                                    }
                                    Text {
                                        text: String(parameterEditor.modelData.id)
                                        color: Theme.muted
                                        font.pixelSize: 11
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 6
                                        TextField {
                                            id: parameterValue
                                            objectName: "parameterValue_" + String(parameterEditor.modelData.id)
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 40
                                            text: String(parameterEditor.modelData.value)
                                            horizontalAlignment: TextInput.AlignRight
                                            Accessible.name: qsTr("Value for %1 in model units").arg(String(parameterEditor.modelData.id))
                                            onAccepted: parameterEditor.commitValue()
                                        }
                                        ShellButton {
                                            objectName: "applyParameter_" + String(parameterEditor.modelData.id)
                                            implicitWidth: 54
                                            text: qsTr("Apply")
                                            enabled: parameterEditor.hasChange
                                            Accessible.description: qsTr("Commit a changed parameter and regenerate the model; Enter also applies")
                                            onClicked: parameterEditor.commitValue()
                                        }
                                    }
                                    Text {
                                        text: qsTr("Model units · Enter to apply")
                                        color: Theme.quiet
                                        font.pixelSize: 9
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.shellController.selectedFeature.length === 0
                                      ? qsTr("Create or open a CAD document, then select a feature.")
                                      : qsTr("Dimensions use model units (mm). Apply commits one parameter. Undo and Redo are in Edit.")
                                wrapMode: Text.WordWrap
                                color: Theme.muted
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                text: root.shellController.fileMessage
                                wrapMode: Text.WordWrap
                                color: Theme.muted
                                font.pixelSize: 11
                            }
                        }
                    }
                    ScrollView {
                        id: studyInspector
                        visible: !root.cadWorkspaceActive
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ColumnLayout {
                            width: inspectorDock.width - 24
                            ShellButton {
                                objectName: "prepareStudyButton"
                                text: root.shellController.hasStudy ? qsTr("Rebuild EM draft…") : qsTr("Prepare EM draft")
                                enabled: root.shellController.canPrepareStudy
                                Accessible.description: qsTr("Snapshot current CAD geometry into an incomplete EM setup draft")
                                onClicked: {
                                    if (root.shellController.hasStudy) rebuildStudyDialog.open()
                                    else root.shellController.prepareStudy()
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: root.shellController.studyStatus
                                color: Theme.amber
                            }
                            InspectorSection {
                                title: qsTr("Frequency")
                                objectName: "frequencySection"
                                contentEnabled: root.shellController.canEditStudy
                                visible: root.shellController.hasStudy
                                property string editFingerprint: root.shellController.studyFingerprint
                                id: frequencyEditor
                                Text { text: qsTr("Frequency band (Hz)"); color: Theme.text }
                                TextField {
                                    id: minimumFrequency
                                    objectName: "studyMinimumFrequencyEditor"
                                    Layout.fillWidth: true
                                    text: String(root.shellController.studyMinimumFrequency)
                                    Accessible.name: qsTr("Minimum study frequency in hertz")
                                }
                                TextField {
                                    id: maximumFrequency
                                    Layout.fillWidth: true
                                    text: String(root.shellController.studyMaximumFrequency)
                                    Accessible.name: qsTr("Maximum study frequency in hertz")
                                }
                                ShellButton {
                                    objectName: "applyStudyFrequencyButton"
                                    text: qsTr("Apply frequency band")
                                    onClicked: root.shellController.setStudyFrequency(
                                        Number(minimumFrequency.text), Number(maximumFrequency.text), frequencyEditor.editFingerprint)
                                }
                            }
                            InspectorSection {
                                title: qsTr("Materials")
                                objectName: "materialSection"
                                id: materialEditor
                                visible: root.shellController.hasStudy
                                contentEnabled: root.shellController.canEditStudy
                                Layout.fillWidth: true
                                function loadValues(value: var) {
                                    materialId.text = String(value["id"])
                                    materialPermittivity.text = String(value["permittivity"])
                                    materialPermeability.text = String(value["permeability"])
                                    materialConductivity.text = String(value["conductivity"])
                                    materialLoss.text = String(value["lossTangent"])
                                    materialMinimum.text = String(value["minimumFrequency"])
                                    materialMaximum.text = String(value["maximumFrequency"])
                                    materialProvenance.text = String(value["provenance"])
                                }
                                Text { text: qsTr("Nondispersive material"); color: Theme.text }
                                ComboBox {
                                    Layout.fillWidth: true
                                    model: root.shellController.studyMaterialIds
                                    objectName: "studyMaterialSelector"
                                    Accessible.name: qsTr("Load existing material values")
                                    onActivated: materialEditor.loadValues(root.shellController.materialValues(currentText))
                                }
                                TextField {
                                    id: materialId
                                    Layout.fillWidth: true
                                    text: "user-material"
                                    Accessible.name: qsTr("Material identifier")
                                }
                                Text { text: qsTr("Relative permittivity εr"); color: Theme.text }
                                TextField {
                                    id: materialPermittivity
                                    objectName: "materialPermittivityEditor"
                                    Layout.fillWidth: true
                                    text: "1"
                                    Accessible.name: qsTr("Material relative permittivity")
                                }
                                Text { text: qsTr("Relative permeability μr"); color: Theme.text }
                                TextField {
                                    id: materialPermeability
                                    Layout.fillWidth: true
                                    text: "1"
                                    Accessible.name: qsTr("Material relative permeability")
                                }
                                Text { text: qsTr("Conductivity σ (S/m)"); color: Theme.text }
                                TextField {
                                    id: materialConductivity
                                    Layout.fillWidth: true
                                    text: "0"
                                    Accessible.name: qsTr("Material conductivity in siemens per metre")
                                }
                                Text { text: qsTr("Loss tangent tanδ"); color: Theme.text }
                                TextField {
                                    id: materialLoss
                                    Layout.fillWidth: true
                                    text: "0"
                                    Accessible.name: qsTr("Material loss tangent")
                                }
                                Text { text: qsTr("Validity band (Hz)"); color: Theme.text }
                                TextField {
                                    id: materialMinimum
                                    objectName: "materialMinimumFrequencyEditor"
                                    Layout.fillWidth: true
                                    text: String(root.shellController.studyMinimumFrequency)
                                    Accessible.name: qsTr("Material minimum valid frequency in hertz")
                                }
                                TextField {
                                    id: materialMaximum
                                    objectName: "materialMaximumFrequencyEditor"
                                    Layout.fillWidth: true
                                    text: String(root.shellController.studyMaximumFrequency)
                                    Accessible.name: qsTr("Material maximum valid frequency in hertz")
                                }
                                TextField {
                                    id: materialProvenance
                                    objectName: "materialProvenanceEditor"
                                    Layout.fillWidth: true
                                    placeholderText: qsTr("Source or assumption for these properties")
                                    Accessible.name: qsTr("Material property provenance")
                                }
                                RowLayout {
                                    ShellButton {
                                        objectName: "applyStudyMaterialButton"
                                        text: qsTr("Apply material")
                                        enabled: materialId.text.trim().length > 0
                                                 && materialPermittivity.text.trim().length > 0
                                                 && materialPermeability.text.trim().length > 0
                                                 && materialConductivity.text.trim().length > 0
                                                 && materialLoss.text.trim().length > 0
                                                 && materialMinimum.text.trim().length > 0
                                                 && materialMaximum.text.trim().length > 0
                                                 && materialProvenance.text.trim().length > 0
                                        onClicked: root.shellController.setStudyMaterial(
                                            materialId.text, Number(materialPermittivity.text), Number(materialPermeability.text),
                                            Number(materialConductivity.text), Number(materialLoss.text),
                                            Number(materialMinimum.text), Number(materialMaximum.text), materialProvenance.text,
                                            root.shellController.studyFingerprint)
                                    }
                                    ShellButton {
                                        text: qsTr("Remove material")
                                        onClicked: root.shellController.removeStudyMaterial(materialId.text, root.shellController.studyFingerprint)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: root.shellController.studyMaterials
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WrapAnywhere
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                            }
                            InspectorSection {
                                title: qsTr("Persistent face roles")
                                id: roleSection
                                objectName: "roleSection"
                                visible: root.shellController.hasStudy
                                contentEnabled: root.shellController.canEditStudy
                                Layout.fillWidth: true
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Only resolved faces in the final body are listed. Use the same ID to replace a definition. Choose a material for air/vacuum, conductor, dielectric or ground roles.")
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                                TextField {
                                    id: roleId
                                    Layout.fillWidth: true
                                    text: "region-1"
                                    Accessible.name: qsTr("Role identifier")
                                }
                                ComboBox {
                                    id: roleFace
                                    Layout.fillWidth: true
                                    model: root.shellController.studyFaces
                                    Accessible.name: qsTr("Role target face")
                                }
                                ComboBox {
                                    id: roleKind
                                    Layout.fillWidth: true
                                    model: [qsTr("Air / vacuum"), qsTr("Keep out"), qsTr("Conductor"), qsTr("Dielectric"), qsTr("Ground")]
                                    objectName: "studyRoleKindEditor"
                                    Accessible.name: qsTr("Electromagnetic role")
                                }
                                ComboBox {
                                    id: roleMaterial
                                    objectName: "studyRoleMaterialEditor"
                                    Layout.fillWidth: true
                                    enabled: roleKind.currentIndex !== 1
                                    model: root.shellController.studyMaterialIds
                                    Accessible.name: qsTr("Role material")
                                }
                                RowLayout {
                                    ShellButton {
                                        objectName: "applyStudyRoleButton"
                                        text: qsTr("Apply role")
                                        onClicked: root.shellController.assignStudyRole(
                                            roleId.text, roleFace.currentIndex, roleKind.currentIndex,
                                            roleMaterial.currentText, root.shellController.studyFingerprint)
                                    }
                                    ShellButton {
                                        text: qsTr("Remove role")
                                        onClicked: root.shellController.removeStudyDefinition(
                                            false, roleId.text, root.shellController.studyFingerprint)
                                    }
                                }
                            }
                            InspectorSection {
                                title: qsTr("Discrete ports")
                                id: portSection
                                objectName: "portSection"
                                visible: root.shellController.hasStudy
                                contentEnabled: root.shellController.canEditStudy
                                Layout.fillWidth: true
                                TextField {
                                    id: portId
                                    Layout.fillWidth: true
                                    text: "port-1"
                                    Accessible.name: qsTr("Port identifier")
                                }
                                Text { text: qsTr("Port face"); color: Theme.text }
                                ComboBox {
                                    id: portFace
                                    Layout.fillWidth: true
                                    model: root.shellController.studyFaces
                                    Accessible.name: qsTr("Port face")
                                }
                                Text { text: qsTr("Reference plane face"); color: Theme.text }
                                ComboBox {
                                    id: referenceFace
                                    Layout.fillWidth: true
                                    model: root.shellController.studyFaces
                                    Accessible.name: qsTr("Port reference plane face")
                                }
                                Text { text: qsTr("Inward orientation"); color: Theme.text }
                                ComboBox {
                                    id: portOrientation
                                    Layout.fillWidth: true
                                    model: ["+X", "-X", "+Y", "-Y", "+Z", "-Z"]
                                    currentIndex: 4
                                    Accessible.name: qsTr("Port inward orientation")
                                }
                                Text { text: qsTr("Reference impedance (Ω)"); color: Theme.text }
                                TextField {
                                    id: portImpedance
                                    objectName: "portImpedanceEditor"
                                    Layout.fillWidth: true
                                    text: "50"
                                    Accessible.name: qsTr("Port reference impedance in ohms")
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: qsTr("Matched termination, mode 0, 1 V amplitude, 0 rad phase, 0 m de-embedding. Setup validation does not calibrate the port or launch a solve.")
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                                RowLayout {
                                    ShellButton {
                                        objectName: "applyStudyPortButton"
                                        text: qsTr("Apply port")
                                        onClicked: root.shellController.setStudyPort(
                                            portId.text, portFace.currentIndex, referenceFace.currentIndex,
                                            Number(portImpedance.text), portOrientation.currentIndex,
                                            root.shellController.studyFingerprint)
                                    }
                                    ShellButton {
                                        text: qsTr("Remove port")
                                        onClicked: root.shellController.removeStudyDefinition(
                                            true, portId.text, root.shellController.studyFingerprint)
                                    }
                                }
                            }
                            InspectorSection {
                                title: qsTr("Boundary policies")
                                id: boundarySection
                                objectName: "boundarySection"
                                visible: root.shellController.hasStudy
                                contentEnabled: root.shellController.canEditStudy
                                Layout.fillWidth: true
                                TextField {
                                    id: boundaryId
                                    Layout.fillWidth: true
                                    text: "domain-1"
                                    Accessible.name: qsTr("Boundary identifier")
                                }
                                ComboBox {
                                    id: boundaryKind
                                    Layout.fillWidth: true
                                    model: [qsTr("Open PML"), qsTr("PEC")]
                                    Accessible.name: qsTr("Boundary kind")
                                }
                                ComboBox {
                                    id: boundaryMaterial
                                    Layout.fillWidth: true
                                    model: root.shellController.studyMaterialIds
                                    Accessible.name: qsTr("Boundary background material")
                                }
                                Text { text: qsTr("Padding (m)"); color: Theme.text }
                                TextField {
                                    id: boundaryPadding
                                    objectName: "boundaryPaddingEditor"
                                    Layout.fillWidth: true
                                    text: "0.03"
                                    Accessible.name: qsTr("Boundary padding in metres")
                                }
                                RowLayout {
                                    ShellButton {
                                        objectName: "applyStudyBoundaryButton"
                                        text: qsTr("Apply boundary")
                                        enabled: boundaryId.text.trim().length > 0
                                                 && boundaryPadding.text.trim().length > 0
                                                 && boundaryMaterial.currentIndex >= 0
                                        onClicked: root.shellController.setStudyBoundary(
                                            boundaryId.text, boundaryKind.currentIndex === 0, boundaryMaterial.currentText,
                                            Number(boundaryPadding.text), root.shellController.studyFingerprint)
                                    }
                                    ShellButton {
                                        text: qsTr("Remove boundary")
                                        onClicked: root.shellController.removeStudySetup(
                                            true, boundaryId.text, root.shellController.studyFingerprint)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WordWrap
                                    text: qsTr("An open antenna study needs a PML policy. The local reference job uses a bounded homogeneous preview grid; 3-D CPML remains unfinished.")
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                            }
                            InspectorSection {
                                title: qsTr("Requested observables")
                                id: monitorSection
                                objectName: "monitorSection"
                                visible: root.shellController.hasStudy
                                contentEnabled: root.shellController.canEditStudy
                                Layout.fillWidth: true
                                TextField {
                                    id: monitorId
                                    Layout.fillWidth: true
                                    text: "network-1"
                                    Accessible.name: qsTr("Monitor identifier")
                                }
                                ComboBox {
                                    id: monitorKind
                                    Layout.fillWidth: true
                                    model: [qsTr("S parameters"), qsTr("Impedance")]
                                    Accessible.name: qsTr("Monitor kind")
                                }
                                ComboBox {
                                    id: monitorPort
                                    Layout.fillWidth: true
                                    model: root.shellController.studyPortIds
                                    Accessible.name: qsTr("Monitor port")
                                }
                                Text { text: qsTr("Monitor frequency band (Hz)"); color: Theme.text }
                                TextField {
                                    id: monitorMinimum
                                    objectName: "monitorMinimumFrequencyEditor"
                                    Layout.fillWidth: true
                                    text: String(root.shellController.studyMinimumFrequency)
                                    Accessible.name: qsTr("Monitor minimum frequency in hertz")
                                }
                                TextField {
                                    id: monitorMaximum
                                    Layout.fillWidth: true
                                    text: String(root.shellController.studyMaximumFrequency)
                                    Accessible.name: qsTr("Monitor maximum frequency in hertz")
                                }
                                RowLayout {
                                    ShellButton {
                                        objectName: "applyStudyMonitorButton"
                                        text: qsTr("Apply monitor")
                                        enabled: monitorId.text.trim().length > 0 && monitorPort.currentIndex >= 0
                                                 && monitorMinimum.text.trim().length > 0
                                                 && monitorMaximum.text.trim().length > 0
                                        onClicked: root.shellController.setStudyMonitor(
                                            monitorId.text, monitorKind.currentIndex, monitorPort.currentText,
                                            Number(monitorMinimum.text), Number(monitorMaximum.text),
                                            root.shellController.studyFingerprint)
                                    }
                                    ShellButton {
                                        text: qsTr("Remove monitor")
                                        onClicked: root.shellController.removeStudySetup(
                                            false, monitorId.text, root.shellController.studyFingerprint)
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.WrapAnywhere
                                    textFormat: Text.PlainText
                                    text: root.shellController.studySetup
                                    color: Theme.muted
                                    font.pixelSize: 11
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                textFormat: Text.PlainText
                                text: root.shellController.studyDefinitions
                                color: Theme.text
                                font.pixelSize: 11
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WrapAnywhere
                                text: root.shellController.studySummary
                                textFormat: Text.PlainText
                                color: Theme.muted
                                font.pixelSize: 11
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }

    Popup {
        id: rebuildStudyDialog
        anchors.centerIn: Overlay.overlay
        width: 450
        height: 150
        modal: true
        focus: true
        ColumnLayout {
            anchors.fill: parent
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Rebuild from current CAD? This replaces the session draft and resets material, frequency, role, port, boundary and monitor settings to the preview defaults.")
                color: Theme.text
            }
            RowLayout {
                ShellButton {
                    text: qsTr("Rebuild")
                    onClicked: { root.shellController.prepareStudy(); rebuildStudyDialog.close() }
                }
                ShellButton { text: qsTr("Cancel"); onClicked: rebuildStudyDialog.close() }
            }
        }
    }

    Popup {
        id: documentDialog
        objectName: "documentDialog"
        property string operation: "open"
        anchors.centerIn: Overlay.overlay
        width: Math.min(600, root.width * 0.8)
        height: 260
        modal: true
        focus: true
        ColumnLayout {
            anchors.fill: parent
            Text {
                text: documentDialog.operation === "new" ? qsTr("Create CAD example")
                      : documentDialog.operation === "save-project" ? qsTr("Save CAD + EM project")
                      : documentDialog.operation === "open-project" ? qsTr("Open CAD + EM project")
                      : documentDialog.operation === "save" ? qsTr("Save CAD checkpoint")
                      : qsTr("Open CAD checkpoint")
                color: Theme.text
                font.pixelSize: 16
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: documentDialog.operation === "save-project"
                      ? qsTr("Save CAD history and authored EM setup together using atomic internal project recovery. Incomplete and stale drafts remain reviewable after reopening.")
                      : documentDialog.operation === "open-project"
                      ? qsTr("Replace the active session with a saved CAD + EM project after history and exact geometry verification. Save changes you want to retain first.")
                      : documentDialog.operation === "save"
                      ? qsTr("Enter a local path. Saving replaces an existing file atomically. This is the internal CAD recovery format. EM settings are not included.")
                      : qsTr("This replaces the current document. Save CAD changes you want to keep first. EM settings will be discarded. Use Save CAD + EM project to retain them.")
                color: Theme.muted
            }
            TextField {
                id: documentPath
                objectName: "documentPath"
                Layout.fillWidth: true
                visible: documentDialog.operation !== "new"
                placeholderText: documentDialog.operation.endsWith("project")
                                 ? qsTr("/absolute/path/model.oep-project")
                                 : qsTr("/absolute/path/model.oep-checkpoint")
                Accessible.name: documentDialog.operation.endsWith("project")
                                 ? qsTr("CAD and EM project recovery path") : qsTr("CAD checkpoint path")
            }
            Text {
                Layout.fillWidth: true
                text: root.shellController.fileMessage
                wrapMode: Text.WordWrap
                color: Theme.muted
            }
            RowLayout {
                ShellButton {
                    objectName: "confirmDocumentAction"
                    text: documentDialog.operation === "new" ? qsTr("Create")
                          : documentDialog.operation.startsWith("save") ? qsTr("Save") : qsTr("Open")
                    enabled: documentDialog.operation === "new" || documentPath.text.trim().length > 0
                    onClicked: {
                        const succeeded = documentDialog.operation === "new"
                            ? root.shellController.newCadDocument()
                            : documentDialog.operation === "save-project"
                              ? root.shellController.saveProject(documentPath.text)
                            : documentDialog.operation === "open-project"
                              ? root.shellController.loadProject(documentPath.text)
                            : documentDialog.operation === "save"
                              ? root.shellController.saveDocument(documentPath.text)
                              : root.shellController.loadDocument(documentPath.text)
                        if (succeeded) documentDialog.close()
                    }
                }
                ShellButton { text: qsTr("Cancel"); onClicked: documentDialog.close() }
            }
        }
    }

    Popup {
        id: commandPalette
        objectName: "commandPalette"
        anchors.centerIn: Overlay.overlay
        width: Math.min(590, root.width * 0.8)
        height: 330
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surfaceRaised
            border.color: Theme.lineStrong
            radius: Theme.radius + 4
        }
        ColumnLayout {
            id: commandPaletteContent
            anchors.fill: parent
            spacing: 8
            Accessible.role: Accessible.Dialog
            Accessible.name: qsTr("Command palette")
            TextField {
                id: commandSearch
                Layout.fillWidth: true
                placeholderText: qsTr("Search commands")
                Accessible.name: qsTr("Search commands")
            }
            Text {
                text: qsTr("QUICK ACTIONS")
                color: Theme.quiet
                font.pixelSize: 9
                font.bold: true
                font.letterSpacing: 1.1
            }
            ShellButton {
                Layout.fillWidth: true
                text: qsTr("Switch to CAD Studio                                      Ctrl 1")
                Accessible.description: qsTr("Open the CAD workspace")
                onClicked: {
                    root.shellController.selectWorkspace("cad")
                    commandPalette.close()
                }
            }
            ShellButton {
                Layout.fillWidth: true
                text: qsTr("Switch to EM Studio                                        Ctrl 2")
                Accessible.description: qsTr("Open the EM workspace")
                onClicked: {
                    root.shellController.selectWorkspace("em")
                    commandPalette.close()
                }
            }
            ShellButton {
                Layout.fillWidth: true
                text: qsTr("Toggle diagnostics                                         Ctrl J")
                Accessible.description: qsTr("Show or hide diagnostics")
                onClicked: {
                    root.shellController.setPanelVisible(
                        "bottom", !root.shellController.bottomPanelVisible)
                    commandPalette.close()
                }
            }
            Item { Layout.fillHeight: true }
            Text {
                text: qsTr("Tab moves focus · Enter activates · Escape closes")
                color: Theme.muted
                font.pixelSize: 10
            }
        }
        onOpened: commandSearch.forceActiveFocus()
    }
}
