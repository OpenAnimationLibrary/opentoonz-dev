#include "toonzqt/lutgenerator.h"
#include "lutgeneratorwait.h"

#include "palette_fit.h"
#include "lut_writer.h"
#include "tpalette.h"
#include "tsimplecolorstyles.h"
#include "thirdparty.h"
#include "toonz/studiopalette.h"
#include "toonz/lut3d.h"
#include "tenv.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>

#include <atomic>
#include <future>
#include <memory>
#include <map>
#include <cmath>
#include <sstream>

namespace {

std::array<float, 3> rgb(const TPixel32 &p) {
  return {p.r / 255.0f, p.g / 255.0f, p.b / 255.0f};
}

void setSwatch(QLabel *label, const TPixel32 &p) {
  label->setText(QString("%1, %2, %3").arg(p.r).arg(p.g).arg(p.b));
  const QColor color(p.r, p.g, p.b);
  label->setStyleSheet(QString("background:%1;color:%2;padding:4px;")
                           .arg(color.name())
                           .arg(color.lightness() > 128 ? "black" : "white"));
}

bool commitOutput(const QString &path, const QByteArray &data, QString &error) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
      !file.commit()) {
    error = file.errorString();
    return false;
  }
  return true;
}

// Validate the serialized file with the same reader used by OpenToonz's FX.
bool readGenerated(const QByteArray &data, Lut3D &lut, QString &error) {
  QTemporaryFile file(QDir::tempPath() + "/otlut-XXXXXX.cube");
  if (!file.open() || file.write(data) != data.size() || !file.flush()) {
    error = file.errorString();
    return false;
  }
  file.close();
  return lut.load(file.fileName(), &error);
}

struct StyleRow {
  int id;
  bool usable = false;
  QCheckBox *include;
  QComboBox *sourceKey;
  QComboBox *targetKey;
  QDoubleSpinBox *tolerance;
  QLabel *source;
  QLabel *target;
  QLabel *preview;
  QTableWidgetItem *status;
};

class PaletteLutDialog final : public QDialog {
  TPaletteP m_palette;
  QSpinBox *m_sourceFrame;
  QSpinBox *m_targetFrame;
  QDoubleSpinBox *m_tolerance;
  QComboBox *m_size;
  QTableWidget *m_table;
  QLabel *m_paletteLabel;
  QLabel *m_summary;
  QLabel *m_before;
  QLabel *m_after;
  std::vector<StyleRow> m_rows;

  int keyFrame(QComboBox *choice, QSpinBox *global) const {
    const int selected = choice->currentData().toInt();
    return selected < 0 ? global->value() - 1 : selected;
  }

  std::vector<otlut::ColorPair> pairs(bool update) {
    std::vector<otlut::ColorPair> result;
    int changes = 0, preserved = 0, skipped = 0;
    std::map<int, TPaletteP> frames;
    auto colorAt = [&](int id, int frame) {
      auto found = frames.find(frame);
      if (found == frames.end()) {
        TPaletteP copy = m_palette->clone();
        copy->setFrame(-1);
        copy->setFrame(frame);
        found = frames.emplace(frame, copy).first;
      }
      return found->second->getStyle(id)->getMainColor();
    };
    for (StyleRow &row : m_rows) {
      const int sourceFrame = keyFrame(row.sourceKey, m_sourceFrame);
      const int targetFrame = keyFrame(row.targetKey, m_targetFrame);
      const TPixel32 source = colorAt(row.id, sourceFrame);
      TPixel32 target       = colorAt(row.id, targetFrame);
      const bool keys       = m_palette->isKeyframe(row.id, sourceFrame) &&
                        m_palette->isKeyframe(row.id, targetFrame);
      if (!keys || !row.include->isChecked()) target = source;
      const bool solid =
          dynamic_cast<TSolidColorStyle *>(m_palette->getStyle(row.id));
      const bool usable = solid && source.m == 255 && target.m == 255;
      row.usable        = usable;
      QString status;
      if (!usable) {
        status = QObject::tr("Skipped: requires opaque solid colors");
        ++skipped;
      } else {
        const bool changed = rgb(source) != rgb(target);
        if (changed)
          ++changes;
        else
          ++preserved;
        status = !keys     ? QObject::tr("Preserved: missing an explicit key")
                 : changed ? QObject::tr("Change")
                           : QObject::tr("Preserved");
        otlut::ColorPair pair;
        pair.source    = rgb(source);
        pair.target    = rgb(target);
        pair.tolerance = static_cast<float>(row.tolerance->value() / 255.0);
        pair.label =
            std::to_string(row.id) + ": " +
            QString::fromStdWString(m_palette->getStyle(row.id)->getName())
                .toUtf8()
                .toStdString();
        result.push_back(pair);
      }
      if (update) {
        setSwatch(row.source, source);
        setSwatch(row.target, target);
        row.status->setText(status);
        row.preview->clear();
      }
    }
    if (update) {
      m_summary->setText(
          QObject::tr("%1 changes, %2 preserved colors, %3 skipped styles.")
              .arg(changes)
              .arg(preserved)
              .arg(skipped));
      m_after->clear();
    }
    return result;
  }

