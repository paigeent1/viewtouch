#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


class QQmlEngine;
class LayoutController;

namespace vt::ui {

// The screen speaks the language of whoever is logged in (or the store's):
// switches the translator and re-translates the QML on each change.
void followLanguage(QQmlEngine *engine, LayoutController *controller);

} // namespace vt::ui
