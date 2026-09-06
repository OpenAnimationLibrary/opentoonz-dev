#include "reducecolorscommand.h"
#include "palettecolorreduction.h"

#include "tapp.h"
#include "menubarcommandids.h"
#include "tools/toolutils.h"
#include "toonz/levelset.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
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
#include <QCheckBox>
#include <QComboBox>
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

std::vector<std::vector<int>> palettePages(TPalette *palette) {
  std::vector<std::vector<int>> pages(palette->getPageCount());
  for (int p = 0; p < palette->getPageCount(); ++p) {
    TPalette::Page *page = palette->getPage(p);
    for (int i = 0; i < page->getStyleCount(); ++i)
      pages[p].push_back(page->getStyleId(i));
  }
  return pages;
}

StyleMask paletteScope(TPalette *palette) {
  StyleMask styles;
  // Removed chips keep their numeric slots in TPalette. Only visible palette
  // entries should count toward the dialog's scope and target limit.
  for (int p = 0; p < palette->getPageCount(); ++p) {
    TPalette::Page *page = palette->getPage(p);
    for (int i = 0; i < page->getStyleCount(); ++i) {
      int id = page->getStyleId(i);
      if (id > 0 && id < int(styles.size())) styles.set(id);
    }
  }
  return styles;
}

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
  if (allStyles) styles = paletteScope(palette);
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

