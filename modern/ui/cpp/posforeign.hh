#pragma once
#include <QQmlEngine>
#include <QtQml>
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


// Registers the app-layer PosSession (local PosService or a remote terminal)
// with QML without making app/ depend on QtQml. Widgets see it as the type
// `PosService`.

#include "app/pos_session.hh"

#include <QtQml/qqmlregistration.h>

struct PosServiceForeign {
    Q_GADGET
    QML_FOREIGN(vt::app::PosSession)
    QML_NAMED_ELEMENT(PosService)
    QML_UNCREATABLE("Provided by LayoutController.pos")
};
