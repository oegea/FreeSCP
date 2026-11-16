//---------------------------------------------------------------------------
// updatecheck.cpp — see updatecheck.h.
//---------------------------------------------------------------------------
#include "updatecheck.h"
#include "versioncmp.h"
#include "uicommon.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const char * const kLatestUrl   = "https://api.github.com/repos/oegea/FreeSCP/releases/latest";
const char * const kReleasesUrl = "https://freescp.com/releases.html#latest";
const qint64 kDaySecs = 24 * 60 * 60;
const qint64 kMaxReply = 512 * 1024;   // the real answer is a few KB; anything bigger is not it
const int kTimeoutMs = 10000;

// Whatever goes wrong here - no network, no TLS, a proxy page, a hung or hostile server, garbage -
// the outcome is the same: no notice, nothing stored, the app carries on. Nothing in this file may
// block the UI thread, throw, or leave anything behind (no files, no threads; the request object is
// owned by the application and dies with it).

// Test hook: WINSCP_UPDATE_URL points the check at another address (a local file or server).
QString latestUrl()
{
  const QString o = qEnvironmentVariable("WINSCP_UPDATE_URL");
  return o.isEmpty() ? QString::fromLatin1(kLatestUrl) : o;
}
bool urlOverridden() { return !qEnvironmentVariableIsEmpty("WINSCP_UPDATE_URL"); }

QWidget * dialogParent(QWidget * fallback)
{
  QWidget * m = QApplication::activeModalWidget();   // e.g. the Login dialog at startup
  return m ? m : fallback;
}

void showAvailable(QWidget * parent, const QString & latest)
{
  QDialog d(dialogParent(parent)); d.setWindowTitle("Update available");
  auto * lay = new QVBoxLayout(&d); lay->setContentsMargins(24, 20, 24, 16); lay->setSpacing(14);
  auto * row = new QHBoxLayout; row->setSpacing(18);
  auto * icon = new QLabel;
  icon->setPixmap(QPixmap(":/winscp.png").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  icon->setAlignment(Qt::AlignTop);
  row->addWidget(icon);
  auto * body = new QLabel(QString(
      "<h3 style='margin:0 0 6px 0'>FreeSCP %1 is available</h3>"
      "<p style='margin:0'>You are using version %2. The new version is a free download, with its "
      "release notes, on the FreeSCP website.</p>"
      "<p style='margin:10px 0 0 0;color:gray'>FreeSCP only asked for the latest version number; nothing "
      "about you or this computer was sent. You can turn this check off in Preferences.</p>")
      .arg(latest.toHtmlEscaped(), QStringLiteral(FREESCP_VERSION)));
  body->setTextFormat(Qt::RichText); body->setWordWrap(true); body->setMinimumWidth(380); body->setMaximumWidth(440);
  row->addWidget(body, 1);
  lay->addLayout(row);
  auto * bb = new QDialogButtonBox;
  auto * closeBtn = bb->addButton("Close", QDialogButtonBox::RejectRole);
  auto * update = bb->addButton("Update\xE2\x80\xA6", QDialogButtonBox::AcceptRole);
  update->setDefault(true); (void)closeBtn;
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  if (execDialog(d, "update") != QDialog::Accepted) return;
  if (uiScripted()) { fprintf(stderr, "[update] open %s\n", kReleasesUrl); return; }   // no browser in test runs
  QDesktopServices::openUrl(QUrl(QString::fromLatin1(kReleasesUrl)));
}

QPointer<QNetworkReply> g_reply;      // the request in flight, if any

}

namespace updates {

bool enabled() { return QSettings().value("updates/check", true).toBool(); }
void setEnabled(bool on) { QSettings().setValue("updates/check", on); }

void check(QWidget * parent, bool manual)
{
  if (g_reply) return;
  if (!manual)
  {
    if (!enabled()) return;
    if (uiScripted() && !urlOverridden()) return;       // headless/test runs never touch the network
    const qint64 last = QSettings().value("updates/lastCheck", 0).toLongLong();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!urlOverridden() && last > 0 && now >= last && now - last < kDaySecs) return;
  }

  const QUrl url(latestUrl());
  if (!url.isValid() || url.scheme().isEmpty()) return;
  static QPointer<QNetworkAccessManager> nam;
  if (!nam)
  {
    nam = new QNetworkAccessManager(qApp);
    // quitting with the request still open: drop it quietly, before anything is torn down
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, qApp, [] {
      if (g_reply) { QNetworkReply * r = g_reply; g_reply = nullptr; r->disconnect(); r->abort(); r->deleteLater(); }
    });
  }
  QNetworkRequest rq{ url };
  rq.setRawHeader("Accept", "application/vnd.github+json");
  rq.setRawHeader("User-Agent", "FreeSCP");             // required by the API; deliberately no version
  rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  rq.setMaximumRedirectsAllowed(3);
  rq.setTransferTimeout(kTimeoutMs);
  QNetworkReply * reply = nam->get(rq);
  if (!reply) return;
  g_reply = reply;
  // Hard limits on top of the transfer timeout (which only watches for inactivity): a total time,
  // and a size. Both end the request; `finished` then runs with an error.
  QTimer::singleShot(kTimeoutMs + 2000, reply, [reply] { if (reply->isRunning()) reply->abort(); });
  QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64 total) {
    if (got > kMaxReply || total > kMaxReply) reply->abort();
  });
  QObject::connect(reply, &QNetworkReply::sslErrors, reply, [reply] { reply->abort(); });   // never accept a bad certificate
  QPointer<QWidget> guard(parent);
  QObject::connect(reply, &QNetworkReply::finished, qApp, [reply, guard, manual] {
    if (g_reply == reply) g_reply = nullptr;
    reply->deleteLater();
    QString latest;
    if (reply->error() == QNetworkReply::NoError)
    {
      const QByteArray data = reply->read(kMaxReply + 1);
      if (data.size() <= kMaxReply)
      {
        const QJsonValue tag = QJsonDocument::fromJson(data).object().value("tag_name");
        // only a plain version tag ("v1.2.3") is believed; anything else is treated as no answer
        static const QRegularExpression plain(QStringLiteral("\\Av?[0-9]{1,6}(\\.[0-9]{1,6}){0,3}\\z"));
        if (tag.isString() && plain.match(tag.toString()).hasMatch()) latest = tag.toString();
      }
    }
    if (latest.startsWith('v') || latest.startsWith('V')) latest.remove(0, 1);
    const bool known = !versionParts(latest.toStdString()).empty();
    const bool newer = known && versionIsNewer(latest.toStdString(), FREESCP_VERSION);
    if (uiScripted())   // test runs assert on this line; a normal run prints nothing
      fprintf(stderr, "[update] latest=%s current=%s newer=%d%s\n", known ? latest.toUtf8().constData() : "?",
              FREESCP_VERSION, newer ? 1 : 0, known ? "" : " (check failed)");
    if (known) QSettings().setValue("updates/lastCheck", QDateTime::currentSecsSinceEpoch());
    if (!guard) return;
    if (newer) showAvailable(guard, latest);
    else if (manual)
      appMessage(dialogParent(guard), "Check for Updates",
                 known ? QString("You have the latest version of FreeSCP (%1).").arg(QStringLiteral(FREESCP_VERSION))
                       : QString("Could not check for a new version right now.\n\nYou can see every release at freescp.com/releases.html."));
  });
}

}
