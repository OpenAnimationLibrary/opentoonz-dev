#include "reducecolorscommand.h"
#include "palettecolorreduction.h"

#include "tapp.h"
#include "menubarcommandids.h"
#include "tools/toolutils.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/ttilesaver.h"
#include "toonz/ttileset.h"
#include "toonz/txshcell.h"
#include "toonz/txshcolumn.h"
#include "toonz/txsheet.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "toonzqt/dvdialog.h"
#include "toonzqt/menubarcommand.h"
#include "toonzqt/styleselection.h"
#include "toonzqt/tselectionhandle.h"
#include "tsimplecolorstyles.h"
#include "ttoonzimage.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include <bitset>
#include <limits>
#include <memory>
#include <stdexcept>
#include <typeinfo>

namespace {

using namespace PaletteColorReduction;
using StyleMask = std::bitset<4096>;
bool reducing   = false;

bool getTarget(TXshSimpleLevel *&level, StyleMask &styles, bool &allStyles) {
  TApp *app = TApp::instance();
  if (app->getCurrentFrame()->isPlaying()) return false;
  TXshCell cell = TTool::getImageCell();
  level         = cell.getSimpleLevel();
  if (!level || level->getType() != TZP_XSHLEVEL || level->isReadOnly() ||
      level->isSubsequence())
    return false;
  if (!app->getCurrentFrame()->isEditingLevel()) {
    TXshColumn *column = app->getCurrentXsheet()->getXsheet()->getColumn(
        app->getCurrentColumn()->getColumnIndex());
    if (!column || column->isLocked()) return false;
  }
  TPalette *palette = app->getCurrentPalette()->getPalette();
  if (!palette || palette != level->getPalette() ||
      palette->isCleanupPalette() || palette->isLocked())
    return false;

  TStyleSelection *selection = dynamic_cast<TStyleSelection *>(
      app->getCurrentSelection()->getSelection());
  int selectedCount = 0;
  if (selection && !selection->isEmpty()) {
    if (!selection->getPaletteHandle() || selection->getPalette() != palette)
      return false;
    int pageIndex = selection->getPageIndex();
    if (pageIndex < 0 || pageIndex >= palette->getPageCount()) return false;
    TPalette::Page *page = palette->getPage(pageIndex);
    for (int index : selection->getIndicesInPage()) {
      if (index < 0 || index >= page->getStyleCount()) continue;
      ++selectedCount;
      int id = page->getStyleId(index);
      if (id > 0 && id < int(styles.size())) styles.set(id);
    }
  }
  // Count the user's selection BEFORE filtering animated/protected styles.
  // Selecting two animated styles must not expand to the entire palette.
  allStyles = selectedCount < 2;
  if (allStyles) {
    styles.reset();
    for (int id = 1; id < palette->getStyleCount() && id < int(styles.size());
         ++id)
      styles.set(id);
  }
  return styles.any();
}

bool eligible(TPalette *palette, int id) {
  TColorStyle *style = palette->getStyle(id);
  return style && typeid(*style) == typeid(TSolidColorStyle) &&
         palette->getKeyframeCount(id) == 0 && style->getGlobalName().empty() &&
         style->getOriginalName().empty();
}

// Pump events only outside raster locks and at a bounded frequency. The modal
// progress window prevents editing while the command retains its original
// target.
class Progress {
  QProgressDialog m_dialog;
  QElapsedTimer m_timer;

public:
  Progress()
      : m_dialog(QObject::tr("Analyzing colors..."), QObject::tr("Cancel"), 0,
                 1000, TApp::instance()->getMainWindow()) {
    m_dialog.setWindowTitle(QObject::tr("Reduce Colors"));
    m_dialog.setWindowModality(Qt::ApplicationModal);
    m_dialog.setMinimumDuration(0);
    m_dialog.setAutoClose(false);
    m_dialog.setAutoReset(false);
    m_dialog.show();
    m_timer.start();
  }

  bool canceled(int value = -1) {
    if (m_timer.elapsed() >= 30 || value >= 0) {
      if (value >= 0) m_dialog.setValue(value);
      QApplication::processEvents();
      m_timer.restart();
    }
    return m_dialog.wasCanceled();
  }

