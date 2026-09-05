#include "convertlinestoareascommand.h"

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
#include "toonzqt/menubarcommand.h"
#include "toonzqt/styleselection.h"
#include "toonzqt/tselectionhandle.h"
#include "ttoonzimage.h"

#include <QTimer>

#include <bitset>
#include <memory>

namespace {

using StyleMask = std::bitset<4096>;

// Only convert ink over transparent paint. Keeping occupied paint slots intact
// avoids changing another style's role or discarding existing area data.
bool convertPixel(TPixelCM32 &pixel, const StyleMask &styles) {
  const int ink = pixel.getInk();
  if (ink == 0 || !styles[ink] || pixel.getPaint() != 0 || pixel.isPurePaint())
    return false;

  pixel = TPixelCM32(0, ink, TPixelCM32::getMaxTone() - pixel.getTone());
  return true;
}

void convertLinesToAreas(const TRasterCM32P &ras, const StyleMask &styles,
                         TTileSaverCM32 *saver = nullptr) {
  ras->lock();
  for (int y = 0; y < ras->getLy(); ++y) {
    TPixelCM32 *row = ras->pixels(y);
    for (int x = 0; x < ras->getLx(); ++x) {
      TPixelCM32 converted = row[x];
      if (!convertPixel(converted, styles)) continue;
      if (saver) saver->save(TPoint(x, y));
      row[x] = converted;
    }
  }
  ras->unlock();
}

bool getTarget(TXshCell &cell, StyleMask &styles) {
  TApp *app = TApp::instance();
  if (app->getCurrentFrame()->isPlaying()) return false;

  cell                = TTool::getImageCell();
  TXshSimpleLevel *sl = cell.getSimpleLevel();
  if (!sl || sl->getType() != TZP_XSHLEVEL || sl->isReadOnly() ||
      !sl->isFid(cell.getFrameId()) || sl->isFrameReadOnly(cell.getFrameId()))
    return false;

  if (!app->getCurrentFrame()->isEditingLevel()) {
    TXsheet *xsheet = app->getCurrentXsheet()->getXsheet();
    TXshColumn *column =
        xsheet->getColumn(app->getCurrentColumn()->getColumnIndex());
    if (!column || column->isLocked()) return false;
  }

  TPaletteHandle *handle = app->getCurrentPalette();
  TPalette *palette      = handle->getPalette();
  if (!palette || palette != sl->getPalette() || palette->isCleanupPalette())
    return false;

  const auto addStyle = [&](int id) {
    if (id > 0 && id < palette->getStyleCount() &&
        id <= TPixelCM32::getMaxInk())
      styles.set(id);
  };
  TStyleSelection *selection = dynamic_cast<TStyleSelection *>(
      app->getCurrentSelection()->getSelection());
  if (selection && !selection->isEmpty()) {
    if (!selection->getPaletteHandle() || selection->getPalette() != palette)
      return false;
    const int pageIndex = selection->getPageIndex();
    if (pageIndex < 0 || pageIndex >= palette->getPageCount()) return false;
    TPalette::Page *page = palette->getPage(pageIndex);
    for (int index : selection->getIndicesInPage()) {
      if (index >= 0 && index < page->getStyleCount())
        addStyle(page->getStyleId(index));
    }
  } else {
    // A shortcut used from the viewer acts on the current drawing style.
    addStyle(handle->getStyleIndex());
  }
  return styles.any();
}

class ConvertLinesToAreasUndo final : public ToolUtils::TRasterUndo {
  StyleMask m_styles;

public:
  ConvertLinesToAreasUndo(TTileSetCM32 *tiles, TXshSimpleLevel *sl,
                          const TFrameId &fid, const StyleMask &styles)
      : TRasterUndo(tiles, sl, fid, false, false, nullptr, false)
      , m_styles(styles) {}

