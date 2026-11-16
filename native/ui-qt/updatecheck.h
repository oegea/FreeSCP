//---------------------------------------------------------------------------
// updatecheck.h — "a newer version is available" notice.
//
// The ONLY network request FreeSCP makes on its own: one GET for the latest release of the project
// (GitHub's public API), at most once a day, with no identifier, no version and no system
// information in it. It can be turned off (Preferences > Environment). Nothing is downloaded or
// installed: "Update" opens the releases page in the browser.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_UPDATECHECK_H
#define WINSCP_UIQT_UPDATECHECK_H

class QWidget;

namespace updates {

bool enabled();                 // Preferences switch (default: on)
void setEnabled(bool on);

// automatic (manual == false): only if enabled and not already checked in the last 24 hours; says
// nothing unless a newer version exists. manual: always asks and always answers.
void check(QWidget * parent, bool manual);

}

#endif