  void label(const QString &text) { m_dialog.setLabelText(text); }
};

// RAII also unlocks the raster if tile allocation fails.
class RasterLock {
  TRasterCM32P m_raster;

public:
  explicit RasterLock(const TRasterCM32P &raster) : m_raster(raster) {
    m_raster->lock();
  }
  ~RasterLock() { m_raster->unlock(); }
};

bool mapRaster(const TRasterCM32P &raster, const StyleMap &styles,
               TTileSaverCM32 *saver = nullptr, Progress *progress = nullptr) {
  for (int start = 0; start < raster->getLy(); start += 32) {
    if (progress && progress->canceled()) return false;
    RasterLock lock(raster);
    for (int y = start; y < std::min(start + 32, raster->getLy()); ++y) {
      TPixelCM32 *row = raster->pixels(y);
      for (int x = 0; x < raster->getLx(); ++x) {
        TPixelCM32 pixel = row[x];
        if (!remap(pixel, styles)) continue;
        if (saver) saver->save(TPoint(x, y));
        row[x] = pixel;
      }
    }
  }
  return true;
}

TToonzImageP readFrame(TXshSimpleLevel *level, const TFrameId &fid,
                       bool modify) {
  if (!level->isFid(fid) || level->isFrameReadOnly(fid))
    throw std::runtime_error("Frame is no longer editable");
  TToonzImageP image = level->getFullsampledFrame(
      fid, modify ? ImageManager::toBeModified : ImageManager::none);
  if (!image || !image->getRaster() || image->getSubsampling() > 1)
    throw std::runtime_error("Full-resolution drawing is unavailable");
  return image;
}

class FrameUndo final : public ToolUtils::TRasterUndo {
  std::shared_ptr<const StyleMap> m_styles;

public:
  FrameUndo(TTileSetCM32 *tiles, TXshSimpleLevel *level, const TFrameId &fid,
            const std::shared_ptr<const StyleMap> &styles)
      : TRasterUndo(tiles, level, fid, false, false, nullptr, false)
      , m_styles(styles) {}

  void notify() const {
    m_level->touchFrame(m_frameId);
    notifyImageChanged();
  }
  void undo() const override {
    if (!getImage()) return;
    TRasterUndo::undo();
    notify();
  }
  void redo() const override {
    TToonzImageP image = getImage();
    if (!image || !image->getRaster()) return;
    mapRaster(image->getRaster(), *m_styles);
    notify();
  }
};

class ReduceColorsUndo final : public TUndo {
public:
  std::vector<std::unique_ptr<FrameUndo>> frames;

  void notify() const {
    TApp::instance()->getCurrentLevel()->notifyLevelChange();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }
  void undo() const override {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) (*it)->undo();
    notify();
  }
  void redo() const override {
    for (const auto &frame : frames) frame->redo();
    notify();
  }
  int getSize() const override {
    size_t size = sizeof(*this) + sizeof(StyleMap);
    for (const auto &frame : frames) size += frame->getSize();
    return int(std::min(size, size_t(std::numeric_limits<int>::max())));
  }
  QString getHistoryString() override { return QObject::tr("Reduce Colors"); }
};

struct PreparedFrame {
  TFrameId fid;
  TToonzImageP image;
  TRasterCM32P raster;
  TRect savebox;
};

