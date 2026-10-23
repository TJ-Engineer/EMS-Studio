// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// Keep editors instantiated while collapsed so an unfinished user entry is not
// lost. Expanded state is presentation-only and never changes study authority.
ColumnLayout {
    id: section

    required property string title
    property bool expanded: true
    property bool contentEnabled: true
    readonly property bool headerHasFocus: toggle.activeFocus
    default property alias contentData: body.data

    // Focus the actionable header rather than a noninteractive layout item.
    function reveal() {
        expanded = true
        toggle.forceActiveFocus()
    }
    Layout.fillWidth: true
    spacing: 8

    // Explicit data avoids routing the section's own chrome through contentData.
    data: [
        Button {
            id: toggle
            objectName: section.objectName + "Toggle"
            Layout.fillWidth: true
            implicitHeight: 34
            text: (section.expanded ? "▾  " : "▸  ") + section.title
            hoverEnabled: true
            Keys.onLeftPressed: function(event) { section.expanded = false; event.accepted = true }
            Keys.onRightPressed: function(event) { section.expanded = true; event.accepted = true }
            Accessible.name: section.title
            Accessible.description: section.expanded ? qsTr("Collapse section") : qsTr("Expand section")
            onClicked: section.expanded = !section.expanded
            contentItem: Text {
                text: toggle.text
                color: Theme.text
                font.pixelSize: 11
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: Theme.surfaceRaised
                radius: 4
                border.color: toggle.activeFocus ? Theme.cyan : Theme.line
            }
        },
        ColumnLayout {
            id: body
            visible: section.expanded
            enabled: section.contentEnabled
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 6
        }
    ]
}
