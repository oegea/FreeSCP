//---------------------------------------------------------------------------
// uicommon.h — small helpers shared by the Qt UI translation units.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_UICOMMON_H
#define WINSCP_UIQT_UICOMMON_H

#include <string>
#include <QString>
#include <QStringList>
#include <QDialog>
#include <QMessageBox>
#include <QPixmap>
#include <QTimer>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QAbstractButton>
#include <QPushButton>

inline QString u8(const std::string & s) { return QString::fromUtf8(s.c_str(), static_cast<qsizetype>(s.size())); }
inline std::string s8(const QString & s) { QByteArray b = s.toUtf8(); return std::string(b.constData(), static_cast<size_t>(b.size())); }

// True in headless/scripted runs (offscreen platform or one of the render/test hooks): no dialog may
// wait for a human there.
inline bool uiScripted()
{
  return qgetenv("QT_QPA_PLATFORM") == "offscreen" || !qEnvironmentVariableIsEmpty("WINSCP_SHOT")
      || !qEnvironmentVariableIsEmpty("WINSCP_DIALOG_SHOTS") || !qEnvironmentVariableIsEmpty("WINSCP_UITEST");
}

// App-branded message box: shows the FreeSCP icon (like a native macOS NSAlert) instead of a generic
// severity icon, so alerts never look empty/unbranded. Returns the clicked button.
// In scripted runs it does not block: the text goes to the diagnostics output and the default
// button (or the first "positive" one) is returned.
inline QMessageBox::StandardButton appMessage(
    QWidget * parent, const QString & title, const QString & text,
    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
    QMessageBox::StandardButton def = QMessageBox::NoButton)
{
  if (uiScripted())
  {
    fprintf(stderr, "[dialog] %s: %s\n", title.toUtf8().constData(), text.toUtf8().constData());
    if (def != QMessageBox::NoButton) return def;
    for (QMessageBox::StandardButton b : { QMessageBox::Ok, QMessageBox::Yes, QMessageBox::Save, QMessageBox::Close, QMessageBox::Cancel })
      if (buttons & b) return b;
    return QMessageBox::Ok;
  }
  QMessageBox box(parent);
  box.setWindowTitle(title);
  box.setText(text);
  box.setStandardButtons(buttons);
  if (def != QMessageBox::NoButton) box.setDefaultButton(def);
  QPixmap p(":/winscp.png");
  if (!p.isNull()) box.setIconPixmap(p.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  return static_cast<QMessageBox::StandardButton>(box.exec());
}

// exec() a dialog. Dev/test affordance: with WINSCP_DIALOG_SHOTS=<dir> the dialog is rendered to
// <dir>/<name>.png shortly after it opens and then closed - accepted if <name> is listed in
// WINSCP_DIALOG_ACCEPT (comma separated), rejected otherwise - so every dialog can be looked at and
// driven without a human.
//
// WINSCP_DIALOG_SET="<dialog>.<objectName>=<value>;..." fills in the dialog first (line edits, check
// boxes "1"/"0", combo boxes by text or index, spin boxes), so a scripted run goes through the real
// dialog - its validation included - instead of around it.
inline void applyDialogPresets(QDialog & d, const QString & name)
{
  const QStringList sets = qEnvironmentVariable("WINSCP_DIALOG_SET").split(';', Qt::SkipEmptyParts);
  for (const QString & kv : sets)
  {
    const QString key = kv.section('=', 0, 0), value = kv.section('=', 1);
    if (key.section('.', 0, 0) != name) continue;
    const QString obj = key.section('.', 1);
    if (auto * e = d.findChild<QLineEdit *>(obj)) { e->setText(value); continue; }
    if (auto * c = d.findChild<QCheckBox *>(obj)) { c->setChecked(value != "0"); continue; }
    if (auto * c = d.findChild<QComboBox *>(obj))
    { bool isNum = false; const int ix = value.toInt(&isNum); if (isNum) c->setCurrentIndex(ix); else c->setCurrentText(value); continue; }
    if (auto * sp = d.findChild<QSpinBox *>(obj)) { sp->setValue(value.toInt()); continue; }
    fprintf(stderr, "[dialog] %s: no field named %s\n", name.toUtf8().constData(), obj.toUtf8().constData());
  }
}

inline int execDialog(QDialog & d, const QString & name)
{
  const QString dir = qEnvironmentVariable("WINSCP_DIALOG_SHOTS");
  if (!dir.isEmpty() || uiScripted())      // scripted without a shots directory: just do not wait for a human
  {
    const bool accept = qEnvironmentVariable("WINSCP_DIALOG_ACCEPT").split(',').contains(name);
    QTimer::singleShot(60, &d, [&d, name] { applyDialogPresets(d, name); });
    QTimer::singleShot(250, &d, [&d, dir, name, accept] {
      if (!dir.isEmpty()) d.grab().save(dir + "/" + name + ".png");
      if (!accept) { d.reject(); return; }
      // A dialog that checks its input on OK names that button "dialogOk": click it, so the checks
      // run as they do for a user; if it refuses (the message went to the diagnostics output), cancel.
      if (auto * ok = d.findChild<QPushButton *>("dialogOk")) { ok->click(); if (d.isVisible()) d.reject(); }
      else d.done(QDialog::Accepted);
    });
  }
  return d.exec();
}

#endif
