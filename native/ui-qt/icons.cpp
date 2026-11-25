//---------------------------------------------------------------------------
// icons.cpp — see icons.h. Every icon is a function drawing on a 16x16 unit square.
//---------------------------------------------------------------------------
#include "icons.h"

#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPixmapCache>
#include <cmath>
#include <functional>

namespace {

const QColor kBlue(0x2f, 0x6f, 0xd0), kBlueDark(0x1f, 0x4f, 0xa0), kGreen(0x3a, 0x9d, 0x3a), kGreenDark(0x23, 0x70, 0x23);
const QColor kRed(0xd2, 0x3c, 0x3c), kRedDark(0x9a, 0x22, 0x22), kYellow(0xf3, 0xc6, 0x4b), kYellowDark(0xb0, 0x82, 0x18);
const QColor kGray(0x6a, 0x6e, 0x75), kGrayLight(0xc9, 0xcd, 0xd3), kPaper(0xfb, 0xfb, 0xfb), kOrange(0xe9, 0x8a, 0x25);

typedef QPainter P;

QPen pen(const QColor & c, qreal w = 1.3) { QPen p(c, w); p.setCapStyle(Qt::RoundCap); p.setJoinStyle(Qt::RoundJoin); return p; }

QPolygonF poly(std::initializer_list<QPointF> pts) { return QPolygonF(QList<QPointF>(pts)); }

// A solid arrow from (x1,y1) to (x2,y2): a shaft and a triangular head.
void arrow(P & p, qreal x1, qreal y1, qreal x2, qreal y2, const QColor & c, qreal shaft = 2.2, qreal head = 4.6)
{
  const qreal dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
  if (len <= 0) return;
  const qreal ux = dx / len, uy = dy / len, nx = -uy, ny = ux;
  const qreal hl = qMin(head, len);                      // head length
  const QPointF base(x2 - ux * hl, y2 - uy * hl);
  QPolygonF pg;
  pg << QPointF(x1 + nx * shaft / 2, y1 + ny * shaft / 2) << QPointF(base.x() + nx * shaft / 2, base.y() + ny * shaft / 2)
     << QPointF(base.x() + nx * head * 0.75, base.y() + ny * head * 0.75) << QPointF(x2, y2)
     << QPointF(base.x() - nx * head * 0.75, base.y() - ny * head * 0.75) << QPointF(base.x() - nx * shaft / 2, base.y() - ny * shaft / 2)
     << QPointF(x1 - nx * shaft / 2, y1 - ny * shaft / 2);
  p.setPen(Qt::NoPen); p.setBrush(c); p.drawPolygon(pg);
}

void folder(P & p, qreal x = 1, qreal y = 3, qreal w = 14, qreal h = 10.5)
{
  QPainterPath back;
  back.moveTo(x, y + 1.2); back.lineTo(x + w * 0.38, y + 1.2); back.lineTo(x + w * 0.48, y + 2.8);
  back.lineTo(x + w, y + 2.8); back.lineTo(x + w, y + h); back.lineTo(x, y + h); back.closeSubpath();
  p.setPen(pen(kYellowDark, 1.0)); p.setBrush(kYellow.darker(112)); p.drawPath(back);
  p.setBrush(kYellow); p.drawRoundedRect(QRectF(x, y + 4.2, w, h - 4.2), 0.8, 0.8);
}

void page(P & p, qreal x = 3, qreal y = 1.2, qreal w = 10, qreal h = 13.6)
{
  const qreal f = w * 0.34;   // folded corner
  QPainterPath pg;
  pg.moveTo(x, y); pg.lineTo(x + w - f, y); pg.lineTo(x + w, y + f); pg.lineTo(x + w, y + h); pg.lineTo(x, y + h); pg.closeSubpath();
  p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawPath(pg);
  p.setBrush(kGrayLight); p.drawPolygon(poly({ { x + w - f, y }, { x + w, y + f }, { x + w - f, y + f } }));
}

void lines(P & p, qreal x1, qreal x2, std::initializer_list<qreal> ys, const QColor & c = kGray, qreal w = 1.1)
{
  p.setPen(pen(c, w));
  for (qreal y : ys) p.drawLine(QPointF(x1, y), QPointF(x2, y));
}

void plusBadge(P & p, qreal cx, qreal cy, const QColor & c = kGreen, qreal r = 3.0)
{
  p.setPen(pen(QColor(255, 255, 255), 3.2)); p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy)); p.drawLine(QPointF(cx, cy - r), QPointF(cx, cy + r));
  p.setPen(pen(c, 2.0)); p.drawLine(QPointF(cx - r, cy), QPointF(cx + r, cy)); p.drawLine(QPointF(cx, cy - r), QPointF(cx, cy + r));
}

