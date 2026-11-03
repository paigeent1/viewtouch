#pragma once

#include "print/document.hh"

#include <QImage>

namespace vt::print {

// A ticket as a picture of the paper: white, black monospaced text laid out
// exactly as the printer would (renderText's rows), bold and big text as
// printed. Pictures on the ticket (the logo) are left out. `widthChars`:
// the paper's characters a line (42 on 80 mm receipts).
QImage ticketImage(const Document &doc, int widthChars);

} // namespace vt::print