bool collectSharedUsage(TXshSimpleLevel *current, Used &used,
                        Progress &progress) {
  // Include unexposed scene-cast levels, not just cells in the current Xsheet.
  TLevelSet *cast =
      TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
  std::vector<TXshSimpleLevelP> levels;
  for (int i = 0; i < cast->getLevelCount(); ++i) {
    TXshSimpleLevel *level = cast->getLevel(i)->getSimpleLevel();
    if (level && level != current &&
        level->getPalette() == current->getPalette())
      levels.push_back(TXshSimpleLevelP(level));
  }
  for (const auto &level : levels) {
    for (const TFrameId &fid : level->getFids()) {
      if (progress.canceled()) return false;
      TImageP image    = level->getFullsampledFrame(fid, ImageManager::none);
      TToonzImageP tlv = image;
      TVectorImageP vector = image;
      if (tlv && tlv->getRaster() && tlv->getSubsampling() <= 1) {
        TRasterCM32P raster = tlv->getRaster();
        for (int start = 0; start < raster->getLy(); start += 32) {
          if (progress.canceled()) return false;
          RasterLock lock(raster);
          for (int y = start; y < std::min(start + 32, raster->getLy()); ++y) {
            const TPixelCM32 *row = raster->pixels(y);
            for (int x = 0; x < raster->getLx(); ++x)
              used[row[x].getInk()] = used[row[x].getPaint()] = true;
          }
        }
      } else if (vector) {
        std::set<int> ids;
        vector->getUsedStyles(ids);
        for (int id : ids)
          if (id >= 0 && id < int(used.size())) used[id] = true;
      } else {
        // An unreadable/unsupported shared level is not evidence of disuse.
        throw std::runtime_error("Cannot verify shared palette usage");
      }
    }
  }
  return true;
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
  TPaletteP m_palette;

public:
  struct RemovedStyle {
    int page, index, id;
    std::unique_ptr<TColorStyle> style;
  };
  std::vector<std::unique_ptr<FrameUndo>> frames;
  std::vector<RemovedStyle> removedStyles;

  explicit ReduceColorsUndo(const TPaletteP &palette) : m_palette(palette) {}

  void removeStyles() const {
    // Original page indices stay valid when removed in reverse order.
    for (auto it = removedStyles.rbegin(); it != removedStyles.rend(); ++it)
      m_palette->getPage(it->page)->removeStyle(it->index);
  }

  void notify() const {
    if (!removedStyles.empty()) {
      m_palette->setDirtyFlag(true);
      TPaletteHandle *handle = TApp::instance()->getCurrentPalette();
      if (handle->getPalette() == m_palette.getPointer()) {
        auto selection = dynamic_cast<TStyleSelection *>(
            TApp::instance()->getCurrentSelection()->getSelection());
        if (selection && selection->getPaletteHandle() &&
            selection->getPalette() == m_palette.getPointer())
          selection->selectNone();
        const int id = handle->getStyleIndex();
        if (id < 0 || id >= m_palette->getStyleCount() ||
            !m_palette->getStylePage(id))
          handle->setStyleIndex(1);
        handle->notifyPaletteChanged();
        handle->notifyColorStyleChanged(false, false);
      }
    }
    TApp::instance()->getCurrentLevel()->notifyLevelChange();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }
  void undo() const override {
    for (const RemovedStyle &removed : removedStyles) {
      // Unpaged IDs may be reused by later style creation. Restore the saved
      // definition, including its name, before putting the chip back.
      m_palette->setStyle(removed.id, removed.style->clone());
      m_palette->getPage(removed.page)->insertStyle(removed.index, removed.id);
    }
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) (*it)->undo();
    notify();
  }
  void redo() const override {
    for (const auto &frame : frames) frame->redo();
    removeStyles();
    notify();
  }
  int getSize() const override {
    size_t size = sizeof(*this) + sizeof(StyleMap);
    size += removedStyles.size() *
            (sizeof(RemovedStyle) + sizeof(TSolidColorStyle));
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
  const StyleMask originalScope = scope;
  TXshSimpleLevelP keepLevel(level);
  TPaletteP palette(level->getPalette());
  const auto originalPages = palettePages(palette.getPointer());
  ToonzScene *scene        = TApp::instance()->getCurrentScene()->getScene();
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

  QDialog dialog(TApp::instance()->getMainWindow());
  dialog.setWindowTitle(QObject::tr("Reduce Colors"));
  QVBoxLayout *layout = new QVBoxLayout(&dialog);
  const auto addText  = [&](const QString &text) {
    QLabel *label = new QLabel(text, &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);
    return label;
  };
  addText(QObject::tr("Styles to process:"));
  QComboBox *scopeChoice = new QComboBox(&dialog);
  scopeChoice->setAccessibleName(QObject::tr("Styles to process"));
  if (!allStyles) scopeChoice->addItem(QObject::tr("Selected styles"));
  scopeChoice->addItem(QObject::tr("All palette styles"));
  layout->addWidget(scopeChoice);
  QLabel *scopeInfo = addText(QString());
  QRadioButton *identical =
      new QRadioButton(QObject::tr("Merge identical colors"), &dialog);
  QRadioButton *reduce = new QRadioButton(
      QObject::tr("Reduce to at most this many colors:"), &dialog);
  QSpinBox *target = new QSpinBox(&dialog);
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
      "used by the eligible styles in this scope. Opacity is preserved; "
      "different opacity values require separate colors."));
  addText(
      QObject::tr("The target cannot exceed the eligible style count. "
                  "Increasing it does not restore previously combined colors; "
                  "use Undo for that."));
  QCheckBox *cleanup =
      new QCheckBox(QObject::tr("Remove unused styles in this scope"), &dialog);
  cleanup->setChecked(true);
  layout->addWidget(cleanup);
  addText(QObject::tr(
      "All drawings in the current level will be processed. Cleanup keeps "
      "reserved, animated, linked and non-solid styles, and styles still used "
      "by other levels in the scene sharing this palette."));
  const TDimension resolution = level->getResolution();
  const double pixelCount = double(resolution.lx) * resolution.ly * fids.size();
  QLabel *largeOperation  = addText(
      QObject::tr("This is a large operation and may take considerable time "
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

  std::vector<Color> colors;
  const auto updateScope = [&]() {
    scope = (allStyles || scopeChoice->currentIndex() == 1)
                ? paletteScope(palette.getPointer())
                : originalScope;
    colors.clear();
    int skipped = 0;
    for (int id = 1; id < palette->getStyleCount() && id < int(scope.size());
         ++id) {
      if (!scope[id]) continue;
      if (eligible(palette.getPointer(), id))
        colors.push_back({id, palette->getStyle(id)->getMainColor()});
      else
        ++skipped;
    }
    scopeInfo->setText(
        QObject::tr("%1 eligible styles in %2 drawings. "
                    "%3 animated, linked or non-solid styles will be skipped.")
            .arg(int(colors.size()))
            .arg(int(fids.size()))
            .arg(skipped));
    target->setRange(1, std::max(1, int(colors.size())));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!colors.empty());
    largeOperation->setVisible(colors.size() >= 256 || fids.size() >= 100 ||
                               pixelCount >= 50000000.0);
  };
  QObject::connect(scopeChoice,
                   QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog,
                   updateScope);
  updateScope();
  target->setValue(std::min(16, int(colors.size())));
  if (dialog.exec() != QDialog::Accepted) return;
  const int requested = reduce->isChecked() ? target->value() : 0;

  auto undo = std::make_unique<ReduceColorsUndo>(palette);
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
    if (cleanup->isChecked()) {
      progress.label(
          QObject::tr("Checking unused styles and shared palettes..."));
      Used remaining = remappedUsage(used, plan.styles);
      if (!collectSharedUsage(level, remaining, progress)) return;
      for (int p = 0; p < palette->getPageCount(); ++p) {
        TPalette::Page *page = palette->getPage(p);
        for (int i = 0; i < page->getStyleCount(); ++i) {
          const int id = page->getStyleId(i);
          // Match Delete Unused Styles' protection for the first two chips.
          // Keep ID 1 protected even if the user has moved it to another page.
          if ((p == 0 && i < 2) || id <= 1 || id >= int(scope.size()) ||
              !scope[id] || remaining[id] ||
              !eligible(palette.getPointer(), id))
            continue;
          undo->removedStyles.push_back(
              {p, i, id,
               std::unique_ptr<TColorStyle>(palette->getStyle(id)->clone())});
        }
      }
    }

    progress.label(QObject::tr("Preparing drawings and undo data..."));
    const auto mapping = std::make_shared<const StyleMap>(plan.styles);
    std::vector<PreparedFrame> prepared;
    for (size_t f = 0; plan.before != plan.after && f < fids.size(); ++f) {
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
        currentLevel != level || currentScope != originalScope ||
        level->getPalette() != palette.getPointer() ||
        level->getFids() != fids ||
        palettePages(palette.getPointer()) != originalPages ||
        TApp::instance()->getCurrentScene()->getScene() != scene)
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
    undo->removeStyles();
  }
  if (undo->frames.empty() && undo->removedStyles.empty()) {
    DVGui::info(QObject::tr(
        "No colors need to be combined and no styles need to be removed."));
    return;
  }
  const int removedCount = int(undo->removedStyles.size());
  for (const auto &frame : undo->frames) frame->notify();
  undo->notify();
  TUndoManager::manager()->add(undo.release());
  DVGui::info(
      QObject::tr("Reduced %1 used styles to %2 colors in the chosen scope. "
                  "Removed %3 unused styles.")
          .arg(plan.before)
          .arg(plan.after)
          .arg(removedCount));
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