// An arc with an arrow head at its end (angles in degrees, counter-clockwise from 3 o'clock).
void arcArrow(P & p, qreal cx, qreal cy, qreal r, qreal start, qreal span, const QColor & c)
{
  p.setPen(pen(c, 1.9)); p.setBrush(Qt::NoBrush);
  p.drawArc(QRectF(cx - r, cy - r, 2 * r, 2 * r), int(start * 16), int(span * 16));
  const qreal a = (start + span) * M_PI / 180.0;
  const QPointF tip(cx + r * std::cos(a), cy - r * std::sin(a));
  const qreal dir = (span >= 0) ? 1.0 : -1.0;
  const QPointF t(-std::sin(a) * dir, -std::cos(a) * dir);   // tangent, in screen coordinates
  const QPointF n(std::cos(a), -std::sin(a));
  p.setPen(Qt::NoPen); p.setBrush(c);
  p.drawPolygon(poly({ tip + t * 2.6, tip - t * 0.6 + n * 2.3, tip - t * 0.6 - n * 2.3 }));
}

typedef std::function<void(P &)> Draw;

const QHash<QString, Draw> & table()
{
  static const QHash<QString, Draw> t = {
    { "back",    [](P & p) { arrow(p, 13.5, 8, 2.5, 8, kBlue, 3.4, 6.0); } },
    { "forward", [](P & p) { arrow(p, 2.5, 8, 13.5, 8, kBlue, 3.4, 6.0); } },
    { "folder",  [](P & p) { folder(p); } },
    { "file",    [](P & p) { page(p); } },
    { "textfile", [](P & p) { page(p); lines(p, 5, 11, { 7.2, 9.4, 11.6 }); } },
    { "folderup", [](P & p) { folder(p); arrow(p, 8, 12.4, 8, 5.6, kGreenDark, 2.0, 3.6); } },
    { "parent",  [](P & p) { folder(p); arrow(p, 8, 12.4, 8, 5.6, kGreenDark, 2.0, 3.6); } },
    { "root",    [](P & p) { folder(p); p.setPen(pen(kBlueDark, 1.8)); p.drawLine(QPointF(6.4, 12.4), QPointF(9.8, 7.4)); } },
    { "home",    [](P & p) {
        p.setPen(pen(kRedDark, 1.0)); p.setBrush(kRed);
        p.drawPolygon(poly({ { 8, 1.6 }, { 15, 8.2 }, { 12.8, 8.2 }, { 8, 3.9 }, { 3.2, 8.2 }, { 1, 8.2 } }));
        p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawPolygon(poly({ { 3.2, 8.4 }, { 8, 4.2 }, { 12.8, 8.4 }, { 12.8, 14.2 }, { 3.2, 14.2 } }));
        p.setPen(Qt::NoPen); p.setBrush(kBlue); p.drawRect(QRectF(6.6, 9.8, 2.8, 4.4)); } },
    { "refresh", [](P & p) { arcArrow(p, 8, 8, 5.2, 200, -150, kGreen); arcArrow(p, 8, 8, 5.2, 20, -150, kGreen); } },
    { "find",    [](P & p) {
        p.setPen(pen(kGray, 2.4)); p.drawLine(QPointF(10.2, 10.2), QPointF(14, 14));
        p.setPen(pen(kBlueDark, 1.5)); p.setBrush(QColor(0xd8, 0xe8, 0xfb)); p.drawEllipse(QPointF(6.8, 6.8), 4.6, 4.6); } },
    { "filter",  [](P & p) {
        p.setPen(pen(kBlueDark, 1.0)); p.setBrush(kBlue.lighter(135));
        p.drawPolygon(poly({ { 1.5, 2.5 }, { 14.5, 2.5 }, { 9.6, 8.4 }, { 9.6, 13.8 }, { 6.4, 12.2 }, { 6.4, 8.4 } })); } },
    { "tree",    [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.drawLine(QPointF(3.5, 4), QPointF(3.5, 12.5)); p.drawLine(QPointF(3.5, 8), QPointF(7, 8)); p.drawLine(QPointF(3.5, 12.5), QPointF(7, 12.5));
        p.setPen(pen(kYellowDark, 0.9)); p.setBrush(kYellow);
        p.drawRect(QRectF(1.5, 1.5, 6, 3.6)); p.drawRect(QRectF(7.5, 6.2, 6, 3.6)); p.drawRect(QRectF(7.5, 10.8, 6, 3.6)); } },
    { "bookmark", [](P & p) {
        QPolygonF star;
        for (int i = 0; i < 10; ++i) { const qreal a = -M_PI / 2 + i * M_PI / 5, r = (i % 2 == 0) ? 6.6 : 2.8; star << QPointF(8 + r * std::cos(a), 8.4 + r * std::sin(a)); }
        p.setPen(pen(kYellowDark, 1.0)); p.setBrush(kYellow); p.drawPolygon(star); } },
    { "opendir", [](P & p) {
        folder(p, 1, 3, 12.5, 10.5);
        p.setPen(pen(kYellowDark, 1.0)); p.setBrush(kYellow.lighter(118));
        p.drawPolygon(poly({ { 3.6, 8 }, { 15.2, 8 }, { 13.2, 13.5 }, { 1.2, 13.5 } })); } },
    { "upload",  [](P & p) { lines(p, 2.5, 13.5, { 2.6 }, kGray, 1.8); arrow(p, 8, 14.2, 8, 4.4, kGreen, 3.4, 5.6); } },
    { "download", [](P & p) { arrow(p, 8, 1.8, 8, 11.6, kBlue, 3.4, 5.6); lines(p, 2.5, 13.5, { 13.6 }, kGray, 1.8); } },
    { "edit",    [](P & p) {
        page(p, 2, 1.2, 9.5, 13.6); lines(p, 3.8, 8.6, { 7.4, 9.6 });
        p.save(); p.translate(11.4, 9.6); p.rotate(45);
        p.setPen(pen(kYellowDark, 0.8)); p.setBrush(kOrange); p.drawRect(QRectF(-1.5, -6.2, 3.0, 8.6));
        p.setBrush(QColor(0xf5, 0xd9, 0xa8)); p.drawPolygon(poly({ { -1.5, 2.4 }, { 1.5, 2.4 }, { 0, 5.4 } }));
        p.restore(); } },
    { "delete",  [](P & p) { p.setPen(pen(kRed, 3.0)); p.drawLine(QPointF(3.6, 3.6), QPointF(12.4, 12.4)); p.drawLine(QPointF(12.4, 3.6), QPointF(3.6, 12.4)); } },
    { "properties", [](P & p) {
        page(p); p.setPen(pen(kGreenDark, 1.5)); p.drawPolyline(poly({ { 5, 6.9 }, { 6, 8 }, { 7.6, 5.8 } })); p.drawPolyline(poly({ { 5, 10.9 }, { 6, 12 }, { 7.6, 9.8 } }));
        lines(p, 8.6, 11.2, { 7.2, 11.2 }); } },
    { "newdir",  [](P & p) { folder(p, 1, 2.2, 12.5, 10); plusBadge(p, 12, 11.6); } },
    { "newfile", [](P & p) { page(p, 2, 1.2, 9.5, 12.6); plusBadge(p, 12, 11.6); } },
    { "link",    [](P & p) {
        p.setPen(pen(kBlueDark, 1.8)); p.setBrush(Qt::NoBrush);
        p.save(); p.translate(8, 8); p.rotate(-45);
        p.drawRoundedRect(QRectF(-7, -2.4, 7.4, 4.8), 2.4, 2.4); p.drawRoundedRect(QRectF(-0.4, -2.4, 7.4, 4.8), 2.4, 2.4);
        p.restore(); } },
    { "select",  [](P & p) { p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawRect(QRectF(2, 2, 12, 12)); p.setPen(pen(kGreenDark, 2.0)); p.drawLine(QPointF(4.8, 8), QPointF(11.2, 8)); p.drawLine(QPointF(8, 4.8), QPointF(8, 11.2)); } },
    { "unselect", [](P & p) { p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawRect(QRectF(2, 2, 12, 12)); p.setPen(pen(kRedDark, 2.0)); p.drawLine(QPointF(4.8, 8), QPointF(11.2, 8)); } },
    { "selectall", [](P & p) { p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawRect(QRectF(2, 2, 12, 12)); p.setPen(pen(kBlueDark, 2.0)); p.drawPolyline(poly({ { 4.6, 8.2 }, { 7.1, 10.8 }, { 11.4, 5.2 } })); } },
    { "sync",    [](P & p) { arrow(p, 2, 5.4, 14, 5.4, kGreen, 2.4, 4.6); arrow(p, 14, 10.8, 2, 10.8, kBlue, 2.4, 4.6); } },
    { "compare", [](P & p) { page(p, 1, 2.4, 6.2, 11.4); page(p, 8.8, 2.4, 6.2, 11.4); lines(p, 2.4, 5.4, { 8.2, 10.4 }, kGreenDark); lines(p, 10.2, 13.2, { 8.2 }, kRedDark); lines(p, 10.2, 13.2, { 10.4 }, kGreenDark); } },
    { "syncbrowse", [](P & p) { folder(p, 0.6, 1.6, 8.6, 7.2); folder(p, 6.8, 7.4, 8.6, 7.2); p.setPen(pen(kBlueDark, 1.3)); p.drawLine(QPointF(3.6, 9.6), QPointF(3.6, 12.4)); p.drawLine(QPointF(3.6, 12.4), QPointF(6.2, 12.4)); } },
    { "terminal", [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.setBrush(QColor(0x2b, 0x2f, 0x36)); p.drawRoundedRect(QRectF(1, 2.4, 14, 11.2), 1.4, 1.4);
        p.setPen(pen(QColor(0x8f, 0xe0, 0x8f), 1.4)); p.drawPolyline(poly({ { 3.6, 5.8 }, { 6.4, 8 }, { 3.6, 10.2 } })); p.drawLine(QPointF(7.8, 10.6), QPointF(11.6, 10.6)); } },
    { "queue",   [](P & p) { lines(p, 2, 9.4, { 3.6, 7, 10.4 }, kGray, 1.6); arrow(p, 12.2, 2.6, 12.2, 13.4, kBlue, 2.0, 4.2); } },
    { "log",     [](P & p) { page(p); lines(p, 5, 11, { 6.2, 8.4, 10.6, 12.8 }, kGray, 1.0); } },
    { "newtab",  [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.setBrush(QColor(0xd8, 0xe8, 0xfb)); p.drawRoundedRect(QRectF(1, 2, 12.4, 8.8), 1, 1);
        p.setBrush(kGrayLight); p.drawRect(QRectF(4.6, 10.8, 5.2, 1.4)); p.drawRect(QRectF(3, 12.2, 8.4, 1.4));
        plusBadge(p, 12.2, 11.4); } },
    { "login",   [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.setBrush(QColor(0xd8, 0xe8, 0xfb)); p.drawRoundedRect(QRectF(1.8, 2, 12.4, 8.8), 1, 1);
        p.setBrush(kGrayLight); p.drawRect(QRectF(5.4, 10.8, 5.2, 1.4)); p.drawRect(QRectF(3.8, 12.2, 8.4, 1.4)); } },
    { "disconnect", [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.setBrush(QColor(0xe4, 0xe6, 0xea)); p.drawRoundedRect(QRectF(1, 2, 12.4, 8.8), 1, 1);
        p.setBrush(kGrayLight); p.drawRect(QRectF(4.6, 10.8, 5.2, 1.4)); p.drawRect(QRectF(3, 12.2, 8.4, 1.4));
        p.setPen(pen(QColor(255, 255, 255), 3.4)); p.drawLine(QPointF(9.6, 8.8), QPointF(14.2, 13.4)); p.drawLine(QPointF(14.2, 8.8), QPointF(9.6, 13.4));
        p.setPen(pen(kRed, 2.0)); p.drawLine(QPointF(9.6, 8.8), QPointF(14.2, 13.4)); p.drawLine(QPointF(14.2, 8.8), QPointF(9.6, 13.4)); } },
    { "settings", [](P & p) {
        p.setPen(Qt::NoPen); p.setBrush(kGray);
        for (int i = 0; i < 8; ++i) { p.save(); p.translate(8, 8); p.rotate(i * 45); p.drawRoundedRect(QRectF(-1.3, -7, 2.6, 4), 0.6, 0.6); p.restore(); }
        p.drawEllipse(QPointF(8, 8), 5, 5); p.setBrush(kPaper); p.drawEllipse(QPointF(8, 8), 2.2, 2.2); } },
    { "transfer", [](P & p) {
        p.setPen(Qt::NoPen); p.setBrush(kGray);
        for (int i = 0; i < 8; ++i) { p.save(); p.translate(6, 6.4); p.rotate(i * 45); p.drawRoundedRect(QRectF(-1.0, -5.4, 2.0, 3), 0.5, 0.5); p.restore(); }
        p.drawEllipse(QPointF(6, 6.4), 3.8, 3.8); p.setBrush(kPaper); p.drawEllipse(QPointF(6, 6.4), 1.6, 1.6);
        arrow(p, 7.6, 12.2, 15, 12.2, kGreen, 2.0, 4.0); } },
    { "rename",  [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawRect(QRectF(1, 4.4, 14, 7.2));
        lines(p, 3, 8, { 8 }, kBlueDark, 1.8); p.setPen(pen(kGray, 1.2)); p.drawLine(QPointF(10.6, 2.6), QPointF(10.6, 13.4)); p.drawLine(QPointF(9.2, 2.6), QPointF(12, 2.6)); p.drawLine(QPointF(9.2, 13.4), QPointF(12, 13.4)); } },
    { "copy",    [](P & p) { page(p, 1.4, 1, 8.2, 10.6); page(p, 6.4, 4.4, 8.2, 10.6); } },
    { "move",    [](P & p) { page(p, 1.4, 1.6, 8.2, 11.2); arrow(p, 6.4, 11.4, 15.2, 11.4, kBlue, 2.4, 4.4); } },
    { "duplicate", [](P & p) { page(p, 1.4, 1, 8.2, 10.6); page(p, 6.4, 4.4, 8.2, 10.6); plusBadge(p, 4.2, 12.8, kGreen, 2.4); } },
    { "quit",    [](P & p) {
        p.setPen(pen(kRed, 1.9)); p.setBrush(Qt::NoBrush); p.drawArc(QRectF(2.6, 3.2, 10.8, 10.8), 125 * 16, 290 * 16); p.drawLine(QPointF(8, 1.6), QPointF(8, 8)); } },
    { "info",    [](P & p) {
        p.setPen(pen(kBlueDark, 1.0)); p.setBrush(kBlue); p.drawEllipse(QPointF(8, 8), 6.4, 6.4);
        p.setPen(Qt::NoPen); p.setBrush(QColor(255, 255, 255)); p.drawEllipse(QPointF(8, 4.9), 1.15, 1.15); p.drawRoundedRect(QRectF(7, 7, 2, 5.2), 0.5, 0.5); } },
    { "lock",    [](P & p) {
        p.setPen(pen(kGray, 1.7)); p.setBrush(Qt::NoBrush); p.drawArc(QRectF(4.6, 1.6, 6.8, 9), 0, 180 * 16);
        p.setPen(pen(kYellowDark, 1.0)); p.setBrush(kYellow); p.drawRoundedRect(QRectF(3, 6.8, 10, 7.4), 1, 1); } },
    { "unlock",  [](P & p) {
        p.setPen(pen(kGray, 1.7)); p.setBrush(Qt::NoBrush); p.drawArc(QRectF(7.4, 1.2, 6.8, 9), 0, 180 * 16);
        p.setPen(pen(kGray, 1.0)); p.setBrush(kGrayLight); p.drawRoundedRect(QRectF(2, 6.8, 10, 7.4), 1, 1); } },
    { "plus",    [](P & p) { p.setPen(pen(kGreenDark, 2.2)); p.drawLine(QPointF(3.4, 8), QPointF(12.6, 8)); p.drawLine(QPointF(8, 3.4), QPointF(8, 12.6)); } },
    { "stop",    [](P & p) { p.setPen(pen(kRedDark, 1.0)); p.setBrush(kRed); p.drawRoundedRect(QRectF(3.4, 3.4, 9.2, 9.2), 1.2, 1.2); } },
    { "clear",   [](P & p) { lines(p, 2, 9, { 4, 7.4, 10.8 }, kGray, 1.5); p.setPen(pen(kGreenDark, 1.9)); p.drawPolyline(poly({ { 9.6, 9.4 }, { 11.6, 11.6 }, { 14.6, 6.8 } })); } },
    { "save",    [](P & p) {
        p.setPen(pen(kBlueDark, 1.0)); p.setBrush(kBlue);
        p.drawPolygon(poly({ { 1.6, 1.6 }, { 11.6, 1.6 }, { 14.4, 4.4 }, { 14.4, 14.4 }, { 1.6, 14.4 } }));
        p.setPen(Qt::NoPen); p.setBrush(kPaper); p.drawRect(QRectF(4, 1.9, 6.6, 4.2)); p.drawRect(QRectF(3.6, 8.6, 8.8, 5.4));
        p.setBrush(kBlueDark); p.drawRect(QRectF(8.2, 2.5, 1.6, 3)); } },
    { "cut",     [](P & p) {
        p.setPen(pen(kGray, 1.4)); p.drawLine(QPointF(4.4, 1.6), QPointF(10.4, 10.2)); p.drawLine(QPointF(11.6, 1.6), QPointF(5.6, 10.2));
        p.setPen(pen(kRedDark, 1.5)); p.setBrush(Qt::NoBrush); p.drawEllipse(QPointF(4.4, 12), 2.2, 2.2); p.drawEllipse(QPointF(11.6, 12), 2.2, 2.2); } },
    { "paste",   [](P & p) {
        p.setPen(pen(kYellowDark, 1.0)); p.setBrush(kYellow.darker(108)); p.drawRoundedRect(QRectF(1.6, 2.6, 10.6, 12), 1, 1);
        p.setPen(pen(kGray, 1.0)); p.setBrush(kGrayLight); p.drawRect(QRectF(4.6, 1.2, 4.6, 2.8));
        page(p, 6.6, 6.2, 8, 8.6); } },
    { "undo",    [](P & p) { arcArrow(p, 8.4, 9.2, 5, 0, 170, kBlue); } },
    { "redo",    [](P & p) { arcArrow(p, 7.6, 9.2, 5, 180, -170, kBlue); } },
    { "replace", [](P & p) {
        p.setPen(pen(kGray, 1.0)); p.setBrush(kPaper); p.drawRect(QRectF(1.2, 1.6, 6, 5.4)); p.drawRect(QRectF(8.8, 9, 6, 5.4));
        lines(p, 2.6, 5.8, { 4.3 }, kRedDark, 1.6); lines(p, 10.2, 13.4, { 11.7 }, kGreenDark, 1.6);
        arrow(p, 9.4, 3.2, 12.4, 8, kBlue, 1.5, 3.2); } },
    { "goto",    [](P & p) { lines(p, 6.6, 14, { 3.4, 12.6 }, kGray, 1.3); lines(p, 6.6, 14, { 8 }, kBlueDark, 1.9); arrow(p, 1, 8, 6, 8, kGreen, 2.0, 3.6); } },
    { "wrap",    [](P & p) {
        lines(p, 2, 14, { 3.2 }, kGray, 1.4); lines(p, 2, 7.4, { 12.8 }, kGray, 1.4);
        p.setPen(pen(kBlue, 1.6)); p.setBrush(Qt::NoBrush);
        QPainterPath w; w.moveTo(2, 8); w.lineTo(11.4, 8); w.cubicTo(14.6, 8, 14.6, 12.8, 11.4, 12.8); w.lineTo(10.4, 12.8); p.drawPath(w);
        p.setPen(Qt::NoPen); p.setBrush(kBlue); p.drawPolygon(poly({ { 8.2, 12.8 }, { 11, 10.8 }, { 11, 14.8 } })); } },
    { "open",    [](P & p) { page(p, 2, 1.2, 9.5, 13.6); arrow(p, 7, 12, 14.6, 4.4, kBlue, 2.0, 4.2); } },
  };
  return t;
}

