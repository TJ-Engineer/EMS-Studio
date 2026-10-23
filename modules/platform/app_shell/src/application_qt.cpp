// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

// Qt host owns the controller and QML engine for the application lifetime.
// Offscreen smoke mode exercises actual QML actions and accessibility/navigation
// contracts as well as compiled document commands; it is not a rendering test.

#include "oep/app_shell/application.hpp"

#include <algorithm>
#include <string_view>

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMetaObject>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <qqml.h>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariant>

#include "oep/app_shell/shell_controller.hpp"

namespace oep::app_shell {
namespace {

[[nodiscard]] bool has_argument(
    const int argc,
    char** const argv,
    const std::string_view expected)
{
    return std::any_of(argv, argv + argc, [expected](const char* const argument) {
        return argument != nullptr && std::string_view(argument) == expected;
    });
}

[[nodiscard]] QString workspace_name(const Workspace workspace)
{
    return workspace == Workspace::em ? QStringLiteral("em") : QStringLiteral("cad");
}

[[nodiscard]] QObject* find_visual_object(QQuickItem* const item, const QString& name)
{
    if (item == nullptr) return nullptr;
    if (item->objectName() == name) return item;
    for (auto* child : item->childItems()) {
        if (auto* found = find_visual_object(child, name)) return found;
    }
    return nullptr;
}

[[nodiscard]] bool activate_shortcut(
    QObject& root,
    const QString& object_name)
{
    QObject* const shortcut = root.findChild<QObject*>(object_name);
    if (shortcut == nullptr) {
        qWarning() << "Shortcut object was not found:" << object_name;
        return false;
    }
    if (!QMetaObject::invokeMethod(shortcut, "activated", Qt::DirectConnection)) {
        qWarning() << "Shortcut activation could not be invoked:" << object_name
                   << shortcut->metaObject()->className();
        return false;
    }
    QCoreApplication::processEvents();
    return true;
}

} // namespace

int run_application(
    int argc,
    char** const argv,
    const ApplicationOptions& options)
{
    const bool smoke_test = has_argument(argc, argv, "--smoke-test");

    QCoreApplication::setOrganizationName(QStringLiteral("OpenEngineeringPlatform"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("openengineeringplatform.org"));
    QCoreApplication::setApplicationName(QStringLiteral("OpenEngineeringPlatform"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication application(argc, argv);
    application.setApplicationDisplayName(QString::fromStdString(options.product_name));
    application.setApplicationVersion(QString::fromStdString(options.version));

    ShellController controller(
        workspace_name(options.initial_workspace),
        QString::fromStdString(options.product_name),
        QString::fromStdString(options.version));
    qmlRegisterUncreatableType<ShellController>(
        "Oep.Shell",
        1,
        0,
        "ShellController",
        QStringLiteral("ShellController is supplied by the native application host"));
    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{QStringLiteral("shellController"), QVariant::fromValue(&controller)}});
    engine.load(QUrl(QStringLiteral("qrc:/oep/app_shell/Main.qml")));

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "The native application shell failed to load";
        return 2;
    }

    if (smoke_test) {
        QObject* const root = engine.rootObjects().constFirst();
        const bool navigation_ok =
            root->property("navigationContractSatisfied").toBool();
        const bool accessibility_ok =
            root->property("accessibilityContractSatisfied").toBool();
        const bool key_sequences_ok =
            root->property("keyboardContractSatisfied").toBool();
        const bool expected_cad = options.initial_workspace == Workspace::cad;
        const bool initial_workspace_ok =
            root->property("cadWorkspaceActive").toBool() == expected_cad;

        controller.selectWorkspace(QStringLiteral("cad"));
        bool keyboard_ok = activate_shortcut(
            *root, QStringLiteral("emWorkspaceShortcut"));
        keyboard_ok = keyboard_ok &&
                      controller.current_workspace() == QStringLiteral("em");
        keyboard_ok = activate_shortcut(
                          *root, QStringLiteral("cadWorkspaceShortcut")) &&
                      keyboard_ok;
        keyboard_ok = keyboard_ok &&
                      controller.current_workspace() == QStringLiteral("cad");
        keyboard_ok = activate_shortcut(
                          *root, QStringLiteral("commandPaletteShortcut")) &&
                      keyboard_ok;
        keyboard_ok = keyboard_ok &&
                      root->property("commandPaletteVisible").toBool();
        keyboard_ok = activate_shortcut(
                          *root, QStringLiteral("escapeShortcut")) &&
                      keyboard_ok;
        keyboard_ok = keyboard_ok &&
                      !root->property("commandPaletteVisible").toBool();
        controller.setPanelVisible(QStringLiteral("bottom"), true);
        keyboard_ok = activate_shortcut(
                          *root, QStringLiteral("diagnosticsShortcut")) &&
                      keyboard_ok;
        keyboard_ok = keyboard_ok && !controller.bottom_panel_visible();
        keyboard_ok = activate_shortcut(
                          *root, QStringLiteral("diagnosticsShortcut")) &&
                      keyboard_ok;
        keyboard_ok = keyboard_ok && controller.bottom_panel_visible();
        keyboard_ok = keyboard_ok && key_sequences_ok;

        bool presentation_ok = true;
        for (const auto& name : {QStringLiteral("viewportGridButton"),
                                 QStringLiteral("viewportOverlayButton")}) {
            QObject* const button = root->findChild<QObject*>(name);
            if (button == nullptr) {
                presentation_ok = false;
                continue;
            }
            const bool initial = button->property("selected").toBool();
            presentation_ok = QMetaObject::invokeMethod(button, "clicked", Qt::DirectConnection) && presentation_ok;
            presentation_ok = button->property("selected").toBool() != initial && presentation_ok;
            presentation_ok = QMetaObject::invokeMethod(button, "clicked", Qt::DirectConnection) && presentation_ok;
            presentation_ok = button->property("selected").toBool() == initial && presentation_ok;
        }

        bool editing_ok = true;
        if (controller.cad_available()) {
            QObject* const new_action = root->findChild<QObject*>(QStringLiteral("newCadDocumentAction"));
            QObject* const confirm = root->findChild<QObject*>(QStringLiteral("confirmDocumentAction"));
            editing_ok = new_action != nullptr && confirm != nullptr;
            if (editing_ok) {
                editing_ok = QMetaObject::invokeMethod(new_action, "triggered", Qt::DirectConnection);
                editing_ok = QMetaObject::invokeMethod(confirm, "clicked", Qt::DirectConnection) && editing_ok;
                controller.selectFeature(0);
                QCoreApplication::processEvents();
                auto* const window = qobject_cast<QQuickWindow*>(root);
                QObject* const viewport_canvas = find_visual_object(
                    window == nullptr ? nullptr : window->contentItem(), QStringLiteral("nativeViewportCanvas"));
                QObject* const fit_viewport = find_visual_object(
                    window == nullptr ? nullptr : window->contentItem(), QStringLiteral("viewportFitButton"));
                QObject* const clip_viewport = find_visual_object(
                    window == nullptr ? nullptr : window->contentItem(), QStringLiteral("viewportClipSlider"));
                presentation_ok = controller.has_viewport_scene()
                    && viewport_canvas != nullptr && viewport_canvas->property("visible").toBool()
                    && fit_viewport != nullptr && clip_viewport != nullptr && presentation_ok;
                if (fit_viewport != nullptr) {
                    presentation_ok = QMetaObject::invokeMethod(
                        fit_viewport, "clicked", Qt::DirectConnection) && presentation_ok;
                }
                QObject* const value = find_visual_object(
                    window == nullptr ? nullptr : window->contentItem(), QStringLiteral("parameterValue_width"));
                QObject* const apply = find_visual_object(
                    window == nullptr ? nullptr : window->contentItem(), QStringLiteral("applyParameter_width"));
                editing_ok = value != nullptr && apply != nullptr && editing_ok;
                if (editing_ok) {
                    presentation_ok = !apply->property("enabled").toBool() && presentation_ok;
                    value->setProperty("text", QStringLiteral("not-a-number"));
                    presentation_ok = !apply->property("enabled").toBool() && presentation_ok;
                    value->setProperty("text", QStringLiteral("-1"));
                    editing_ok = QMetaObject::invokeMethod(value, "accepted", Qt::DirectConnection);
                    editing_ok = controller.document_status().startsWith(
                        QStringLiteral("Current geometry unavailable")) && editing_ok;
                    editing_ok = controller.undoDocument() && editing_ok;
                    editing_ok = controller.document_status().startsWith(QStringLiteral("Evaluated")) && editing_ok;
                    controller.selectWorkspace(QStringLiteral("em"));
                    QCoreApplication::processEvents();
                    QObject* const prepare = find_visual_object(window->contentItem(), QStringLiteral("prepareStudyButton"));
                    QObject* const frequency = find_visual_object(window->contentItem(), QStringLiteral("studyMinimumFrequencyEditor"));
                    QObject* const apply_frequency = find_visual_object(window->contentItem(), QStringLiteral("applyStudyFrequencyButton"));
                    editing_ok = prepare != nullptr && frequency != nullptr && apply_frequency != nullptr && editing_ok;
                    if (editing_ok) {
                        editing_ok = QMetaObject::invokeMethod(prepare, "clicked", Qt::DirectConnection);
                        editing_ok = controller.study_status().contains(QStringLiteral("setup incomplete")) && editing_ok;
                        QObject* const material_value = find_visual_object(window->contentItem(), QStringLiteral("materialPermittivityEditor"));
                        QObject* const material_minimum = find_visual_object(window->contentItem(), QStringLiteral("materialMinimumFrequencyEditor"));
                        QObject* const material_maximum = find_visual_object(window->contentItem(), QStringLiteral("materialMaximumFrequencyEditor"));
                        QObject* const provenance = find_visual_object(window->contentItem(), QStringLiteral("materialProvenanceEditor"));
                        QObject* const apply_material = find_visual_object(window->contentItem(), QStringLiteral("applyStudyMaterialButton"));
                        editing_ok = material_value != nullptr && material_minimum != nullptr
                            && material_maximum != nullptr && provenance != nullptr
                            && apply_material != nullptr && editing_ok;
                        if (editing_ok) {
                            const auto before_bad_material = controller.study_fingerprint();
                            material_value->setProperty("text", QStringLiteral("-1"));
                            material_minimum->setProperty("text", QStringLiteral("1"));
                            material_maximum->setProperty("text", QStringLiteral("1e15"));
                            provenance->setProperty("text", QStringLiteral("synthetic test assumption"));
                            // Collapsing presentation must retain unsaved entries and must not
                            // submit a study command or alter canonical content identity.
                            for (const auto& section_name : {
                                     QStringLiteral("frequencySection"), QStringLiteral("materialSection"),
                                     QStringLiteral("roleSection"), QStringLiteral("portSection"),
                                     QStringLiteral("boundarySection"), QStringLiteral("monitorSection")}) {
                                QObject* const section = find_visual_object(window->contentItem(), section_name);
                                QObject* const toggle = find_visual_object(window->contentItem(), section_name + QStringLiteral("Toggle"));
                                if (section == nullptr || toggle == nullptr) {
                                    presentation_ok = false;
                                    continue;
                                }
                                presentation_ok = section->property("expanded").toBool() && presentation_ok;
                                presentation_ok = QMetaObject::invokeMethod(toggle, "clicked", Qt::DirectConnection) && presentation_ok;
                                presentation_ok = !section->property("expanded").toBool() && presentation_ok;
                                presentation_ok = controller.study_fingerprint() == before_bad_material && presentation_ok;
                                presentation_ok = QMetaObject::invokeMethod(toggle, "clicked", Qt::DirectConnection) && presentation_ok;
                                presentation_ok = section->property("expanded").toBool() && presentation_ok;
                            }
                            // Browse by the original row index after filtering. Selecting a
                            // row reopens its section without changing draft content.
                            const QStringList section_names{
                                QStringLiteral("frequencySection"), QStringLiteral("frequencySection"),
                                QStringLiteral("materialSection"), QStringLiteral("roleSection"),
                                QStringLiteral("portSection"), QStringLiteral("boundarySection"),
                                QStringLiteral("monitorSection")};
                            for (int row = 0; row < section_names.size(); ++row) {
                                QObject* const section = find_visual_object(window->contentItem(), section_names[row]);
                                QObject* const browser_row = find_visual_object(window->contentItem(), QStringLiteral("browserRow_%1").arg(row));
                                if (section == nullptr || browser_row == nullptr) {
                                    presentation_ok = false;
                                    continue;
                                }
                                section->setProperty("expanded", false);
                                presentation_ok = QMetaObject::invokeMethod(browser_row, "clicked", Qt::DirectConnection) && presentation_ok;
                                QCoreApplication::processEvents();
                                presentation_ok = section->property("expanded").toBool() && presentation_ok;
                                presentation_ok = section->property("headerHasFocus").toBool() && presentation_ok;
                                QKeyEvent collapse_key(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
                                QCoreApplication::sendEvent(window, &collapse_key);
                                presentation_ok = !section->property("expanded").toBool() && presentation_ok;
                                QKeyEvent expand_key(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
                                QCoreApplication::sendEvent(window, &expand_key);
                                presentation_ok = section->property("expanded").toBool() && presentation_ok;
                                presentation_ok = root->property("selectedStudyRow").toInt() == row && presentation_ok;
                                presentation_ok = controller.study_fingerprint() == before_bad_material && presentation_ok;
                            }
                            presentation_ok = material_value->property("text").toString() == QStringLiteral("-1") && presentation_ok;
                            presentation_ok = provenance->property("text").toString() == QStringLiteral("synthetic test assumption") && presentation_ok;
                            editing_ok = QMetaObject::invokeMethod(apply_material, "clicked", Qt::DirectConnection);
                            editing_ok = controller.study_fingerprint() == before_bad_material && editing_ok;
                            material_value->setProperty("text", QStringLiteral("2.2"));
                            editing_ok = QMetaObject::invokeMethod(apply_material, "clicked", Qt::DirectConnection) && editing_ok;
                            editing_ok = controller.study_material_ids().contains(QStringLiteral("user-material")) && editing_ok;
                            QObject* const material_selector = find_visual_object(window->contentItem(), QStringLiteral("studyMaterialSelector"));
                            editing_ok = material_selector != nullptr && editing_ok;
                            if (editing_ok) {
                                material_value->setProperty("text", QStringLiteral("3.3"));
                                material_selector->setProperty("currentIndex", 1);
                                editing_ok = QMetaObject::invokeMethod(material_selector, "activated", Qt::DirectConnection, Q_ARG(int, 1));
                                editing_ok = material_value->property("text").toString() == QStringLiteral("2.2") && editing_ok;
                            }
                        }
                        // CAD mutation makes the draft stale: headers remain navigable,
                        // but no study authoring action should remain enabled.
                        const auto before_stale = controller.study_fingerprint();
                        presentation_ok = controller.setParameter(QStringLiteral("width"), 21.0, controller.document_revision()) && presentation_ok;
                        QCoreApplication::processEvents();
                        QObject* const stale_section = find_visual_object(window->contentItem(), QStringLiteral("materialSection"));
                        QObject* const stale_header = find_visual_object(window->contentItem(), QStringLiteral("materialSectionToggle"));
                        QObject* const stale_apply = find_visual_object(window->contentItem(), QStringLiteral("applyStudyMaterialButton"));
                        presentation_ok = stale_section != nullptr && stale_header != nullptr && stale_apply != nullptr && presentation_ok;
                        if (stale_section != nullptr && stale_header != nullptr && stale_apply != nullptr) {
                            presentation_ok = stale_header->property("enabled").toBool() && presentation_ok;
                            presentation_ok = !stale_apply->property("enabled").toBool() && presentation_ok;
                            presentation_ok = QMetaObject::invokeMethod(stale_header, "clicked", Qt::DirectConnection) && presentation_ok;
                            presentation_ok = QMetaObject::invokeMethod(stale_header, "clicked", Qt::DirectConnection) && presentation_ok;
                        }
                        presentation_ok = controller.study_fingerprint() == before_stale && presentation_ok;
                        presentation_ok = controller.undoDocument() && presentation_ok;
                        QCoreApplication::processEvents();
                        presentation_ok = controller.can_edit_study() && presentation_ok;
                        frequency->setProperty("text", QStringLiteral("2300000000"));
                        editing_ok = QMetaObject::invokeMethod(apply_frequency, "clicked", Qt::DirectConnection) && editing_ok;
                        editing_ok = controller.study_minimum_frequency() == 2.3e9 && editing_ok;
                        QObject* const apply_role = find_visual_object(window->contentItem(), QStringLiteral("applyStudyRoleButton"));
                        QObject* const apply_port = find_visual_object(window->contentItem(), QStringLiteral("applyStudyPortButton"));
                        QObject* const impedance = find_visual_object(window->contentItem(), QStringLiteral("portImpedanceEditor"));
                        editing_ok = apply_role != nullptr && apply_port != nullptr && impedance != nullptr && editing_ok;
                        if (editing_ok) {
                            QObject* const role_kind = find_visual_object(window->contentItem(), QStringLiteral("studyRoleKindEditor"));
                            QObject* const role_material = find_visual_object(window->contentItem(), QStringLiteral("studyRoleMaterialEditor"));
                            if (role_kind == nullptr || role_material == nullptr) {
                                editing_ok = false;
                            } else {
                                role_kind->setProperty("currentIndex", 3);
                                role_material->setProperty("currentIndex", 1);
                                editing_ok = QMetaObject::invokeMethod(apply_role, "clicked", Qt::DirectConnection);
                                editing_ok = controller.study_definitions().contains(QStringLiteral("dielectric")) &&
                                             controller.study_definitions().contains(QStringLiteral("user-material")) && editing_ok;
                            }
                            editing_ok = controller.study_definitions().contains(QStringLiteral("region-1")) && editing_ok;
                            const auto before_rejection = controller.study_fingerprint();
                            impedance->setProperty("text", QStringLiteral("-50"));
                            editing_ok = QMetaObject::invokeMethod(apply_port, "clicked", Qt::DirectConnection) && editing_ok;
                            editing_ok = controller.study_fingerprint() == before_rejection && editing_ok;
                            impedance->setProperty("text", QStringLiteral("50"));
                            editing_ok = QMetaObject::invokeMethod(apply_port, "clicked", Qt::DirectConnection) && editing_ok;
                            editing_ok = controller.study_definitions().contains(QStringLiteral("port-1")) && editing_ok;
                            QObject* const padding = find_visual_object(window->contentItem(), QStringLiteral("boundaryPaddingEditor"));
                            QObject* const apply_boundary = find_visual_object(window->contentItem(), QStringLiteral("applyStudyBoundaryButton"));
                            QObject* const apply_monitor = find_visual_object(window->contentItem(), QStringLiteral("applyStudyMonitorButton"));
                            editing_ok = padding != nullptr && apply_boundary != nullptr && apply_monitor != nullptr && editing_ok;
                            if (editing_ok) {
                                const auto before_bad_padding = controller.study_fingerprint();
                                padding->setProperty("text", QStringLiteral("-1"));
                                editing_ok = QMetaObject::invokeMethod(apply_boundary, "clicked", Qt::DirectConnection);
                                editing_ok = controller.study_fingerprint() == before_bad_padding && editing_ok;
                                padding->setProperty("text", QStringLiteral("0.03"));
                                editing_ok = QMetaObject::invokeMethod(apply_boundary, "clicked", Qt::DirectConnection) && editing_ok;
                                editing_ok = QMetaObject::invokeMethod(apply_monitor, "clicked", Qt::DirectConnection) && editing_ok;
                                editing_ok = controller.study_setup().contains(QStringLiteral("open PML")) &&
                                             controller.study_setup().contains(QStringLiteral("S parameters")) && editing_ok;
                                // Exercise the complete Worker tab through its QML controls. The
                                // controller unit suite covers rejection paths; this smoke path
                                // proves the visible controls remain bound to the lifecycle.
                                root->setProperty("outputTab", 1);
                                QCoreApplication::processEvents();
                                QObject* const prepare_worker = find_visual_object(
                                    window->contentItem(), QStringLiteral("prepareWorkerButton"));
                                QObject* const launch_worker = find_visual_object(
                                    window->contentItem(), QStringLiteral("launchWorkerButton"));
                                QObject* const checkpoint_worker = find_visual_object(
                                    window->contentItem(), QStringLiteral("checkpointWorkerButton"));
                                QObject* const restart_worker = find_visual_object(
                                    window->contentItem(), QStringLiteral("restartWorkerButton"));
                                QObject* const collect_worker = find_visual_object(
                                    window->contentItem(), QStringLiteral("collectWorkerButton"));
                                QObject* const progress = find_visual_object(
                                    window->contentItem(), QStringLiteral("workerProgressBar"));
                                presentation_ok = prepare_worker != nullptr && launch_worker != nullptr
                                    && checkpoint_worker != nullptr && restart_worker != nullptr
                                    && collect_worker != nullptr && progress != nullptr && presentation_ok;
                                if (prepare_worker && launch_worker && checkpoint_worker
                                    && restart_worker && collect_worker && progress) {
                                    editing_ok = QMetaObject::invokeMethod(
                                        prepare_worker, "clicked", Qt::DirectConnection) && editing_ok;
                                    editing_ok = controller.can_launch_worker() && editing_ok;
                                    editing_ok = QMetaObject::invokeMethod(
                                        launch_worker, "clicked", Qt::DirectConnection) && editing_ok;
                                    editing_ok = controller.advanceWorker() && editing_ok;
                                    QCoreApplication::processEvents();
                                    presentation_ok = progress->property("value").toDouble() > 0.0
                                        && presentation_ok;
                                    editing_ok = QMetaObject::invokeMethod(
                                        checkpoint_worker, "clicked", Qt::DirectConnection) && editing_ok;
                                    const double checkpoint_progress = controller.worker_progress();
                                    editing_ok = QMetaObject::invokeMethod(
                                        restart_worker, "clicked", Qt::DirectConnection) && editing_ok;
                                    editing_ok = controller.worker_progress() == checkpoint_progress && editing_ok;
                                    while (controller.worker_running()) {
                                        editing_ok = controller.advanceWorker() && editing_ok;
                                    }
                                    QCoreApplication::processEvents();
                                    editing_ok = QMetaObject::invokeMethod(
                                        collect_worker, "clicked", Qt::DirectConnection) && editing_ok;
                                    presentation_ok = controller.has_worker_artifacts() && presentation_ok;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (controller.cad_available() && editing_ok) {
            // Exercise the actual File actions and path popup, not only the
            // controller methods: project saves must include the authored draft
            // and a verified worker restart point when one is available.
            QTemporaryDir project_directory;
            presentation_ok = controller.prepareWorkerJob() && presentation_ok;
            presentation_ok = controller.launchWorker() && presentation_ok;
            presentation_ok = controller.advanceWorker() && presentation_ok;
            presentation_ok = controller.checkpointWorker() && presentation_ok;
            const auto saved_fingerprint = controller.study_fingerprint();
            QObject* const save_action = root->findChild<QObject*>(QStringLiteral("saveProjectAction"));
            QObject* const open_action = root->findChild<QObject*>(QStringLiteral("openProjectAction"));
            QObject* const path = root->findChild<QObject*>(QStringLiteral("documentPath"));
            QObject* const confirm = root->findChild<QObject*>(QStringLiteral("confirmDocumentAction"));
            presentation_ok = project_directory.isValid() && save_action != nullptr && open_action != nullptr &&
                              path != nullptr && confirm != nullptr && presentation_ok;
            if (project_directory.isValid() && save_action && open_action && path && confirm) {
                presentation_ok = QMetaObject::invokeMethod(save_action, "triggered", Qt::DirectConnection) && presentation_ok;
                path->setProperty("text", project_directory.filePath(QStringLiteral("authored.project")));
                presentation_ok = QMetaObject::invokeMethod(confirm, "clicked", Qt::DirectConnection) && presentation_ok;
                presentation_ok = controller.newCadDocument() && presentation_ok;
                presentation_ok = !controller.has_study() && presentation_ok;
                presentation_ok = QMetaObject::invokeMethod(open_action, "triggered", Qt::DirectConnection) && presentation_ok;
                presentation_ok = QMetaObject::invokeMethod(confirm, "clicked", Qt::DirectConnection) && presentation_ok;
                presentation_ok = controller.study_fingerprint() == saved_fingerprint
                    && controller.can_edit_study()
                    && controller.worker_running()
                    && controller.has_worker_checkpoint()
                    && controller.worker_status().contains(QStringLiteral("restored"))
                    && presentation_ok;
            }
        }

        if (!presentation_ok || !editing_ok || !navigation_ok || !accessibility_ok || !initial_workspace_ok ||
            !keyboard_ok) {
            qCritical() << "Shell contract failed: navigation" << navigation_ok
                        << "accessibility" << accessibility_ok
                        << "initial workspace" << initial_workspace_ok
                        << "keyboard sequences" << key_sequences_ok
                        << "keyboard actions" << keyboard_ok
                        << "document editing" << editing_ok
                        << "presentation controls" << presentation_ok;
            return 3;
        }
        QTimer::singleShot(0, &application, &QCoreApplication::quit);
    }

    return application.exec();
}

} // namespace oep::app_shell
