//---------------------------------------------------------------------------
// editor.cpp — see editor.h.
//---------------------------------------------------------------------------
#include "editor.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QStringConverter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "icons.h"
#include "uicommon.h"

using editorlogic::Encoding;
using editorlogic::Eol;

EditorWindow * EditorWindow::create(QWidget * parent, const QString & title, const QByteArray & bytes, QString * refusal)
{
  auto * w = new EditorWindow(parent, title);
  if (!w->load(bytes, refusal)) { delete w; return nullptr; }
  return w;
}

EditorWindow::EditorWindow(QWidget * parent, const QString & title) : QMainWindow(parent), FBase(title)
{
  setAttribute(Qt::WA_DeleteOnClose);
  auto * central = new QWidget;
  auto * lay = new QVBoxLayout(central);
  lay->setContentsMargins(0, 0, 0, 0); lay->setSpacing(0);
  FEdit = new QPlainTextEdit;
  FEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  { QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont); if (f.pointSizeF() < 11) f.setPointSizeF(11); FEdit->setFont(f); }
  FEdit->setTabStopDistance(8 * FEdit->fontMetrics().horizontalAdvance(' '));
  FEdit->setFrameShape(QFrame::NoFrame);
  lay->addWidget(FEdit, 1);

  // --- find / replace bar (hidden until asked for) ---
  FFindBar = new QWidget;
  auto * fl = new QVBoxLayout(FFindBar);
  fl->setContentsMargins(6, 4, 6, 4); fl->setSpacing(4);
  auto * row1 = new QHBoxLayout;
  FFindEdit = new QLineEdit; FFindEdit->setObjectName("find"); FFindEdit->setPlaceholderText("Find");
  FCase = new QCheckBox("&Case sensitive");
  auto * nextBtn = new QPushButton("Find &Next"); auto * prevBtn = new QPushButton("Find &Previous");
  auto * closeBtn = new QToolButton; closeBtn->setIcon(appIcon("delete")); closeBtn->setAutoRaise(true); closeBtn->setToolTip("Close (Esc)");
  row1->addWidget(new QLabel("Find:")); row1->addWidget(FFindEdit, 1); row1->addWidget(FCase); row1->addWidget(nextBtn); row1->addWidget(prevBtn); row1->addWidget(closeBtn);
  fl->addLayout(row1);
  FReplaceRow = new QWidget;
  auto * row2 = new QHBoxLayout(FReplaceRow); row2->setContentsMargins(0, 0, 0, 0);
  FReplaceEdit = new QLineEdit; FReplaceEdit->setObjectName("replace"); FReplaceEdit->setPlaceholderText("Replace with");
  auto * replBtn = new QPushButton("&Replace"); auto * replAllBtn = new QPushButton("Replace &All");
  row2->addWidget(new QLabel("Replace:")); row2->addWidget(FReplaceEdit, 1); row2->addWidget(replBtn); row2->addWidget(replAllBtn);
  fl->addWidget(FReplaceRow);
  FFindBar->setStyleSheet("QWidget#findbar { border-top:1px solid palette(mid); }"); FFindBar->setObjectName("findbar");
  FFindBar->hide();
  lay->addWidget(FFindBar);
  setCentralWidget(central);

  auto notFound = [this](const QString & what) { statusBar()->showMessage(QString("\"%1\" was not found").arg(what), 4000); };
  connect(nextBtn, &QPushButton::clicked, this, [this, notFound] { if (!findNext(FFindEdit->text(), FCase->isChecked())) notFound(FFindEdit->text()); });
  connect(prevBtn, &QPushButton::clicked, this, [this, notFound] { if (!findNext(FFindEdit->text(), FCase->isChecked(), true)) notFound(FFindEdit->text()); });
  connect(FFindEdit, &QLineEdit::returnPressed, this, [this, notFound] { if (!findNext(FFindEdit->text(), FCase->isChecked())) notFound(FFindEdit->text()); });
  connect(closeBtn, &QToolButton::clicked, this, [this] { FFindBar->hide(); FEdit->setFocus(); });
  connect(replBtn, &QPushButton::clicked, this, [this, notFound] {
    // replace the current match (if the selection is one), then move to the next
    QTextCursor c = FEdit->textCursor();
    const Qt::CaseSensitivity cs = FCase->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (c.hasSelection() && QString::compare(c.selectedText(), FFindEdit->text(), cs) == 0) c.insertText(FReplaceEdit->text());
    if (!findNext(FFindEdit->text(), FCase->isChecked())) notFound(FFindEdit->text());
  });
  connect(replAllBtn, &QPushButton::clicked, this, [this] {
    const int n = replaceAll(FFindEdit->text(), FReplaceEdit->text(), FCase->isChecked());
    statusBar()->showMessage(QString("%1 occurrence(s) replaced").arg(n), 4000);
  });

  // --- toolbar, in the order of WinSCP's editor ---
  auto * tb = addToolBar("Editor"); tb->setMovable(false); tb->setIconSize(QSize(16, 16));
  tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  auto add = [&](const QString & icon, const QString & text, const QKeySequence & key, const std::function<void()> & fn, bool showText = false) {
    QAction * a = tb->addAction(appIcon(icon), text);
    if (!key.isEmpty()) { a->setShortcut(key); a->setToolTip(text + " (" + key.toString(QKeySequence::NativeText) + ")"); }
    if (!showText) a->setPriority(QAction::LowPriority);      // icon only
    connect(a, &QAction::triggered, this, fn);
    return a;
  };
  add("save", "Save", QKeySequence::Save, [this] { save(); }, true);
  add("refresh", "Reload", QKeySequence(Qt::CTRL | Qt::Key_R), [this] { reload(); });
  tb->addSeparator();
  add("cut", "Cut", QKeySequence::Cut, [this] { FEdit->cut(); });
  add("copy", "Copy", QKeySequence::Copy, [this] { FEdit->copy(); });
  add("paste", "Paste", QKeySequence::Paste, [this] { FEdit->paste(); });
  tb->addSeparator();
  QAction * undo = add("undo", "Undo", QKeySequence::Undo, [this] { FEdit->undo(); });
  QAction * redo = add("redo", "Redo", QKeySequence::Redo, [this] { FEdit->redo(); });
  undo->setEnabled(false); redo->setEnabled(false);
  connect(FEdit, &QPlainTextEdit::undoAvailable, undo, &QAction::setEnabled);
  connect(FEdit, &QPlainTextEdit::redoAvailable, redo, &QAction::setEnabled);
  tb->addSeparator();
  add("find", "Find", QKeySequence::Find, [this] { showFindBar(false); }, true);
  add("replace", "Replace", QKeySequence(Qt::CTRL | Qt::Key_H), [this] { showFindBar(true); });
  add("forward", "Find Next", QKeySequence(Qt::Key_F3), [this, notFound] {
    if (FFindEdit->text().isEmpty()) showFindBar(false);
    else if (!findNext(FFindEdit->text(), FCase->isChecked())) notFound(FFindEdit->text());
  });
  add("goto", "Go to Line", QKeySequence(Qt::CTRL | Qt::Key_G), [this] {
    bool ok = false;
    const int n = QInputDialog::getInt(this, "Go to line", "Line number:", FEdit->textCursor().blockNumber() + 1, 1, qMax(1, FEdit->blockCount()), 1, &ok);
    if (ok) goToLine(n);
  });
  tb->addSeparator();
  QAction * wrap = tb->addAction(appIcon("wrap"), "Word Wrap"); wrap->setCheckable(true);
  wrap->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W)); wrap->setToolTip("Word wrap"); wrap->setPriority(QAction::LowPriority);
  connect(wrap, &QAction::toggled, this, [this](bool w) { FEdit->setLineWrapMode(w ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap); });
  auto * esc = new QAction(this); esc->setShortcut(Qt::Key_Escape);
  connect(esc, &QAction::triggered, this, [this] { if (FFindBar->isVisible()) { FFindBar->hide(); FEdit->setFocus(); } });
  addAction(esc);

  // --- status bar: position on the left, encoding and line endings on the right ---
  FPos = new QLabel; FEnc = new QLabel;
  statusBar()->addWidget(FPos, 1);
  statusBar()->addPermanentWidget(FEnc);
  connect(FEdit, &QPlainTextEdit::cursorPositionChanged, this, [this] { updateStatus(); });
  connect(FEdit->document(), &QTextDocument::modificationChanged, this, [this](bool) { updateTitle(); });
  resize(820, 600);
}

