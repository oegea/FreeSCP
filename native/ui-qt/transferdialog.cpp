//---------------------------------------------------------------------------
// transferdialog.cpp — see transferdialog.h.
//---------------------------------------------------------------------------
#include "transferdialog.h"
#include "uicommon.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

QString transferSummary(const engine::TransferSettings & x)
{
  const engine::TransferSettings d;
  QStringList parts;
  if (x.mode == 1) parts << "Text mode";
  else if (x.mode == 2) parts << "Automatic text/binary";
  if (!x.preserveTime) parts << "no timestamp";
  if (x.setRights) parts << "permissions " + u8(x.rightsOctal);
  if (x.fileNameCase == 1) parts << "UPPER CASE names";
  else if (x.fileNameCase == 2) parts << "lower case names";
  if (!x.fileMask.empty()) parts << "mask: " + u8(x.fileMask);
  if (x.excludeHidden) parts << "no hidden files";
  if (x.newerOnly) parts << "newer only";
  if (x.resume == 0) parts << "resume all files";
  else if (x.resume == 2) parts << "no resume";
  else if (x.resumeThresholdKB != d.resumeThresholdKB) parts << QString("resume above %1 KB").arg(x.resumeThresholdKB);
  if (x.speedLimitKB > 0) parts << QString("%1 KB/s").arg(x.speedLimitKB);
  return parts.isEmpty() ? QStringLiteral("Default transfer settings") : parts.join(" \xC2\xB7 ");
}

engine::TransferSettings loadTransferSettings()
{
  engine::TransferSettings x;
  QSettings s;
  s.beginGroup("transfer");
  x.mode = qBound(0, s.value("mode", x.mode).toInt(), 2);
  x.textMask = s8(s.value("textMask").toString());
  x.preserveTime = s.value("preserveTime", x.preserveTime).toBool();
  x.setRights = s.value("setRights", x.setRights).toBool();
  x.rightsOctal = s8(s.value("rights", u8(x.rightsOctal)).toString());
  x.fileNameCase = qBound(0, s.value("fileNameCase", x.fileNameCase).toInt(), 2);
  x.resume = qBound(0, s.value("resume", x.resume).toInt(), 2);
  x.resumeThresholdKB = s.value("resumeThresholdKB", (qlonglong)x.resumeThresholdKB).toLongLong();
  x.speedLimitKB = qMax(0, s.value("speedLimitKB", x.speedLimitKB).toInt());
  x.fileMask = s8(s.value("fileMask").toString());
  x.newerOnly = s.value("newerOnly", x.newerOnly).toBool();
  x.excludeHidden = s.value("excludeHidden", x.excludeHidden).toBool();
  s.endGroup();
  QString err;
  if (!validateTransferSettings(x, &err)) x = engine::TransferSettings();   // never start with settings the engine rejects
  return x;
}

void saveTransferSettings(const engine::TransferSettings & x)
{
  QSettings s;
  s.beginGroup("transfer");
  s.setValue("mode", x.mode);
  s.setValue("textMask", u8(x.textMask));
  s.setValue("preserveTime", x.preserveTime);
  s.setValue("setRights", x.setRights);
  s.setValue("rights", u8(x.rightsOctal));
  s.setValue("fileNameCase", x.fileNameCase);
  s.setValue("resume", x.resume);
  s.setValue("resumeThresholdKB", (qlonglong)x.resumeThresholdKB);
  s.setValue("speedLimitKB", x.speedLimitKB);
  s.setValue("fileMask", u8(x.fileMask));
  s.setValue("newerOnly", x.newerOnly);
  s.setValue("excludeHidden", x.excludeHidden);
  s.endGroup();
  s.sync();
}

