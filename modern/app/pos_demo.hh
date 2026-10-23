#pragma once
#ifdef Q_MOC_RUN
#define _GLIBCXX_CONCEPT 1
#define _GLIBCXX_RANGES 1
#define __cpp_lib_concepts 0
#endif


#include "app/pos_service.hh"

namespace vt::app {

// Fills a store with no sales with demo history (see pos_demo.cpp). Returns
// what was added, or why not ("" never).
QString fillDemoData(PosService &pos, std::int64_t realNow);

} // namespace vt::app
