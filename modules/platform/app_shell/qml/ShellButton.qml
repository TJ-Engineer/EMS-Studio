// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls
import "Theme.js" as Theme

Button {
    id: control

    property bool selected: false

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(72, contentItem.implicitWidth + 20)
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: text

    contentItem: Text {
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: control.enabled
               ? (control.selected ? Theme.text : Theme.muted)
               : Theme.quiet
        font.pixelSize: 11
        font.weight: control.selected ? Font.DemiBold : Font.Normal
        text: control.text
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down
               ? Theme.surfaceSoft
               : (control.selected
                  ? Theme.cyanSoft
                  : (control.hovered ? Theme.surfaceRaised : "transparent"))
        border.color: control.activeFocus || control.selected
                      ? Theme.cyan
                      : (control.hovered ? Theme.lineStrong : Theme.line)
        border.width: control.activeFocus ? 2 : 1
    }
}