  void rebuild() {
    m_rows.clear();
    m_table->setRowCount(0);
    // Iterate visible pages: deleted/unpaged style slots are not author input.
    for (int page = 0; page < m_palette->getPageCount(); ++page) {
      const TPalette::Page *p = m_palette->getPage(page);
      for (int index = 0; index < p->getStyleCount(); ++index) {
        const int id = p->getStyleId(index);
        if (id == 0) continue;
        const int n = m_table->rowCount();
        m_table->insertRow(n);
        StyleRow row;
        row.id      = id;
        row.include = new QCheckBox(m_table);
        row.include->setChecked(true);
        m_table->setCellWidget(n, 0, row.include);
        m_table->setItem(
            n, 1,
            new QTableWidgetItem(QString("%1: %2").arg(id).arg(
                QString::fromStdWString(m_palette->getStyle(id)->getName()))));
        row.sourceKey = new QComboBox(m_table);
        row.targetKey = new QComboBox(m_table);
        row.sourceKey->addItem(QObject::tr("Source frame"), -1);
        row.targetKey->addItem(QObject::tr("Target frame"), -1);
        for (int key = 0; key < m_palette->getKeyframeCount(id); ++key) {
          const int f = m_palette->getKeyframe(id, key);
          row.sourceKey->addItem(QString::number(f + 1), f);
          row.targetKey->addItem(QString::number(f + 1), f);
        }
        m_table->setCellWidget(n, 2, row.sourceKey);
        row.source = new QLabel(m_table);
        m_table->setCellWidget(n, 3, row.source);
        m_table->setCellWidget(n, 4, row.targetKey);
        row.target = new QLabel(m_table);
        m_table->setCellWidget(n, 5, row.target);
        row.tolerance = new QDoubleSpinBox(m_table);
        row.tolerance->setRange(0, 255);
        row.tolerance->setDecimals(1);
        row.tolerance->setValue(m_tolerance->value());
        m_table->setCellWidget(n, 6, row.tolerance);
        row.preview = new QLabel(m_table);
        m_table->setCellWidget(n, 7, row.preview);
        row.status = new QTableWidgetItem;
        m_table->setItem(n, 8, row.status);
        m_rows.push_back(row);
        connect(row.include, &QCheckBox::toggled, this,
                [this] { pairs(true); });
        for (QComboBox *choice : {row.sourceKey, row.targetKey})
          connect(choice, QOverload<int>::of(&QComboBox::currentIndexChanged),
                  this, [this] { pairs(true); });
        connect(row.tolerance,
                QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this] { pairs(true); });
      }
    }
    pairs(true);
  }

  void generate(bool save) {
    std::atomic<bool> cancelled{false};
    try {
      const auto input = pairs(false);
      bool changed     = false;
      for (const auto &p : input) changed = changed || p.source != p.target;
      if (!changed) {
        QMessageBox::information(
            this, windowTitle(),
            QObject::tr("Choose two explicit keys with different "
                        "colors for at least one included style."));
        return;
      }
      QProgressDialog progress(QObject::tr("Generating palette LUT..."),
                               QObject::tr("Cancel"), 0, 0, this);
      progress.setWindowModality(Qt::WindowModal);
      progress.setMinimumDuration(0);
      progress.show();
      connect(&progress, &QProgressDialog::canceled, this,
              [&cancelled] { cancelled = true; });
      const int size = m_size->currentData().toInt();
      auto work = std::async(std::launch::async, [input, size, &cancelled] {
        otlut::PaletteFitReport report;
        const auto lut = otlut::fitLutFromColorPairs(
            input, size, &report, [&cancelled] { return cancelled.load(); });
        std::ostringstream stream;
        otlut::LutWriteOptions options;
        options.title = "OpenToonz palette keys";
        otlut::writeCube(stream, lut, options);
        return std::make_pair(stream.str(), report);
      });
      LutGenerator::waitForTask(work);
      progress.reset();
      const auto generated = work.get();
      if (cancelled) return;
      const QByteArray data = QByteArray::fromStdString(generated.first);
      Lut3D lut;
      QString error;
      if (!readGenerated(data, lut, error))
        throw std::runtime_error(error.toStdString());
      // Check the on-disk convention, not just the fitter's internal ordering.
      for (const auto &p : input) {
        auto out = p.source;
        lut.convert(out[0], out[1], out[2]);
        for (int c = 0; c < 3; ++c)
          if (std::fabs(out[c] - p.target[c]) > 0.251f / 255.0f)
            throw std::runtime_error(
                "Serialized LUT does not preserve its color constraints.");
      }
      int pairIndex = 0;
      for (const StyleRow &row : m_rows) {
        if (row.usable) {
          auto out = input.at(pairIndex++).source;
          lut.convert(out[0], out[1], out[2]);
          setSwatch(row.preview,
                    TPixel32(qRound(out[0] * 255), qRound(out[1] * 255),
                             qRound(out[2] * 255), 255));
        }
      }
      QImage before(256, 60, QImage::Format_RGB32), after = before;
      for (int y = 0; y < before.height(); ++y)
        for (int x = 0; x < before.width(); ++x) {
          const QColor color =
              y < 45 ? QColor::fromHsv(x * 359 / 255, 255, 255 - (y / 15) * 80)
                     : QColor(x, x, x);
          before.setPixel(x, y, color.rgb());
          float r = color.redF(), g = color.greenF(), b = color.blueF();
          lut.convert(r, g, b);
          after.setPixel(
              x, y, qRgb(qRound(r * 255), qRound(g * 255), qRound(b * 255)));
        }
      m_before->setPixmap(QPixmap::fromImage(before));
      m_after->setPixmap(QPixmap::fromImage(after));
      m_summary->setText(
          QObject::tr(
              "%1 changed colors, %2 preserved colors. Maximum RGB error: %3 "
              "/ 255.")
              .arg(generated.second.changedColors)
              .arg(generated.second.preservedColors)
              .arg(generated.second.maximumError * 255, 0, 'f', 3));
      if (!save) return;
      const QString directory =
          (TEnv::getStuffDir() + "library" + "luts").getQString();
      QDir().mkpath(directory);
      QFileDialog saveDialog(
          this, QObject::tr("Export Palette LUT"),
          (TEnv::getStuffDir() + "library" + "luts" + "palette.cube")
              .getQString(),
          QObject::tr("3D LUT (*.cube)"));
      saveDialog.setAcceptMode(QFileDialog::AcceptSave);
      saveDialog.setDefaultSuffix("cube");
      if (saveDialog.exec() != QDialog::Accepted ||
          saveDialog.selectedFiles().isEmpty())
        return;
      const QString path = saveDialog.selectedFiles().first();
      if (!commitOutput(path, data, error))
        throw std::runtime_error(error.toStdString());
      QMessageBox::information(this, windowTitle(),
                               QObject::tr("3D LUT created:\n") + path);
    } catch (const std::exception &e) {
      if (!cancelled)
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
    }
  }

