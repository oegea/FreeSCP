//---------------------------------------------------------------------------
// diffview.cpp — see diffview.h.
//
// Each pane is a read-only QPlainTextEdit holding one text block per ROW of the comparison, so the
// two panes always have the same number of blocks and scroll together; a row with no line on one
// side is an empty filler block there. A gutter shows the real line numbers.
//---------------------------------------------------------------------------
#include "diffview.h"
#include "textdiff.h"
#include "uicommon.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>
#include <vector>

namespace {

const qint64 kMaxTextBytes = 16 * 1024 * 1024;   // above this a text view is not useful (or responsive)

enum RowLook { LookSame, LookChanged, LookOnlyHere, LookMissing };

struct Colors { QColor changed, onlyHere, missing, gutterBg, gutterFg; };
Colors colorsFor(const QPalette & pal)
{
  const bool dark = pal.color(QPalette::Base).lightness() < 128;
  Colors c;
  c.changed  = dark ? QColor(92, 78, 20)  : QColor(255, 243, 191);
  c.onlyHere = dark ? QColor(28, 82, 44)  : QColor(211, 245, 219);
  c.missing  = dark ? QColor(58, 58, 58)  : QColor(236, 236, 236);
  c.gutterBg = pal.color(QPalette::Window);
  c.gutterFg = pal.color(QPalette::Disabled, QPalette::Text);
  return c;
}

// One pane: the text plus a line-number gutter and per-row background.
class Pane : public QPlainTextEdit
{
public:
  explicit Pane(QWidget * parent = nullptr) : QPlainTextEdit(parent)
  {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setPointSize(12);
    setFont(f);
    setTabStopDistance(4 * fontMetrics().horizontalAdvance(' '));
    FGutter = new Gutter(this);
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect & r, int dy) {
      if (dy) FGutter->scroll(0, dy); else FGutter->update(0, r.y(), FGutter->width(), r.height());
    });
  }
  void setRows(const QStringList & lines, const std::vector<int> & numbers, const std::vector<RowLook> & looks)
  {
    FNumbers = numbers; FLooks = looks;
    int maxNo = 1; for (int n : numbers) maxNo = qMax(maxNo, n + 1);
    FGutterWidth = 12 + fontMetrics().horizontalAdvance(QString::number(maxNo));
    setViewportMargins(FGutterWidth, 0, 0, 0);
    setPlainText(lines.join("\n"));
    layoutGutter();
    viewport()->update();
  }
  void gotoRow(int row)
  {
    QTextBlock b = document()->findBlockByNumber(row);
    if (!b.isValid()) return;
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
  }
  int firstVisibleRow() const { return firstVisibleBlock().blockNumber(); }

protected:
  void resizeEvent(QResizeEvent * e) override { QPlainTextEdit::resizeEvent(e); layoutGutter(); }
  void paintEvent(QPaintEvent * e) override
  {
    // row backgrounds first (full viewport width, also past the end of the text), then the text
    QPainter p(viewport());
    const Colors c = colorsFor(palette());
    for (QTextBlock b = firstVisibleBlock(); b.isValid(); b = b.next())
    {
      QRectF r = blockBoundingGeometry(b).translated(contentOffset());
      if (r.top() > e->rect().bottom()) break;
      const int row = b.blockNumber();
      if (row >= (int)FLooks.size()) break;
      const RowLook look = FLooks[(size_t)row];
      if (look == LookSame) continue;
      p.fillRect(QRectF(0, r.top(), viewport()->width(), r.height()),
                 look == LookChanged ? c.changed : look == LookOnlyHere ? c.onlyHere : c.missing);
    }
    p.end();
    QPlainTextEdit::paintEvent(e);
  }

private:
  class Gutter : public QWidget
  {
  public:
    explicit Gutter(Pane * pane) : QWidget(pane), FPane(pane) {}
  protected:
    void paintEvent(QPaintEvent * e) override
    {
      QPainter p(this);
      const Colors c = colorsFor(FPane->palette());
      p.fillRect(e->rect(), c.gutterBg);
      p.setPen(c.gutterFg);
      p.setFont(FPane->font());
      for (QTextBlock b = FPane->firstVisibleBlock(); b.isValid(); b = b.next())
      {
        QRectF r = FPane->blockBoundingGeometry(b).translated(FPane->contentOffset());
        if (r.top() > e->rect().bottom()) break;
        const int row = b.blockNumber();
        if (row >= (int)FPane->FNumbers.size()) break;
        const int n = FPane->FNumbers[(size_t)row];
        if (n >= 0)
          p.drawText(QRectF(0, r.top(), width() - 6, r.height()), Qt::AlignRight | Qt::AlignVCenter, QString::number(n + 1));
      }
    }
  private:
    Pane * FPane;
  };
  void layoutGutter() { QRect cr = contentsRect(); FGutter->setGeometry(cr.left(), cr.top(), FGutterWidth, cr.height()); }

  Gutter * FGutter;
  int FGutterWidth = 30;
  std::vector<int> FNumbers;     // row -> 0-based line number, -1 = filler
  std::vector<RowLook> FLooks;
};

