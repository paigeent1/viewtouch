#include <catch2/catch_session.hpp>

#include "app/i18n.hh"

#include <QGuiApplication>
#include <QQuickStyle>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(ViewTouchPlugin)
extern void qml_register_types_ViewTouch();

// Qt plugins (SQLite driver) need an application object, and the UI tests
// need a GUI one. Default to the offscreen platform so tests run headless.
int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // Warnings (QML errors among them) on the screen, not the system journal.
    qputenv("QT_FORCE_STDERR_LOGGING", "1");
    QGuiApplication app(argc, argv);
    // As the app's main.cpp does: keep the static ViewTouch QML module (Main.qml and the rest) from being
    // dropped by the linker, or loadFromModule("ViewTouch", "Main") finds no type named Main.
    qml_register_types_ViewTouch();
    Q_INIT_RESOURCE(qmake_ViewTouch);
    Q_INIT_RESOURCE(vtm_ui_raw_qml_0);
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    vt::i18n::install();   // as the app does: English plurals, Spanish for those who use it
    return Catch::Session().run(argc, argv);
}