class DrawnIconEngine : public QIconEngine
{
public:
  explicit DrawnIconEngine(const QString & name, const QIcon & base = QIcon(), bool overlay = false)
    : FName(name), FBase(base), FOverlay(overlay) {}

  void paint(QPainter * painter, const QRect & rect, QIcon::Mode mode, QIcon::State) override
  {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (mode == QIcon::Disabled) painter->setOpacity(0.38);
    if (FOverlay)
    {
      FBase.paint(painter, rect);
      const qreal s = qMin(rect.width(), rect.height()) / 16.0;
      painter->translate(rect.x(), rect.y()); painter->scale(s, s);
      painter->setPen(pen(kGray, 0.8)); painter->setBrush(QColor(255, 255, 255)); painter->drawRect(QRectF(0.4, 8.6, 7, 7));
      arrow(*painter, 1.8, 14.2, 6.2, 9.8, kBlueDark, 1.3, 3.0);
    }
    else
    {
      const qreal s = qMin(rect.width(), rect.height()) / 16.0;
      painter->translate(rect.x() + (rect.width() - 16 * s) / 2, rect.y() + (rect.height() - 16 * s) / 2);
      painter->scale(s, s);
      auto it = table().find(FName);
      if (it != table().end()) it.value()(*painter);
    }
    painter->restore();
  }

  QPixmap pixmap(const QSize & size, QIcon::Mode mode, QIcon::State state) override
  {
    QPixmap pm(size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(&p, QRect(QPoint(0, 0), size), mode, state);
    return pm;
  }

  QIconEngine * clone() const override { return new DrawnIconEngine(FName, FBase, FOverlay); }

private:
  QString FName;
  QIcon FBase;
  bool FOverlay;
};

} // namespace

QIcon appIcon(const QString & name)
{
  if (!table().contains(name)) return QIcon();
  return QIcon(new DrawnIconEngine(name));
}

QStringList appIconNames()
{
  QStringList names = table().keys();
  names.sort();
  return names;
}

QIcon linkOverlayIcon(const QIcon & base)
{
  return QIcon(new DrawnIconEngine(QString(), base, true));
}