public:
  PaletteLutDialog(TPalette *palette, QWidget *parent)
      : QDialog(parent), m_palette(palette->clone()) {
    setWindowTitle(QObject::tr("Create 3D LUT from Palette Keys"));
    resize(1080, 560);
    auto *layout   = new QVBoxLayout(this);
    m_paletteLabel = new QLabel(
        QObject::tr("Current Palette: %1")
            .arg(QString::fromStdWString(m_palette->getPaletteName())),
        this);
    m_paletteLabel->setWordWrap(true);
    layout->addWidget(m_paletteLabel);
    auto *browse = new QPushButton(QObject::tr("Reference Palette..."), this);
    layout->addWidget(browse);
    auto *form    = new QFormLayout;
    m_sourceFrame = new QSpinBox(this);
    m_targetFrame = new QSpinBox(this);
    for (auto *spin : {m_sourceFrame, m_targetFrame})
      spin->setRange(1, 1000000);
    m_sourceFrame->setValue(1);
    m_targetFrame->setValue(2);
    m_tolerance = new QDoubleSpinBox(this);
    m_tolerance->setRange(0, 255);
    m_tolerance->setDecimals(1);
    m_tolerance->setValue(25.5);
    m_tolerance->setToolTip(QObject::tr(
        "RGB distance in 0-255 units, with smooth falloff. Zero uses the "
        "smallest neighborhood the LUT grid can represent."));
    m_size = new QComboBox(this);
    m_size->addItem("33", 33);
    m_size->addItem("65", 65);
    form->addRow(QObject::tr("Source Frame:"), m_sourceFrame);
    form->addRow(QObject::tr("Target Frame:"), m_targetFrame);
    form->addRow(QObject::tr("Tolerance (set all styles):"), m_tolerance);
    form->addRow(QObject::tr("LUT Grid Size:"), m_size);
    layout->addLayout(form);
    m_table = new QTableWidget(0, 9, this);
    m_table->setHorizontalHeaderLabels(
        {QObject::tr("Include"), QObject::tr("Style"),
         QObject::tr("Source Key"), QObject::tr("Source RGB"),
         QObject::tr("Target Key"), QObject::tr("Target RGB"),
         QObject::tr("Tolerance"), QObject::tr("LUT Result"),
         QObject::tr("Status")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(
        QHeaderView::ResizeToContents);
    layout->addWidget(m_table, 1);
    auto *help = new QLabel(
        QObject::tr(
            "Unkeyed and excluded colors are preserved. Transparent and "
            "complex styles are skipped. RGB values use the palette's "
            "encoding; alpha is unchanged."),
        this);
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *rampLayout = new QHBoxLayout;
    m_before         = new QLabel(this);
    m_after          = new QLabel(this);
    rampLayout->addWidget(new QLabel(QObject::tr("Before:"), this));
    rampLayout->addWidget(m_before);
    rampLayout->addWidget(new QLabel(QObject::tr("After:"), this));
    rampLayout->addWidget(m_after);
    layout->addLayout(rampLayout);
    m_summary = new QLabel(this);
    layout->addWidget(m_summary);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto *preview = buttons->addButton(QObject::tr("Preview"),
                                       QDialogButtonBox::ActionRole);
    auto *save    = buttons->addButton(QObject::tr("Export LUT..."),
                                       QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(preview, &QPushButton::clicked, this, [this] { generate(false); });
    connect(save, &QPushButton::clicked, this, [this] { generate(true); });
    for (auto *spin : {m_sourceFrame, m_targetFrame})
      connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this,
              [this] { pairs(true); });
    connect(m_size, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { pairs(true); });
    connect(m_tolerance, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double value) {
              for (const auto &row : m_rows) {
                row.tolerance->blockSignals(true);
                row.tolerance->setValue(value);
                row.tolerance->blockSignals(false);
              }
              pairs(true);
            });
    connect(browse, &QPushButton::clicked, this, [this] {
      const QString path = QFileDialog::getOpenFileName(
          this, QObject::tr("Reference Palette"), QString(),
          QObject::tr("OpenToonz Palette (*.tpl)"));
      if (path.isEmpty()) return;
      try {
        TPaletteP loaded = StudioPalette::instance()->getPalette(
            TFilePath(path.toStdWString()), false);
        if (!loaded) throw std::runtime_error("Could not read the palette.");
        m_palette = loaded->clone();
        m_paletteLabel->setText(QObject::tr("Reference Palette: %1").arg(path));
        rebuild();
      } catch (const std::exception &e) {
        QMessageBox::warning(this, windowTitle(), QString::fromUtf8(e.what()));
      } catch (...) {
        QMessageBox::warning(this, windowTitle(),
                             QObject::tr("Could not read the palette."));
      }
    });
    rebuild();
  }
};
}  // namespace

