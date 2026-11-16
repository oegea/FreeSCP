//---------------------------------------------------------------------------
// diffview.h — read-only side-by-side comparison of two files (typically the local and the remote
// copy of the same file).
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_DIFFVIEW_H
#define WINSCP_UIQT_DIFFVIEW_H

#include <QString>

class QWidget;

struct DiffSide
{
  QString title;   // shown above the pane, e.g. "report.txt — /var/www (remote)"
  QString path;    // local file holding the content (a downloaded temporary copy for a remote file)
};

// Compares the two files and shows the result: a message when they are identical, binary or too
// large to show as text; otherwise the side-by-side window. Returns a one-line summary of the
// outcome (also what scripted runs print).
QString showFileComparison(QWidget * parent, const DiffSide & left, const DiffSide & right);

#endif
