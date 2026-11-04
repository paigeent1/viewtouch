#pragma once

#include <QFile>
#include <QQmlApplicationEngine>
#include <QString>
#include <QUrl>

namespace vt::test {

// Loads a ViewTouch screen (Main, CustomerDisplayWindow...) the way the app's main.cpp does
// (vtmLoadModuleQml): this build puts the QML module under qrc:/ViewTouch (no QTP0001 policy), where
// loadFromModule("ViewTouch", "Main") finds "no type named Main" and every screen test fails.
inline void loadViewTouchQml(QQmlApplicationEngine &engine, const char *name)
{
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    const QString file = QString::fromLatin1(name) + QStringLiteral(".qml");
    for (const QString &dir : {QStringLiteral(":/qt/qml/ViewTouch/ui/qml/"), QStringLiteral(":/ViewTouch/ui/qml/")}) {
        if (QFile::exists(dir + file)) {
            engine.load(QUrl(QStringLiteral("qrc") + dir + file));
            return;
        }
    }
    engine.loadFromModule("ViewTouch", name);
}

} // namespace vt::test