void openPaletteLutDialog(TPalette *palette, QWidget *parent) {
  if (!palette) return;
  PaletteLutDialog dialog(palette, parent);
  dialog.exec();
}

bool generateLutFromImagePair(QWidget *parent, const QString &source,
                              const QString &target, const QString &destination,
                              int size) {
  const QString title = QObject::tr("Create 3D LUT from Image Pair");
  if (!ThirdParty::checkOtlut()) {
    const QString detected = ThirdParty::autodetectOtlut();
    if (!detected.isEmpty()) ThirdParty::setOtlutDir(detected);
  }
  if (!ThirdParty::checkOtlut()) {
    QMessageBox::warning(
        parent, title,
        QObject::tr("OTLUT was not found. Set the OTLUT Path in "
                    "Preferences > Import/Export."));
    return false;
  }
  QTemporaryFile file(QDir::tempPath() + "/otlut-pair-XXXXXX.cube");
  if (!file.open()) {
    QMessageBox::warning(parent, title, file.errorString());
    return false;
  }
  const QString temporaryPath = file.fileName();
  file.close();
  QProcess process;
  QProgressDialog progress(QObject::tr("Generating image-pair LUT..."),
                           QObject::tr("Cancel"), 0, 0, parent);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  progress.show();
  QEventLoop loop;
  bool cancelled = false, timedOut = false;
  QObject::connect(&progress, &QProgressDialog::canceled, &loop, [&] {
    cancelled = true;
    process.kill();
  });
  QObject::connect(
      &process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
      &loop, &QEventLoop::quit);
  QObject::connect(&process, &QProcess::errorOccurred, &loop,
                   [&](QProcess::ProcessError error) {
                     if (error == QProcess::FailedToStart) loop.quit();
                   });
  QTimer timeout;
  timeout.setSingleShot(true);
  QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    process.kill();
  });
  timeout.start(120000);
  ThirdParty::runOtlut(process,
                       {"--source", source, "--target", target, "--output",
                        temporaryPath, "--size", QString::number(size)});
  loop.exec();
  progress.reset();
  if (cancelled) return false;
  QString error;
  if (timedOut)
    error = QObject::tr("OTLUT timed out.");
  else if (process.exitStatus() != QProcess::NormalExit ||
           process.exitCode() != 0 ||
           process.error() == QProcess::FailedToStart) {
    error = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (error.isEmpty()) error = process.errorString();
  } else {
    Lut3D lut;
    if (lut.load(temporaryPath, &error)) {
      QFile input(temporaryPath);
      if (!input.open(QIODevice::ReadOnly))
        error = input.errorString();
      else if (commitOutput(destination, input.readAll(), error))
        return true;
    }
  }
  QMessageBox::warning(parent, title, error);
  return false;
}