void executeReduction() {
  TXshSimpleLevel *level = nullptr;
  StyleMask scope;
  bool allStyles = false;
  if (!getTarget(level, scope, allStyles)) return;
  TXshSimpleLevelP keepLevel(level);
  TPaletteP palette(level->getPalette());
  std::vector<TFrameId> fids;
  level->getFids(fids);
  if (fids.empty()) return;
  for (const TFrameId &fid : fids)
    if (level->isFrameReadOnly(fid)) {
      DVGui::warning(
          QObject::tr("This level contains read-only drawings. "
                      "Reduce Colors requires every drawing to be editable."));
      return;
    }

  std::vector<Color> colors;
  int skipped = 0;
  for (int id = 1; id < palette->getStyleCount() && id < int(scope.size());
       ++id) {
    if (!scope[id]) continue;
    if (eligible(palette.getPointer(), id))
      colors.push_back({id, palette->getStyle(id)->getMainColor()});
    else
      ++skipped;
  }
  if (colors.size() < 2) {
    DVGui::info(QObject::tr(
        "There are fewer than two eligible styles. Animated, "
        "linked and non-solid styles are skipped, even when selected."));
    return;
  }

  QDialog dialog(TApp::instance()->getMainWindow());
  dialog.setWindowTitle(QObject::tr("Reduce Colors"));
  QVBoxLayout *layout = new QVBoxLayout(&dialog);
  const auto addText  = [&](const QString &text) {
    QLabel *label = new QLabel(text, &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
  };
  addText(
      (allStyles
           ? QObject::tr("All styles: %1 eligible styles in %2 drawings.")
           : QObject::tr("Selected styles: %1 eligible styles in %2 drawings."))
          .arg(int(colors.size()))
          .arg(int(fids.size())));
  if (skipped)
    addText(
        QObject::tr("%1 animated, linked or non-solid styles will be skipped.")
            .arg(skipped));
  QRadioButton *identical =
      new QRadioButton(QObject::tr("Merge identical colors"), &dialog);
  QRadioButton *reduce = new QRadioButton(
      QObject::tr("Reduce to at most this many colors:"), &dialog);
  QSpinBox *target = new QSpinBox(&dialog);
  target->setRange(1, int(colors.size()));
  target->setValue(std::min(16, int(colors.size())));
  target->setEnabled(false);
  target->setAccessibleName(QObject::tr("Target color count"));
  identical->setChecked(true);
  QObject::connect(reduce, &QRadioButton::toggled, target,
                   &QSpinBox::setEnabled);
  layout->addWidget(identical);
  layout->addWidget(reduce);
  layout->addWidget(target);
  addText(QObject::tr(
      "Identical colors are merged first. The target counts colors "
      "used by the eligible styles in this level. Opacity is preserved; "
      "different opacity values require separate colors."));
  addText(QObject::tr(
      "All drawings in the current level will be processed. Palette "
      "styles are kept; use Delete Unused Styles afterward. Styles "
      "still used by other levels sharing this palette remain in use."));
  const TDimension resolution = level->getResolution();
  const double pixelCount = double(resolution.lx) * resolution.ly * fids.size();
  if (colors.size() >= 256 || fids.size() >= 100 || pixelCount >= 50000000.0)
    addText(QObject::tr(
        "This is a large operation and may take considerable time "
        "and memory. You can cancel during analysis or preparation; "
        "no drawings are changed until preparation finishes."));
  QDialogButtonBox *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Ok)->setText(QObject::tr("Reduce Colors"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                   &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                   &QDialog::reject);
  layout->addWidget(buttons);
  if (dialog.exec() != QDialog::Accepted) return;
  const int requested = reduce->isChecked() ? target->value() : 0;

  auto undo = std::make_unique<ReduceColorsUndo>();
  Plan plan;
  {
    Progress progress;
    Usage usage{};
    Used used{};
    for (size_t f = 0; f < fids.size(); ++f) {
      if (progress.canceled(int(400 * f / fids.size()))) return;
      TToonzImageP image  = readFrame(level, fids[f], false);
      TRasterCM32P raster = image->getRaster();
      for (int start = 0; start < raster->getLy(); start += 32) {
        if (progress.canceled()) return;
        RasterLock lock(raster);
        for (int y = start; y < std::min(start + 32, raster->getLy()); ++y) {
          const TPixelCM32 *row = raster->pixels(y);
          for (int x = 0; x < raster->getLx(); ++x) count(row[x], usage, used);
        }
      }
    }
    progress.label(QObject::tr("Choosing surviving colors..."));
    plan = makePlan(colors, usage, used, requested,
                    [&]() { return progress.canceled(); });
    if (plan.canceled || progress.canceled()) return;
    if (requested > 0 && requested < plan.minimum) {
      DVGui::warning(
          QObject::tr("At least %1 colors are needed to preserve the "
                      "opacity values used in this scope. Choose a target "
                      "of %1 or more. No drawings have been changed.")
              .arg(plan.minimum));
      return;
    }
    if (plan.before == plan.after) {
      DVGui::info(QObject::tr(
          "No colors need to be combined. No drawings have been changed."));
      return;
    }

    progress.label(QObject::tr("Preparing drawings and undo data..."));
    const auto mapping = std::make_shared<const StyleMap>(plan.styles);
    std::vector<PreparedFrame> prepared;
    for (size_t f = 0; f < fids.size(); ++f) {
      if (progress.canceled(400 + int(600 * f / fids.size()))) return;
      TToonzImageP image  = readFrame(level, fids[f], false);
      TRasterCM32P raster = image->getRaster()->clone();
      auto tiles          = std::make_unique<TTileSetCM32>(raster->getSize());
      TTileSaverCM32 saver(raster, tiles.get());
      if (!mapRaster(raster, plan.styles, &saver, &progress)) return;
      if (tiles->getTileCount() == 0) continue;
      // All expensive work is done on copies. A canceled operation never
      // modifies pixels, dirty flags, palette definitions, or undo history.
      auto frameUndo =
          std::make_unique<FrameUndo>(tiles.get(), level, fids[f], mapping);
      tiles.release();
      undo->frames.push_back(std::move(frameUndo));
      prepared.push_back({fids[f], image, raster, image->getSavebox()});
    }
    if (progress.canceled(999)) return;

    TXshSimpleLevel *currentLevel = nullptr;
    StyleMask currentScope;
    bool currentAll = false;
    if (!getTarget(currentLevel, currentScope, currentAll) ||
        currentLevel != level || currentScope != scope ||
        level->getPalette() != palette.getPointer() || level->getFids() != fids)
      throw std::runtime_error("Reduction target changed");
    for (const Color &color : colors)
      if (color.id >= palette->getStyleCount() ||
          !eligible(palette.getPointer(), color.id) ||
          palette->getStyle(color.id)->getMainColor() != color.rgba)
        throw std::runtime_error("Palette changed during reduction");

    // Mark cached images as editable before the commit. Retained image pointers
    // keep them resident. The commit itself only swaps prepared raster
    // pointers.
    for (const PreparedFrame &frame : prepared)
      if (readFrame(level, frame.fid, true) != frame.image)
        throw std::runtime_error("Drawing changed during reduction");
    for (PreparedFrame &frame : prepared) {
      frame.image->setCMapped(frame.raster);
      frame.image->setSavebox(frame.savebox);
    }
  }
  if (undo->frames.empty()) return;
  for (const auto &frame : undo->frames) frame->notify();
  undo->notify();
  TUndoManager::manager()->add(undo.release());
  DVGui::info(
      QObject::tr("Reduced %1 used styles to %2 colors in the chosen scope. "
                  "Palette styles were kept. Use Delete Unused Styles to "
                  "remove styles that are no longer used.")
          .arg(plan.before)
          .arg(plan.after));
}

class ReduceColorsCommand final : public MenuItemHandler {
public:
  ReduceColorsCommand() : MenuItemHandler(MI_ReduceColors) {}
  void execute() override {
    if (reducing) return;
    QScopedValueRollback<bool> guard(reducing, true);
    try {
      executeReduction();
    } catch (...) {
      DVGui::error(QObject::tr(
          "Reduce Colors could not be completed. Check that "
          "all drawings can be loaded and that enough memory is available."));
    }
  }
} reduceColorsCommand;

}  // namespace

