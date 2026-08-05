
#include "levelcommand.h"
#include "toonzqt/menubarcommand.h"
#include "menubarcommandids.h"
#include "tapp.h"
#include "toonz/tscenehandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/txsheethandle.h"
#include "filmstripselection.h"
#include "castselection.h"
#include "cellselection.h"
#include "timagecache.h"

#include "toonz/txshsimplelevel.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshleveltypes.h"
#include "toonz/levelset.h"
#include "toonz/txshcell.h"
#include "toonz/childstack.h"
#include "toonz/txshchildlevel.h"
#include "toonz/textureutils.h"

#include "toonzqt/dvdialog.h"
#include "toonzqt/icongenerator.h"

#include "tundo.h"
#include "tconvert.h"
#include "tlevel_io.h"
#include "trasterimage.h"
#include "ttoonzimage.h"
#include "tsystem.h"

#include "toonzqt/gutil.h"
#include "toonz/namebuilder.h"

#include <QProgressDialog>
#include <QMainWindow>
#include <QApplication>
#include <QMessageBox>
#include <QPushButton>

#include <cstring>

namespace {

class DeleteLevelUndo final : public TUndo {
  TXshLevelP m_xl;

public:
  DeleteLevelUndo(TXshLevel *xl) : m_xl(xl) {}

  void undo() const override {
    ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
    scene->getLevelSet()->insertLevel(m_xl.getPointer());
    TApp::instance()->getCurrentScene()->notifyCastChange();
  }
  void redo() const override {
    ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
    scene->getLevelSet()->removeLevel(m_xl.getPointer());
    TApp::instance()->getCurrentScene()->notifyCastChange();
  }

  int getSize() const override { return sizeof *this + 100; }

  QString getHistoryString() override {
    return QObject::tr("Delete Level  : %1")
        .arg(QString::fromStdWString(m_xl->getName()));
  }
};

}  // namespace

//-----------------------------------------------------------------------------

bool LevelCmd::removeUnusedLevelsFromCast(bool showMessage) {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();

  TLevelSet *levelSet = scene->getLevelSet();

  std::set<TXshLevel *> usedLevels;
  scene->getTopXsheet()->getUsedLevels(usedLevels);

  std::vector<TXshLevel *> unused;

  for (int i = 0; i < levelSet->getLevelCount(); i++) {
    TXshLevel *xl = levelSet->getLevel(i);
    if (usedLevels.count(xl) == 0) unused.push_back(xl);
  }
  if (unused.empty()) {
    if (showMessage) DVGui::error(QObject::tr("No unused levels"));
    return false;
  } else {
    TUndoManager *um = TUndoManager::manager();
    um->beginBlock();
    for (int i = 0; i < (int)unused.size(); i++) {
      TXshLevel *xl = unused[i];
      um->add(new DeleteLevelUndo(xl));
      scene->getLevelSet()->removeLevel(xl);
    }
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->notifyCastChange();

    um->endBlock();
  }
  return true;
}

bool LevelCmd::removeLevelFromCast(TXshLevel *level, ToonzScene *scene,
                                   bool showMessage) {
  if (!scene) scene = TApp::instance()->getCurrentScene()->getScene();
  if (scene->getChildStack()->getTopXsheet()->isLevelUsed(level)) {
    if (showMessage) {
      DVGui::error(
          QObject::tr("It is not possible to delete the used level %1.")
              .arg(QString::fromStdWString(
                  level->getName())));  //"E_CantDeleteUsedLevel_%1"
    }
    return false;
  } else {
    TUndoManager *um = TUndoManager::manager();
    um->add(new DeleteLevelUndo(level));
    scene->getLevelSet()->removeLevel(level);
  }
  return true;
}

