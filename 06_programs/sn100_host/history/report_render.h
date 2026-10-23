#ifndef HISTORY_REPORT_RENDER_H
#define HISTORY_REPORT_RENDER_H

#include "report_document.h"

class QtRPT;

// The original embedded template is the only allowed report source. Bindings
// own a value snapshot until the engine is destroyed or prepared again.
// Caller supplies a fresh engine on its GUI thread and captures renderError
// during preview/PDF rendering; successful preparation alone is not output.
bool prepareHistoryReport(QtRPT *engine, const HistoryPrintDocument &document, QString *error);

#endif