bool EditorWindow::load(const QByteArray & bytes, QString * refusal)
{
  const std::string raw(bytes.constData(), static_cast<size_t>(bytes.size()));
  const editorlogic::FileKind k = editorlogic::detect(raw);
  if (k.binary)
  {
    if (refusal) *refusal = "This does not look like a text file. The editor only opens text files, because saving would change a binary file.";
    return false;
  }
  const QByteArray body = bytes.mid(static_cast<qsizetype>(k.bomSize));
  QString text;
  switch (k.encoding)
  {
    case Encoding::Utf8: case Encoding::Utf8Bom: text = QString::fromUtf8(body); break;
    case Encoding::Latin1: text = QString::fromLatin1(body); break;
    case Encoding::Utf16LE: { QStringDecoder d(QStringConverter::Utf16LE); text = d(body); break; }
    case Encoding::Utf16BE: { QStringDecoder d(QStringConverter::Utf16BE); text = d(body); break; }
  }
  text.replace("\r\n", "\n"); text.replace('\r', '\n');
  FKind = k;
  FEdit->setPlainText(text);
  FEdit->document()->setModified(false);
  updateTitle(); updateStatus();
  return true;
}

QString EditorWindow::text() const
{
  // toRawText(): toPlainText() turns no-break spaces into spaces, i.e. it would change the file.
  QString t = FEdit->document()->toRawText();
  t.replace(QChar::ParagraphSeparator, QChar('\n'));
  return t;
}