void LevelCmd::loadAllUsedRasterLevelsAndPutInCache(bool cacheImagesAsWell) {
  TApp *app         = TApp::instance();
  ToonzScene *scene = app->getCurrentScene()->getScene();

  TLevelSet *levelSet = scene->getLevelSet();

  std::set<TXshLevel *> usedLevels;
  scene->getTopXsheet()->getUsedLevels(usedLevels);

  std::map<TXshSimpleLevel *, int>
      targetLevels;  // level pointer and its frame amount
  int totalFrames = 0;
  // estimate the amount
  for (auto xl : usedLevels) {
    TXshSimpleLevel *simpleLevel = xl->getSimpleLevel();
    if (simpleLevel && (simpleLevel->getType() == TZP_XSHLEVEL ||
                        simpleLevel->getType() == OVL_XSHLEVEL)) {
      targetLevels[simpleLevel] = simpleLevel->getFrameCount();
      totalFrames += simpleLevel->getFrameCount();
    }
  }

  // if the amount of frames is more than 10, open a progress dialog and
  // shows WaitCursor
  QProgressDialog *pd = nullptr;
  if (totalFrames > 10) {
    pd = new QProgressDialog(QObject::tr("Loading Raster Images To Cache..."),
                             QObject::tr("Cancel"), 0, totalFrames,
                             app->getMainWindow());
    pd->setAttribute(Qt::WA_DeleteOnClose, true);
    pd->setWindowModality(Qt::WindowModal);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    pd->show();
  }
  std::map<TXshSimpleLevel *, int>::iterator i = targetLevels.begin();
  while (i != targetLevels.end()) {
    if (pd && pd->wasCanceled()) break;
    i->first->loadAllIconsAndPutInCache(cacheImagesAsWell);
    if (pd) {
      pd->setValue(pd->value() + i->second);
      QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    ++i;
  }

  if (pd) {
    QApplication::restoreOverrideCursor();
    pd->close();
  }
  return;
}

//=============================================================================
// RemoveUnusedLevelCommand
//-----------------------------------------------------------------------------

class RemoveUnusedLevelsCommand final : public MenuItemHandler {
public:
  RemoveUnusedLevelsCommand() : MenuItemHandler(MI_RemoveUnused) {}

  void execute() override { LevelCmd::removeUnusedLevelsFromCast(); }
} removeUnusedLevelsCommand;

//=============================================================================
// RemoveLevelCommand
//-----------------------------------------------------------------------------

class RemoveLevelCommand final : public MenuItemHandler {
public:
  RemoveLevelCommand() : MenuItemHandler(MI_RemoveLevel) {}

  void execute() override {
    TXsheet *xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
    CastSelection *castSelection =
        dynamic_cast<CastSelection *>(TSelection::getCurrent());
    if (!castSelection) return;

    std::vector<TXshLevel *> levels;
    castSelection->getSelectedLevels(levels);
    if (levels.empty()) {
      DVGui::error("No level selected");  // E_NoSelectedLevel
      return;
    }
    int count = 0;
    for (int i = 0; i < (int)levels.size(); i++)
      if (LevelCmd::removeLevelFromCast(levels[i])) count++;
    if (count == 0) return;
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->notifyCastChange();
  }

} removeLevelCommand;

//=============================================================================
namespace {
//-----------------------------------------------------------------------------

TFilePath getUnpaintedLevelPath(TXshSimpleLevel *simpleLevel) {
  ToonzScene *scene   = simpleLevel->getScene();
  TFilePath levelPath = scene->decodeFilePath(simpleLevel->getPath());
  if (levelPath.isEmpty()) return TFilePath();
  std::string name = levelPath.getName() + "_np." + levelPath.getType();
  return levelPath.getParentDir() + "nopaint\\" + TFilePath(name);
}

//-----------------------------------------------------------------------------

void getLevelSelectedFids(std::set<TFrameId> &fids, TXshSimpleLevel *level,
                          int r0, int c0, int r1, int c1) {
  TXsheet *xsheet = TApp::instance()->getCurrentXsheet()->getXsheet();
  int c, r;
  for (c = c0; c <= c1; c++)
    for (r = r0; r <= r1; r++) {
      TXshCell cell             = xsheet->getCell(r, c);
      TXshSimpleLevel *curLevel = (!cell.isEmpty()) ? cell.getSimpleLevel() : 0;
      if (curLevel != level) continue;
      fids.insert(cell.getFrameId());
    }
}

//-----------------------------------------------------------------------------

enum class ImageComparison { Same, Different, Unsupported };

bool rasterDataMatches(const TRasterP &cachedRaster,
                       const TRasterP &diskRaster) {
  if (!cachedRaster || !diskRaster) return false;
  if (cachedRaster->getLx() != diskRaster->getLx() ||
      cachedRaster->getLy() != diskRaster->getLy() ||
      cachedRaster->getPixelSize() != diskRaster->getPixelSize())
    return false;

  if (cachedRaster->isEmpty()) return true;

  cachedRaster->lock();
  diskRaster->lock();

  bool matches       = true;
  const int rowBytes = cachedRaster->getRowSize();
  for (int y = 0; y < cachedRaster->getLy(); ++y) {
    if (std::memcmp(cachedRaster->getRawData(0, y),
                    diskRaster->getRawData(0, y), rowBytes) != 0) {
      matches = false;
      break;
    }
  }

  diskRaster->unlock();
  cachedRaster->unlock();
  return matches;
}

ImageComparison compareCachedAndDiskImages(const TImageP &cachedImage,
                                           const TImageP &diskImage) {
  if (!cachedImage || !diskImage) return ImageComparison::Unsupported;

  if (TRasterImageP cachedRasterImage = cachedImage) {
    TRasterImageP diskRasterImage = diskImage;
    if (!diskRasterImage) return ImageComparison::Different;

    return rasterDataMatches(cachedRasterImage->getRaster(),
                             diskRasterImage->getRaster())
               ? ImageComparison::Same
               : ImageComparison::Different;
  }

  if (TToonzImageP cachedToonzImage = cachedImage) {
    TToonzImageP diskToonzImage = diskImage;
    if (!diskToonzImage) return ImageComparison::Different;

    return rasterDataMatches(cachedToonzImage->getRaster(),
                             diskToonzImage->getRaster())
               ? ImageComparison::Same
               : ImageComparison::Different;
  }

  return ImageComparison::Unsupported;
}

struct LoadedFrame {
  TFrameId m_fid;
  TImageP m_image;
};

bool confirmChangedSource(TXshSimpleLevel *sl, const TFilePath &path) {
  QMessageBox box(TApp::instance()->getMainWindow());
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(QObject::tr("Source File Changed"));
  box.setText(
      QObject::tr("The source data for level %1 differs from the version "
                  "currently loaded in OpenToonz.")
          .arg(QString::fromStdWString(sl->getName())));
  box.setInformativeText(
      QObject::tr("Load the updated file, or keep the current in-memory "
                  "version unchanged.\n\nSource: %1")
          .arg(QString::fromStdWString(path.getWideString())));

  QPushButton *loadButton =
      box.addButton(QObject::tr("Load Updated File"), QMessageBox::AcceptRole);
  QPushButton *keepButton = box.addButton(
      QObject::tr("Keep Current Version"), QMessageBox::RejectRole);
  box.setDefaultButton(loadButton);
  box.setEscapeButton(keepButton);
  box.exec();

  return box.clickedButton() == loadButton;
}

//-----------------------------------------------------------------------------

bool loadFids(const TFilePath &path, TXshSimpleLevel *sl,
              const std::set<TFrameId> &selectedFids,
              bool verifyCachedSource = false) {
  assert(sl && !selectedFids.empty());

  TLevelReaderP levelReader = TLevelReaderP(path);
  if (!levelReader.getPointer()) return false;

  // Load disk images first so that they can be compared with unmodified cached
  // images before either version is discarded.
  TLevelP level = levelReader->loadInfo();
  if (!level || level->getFrameCount() == 0) return false;

  std::vector<LoadedFrame> loadedFrames;
  bool sourceDiffers = false;

  for (TLevel::Iterator levelIt = level->begin(); levelIt != level->end();
       ++levelIt) {
    TFrameId fid = levelIt->first;
    if (selectedFids.find(fid) == selectedFids.end()) continue;

    TImageP diskImage = levelReader->getFrameReader(fid)->load();
    if (!diskImage) continue;

    loadedFrames.push_back({fid, diskImage});

    // Dirty levels are intentionally being reverted, so the normal Revert To
    // Last Saved workflow remains unchanged. The comparison targets clean,
    // externally sourced levels whose cached image may have become stale.
    const std::string imageId = sl->getImageId(fid);
    if (verifyCachedSource && !sl->getDirtyFlag() &&
        ImageManager::instance()->isCached(imageId) &&
        sl->getImageSubsampling(fid) == 1) {
      TImageP cachedImage = sl->getFrame(fid, false);
      if (compareCachedAndDiskImages(cachedImage, diskImage) ==
          ImageComparison::Different)
        sourceDiffers = true;
    }
  }

  if (loadedFrames.empty()) return false;

  if (sourceDiffers && !confirmChangedSource(sl, path)) return false;

  for (const LoadedFrame &frame : loadedFrames) {
    sl->invalidateFrame(frame.m_fid);
    texture_utils::invalidateTexture(sl, frame.m_fid);
    sl->setFrame(frame.m_fid, frame.m_image);
  }

  invalidateIcons(sl, selectedFids);
  return true;
}

//-----------------------------------------------------------------------------

bool loadUnpaintedFids(TXshSimpleLevel *sl,
                       const std::set<TFrameId> &selectedFids) {
  TFilePath path = getUnpaintedLevelPath(sl);

  if (!TSystem::doesExistFileOrLevel(path)) {
    DVGui::error(QObject::tr(
        "No cleaned up drawings available for the current selection."));
    return false;
  }

  return loadFids(path, sl, selectedFids);
}

//-----------------------------------------------------------------------------

bool loadLastSaveFids(TXshSimpleLevel *sl,
                      const std::set<TFrameId> &selectedFids,
                      bool verifyCachedSource = true) {
  TFilePath path = sl->getPath();
  path           = sl->getScene()->decodeFilePath(path);

  if (!TSystem::doesExistFileOrLevel(path)) {
    DVGui::error(
        QObject::tr("No saved drawings available for the current selection."));
    return false;
  }

  return loadFids(path, sl, selectedFids, verifyCachedSource);
}

//=============================================================================
// Undo RevertToCommandUndo
//-----------------------------------------------------------------------------

class RevertToCommandUndo final : public TUndo {
  TXshSimpleLevel *m_sl;
  std::vector<QString> m_replacedImgsId;
  std::set<TFrameId> m_selectedFids;
  bool m_isCleanedUp;

public:
  RevertToCommandUndo(TXshSimpleLevel *sl, std::set<TFrameId> &selectedFids,
                      bool isCleanedUp)
      : m_sl(sl), m_selectedFids(selectedFids), m_isCleanedUp(isCleanedUp) {
    static int revertToCommandCount = 0;
    for (auto const &fid : m_selectedFids) {
      if (!sl->isFid(fid)) continue;
      TImageP image = sl->getFrame(fid, false);
      assert(image);
      QString newImageId = "RevertToUndo" +
                           QString::number(revertToCommandCount) + "-" +
                           QString::number(fid.getNumber());
      TImageCache::instance()->add(newImageId, image->cloneImage());
      m_replacedImgsId.push_back(newImageId);
    }
    revertToCommandCount++;
  }

  ~RevertToCommandUndo() {
    int i;
    for (i = 0; i < (int)m_replacedImgsId.size(); i++)
      TImageCache::instance()->remove(m_replacedImgsId[i]);
  }

  void undo() const override {
    assert((int)m_replacedImgsId.size() == (int)m_selectedFids.size());
    int i = 0;
    for (auto const &fid : m_selectedFids) {
      QString imageId = m_replacedImgsId[i++];
      TImageP img = TImageCache::instance()->get(imageId, false)->cloneImage();
      if (!img.getPointer()) continue;
      m_sl->invalidateFrame(fid);
      texture_utils::invalidateTexture(m_sl, fid);
      m_sl->setFrame(fid, img);
    }
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    invalidateIcons(m_sl, m_selectedFids);
  }

  void redo() const override {
    if (m_isCleanedUp)
      loadUnpaintedFids(m_sl, m_selectedFids);
    else
      loadLastSaveFids(m_sl, m_selectedFids, false);
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
  }

  int getSize() const override {
    return sizeof(*this) + m_selectedFids.size() * sizeof(TFrameId);
  }

  QString getHistoryString() override {
    return QObject::tr("Revert To %1  : Level %2")
        .arg((m_isCleanedUp) ? QString("Cleaned Up") : QString("Last Saved"))
        .arg(QString::fromStdWString(m_sl->getName()));
  }
};

//-----------------------------------------------------------------------------
/*--isCleanedUpが	Trueのとき： "revert to cleaned up" コマンド
                                        Falseのとき："revert to last saved"
コマンド
--*/
void revertTo(bool isCleanedUp) {
  TApp *app = TApp::instance();

  TFilmstripSelection *filmstripSelection =
      dynamic_cast<TFilmstripSelection *>(TSelection::getCurrent());
  TCellSelection *cellSelection =
      dynamic_cast<TCellSelection *>(TSelection::getCurrent());

  /*-- FilmStrip選択の場合 --*/
  if (filmstripSelection) {
    std::set<TFrameId> selectedFids = filmstripSelection->getSelectedFids();
    TXshSimpleLevel *sl             = app->getCurrentLevel()->getSimpleLevel();
    if (!sl || selectedFids.empty()) {
      DVGui::error(QObject::tr("The current selection is invalid."));
      return;
    }
    RevertToCommandUndo *undo =
        new RevertToCommandUndo(sl, selectedFids, isCleanedUp);
    bool commandExecuted = false;
    if (isCleanedUp)
      commandExecuted = loadUnpaintedFids(sl, selectedFids);
    else
      commandExecuted = loadLastSaveFids(sl, selectedFids);
    if (!commandExecuted)
      delete undo;
    else {
      TUndoManager::manager()->add(undo);
      if (isCleanedUp) sl->setDirtyFlag(true);
    }
    app->getCurrentLevel()->notifyLevelChange();
  }
  /*-- セル選択の場合 --*/
  else if (cellSelection) {
    std::set<TXshSimpleLevel *> levels;
    int r0, r1, c0, c1;
    cellSelection->getSelectedCells(r0, c0, r1, c1);
    TXsheet *xsheet = app->getCurrentXsheet()->getXsheet();
    // Cerco tutti i livelli, con estensensione "tlv", contenuti nella selezione
    bool selectionContainLevel = false;
    /*-- セル選択範囲の各セルについて --*/
    int c, r;
    for (c = c0; c <= c1; c++)
      for (r = r0; r <= r1; r++) {
        TXshCell cell          = xsheet->getCell(r, c);
        TXshSimpleLevel *level = (!cell.isEmpty()) ? cell.getSimpleLevel() : 0;
        if (!level) continue;
        std::string ext = level->getPath().getType();
        int type        = level->getType();
        /*-- Revert可能なLevelタイプの条件 --*/
        if ((isCleanedUp && type == TZP_XSHLEVEL) ||
            (!isCleanedUp && (type == TZP_XSHLEVEL || type == PLI_XSHLEVEL ||
                              type == OVL_XSHLEVEL))) {
          levels.insert(level);
          selectionContainLevel = true;
        }
      }
    if (levels.empty() || !selectionContainLevel) {
      DVGui::error(
          QObject::tr("The Reload command is not supported for "
                      "the current selection."));
      return;
    }
    // Per ogni livello trovo i TFrameId contenuti nella selezione e richiamo
    // loadLastSaveFids.
    TUndoManager::manager()->beginBlock();
    std::set<TXshSimpleLevel *>::iterator it = levels.begin();
    /*-- Revert対象の各レベルについて --*/
    for (auto const sl : levels) {
      std::set<TFrameId> selectedFids;
      /*- 選択範囲のFrameIdを取得する -*/
      getLevelSelectedFids(selectedFids, *it, r0, c0, r1, c1);
      RevertToCommandUndo *undo =
          new RevertToCommandUndo(sl, selectedFids, isCleanedUp);
      bool commandExecuted = false;
      /*- "Revert to Cleaned up" の場合 -*/
      if (isCleanedUp) commandExecuted = loadUnpaintedFids(sl, selectedFids);
      /*- "Revert to Last Saved" の場合 -*/
      else
        commandExecuted = loadLastSaveFids(sl, selectedFids);
      if (!commandExecuted)
        delete undo;
      else {
        TUndoManager::manager()->add(undo);
        if (isCleanedUp) sl->setDirtyFlag(true);
      }
    }
    TUndoManager::manager()->endBlock();
    app->getCurrentXsheet()->notifyXsheetChanged();
  } else
    DVGui::error(QObject::tr("The current selection is invalid."));
}

//-----------------------------------------------------------------------------
}  // namespace
//=============================================================================

//=============================================================================
// RevertToCleanedUpCommand
//-----------------------------------------------------------------------------

class RevertToCleanedUpCommand final : public MenuItemHandler {
public:
  RevertToCleanedUpCommand() : MenuItemHandler(MI_RevertToCleanedUp) {}

  void execute() override { revertTo(true); }

} revertToCleanedUpCommand;

//=============================================================================
// RevertToLastSaveCommand
//-----------------------------------------------------------------------------

class RevertToLastSaveCommand final : public MenuItemHandler {
public:
  RevertToLastSaveCommand() : MenuItemHandler(MI_RevertToLastSaved) {}

  void execute() override { revertTo(false); }

} revertToLastSaveCommand;

//-----------------------------------------------------------------------------
namespace {
class addLevelToCastUndo final : public TUndo {
  TXshLevelP m_xl;
  std::wstring m_newName, m_oldName;

public:
  addLevelToCastUndo(TXshLevel *xl, const std::wstring newName = L"",
                     const std::wstring oldName = L"")
      : m_xl(xl), m_newName(newName), m_oldName(oldName) {}

  void undo() const override {
    TLevelSet *levelSet =
        TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
    levelSet->removeLevel(m_xl.getPointer());
    if (m_oldName != L"") m_xl->setName(m_oldName);
    if (m_isLastInBlock)
      TApp::instance()->getCurrentScene()->notifyCastChange();
  }
  void redo() const override {
    TLevelSet *levelSet =
        TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
    if (m_newName != L"") m_xl->setName(m_newName);
    levelSet->insertLevel(m_xl.getPointer());
    if (m_isLastInRedoBlock)
      TApp::instance()->getCurrentScene()->notifyCastChange();
  }

  int getSize() const override { return sizeof *this + 100; }

  QString getHistoryString() override {
    return QObject::tr("Add Level to Scene Cast : %1")
        .arg(QString::fromStdWString(m_xl->getName()));
  }
};

};  // namespace

void LevelCmd::addMissingLevelsToCast(const QList<TXshColumnP> &columns) {
  // make sure that the levels contained in the pasted columns are registered in
  // the scene cast it may rename the level if there is another level with the
  // same name
  std::set<TXshLevel *> levels;
  // obtain level set contained in the specified columns
  // it is used for checking and updating the scene cast when pasting
  // based on TXsheet::getUsedLevels
  for (auto column : columns) {
    if (!column) continue;

    TXshCellColumn *cellColumn = column->getCellColumn();
    if (!cellColumn) continue;

    int r0, r1;
    if (!cellColumn->getRange(r0, r1)) continue;

    TXshLevel *level = 0;
    for (int r = r0; r <= r1; r++) {
      TXshCell cell = cellColumn->getCell(r);
      if (cell.isEmpty() || !cell.m_level) continue;

      if (level != cell.m_level.getPointer()) {
        level = cell.m_level.getPointer();
        levels.insert(level);
        if (level->getChildLevel()) {
          TXsheet *childXsh = level->getChildLevel()->getXsheet();
          childXsh->getUsedLevels(levels);
        }
      }
    }
  }
  LevelCmd::addMissingLevelsToCast(levels);
}

void LevelCmd::addMissingLevelsToCast(std::set<TXshLevel *> &levels) {
  // remove zerary fx levels which are not registered in the cast
  for (auto it = levels.begin(); it != levels.end();) {
    if ((*it)->getZeraryFxLevel())
      it = levels.erase(it);
    else
      ++it;
  }

  if (levels.empty()) return;
  TUndoManager::manager()->beginBlock();
  TLevelSet *levelSet =
      TApp::instance()->getCurrentScene()->getScene()->getLevelSet();
  bool castChanged = false;
  // for each level
  for (auto level : levels) {
    std::wstring levelName = level->getName();

    // search by level name
    if (TXshLevel *levelInCast = levelSet->getLevel(levelName)) {
      // continue if it is the same level. This should be in most cases
      if (level == levelInCast) continue;

      // if the the name is occupied by another level, then rename and register
      // it
      std::wstring oldName = levelName;
      NameModifier nm(levelName);
      levelName = nm.getNext();
      while (1) {
        TXshLevel *existingLevel = levelSet->getLevel(levelName);
        // if the level name is not used in the cast, nothing to do
        if (!existingLevel) break;
        // try if the existing level is unused in the xsheet and remove from the
        // cast
        else if (Preferences::instance()->isAutoRemoveUnusedLevelsEnabled() &&
                 LevelCmd::removeLevelFromCast(
                     existingLevel,
                     TApp::instance()->getCurrentScene()->getScene(), false)) {
          DVGui::info(
              QObject::tr("Removed unused level %1 from the scene cast. (This "
                          "behavior can be disabled in Preferences.)")
                  .arg(QString::fromStdWString(levelName)));
          break;
        }
        levelName = nm.getNext();
      }
      addLevelToCastUndo *undo =
          new addLevelToCastUndo(level, levelName, oldName);
      undo->m_isLastInRedoBlock = false;  // prevent to emit signal
      undo->redo();
      TUndoManager::manager()->add(undo);
      castChanged = true;
    }
    // if not found
    else {
      // register the level
      addLevelToCastUndo *undo = new addLevelToCastUndo(level);
      undo->redo();
      TUndoManager::manager()->add(undo);
      castChanged = true;
    }
  }

  TUndoManager::manager()->endBlock();

  if (castChanged) TApp::instance()->getCurrentScene()->notifyCastChange();
}