  void notify() const {
    m_level->touchFrame(m_frameId);
    notifyImageChanged();
    TApp::instance()->getCurrentLevel()->notifyLevelChange();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  void undo() const override {
    if (!getImage()) return;
    TRasterUndo::undo();
    notify();
  }

  void redo() const override {
    TToonzImageP image = getImage();
    if (!image || !image->getRaster()) return;
    convertLinesToAreas(image->getRaster(), m_styles);
    notify();
  }

  int getSize() const override {
    return TRasterUndo::getSize() + sizeof(m_styles);
  }

  QString getToolName() override {
    return QObject::tr("Convert Lines to Areas");
  }
};

class ConvertLinesToAreasCommand final : public MenuItemHandler {
public:
  ConvertLinesToAreasCommand() : MenuItemHandler(MI_ConvertLinesToAreas) {}

  void execute() override {
    TXshCell cell;
    StyleMask styles;
    if (!getTarget(cell, styles)) return;

    TToonzImageP image = cell.getImage(true);
    if (!image || !image->getRaster()) return;
    TRasterCM32P ras = image->getRaster();
    auto tiles       = std::make_unique<TTileSetCM32>(ras->getSize());
    TTileSaverCM32 saver(ras, tiles.get());
    convertLinesToAreas(ras, styles, &saver);
    if (tiles->getTileCount() == 0) return;

    auto undo = std::make_unique<ConvertLinesToAreasUndo>(
        tiles.release(), cell.getSimpleLevel(), cell.getFrameId(), styles);
    undo->notify();
    TUndoManager::manager()->add(undo.release());
  }
} convertLinesToAreasCommand;

}  // namespace

void initConvertLinesToAreasCommand(QAction *action) {
  TApp *app = TApp::instance();
  action->setToolTip(QObject::tr(
      "Convert selected styles' lines over transparent areas in the current "
      "Toonz Raster drawing. Pixels with existing area styles are left "
      "unchanged."));
  // Palette clicks update the style and selection in separate steps. Refresh
  // after both changes so Ctrl/Shift selections are evaluated together.
  const auto update = []() {
    TXshCell cell;
    StyleMask styles;
    CommandManager::instance()->enable(MI_ConvertLinesToAreas,
                                       getTarget(cell, styles));
  };
  const auto scheduleUpdate = [action, update]() {
    QTimer::singleShot(0, action, update);
  };
  QObject::connect(app->getCurrentSelection(),
                   &TSelectionHandle::selectionSwitched, action,
                   scheduleUpdate);
  QObject::connect(app->getCurrentSelection(),
                   &TSelectionHandle::selectionChanged, action, scheduleUpdate);
  QObject::connect(app->getCurrentLevel(), &TXshLevelHandle::xshLevelSwitched,
                   action, scheduleUpdate);
  QObject::connect(app->getCurrentFrame(), &TFrameHandle::frameSwitched, action,
                   scheduleUpdate);
  QObject::connect(app->getCurrentFrame(),
                   &TFrameHandle::isPlayingStatusChanged, action,
                   scheduleUpdate);
  QObject::connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetChanged,
                   action, scheduleUpdate);
  QObject::connect(app->getCurrentPalette(), &TPaletteHandle::paletteSwitched,
                   action, scheduleUpdate);
  QObject::connect(app->getCurrentPalette(),
                   &TPaletteHandle::colorStyleSwitched, action, scheduleUpdate);
  QObject::connect(app->getCurrentLevel(), &TXshLevelHandle::xshLevelChanged,
                   action, scheduleUpdate);
  QObject::connect(app->getCurrentColumn(), &TColumnHandle::columnIndexSwitched,
                   action, scheduleUpdate);
  QObject::connect(app->getCurrentFrame(), &TFrameHandle::frameTypeChanged,
                   action, scheduleUpdate);
  QObject::connect(app->getCurrentXsheet(), &TXsheetHandle::xsheetSwitched,
                   action, scheduleUpdate);
  update();
}