void EditorWindow::setText(const QString & text)
{
  QTextCursor c(FEdit->document());
  c.beginEditBlock(); c.select(QTextCursor::Document); c.insertText(text); c.endEditBlock();
}

bool EditorWindow::isModified() const { return FEdit->document()->isModified(); }

QByteArray EditorWindow::encoded(bool * cancelled)
{
  if (cancelled) *cancelled = false;
  QString t = text();
  if (FKind.encoding == Encoding::Latin1)
  {
    bool fits = true;
    for (const QChar ch : t) if (ch.unicode() > 0xFF) { fits = false; break; }
    if (!fits)
    {
      // typed or pasted text the file's encoding cannot hold
      const auto r = appMessage(this, "Save", "The file is encoded in ISO-8859-1, but the text now contains characters that encoding "
                                              "does not have.\n\nSave it as UTF-8 instead?", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
      if (r != QMessageBox::Yes) { if (cancelled) *cancelled = true; return QByteArray(); }
      FKind.encoding = Encoding::Utf8;
    }
  }
  if (FKind.eol == Eol::CrLf) t.replace("\n", "\r\n");
  else if (FKind.eol == Eol::Cr) t.replace('\n', '\r');
  QByteArray out;
  switch (FKind.encoding)
  {
    case Encoding::Utf8: out = t.toUtf8(); break;
    case Encoding::Utf8Bom: out = QByteArray("\xEF\xBB\xBF") + t.toUtf8(); break;
    case Encoding::Latin1: out = t.toLatin1(); break;
    case Encoding::Utf16LE: { QStringEncoder e(QStringConverter::Utf16LE); out = QByteArray("\xFF\xFE", 2) + QByteArray(e(t)); break; }
    case Encoding::Utf16BE: { QStringEncoder e(QStringConverter::Utf16BE); out = QByteArray("\xFE\xFF", 2) + QByteArray(e(t)); break; }
  }
  return out;
}

bool EditorWindow::save()
{
  bool cancelled = false;
  const QByteArray bytes = encoded(&cancelled);
  if (cancelled || !onSave || !onSave(bytes)) return false;
  FEdit->document()->setModified(false);
  updateTitle(); updateStatus();
  return true;
}

bool EditorWindow::reload()
{
  if (!onReload) return false;
  if (isModified() && appMessage(this, "Reload", "Discard the changes and read the file again?",
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) return false;
  QByteArray bytes; QString error;
  if (!onReload(bytes, &error)) { appMessage(this, "Reload", "The file could not be read again:\n\n" + error); return false; }
  const int line = FEdit->textCursor().blockNumber() + 1;
  QString refusal;
  if (!load(bytes, &refusal)) { appMessage(this, "Reload", refusal); return false; }
  goToLine(line);
  statusBar()->showMessage("Reloaded", 3000);
  return true;
}

bool EditorWindow::findNext(const QString & what, bool caseSensitive, bool backward)
{
  if (what.isEmpty()) return false;
  QTextDocument::FindFlags flags;
  if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
  if (backward) flags |= QTextDocument::FindBackward;
  if (FEdit->find(what, flags)) return true;
  // wrap around
  QTextCursor saved = FEdit->textCursor();
  FEdit->moveCursor(backward ? QTextCursor::End : QTextCursor::Start);
  if (FEdit->find(what, flags)) return true;
  FEdit->setTextCursor(saved);
  return false;
}

int EditorWindow::replaceAll(const QString & what, const QString & with, bool caseSensitive)
{
  if (what.isEmpty()) return 0;
  QTextDocument::FindFlags flags;
  if (caseSensitive) flags |= QTextDocument::FindCaseSensitively;
  int n = 0;
  QTextCursor edit(FEdit->document());
  edit.beginEditBlock();                       // one undo step
  QTextCursor c(FEdit->document());
  for (;;)
  {
    c = FEdit->document()->find(what, c, flags);
    if (c.isNull()) break;
    c.insertText(with);                        // the cursor ends after the inserted text: no endless loop when `with` contains `what`
    ++n;
  }
  edit.endEditBlock();
  return n;
}

void EditorWindow::goToLine(int line)
{
  const QTextBlock b = FEdit->document()->findBlockByNumber(qBound(1, line, qMax(1, FEdit->blockCount())) - 1);
  if (!b.isValid()) return;
  QTextCursor c(b);
  FEdit->setTextCursor(c); FEdit->centerCursor(); FEdit->setFocus();
}

void EditorWindow::showFindBar(bool withReplace)
{
  FReplaceRow->setVisible(withReplace);
  FFindBar->show();
  const QString sel = FEdit->textCursor().selectedText();
  if (!sel.isEmpty() && !sel.contains(QChar::ParagraphSeparator)) FFindEdit->setText(sel);
  FFindEdit->setFocus(); FFindEdit->selectAll();
}

QString EditorWindow::info() const
{
  const QTextCursor c = FEdit->textCursor();
  return QString("encoding=%1 eol=%2 lines=%3 modified=%4 line=%5 column=%6")
    .arg(editorlogic::encodingName(FKind.encoding), editorlogic::eolName(FKind.eol)).arg(FEdit->blockCount())
    .arg(isModified() ? 1 : 0).arg(c.blockNumber() + 1).arg(c.positionInBlock() + 1);
}

void EditorWindow::updateTitle() { setWindowTitle((isModified() ? "* " : "") + FBase); }

void EditorWindow::updateStatus()
{
  const QTextCursor c = FEdit->textCursor();
  QString ch;
  const QString block = c.block().text();
  if (c.positionInBlock() < block.size())
  {
    const uint cp = block.at(c.positionInBlock()).unicode();
    ch = QString("   Character: %1 (0x%2)").arg(cp).arg(QString::number(cp, 16).toUpper().rightJustified(2, QChar('0')));
  }
  FPos->setText(QString("Line: %1/%2   Column: %3%4").arg(c.blockNumber() + 1).arg(FEdit->blockCount()).arg(c.positionInBlock() + 1).arg(ch));
  FEnc->setText(QString("Encoding: %1   %2").arg(editorlogic::encodingName(FKind.encoding), editorlogic::eolName(FKind.eol)));
}

void EditorWindow::closeEvent(QCloseEvent * e)
{
  if (isModified())
  {
    const auto r = appMessage(this, "Editor", QString("The file \"%1\" was modified. Save the changes?").arg(FBase.section("  ", 0, 0)),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (r == QMessageBox::Cancel) { e->ignore(); return; }
    if (r == QMessageBox::Save && !save()) { e->ignore(); return; }
  }
  e->accept();
}