void initReduceColorsCommand(QAction *action) {
  TApp *app = TApp::instance();
  action->setToolTip(
      QObject::tr("Reduce colors in every drawing of the current Toonz Raster "
                  "level. Select two or more styles to limit the operation. "
                  "Animated styles are always skipped."));
  const auto update = []() {
    TXshSimpleLevel *level = nullptr;
    StyleMask scope;
    bool allStyles = false;
    CommandManager::instance()->enable(MI_ReduceColors,
                                       getTarget(level, scope, allStyles));
  };
  const auto schedule = [action, update]() {
    QTimer::singleShot(0, action, update);
  };
  QObject::connect(action, &QAction::triggered, action, schedule);
  QObject::connect(app->getCurrentSelection(),
                   &TSelectionHandle::selectionSwitched, action, schedule);
  QObject::connect(app->getCurrentSelection(),
                   &TSelectionHandle::selectionChanged, action, schedule);
  QObject::connect(app->getCurrentLevel(), &TXshLevelHandle::xshLevelSwitched,
                   action, schedule);
  QObject::connect(app->getCurrentLevel(), &TXshLevelHandle::xshLevelChanged,
                   action, schedule);
  QObject::connect(app->getCurrentFrame(), &TFrameHandle::frameSwitched, action,
                   schedule);
  QObject::connect(app->getCurrentFrame(), &TFrameHandle::frameTypeChanged,
                   action, schedule);
  QObject::connect(app->getCurrentFrame(),
                   &TFrameHandle::isPlayingStatusChanged, action, schedule);
  QObject::connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged,
                   action, schedule);
  QObject::connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched,
                   action, schedule);
  QObject::connect(app->getCurrentPalette(), &TPaletteHandle::paletteSwitched,
                   action, schedule);
  QObject::connect(app->getCurrentPalette(), &TPaletteHandle::paletteChanged,
                   action, schedule);
  QObject::connect(app->getCurrentPalette(),
                   &TPaletteHandle::paletteLockChanged, action, schedule);
  QObject::connect(app->getCurrentPalette(), &TPaletteHandle::colorStyleChanged,
                   action, schedule);
  QObject::connect(app->getCurrentColumn(), &TColumnHandle::columnIndexSwitched,
                   action, schedule);
  update();
}
