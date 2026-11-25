//---------------------------------------------------------------------------
// editor.h — the internal text editor (WinSCP's "Internal editor"): edits a file's text and gives
// back the same kind of file - encoding, byte-order mark and line endings are detected when it is
// opened and kept when it is saved (editorlogic.h). Find / replace, go to line, reload, word wrap.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_EDITOR_H
#define WINSCP_UIQT_EDITOR_H

#include <QByteArray>
#include <QMainWindow>
#include <QString>
#include <functional>
#include "editorlogic.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QWidget;

class EditorWindow : public QMainWindow
{
public:
  // Write the file (and, for a remote file, upload it). Returns true when the bytes are safe.
  std::function<bool(const QByteArray &)> onSave;
  // Read the file again (for a remote file: download it again). False + message on failure.
  std::function<bool(QByteArray &, QString *)> onReload;

  // A window for `bytes`, or nullptr with `refusal` set when they are not text (the editor would
  // damage a binary file by saving it).
  static EditorWindow * create(QWidget * parent, const QString & title, const QByteArray & bytes, QString * refusal);

  bool save();
  bool reload();
  bool isModified() const;
  QString text() const;                       // with "\n" line ends
  void setText(const QString & text);         // as if typed: marks the document modified
  bool findNext(const QString & what, bool caseSensitive, bool backward = false);
  int replaceAll(const QString & what, const QString & with, bool caseSensitive);
  void goToLine(int line);
  void showFindBar(bool withReplace);
  // "encoding=UTF-8 eol=LF lines=3 modified=0 line=1 column=1" - the status bar's content, for scripted checks
  QString info() const;
  editorlogic::FileKind kind() const { return FKind; }

protected:
  void closeEvent(QCloseEvent * e) override;

private:
  EditorWindow(QWidget * parent, const QString & title);
  bool load(const QByteArray & bytes, QString * refusal);
  QByteArray encoded(bool * cancelled);
  void updateTitle();
  void updateStatus();

  QString FBase;
  QPlainTextEdit * FEdit = nullptr;
  QWidget * FFindBar = nullptr;
  QLineEdit * FFindEdit = nullptr, * FReplaceEdit = nullptr;
  QWidget * FReplaceRow = nullptr;
  QCheckBox * FCase = nullptr;
  QLabel * FPos = nullptr, * FEnc = nullptr;
  editorlogic::FileKind FKind;
};

#endif