bool validateTransferSettings(const engine::TransferSettings & x, QString * error)
{
  std::string why;
  if (!x.textMask.empty() && !engine::validateFileMask(x.textMask, &why))
  { if (error) *error = "The text file mask is not valid.\n" + u8(why); return false; }
  if (!x.fileMask.empty() && !engine::validateFileMask(x.fileMask, &why))
  { if (error) *error = "The file mask is not valid.\n" + u8(why); return false; }
  if (x.setRights && !QRegularExpression("^[0-7]{3,4}$").match(u8(x.rightsOctal)).hasMatch())
  { if (error) *error = "Permissions must be 3 or 4 octal digits, for example 644."; return false; }
  if (x.resumeThresholdKB < 0) { if (error) *error = "The resume threshold cannot be negative."; return false; }
  return true;
}

bool showTransferSettingsDialog(QWidget * parent, engine::TransferSettings & settings)
{
  QDialog d(parent);
  d.setWindowTitle("Transfer Settings");
  auto * outer = new QVBoxLayout(&d);

  //--- transfer mode ---
  auto * modeBox = new QGroupBox("Transfer mode");
  auto * modeLay = new QGridLayout(modeBox);
  auto * rbBinary = new QRadioButton("&Binary");
  auto * rbText = new QRadioButton("&Text (plain text, html, scripts, ...)");
  auto * rbAuto = new QRadioButton("&Automatic");
  auto * textMask = new QLineEdit;
  textMask->setPlaceholderText(u8(engine::defaultTextMask()));
  textMask->setText(u8(settings.textMask));
  textMask->setMinimumWidth(360);
  auto * maskLabel = new QLabel("Transfer following files in text mode:");
  modeLay->addWidget(rbBinary, 0, 0, 1, 2);
  modeLay->addWidget(rbText, 1, 0, 1, 2);
  modeLay->addWidget(rbAuto, 2, 0, 1, 2);
  modeLay->addWidget(maskLabel, 3, 0); modeLay->addWidget(textMask, 3, 1);
  auto * modeHint = new QLabel("Text mode converts line endings between this computer (LF) and the server.");
  modeHint->setStyleSheet("color: gray;"); modeHint->setWordWrap(true);
  modeLay->addWidget(modeHint, 4, 0, 1, 2);
  (settings.mode == 1 ? rbText : settings.mode == 2 ? rbAuto : rbBinary)->setChecked(true);
  outer->addWidget(modeBox);

  auto * row = new QHBoxLayout;
  //--- common options ---
  auto * commonBox = new QGroupBox("Common options");
  auto * commonLay = new QFormLayout(commonBox);
  auto * cbTime = new QCheckBox("&Preserve timestamp"); cbTime->setChecked(settings.preserveTime);
  auto * nameCase = new QComboBox; nameCase->addItems({ "No change", "Upper case", "Lower case" });
  nameCase->setCurrentIndex(qBound(0, settings.fileNameCase, 2));
  commonLay->addRow(cbTime);
  commonLay->addRow("File &name modification:", nameCase);
  row->addWidget(commonBox, 1);

  //--- upload options ---
  auto * upBox = new QGroupBox("Upload options");
  auto * upLay = new QFormLayout(upBox);
  auto * cbRights = new QCheckBox("Set pe&rmissions:"); cbRights->setChecked(settings.setRights);
  auto * rights = new QLineEdit(u8(settings.rightsOctal)); rights->setMaximumWidth(70); rights->setMaxLength(4);
  upLay->addRow(cbRights, rights);
  auto * upHint = new QLabel("Octal, e.g. 644. Directories also get x.");
  upHint->setStyleSheet("color: gray;");
  upLay->addRow(upHint);
  row->addWidget(upBox, 1);
  outer->addLayout(row);

  //--- other ---
  auto * otherBox = new QGroupBox("Other");
  auto * otherLay = new QFormLayout(otherBox);
  auto * fileMask = new QLineEdit(u8(settings.fileMask));
  fileMask->setPlaceholderText("all files   (example:  *.html; *.css | *.bak; temp/ )");
  auto * cbHidden = new QCheckBox("E&xclude hidden files"); cbHidden->setChecked(settings.excludeHidden);
  auto * cbNewer = new QCheckBox("Ne&w and updated files only"); cbNewer->setChecked(settings.newerOnly);
  otherLay->addRow("File &mask:", fileMask);
  otherLay->addRow(cbHidden);
  otherLay->addRow(cbNewer);
  auto * resume = new QComboBox;
  resume->addItems({ "All files", "Files above the size below", "Disable" });
  resume->setCurrentIndex(qBound(0, settings.resume, 2));
  auto * threshold = new QSpinBox; threshold->setRange(0, 100000000); threshold->setSuffix(" KB");
  threshold->setValue((int)std::min<std::int64_t>(std::max<std::int64_t>(settings.resumeThresholdKB, 0), 100000000));
  auto * resumeRow = new QWidget; auto * resumeLay = new QHBoxLayout(resumeRow); resumeLay->setContentsMargins(0, 0, 0, 0);
  resumeLay->addWidget(resume, 1); resumeLay->addWidget(threshold);
  otherLay->addRow("Transfer resume / to temporary file name for:", resumeRow);
  auto * speed = new QSpinBox; speed->setRange(0, 10000000); speed->setSuffix(" KB/s"); speed->setSpecialValueText("Unlimited");
  speed->setSingleStep(128); speed->setValue(qMax(0, settings.speedLimitKB));
  otherLay->addRow("&Speed limit:", speed);
  outer->addWidget(otherBox);

  auto sync = [=] {
    textMask->setEnabled(rbAuto->isChecked()); maskLabel->setEnabled(rbAuto->isChecked());
    rights->setEnabled(cbRights->isChecked());
    threshold->setEnabled(resume->currentIndex() == 1);
  };
  for (QRadioButton * rb : { rbBinary, rbText, rbAuto }) QObject::connect(rb, &QRadioButton::toggled, &d, [=](bool) { sync(); });
  QObject::connect(cbRights, &QCheckBox::toggled, &d, [=](bool) { sync(); });
  QObject::connect(resume, &QComboBox::currentIndexChanged, &d, [=](int) { sync(); });
  sync();

  engine::TransferSettings result = settings;
  auto collect = [&] {
    engine::TransferSettings x;
    x.mode = rbText->isChecked() ? 1 : rbAuto->isChecked() ? 2 : 0;
    x.textMask = s8(textMask->text().trimmed());
    x.preserveTime = cbTime->isChecked();
    x.setRights = cbRights->isChecked();
    x.rightsOctal = s8(rights->text().trimmed());
    x.fileNameCase = nameCase->currentIndex();
    x.resume = resume->currentIndex();
    x.resumeThresholdKB = threshold->value();
    x.speedLimitKB = speed->value();
    x.fileMask = s8(fileMask->text().trimmed());
    x.newerOnly = cbNewer->isChecked();
    x.excludeHidden = cbHidden->isChecked();
    return x;
  };

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
  outer->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  QObject::connect(bb->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &d, [&] {
    engine::TransferSettings def;
    rbBinary->setChecked(true); textMask->clear(); cbTime->setChecked(def.preserveTime);
    cbRights->setChecked(def.setRights); rights->setText(u8(def.rightsOctal)); nameCase->setCurrentIndex(0);
    fileMask->clear(); cbHidden->setChecked(false); cbNewer->setChecked(false);
    resume->setCurrentIndex(def.resume); threshold->setValue((int)def.resumeThresholdKB); speed->setValue(0);
    sync();
  });
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    engine::TransferSettings x = collect();
    QString err;
    if (!validateTransferSettings(x, &err)) { appMessage(&d, "Transfer Settings", err); return; }
    result = x;
    d.accept();
  });

  if (execDialog(d, "transfersettings") != QDialog::Accepted) return false;
  QString err;
  engine::TransferSettings x = collect();          // (scripted accept bypasses the OK handler)
  if (!validateTransferSettings(x, &err)) return false;
  settings = x;
  return true;
}
