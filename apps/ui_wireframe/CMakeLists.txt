# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

set(
    OEP_UI_WIREFRAME_FILES
    index.html
    styles.css
    app.js
    README.md
)

add_custom_target(oep_ui_wireframe SOURCES ${OEP_UI_WIREFRAME_FILES})

install(
    FILES index.html styles.css app.js
    DESTINATION share/oep/ui-wireframe
)