bool readAll(const QString & path, QByteArray & data, QString * error)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) { if (error) *error = "Cannot read " + path + ": " + f.errorString(); return false; }
  data = f.readAll();
  return true;
}

// UTF-8 when the bytes are valid UTF-8, Latin-1 otherwise (never loses a byte to a replacement char).
QString decode(const std::string & line)
{
  QString s = QString::fromUtf8(line.data(), (qsizetype)line.size());
  if (s.contains(QChar::ReplacementCharacter)) s = QString::fromLatin1(line.data(), (qsizetype)line.size());
  // One row must stay one text block: characters QTextDocument treats as paragraph breaks (a lone
  // CR, line/paragraph separators, form feed, ...) are shown as visible symbols instead.
  for (QChar & c : s)
  {
    const ushort u = c.unicode();
    if (u == '\r') c = QChar(0x240D);
    else if (u == '\n') c = QChar(0x240A);
    else if (u == 0x2028 || u == 0x2029 || u == '\f' || u == '\v' || u == 0x85) c = QChar(0x00B6);
    else if (u == 0) c = QChar(0x2400);
  }
  return s;
}

} // namespace

QString showFileComparison(QWidget * parent, const DiffSide & left, const DiffSide & right)
{
  const QFileInfo li(left.path), ri(right.path);
  QString err;
  if (!li.isFile() || !ri.isFile())
  {
    QString m = "Cannot compare: " + (li.isFile() ? right.path : left.path) + " is not a file.";
    appMessage(parent, "Compare Files", m);
    return m;
  }
  const QString sizes = QString("%1: %2 bytes\n%3: %4 bytes").arg(left.title).arg(li.size()).arg(right.title).arg(ri.size());

  QByteArray la, ra;
  if (li.size() > kMaxTextBytes || ri.size() > kMaxTextBytes)
  {
    // too large to show: still answer the question "are they the same?"
    QFile fa(left.path), fb(right.path);
    bool same = (li.size() == ri.size()) && fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly);
    while (same && !fa.atEnd()) same = (fa.read(1 << 20) == fb.read(1 << 20));
    QString m = QString(same ? "The files are identical." : "The files are different.") + " (Too large to show side by side.)";
    appMessage(parent, "Compare Files", m + "\n\n" + sizes);
    return m;
  }
  if (!readAll(left.path, la, &err) || !readAll(right.path, ra, &err)) { appMessage(parent, "Compare Files", err); return err; }
  if (la == ra)
  {
    QString m = "The files are identical.";
    appMessage(parent, "Compare Files", m + "\n\n" + sizes);
    return m;
  }
  const std::string ls(la.constData(), (size_t)la.size()), rs(ra.constData(), (size_t)ra.size());
  if (textdiff::looksBinary(ls) || textdiff::looksBinary(rs))
  {
    QString m = "The files are different (binary files, not shown as text).";
    appMessage(parent, "Compare Files", m + "\n\n" + sizes);
    return m;
  }

  bool lcrlf = false, rcrlf = false;
  const std::vector<std::string> ll = textdiff::splitLines(ls, &lcrlf), rl = textdiff::splitLines(rs, &rcrlf);

  QDialog d(parent);
  d.setWindowTitle("Compare Files");
  d.resize(1100, 680);
  d.setWindowFlag(Qt::WindowMaximizeButtonHint, true);
  auto * lay = new QVBoxLayout(&d);

  auto * bar = new QHBoxLayout;
  auto * prevBtn = new QPushButton("\xE2\x96\xB2 Previous difference");
  auto * nextBtn = new QPushButton("\xE2\x96\xBC Next difference");
  prevBtn->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7)); nextBtn->setShortcut(QKeySequence(Qt::Key_F7));
  prevBtn->setToolTip("Shift+F7"); nextBtn->setToolTip("F7");
  auto * cbSpace = new QCheckBox("Ignore trailing &whitespace");
  auto * cbCase = new QCheckBox("Ignore &case");
  auto * summary = new QLabel; summary->setTextFormat(Qt::PlainText);
  bar->addWidget(prevBtn); bar->addWidget(nextBtn); bar->addSpacing(12);
  bar->addWidget(cbSpace); bar->addWidget(cbCase); bar->addStretch(1); bar->addWidget(summary);
  lay->addLayout(bar);

  auto * split = new QSplitter(Qt::Horizontal);
  auto makeSide = [&](const QString & title, Pane *& pane) {
    auto * w = new QWidget; auto * v = new QVBoxLayout(w); v->setContentsMargins(0, 0, 0, 0); v->setSpacing(2);
    auto * t = new QLabel(title); t->setTextFormat(Qt::PlainText);
    t->setStyleSheet("padding:3px 6px; font-weight:bold; background:palette(midlight);");
    t->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pane = new Pane;
    v->addWidget(t); v->addWidget(pane, 1);
    split->addWidget(w);
  };
  Pane * lp = nullptr; Pane * rp = nullptr;
  makeSide(left.title, lp); makeSide(right.title, rp);
  split->setSizes({ 550, 550 });
  lay->addWidget(split, 1);

  auto * legend = new QLabel; legend->setTextFormat(Qt::RichText);
  {
    const Colors c = colorsFor(d.palette());
    auto chip = [](const QColor & col, const char * text) {
      return QString("<span style='background:%1'>&nbsp;&nbsp;&nbsp;&nbsp;</span> %2 &nbsp;&nbsp;").arg(col.name(), text);
    };
    QString eol;
    if (lcrlf != rcrlf)
      eol = QString(" &nbsp; Line endings differ (%1 vs %2) \xE2\x80\x94 ignored in the comparison.").arg(lcrlf ? "CR/LF" : "LF", rcrlf ? "CR/LF" : "LF");
    legend->setText(chip(c.changed, "changed") + chip(c.onlyHere, "only on this side") + chip(c.missing, "missing on this side") + eol);
  }
  lay->addWidget(legend);

  // synchronized scrolling, both directions
  bool syncing = false;
  auto link = [&](QScrollBar * a, QScrollBar * b) {
    QObject::connect(a, &QScrollBar::valueChanged, &d, [&syncing, b](int v) { if (syncing) return; syncing = true; b->setValue(v); syncing = false; });
  };
  link(lp->verticalScrollBar(), rp->verticalScrollBar()); link(rp->verticalScrollBar(), lp->verticalScrollBar());
  link(lp->horizontalScrollBar(), rp->horizontalScrollBar()); link(rp->horizontalScrollBar(), lp->horizontalScrollBar());

  std::vector<int> blockStarts;   // row index where each difference block starts
  int blocks = 0;
  auto rebuild = [&] {
    textdiff::Options o;
    o.ignoreTrailingSpace = cbSpace->isChecked(); o.ignoreCase = cbCase->isChecked();
    const std::vector<textdiff::Row> rows = textdiff::diff(ll, rl, o);
    QStringList ltext, rtext; std::vector<int> lno, rno; std::vector<RowLook> llook, rlook;
    blockStarts.clear();
    bool in = false;
    for (size_t i = 0; i < rows.size(); ++i)
    {
      const textdiff::Row & r = rows[i];
      ltext << (r.left >= 0 ? decode(ll[(size_t)r.left]) : QString());
      rtext << (r.right >= 0 ? decode(rl[(size_t)r.right]) : QString());
      lno.push_back(r.left); rno.push_back(r.right);
      switch (r.kind)
      {
        case textdiff::Row::Same:      llook.push_back(LookSame); rlook.push_back(LookSame); break;
        case textdiff::Row::Changed:   llook.push_back(LookChanged); rlook.push_back(LookChanged); break;
        case textdiff::Row::LeftOnly:  llook.push_back(LookOnlyHere); rlook.push_back(LookMissing); break;
        case textdiff::Row::RightOnly: llook.push_back(LookMissing); rlook.push_back(LookOnlyHere); break;
      }
      if (r.kind != textdiff::Row::Same) { if (!in) { blockStarts.push_back((int)i); in = true; } }
      else in = false;
    }
    blocks = (int)blockStarts.size();
    const int keep = lp->firstVisibleRow();
    lp->setRows(ltext, lno, llook); rp->setRows(rtext, rno, rlook);
    lp->verticalScrollBar()->setValue(keep);
    summary->setText(blocks == 0 ? QStringLiteral("No differences with these options")
                                 : QString("%1 difference%2 \xC2\xB7 %3 / %4 lines").arg(blocks).arg(blocks == 1 ? "" : "s").arg(ll.size()).arg(rl.size()));
    prevBtn->setEnabled(blocks > 0); nextBtn->setEnabled(blocks > 0);
  };
  auto jump = [&](bool forward) {
    if (blockStarts.empty()) return;
    const int cur = lp->textCursor().blockNumber();
    int target = -1;
    if (forward) { for (int s : blockStarts) if (s > cur) { target = s; break; } if (target < 0) target = blockStarts.front(); }
    else { for (auto it = blockStarts.rbegin(); it != blockStarts.rend(); ++it) if (*it < cur) { target = *it; break; } if (target < 0) target = blockStarts.back(); }
    lp->gotoRow(target); rp->gotoRow(target);
  };
  QObject::connect(nextBtn, &QPushButton::clicked, &d, [&] { jump(true); });
  QObject::connect(prevBtn, &QPushButton::clicked, &d, [&] { jump(false); });
  QObject::connect(cbSpace, &QCheckBox::toggled, &d, [&](bool) { rebuild(); });
  QObject::connect(cbCase, &QCheckBox::toggled, &d, [&](bool) { rebuild(); });

  rebuild();
  if (!blockStarts.empty()) { lp->gotoRow(blockStarts.front()); rp->gotoRow(blockStarts.front()); }
  const QString result = QString("The files are different: %1 difference%2 (%3 / %4 lines).")
                           .arg(blocks).arg(blocks == 1 ? "" : "s").arg(ll.size()).arg(rl.size());
  execDialog(d, "diff");
  return result;
}
