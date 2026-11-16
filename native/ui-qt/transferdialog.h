//---------------------------------------------------------------------------
// transferdialog.h — WinSCP's "Transfer Settings": how files are transferred (text/binary,
// timestamps, permissions, file mask, resume, speed limit), their persistence and a one-line summary.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_TRANSFERDIALOG_H
#define WINSCP_UIQT_TRANSFERDIALOG_H

#include <QString>
#include "enginebridge.h"

class QWidget;

// Edit `settings` in a dialog; returns true if the user confirmed (settings then hold the result).
bool showTransferSettingsDialog(QWidget * parent, engine::TransferSettings & settings);

// What differs from the defaults, e.g. "Text mode · 300 KB/s · mask: *.txt"; "Default transfer settings" if nothing.
QString transferSummary(const engine::TransferSettings & settings);

// QSettings group "transfer". loadTransferSettings() returns the defaults for anything not stored.
engine::TransferSettings loadTransferSettings();
void saveTransferSettings(const engine::TransferSettings & settings);

// Checks the masks and the permissions string; on failure returns false and a message for the user.
bool validateTransferSettings(const engine::TransferSettings & settings, QString * error);

#endif
