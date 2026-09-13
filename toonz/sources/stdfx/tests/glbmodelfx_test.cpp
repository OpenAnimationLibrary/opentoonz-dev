// Exercise the production FX and CPU renderer. As with lut3dbake_test, compile
// the production source here without
// linking tnzstdfx so the same FX factory registration is tested in isolation.
#include "../glbmodelfx.cpp"
#include "tfilepath.h"
#include "tparamcontainer.h"
#include "tstream.h"
#include "trop.h"
#include "../../glb/tests/glbfixture.h"
#include "toonzqt/paramfield.h"
#include "toonzqt/functiontreeviewer.h"
#include "toonz/txsheet.h"
#include "toonz/fxdag.h"
#include "toonz/tcolumnfxset.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template <class PARAM>
PARAM *parameter(GlbModelFx &fx, const char *name) {
  auto *param = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  require(param != nullptr, "Missing parameter or unexpected parameter type");
  return param;
}

struct DoubleCase {
  const char *name;
  double defaultValue;
  double firstValue;
  double lastValue;
};

const DoubleCase doubleCases[] = {
    {"positionX", 0.0, -2.0, 3.0},
    {"positionY", 0.0, 1.0, 4.0},
    {"positionZ", 0.0, -1.0, 2.0},
    {"rotationX", 0.0, 15.0, 30.0},
    {"rotationY", 0.0, -20.0, 40.0},
    {"rotationZ", 0.0, 10.0, 45.0},
    {"scale", 100.0, 50.0, 150.0},
    {"cameraDistance", 10.0, 5.0, 20.0},
    {"fieldOfView", 45.0, 30.0, 60.0},
    {"orthoSize", 10.0, 5.0, 15.0},
    {"nearClip", 0.1, 0.2, 0.5},
    {"farClip", 1000.0, 100.0, 500.0},
};

struct EnumCase {
  const char *name;
  const char *firstCaption;
  const char *lastCaption;
};

const EnumCase enumCases[] = {
    {"projection", "Orthographic", "Perspective"},
    {"lighting", "Unlit", "Headlight"},
    {"renderStyle", "Solid", "Wireframe"},
};

void expectValue(double actual, double expected) {
  require(std::fabs(actual - expected) < 0.000001,
          "Unexpected parameter value");
}

void testDefaults() {
  GlbModelFx fx;
  require(fx.getFxType() == "STD_glbModelFx", "Unexpected FX type");
  require(fx.isZerary(), "GLB Model is not a zerary FX");
  require(fx.getInputPortCount() == 0, "GLB Model acquired an input port");
  require(fx.getParams()->getParamCount() == 18,
          "Unexpected framework parameter count");
  require(parameter<TIntEnumParam>(fx, "colorMode")->getValue() == 0,
          "Existing scenes must retain grayscale by default");
  require(parameter<TParamSet>(fx, "materialColors")->getValueAlias(0, 3) == "()",
          "Empty material overrides have no safe alias");
  require(parameter<TStringParam>(fx, "modelFile")->getValue().empty(),
          "Default model path is not empty");
  for (const DoubleCase &test : doubleCases) {
    auto *param = parameter<TDoubleParam>(fx, test.name);
    expectValue(param->getValue(0), test.defaultValue);
    require(param->getKeyframeCount() == 0, "Default parameter has keyframes");
  }
  for (const EnumCase &test : enumCases) {
    auto *param = parameter<TIntEnumParam>(fx, test.name);
    require(param->getValue() == 0, "Unexpected default mode");
    require(param->getItemCount() == 2, "Unexpected mode count");
    int value = -1;
    std::string caption;
    param->getItem(0, value, caption);
    require(value == 0 && caption == test.firstCaption,
            "Unexpected first mode");
    param->getItem(1, value, caption);
    require(value == 1 && caption == test.lastCaption,
            "Unexpected second mode");
  }
}

void populate(GlbModelFx &fx, const QString &path) {
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  for (const DoubleCase &test : doubleCases) {
    auto *param = parameter<TDoubleParam>(fx, test.name);
    param->setValue(0, test.firstValue);
    param->setValue(12, test.lastValue);
  }
  for (const EnumCase &test : enumCases)
    parameter<TIntEnumParam>(fx, test.name)->setValue(1);
}

void verifyPopulated(GlbModelFx &fx, const QString &path) {
  require(fx.isZerary() && fx.getInputPortCount() == 0,
          "Restored FX changed its port contract");
  require(parameter<TStringParam>(fx, "modelFile")->getValue() ==
              path.toStdWString(),
          "Model path did not survive cloning or persistence");
  for (const DoubleCase &test : doubleCases) {
    auto *param = parameter<TDoubleParam>(fx, test.name);
    require(param->getKeyframeCount() == 2 && param->isKeyframe(0) &&
                param->isKeyframe(12),
            "Parameter keyframes did not survive cloning or persistence");
    expectValue(param->getValue(0), test.firstValue);
    expectValue(param->getValue(12), test.lastValue);
  }
  for (const EnumCase &test : enumCases)
    require(parameter<TIntEnumParam>(fx, test.name)->getValue() == 1,
            "Mode did not survive cloning or persistence");
}

void testCloneAndPersistence(const QString &scenePath, const QString &modelPath) {
  GlbModelFx original;
  populate(original, modelPath);
  std::unique_ptr<TFx> clone(original.clone(false));
  auto *cloned = dynamic_cast<GlbModelFx *>(clone.get());
  require(cloned != nullptr, "Factory clone returned the wrong FX type");
  verifyPopulated(*cloned, modelPath);
  parameter<TStringParam>(*cloned, "modelFile")->setValue(L"changed.glb");
  parameter<TDoubleParam>(*cloned, "positionX")->setValue(0, 7.0);
  verifyPopulated(original, modelPath);

  {
    TOStream os(TFilePath(scenePath.toStdWString()));
    // Include the FX type tag so the real persistence factory reloads the node.
    os << &original;
  }
  TIStream is(TFilePath(scenePath.toStdWString()));
  TPersist *loaded = nullptr;
  is >> loaded;
  std::unique_ptr<TPersist> owner(loaded);
  auto *restored = dynamic_cast<GlbModelFx *>(loaded);
  require(restored != nullptr, "FX type tag did not reload GLB Model");
  verifyPopulated(*restored, modelPath);
}

template <class PIXEL>
void testTransparent(GlbModelFx &fx, int bpp) {
  TRasterPT<PIXEL> output(3, 2);
  TTile tile;
  tile.m_pos = TPointD(17, -23);
  tile.setRaster(output);
  TRenderSettings settings;
  settings.m_bpp = bpp;
  for (double frame : {0.0, 6.0, 12.0}) {
    TRectD bbox(1.0, 2.0, 3.0, 4.0);
    require(!fx.doGetBBox(frame, bbox, settings) && bbox.isEmpty(),
            "Framework-only FX reported visible model content");
    for (int y = 0; y < output->getLy(); ++y)
      for (int x = 0; x < output->getLx(); ++x)
        output->pixels(y)[x] = PIXEL(1, 1, 1, 1);
    fx.doCompute(tile, frame, settings);
    for (int y = 0; y < output->getLy(); ++y)
      for (int x = 0; x < output->getLx(); ++x) {
        const PIXEL &pixel = output->pixels(y)[x];
        require(pixel.r == 0 && pixel.g == 0 && pixel.b == 0 && pixel.m == 0,
                "Framework-only FX did not clear output to transparent");
      }
  }
}

void testEmptyRendering() {
  GlbModelFx fx;
  testTransparent<TPixel32>(fx, 32);
  testTransparent<TPixel64>(fx, 64);
  testTransparent<TPixelF>(fx, 128);
}

void writeModel(const QString &path, float offset = 0) {
  const auto bytes = triangleGlb(offset);
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "Cannot write GLB fixture");
  require(file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) ==
              qint64(bytes.size()), "Incomplete GLB fixture");
}

template <class PIXEL>
void testVisible(GlbModelFx &fx, int bpp) {
  TRasterPT<PIXEL> output(80, 80);
  TTile tile; tile.setRaster(output); tile.m_pos = TPointD(-40, -40);
  TRenderSettings settings; settings.m_bpp = bpp; settings.m_affine = TScale(.2);
  TRectD bbox;
  require(fx.doGetBBox(0, bbox, settings) && !bbox.isEmpty(), "Model has no bounding box");
  expectValue(bbox.x0, -100); expectValue(bbox.x1, 100);
  fx.doCompute(tile, 0, settings);
  const auto center = output->pixels(40)[40];
  require(center.m == PIXEL::maxChannelValue && center.r == center.g && center.g == center.b,
          "Model did not produce opaque grayscale pixels");
  require(std::abs(double(center.r) / PIXEL::maxChannelValue - .75) < .005,
          "Incorrect grayscale conversion for output precision");
  require(output->pixels(0)[0].m == 0, "Background is not transparent");

  // Exercise OpenToonz's actual downstream Over operation at every precision.
  TRasterPT<PIXEL> composite(80, 80);
  const auto maximum = PIXEL::maxChannelValue;
  composite->fill(PIXEL(0, 0, maximum, maximum));
  // Match OverFx::process, including its floating-point path.
  TRop::over(composite, output);
  require(composite->pixels(0)[0].b == maximum && composite->pixels(0)[0].r == 0,
          "Composite lost its background outside the model");
  require(composite->pixels(40)[40].r == center.r && composite->pixels(40)[40].b == center.b,
          "Opaque model did not cover the background");
}

void testRenderingAndReload(const QString &path) {
  writeModel(path);
  QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read fixture");
  const auto originalBytes = file.readAll(); file.close();
  GlbModelFx fx;
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  testVisible<TPixel32>(fx, 32); testVisible<TPixel64>(fx, 64); testVisible<TPixelF>(fx, 128);
  // A direct request larger than one internal band must retain the same
  // coordinates and write rows after the first band correctly.
  {
    TRaster32P tall(80, 260); TTile tile; tile.setRaster(tall);
    tile.m_pos = TPointD(-40, -130);
    TRenderSettings settings; settings.m_affine = TScale(.2);
    fx.doCompute(tile, 0, settings);
    require(tall->pixels(130)[40].m == 255 && tall->pixels(150)[40].m == 0,
            "Banded rendering shifted or omitted the model");
    require(fx.getMemoryRequirement(TRectD(0, 0, 4096, 2160), 0, settings) > 0,
            "Renderer did not report temporary memory to the scheduler");
    parameter<TDoubleParam>(fx, "positionX")->setValue(12, 2.0);
    TRectD animated; require(fx.doGetBBox(12, animated, settings), "Animated model disappeared");
    expectValue(animated.x0, 100);
    parameter<TDoubleParam>(fx, "positionX")->setValue(0, 0.0);
  }
  std::unique_ptr<TFx> owner(fx.clone(false));
  auto *clone = dynamic_cast<GlbModelFx *>(owner.get());
  require(clone != nullptr, "Render clone has wrong type");
  testVisible<TPixel32>(*clone, 32);
  require(file.open(QIODevice::ReadOnly), "Source removed during rendering");
  require(file.readAll() == originalBytes, "Rendering modified source GLB"); file.close();
  const TRenderSettings settings;
  const auto alias = fx.getAlias(0, settings);
  writeModel(path, 100); // Changes the file size, even on coarse timestamp filesystems.
  require(alias != fx.getAlias(0, settings), "File replacement did not invalidate FX alias");
  TRectD bbox; require(clone->doGetBBox(0, bbox, settings), "Reload lost model");
  expectValue(bbox.x0, 9900);
  parameter<TDoubleParam>(fx, "positionX")->setValue(0, .00001);
  require(fx.getAlias(0, settings) != clone->getAlias(0, settings), "Small animated change lost from cache key");
  parameter<TDoubleParam>(fx, "farClip")->setValue(0, .05);
  TRaster32P output(4, 4); TTile tile; tile.setRaster(output);
  bool failed = false;
  try { fx.doCompute(tile, 0, settings); } catch (const TException &) { failed = true; }
  require(failed, "Invalid camera did not produce an explicit error");
}

void testLoadError(const QString &path) {
  GlbModelFx fx;
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  TRaster32P output(4, 4); TTile tile; tile.setRaster(output);
  bool failed = false;
  try { fx.doCompute(tile, 0, TRenderSettings()); } catch (const TException &) { failed = true; }
  require(failed, "Missing or invalid GLB did not report an error");
}

template <class PIXEL>
void expectColor(GlbModelFx &fx, double frame, double r, double g, double b) {
  TRasterPT<PIXEL> raster(80, 80);
  TTile tile; tile.setRaster(raster); tile.m_pos = TPointD(-40, -40);
  TRenderSettings settings; settings.m_affine = TScale(.2);
  fx.doCompute(tile, frame, settings);
  const auto p = raster->pixels(40)[40];
  const double max = PIXEL::maxChannelValue;
  const double tolerance = std::is_floating_point<typename PIXEL::Channel>::value ? 1e-6 : 1.0 / max + 1e-6;
  require(std::abs(p.r / max - r) <= tolerance &&
          std::abs(p.g / max - g) <= tolerance &&
          std::abs(p.b / max - b) <= tolerance && p.m == max,
          "Material color did not reach the output raster");
}

void testMaterialControls(const QString &path, const QString &saved) {
  const auto bytes = triangleGlb(0, true);
  { QFile f(path); require(f.open(QIODevice::WriteOnly), "Cannot create material GLB");
    require(f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) == qint64(bytes.size()), "Cannot write material GLB"); }
  TFxP actualFx = new GlbModelFx;
  auto &fx = *static_cast<GlbModelFx *>(actualFx.getPointer());
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  parameter<TIntEnumParam>(fx, "colorMode")->setValue(1);
  const auto materials = fx.getMaterials();
  require(materials.size() == 2 && materials[0].name == "Body" && materials[1].name == "Material 2",
          "Material names or unnamed fallback are wrong");
  expectColor<TPixel32>(fx, 0, 1, 0, 0);
  expectColor<TPixel64>(fx, 0, 1, 0, 0);
  expectColor<TPixelF>(fx, 0, 1, 0, 0);
  auto *actual = parameter<TParamSet>(fx, "materialColors");
  TXsheet sheet;
  sheet.getFxDag()->getInternalFxs()->addFx(&fx);
  FunctionTreeModel tree;
  tree.setFxHandle(nullptr);
  tree.setObjectHandle(nullptr);
  tree.refreshData(&sheet);
  auto *fxChannels = tree.getFxChannel(0);
  require(fxChannels, "Missing FX channel group");
  TreeModel::Item *materialChannels = nullptr;
  for (int i = 0; i < fxChannels->getChildCount(); ++i) {
    auto *wrapper = dynamic_cast<FunctionTreeModel::ParamWrapper *>(fxChannels->getChild(i));
    if (wrapper && wrapper->getParam().getPointer() == actual)
      materialChannels = fxChannels->getChild(i);
  }
  require(materialChannels && materialChannels->getChildCount() == 0,
          "Empty material channel group missing or created scene overrides");
  TFxP preview = fx.clone(false);
  TParamP current = preview->getParams()->getParam("materialColors");
  std::unique_ptr<ParamField> field(ParamField::create(nullptr, "Material", TParamP(actual)));
  require(bool(field), "Material editor was not created");
  field->setFx(preview, actualFx);
  field->setParam(current, TParamP(actual), 0);
  QApplication::processEvents();
  require(actual->getParamCount() == 0, "Viewing materials mutated the scene");
  auto *selector = field->findChild<QComboBox *>();
  require(selector && selector->count() == 2, "Material list was not populated");
  auto *color = field->findChild<PixelParamField *>();
  require(color && color->getColor() == TPixel32(255, 0, 0, 255), "Material swatch is wrong");
  const auto redAlias = fx.getAlias(0, TRenderSettings());
  color->setColor(TPixel32(0, 255, 0, 255));
  require(actual->getParamCount() == 1, "First edit did not attach a scene override");
  expectColor<TPixel32>(fx, 0, 0, 1, 0);
  require(redAlias != fx.getAlias(0, TRenderSettings()), "Color edit reused a stale cache alias");
  TUndoManager::manager()->undo();
  field->update(0);
  expectColor<TPixel32>(fx, 0, 1, 0, 0);
  TUndoManager::manager()->redo();
  field->update(0);
  require(QMetaObject::invokeMethod(color, "onKeyToggled", Qt::DirectConnection), "Cannot set a native color key");
  field->update(12);
  color->setColor(TPixel32(0, 0, 255, 255));
  require(QMetaObject::invokeMethod(color, "onKeyToggled", Qt::DirectConnection), "Cannot set the second color key");
  TPixelParamP animated = actual->getParam(0);
  require(animated && animated->isKeyframe(0) && animated->isKeyframe(12), "Native controls did not create color keyframes");
  tree.refreshData(&sheet);
  require(materialChannels->getChildCount() == 1, "First material did not appear in the existing FX listing");
  auto *bodyChannels = materialChannels->getChild(0);
  require(bodyChannels->getChildCount() == 3 && bodyChannels->data(Qt::DisplayRole).toString().contains("Body"),
          "Material name or RGB channels missing");
  auto *redCurve = dynamic_cast<FunctionTreeModel::Channel *>(bodyChannels->getChild(0));
  auto *greenCurve = dynamic_cast<FunctionTreeModel::Channel *>(bodyChannels->getChild(1));
  auto *blueCurve = dynamic_cast<FunctionTreeModel::Channel *>(bodyChannels->getChild(2));
  require(redCurve && greenCurve && blueCurve && redCurve->getParam() == animated->getRed().getPointer() &&
          greenCurve->getParam() == animated->getGreen().getPointer() && blueCurve->getParam() == animated->getBlue().getPointer(),
          "Channel listing duplicated or misbound the material animation");
  require(redCurve->getChannelGroup() == fxChannels && redCurve->getExprRefName().isEmpty(),
          "Material channel lost its FX owner or advertised an unsupported expression reference");
  greenCurve->setIsActive(true);
  QPersistentModelIndex selected(greenCurve->createIndex());
  redCurve->getParam()->setValue(12, .25);
  expectColor<TPixelF>(fx, 12, .25, 0, 1);
  field->update(12);
  require(color->getColor().r >= 63 && color->getColor().r <= 64,
          "Function channel edit did not update the FX color control");
  redCurve->getParam()->setValue(12, 0);
  expectColor<TPixel32>(fx, 0, 0, 1, 0);
  expectColor<TPixel64>(fx, 12, 0, 0, 1);
  const auto middle = animated->getValueD(6);
  require(middle.g > 0 && middle.b > 0 && middle.g < 1 && middle.b < 1, "Color did not interpolate");
  expectColor<TPixelF>(fx, 6, middle.r, middle.g, middle.b);
  TFxP clone = fx.clone(false);
  auto &copy = *static_cast<GlbModelFx *>(clone.getPointer());
  expectColor<TPixelF>(copy, 6, middle.r, middle.g, middle.b);
  copy.getParams()->unlink();
  expectColor<TPixelF>(copy, 6, middle.r, middle.g, middle.b);
  TPixelParamP clonedColor = parameter<TParamSet>(copy, "materialColors")->getParam(0);
  clonedColor->setValue(12, TPixel32::White);
  expectColor<TPixel32>(fx, 12, 0, 0, 1);
  { TOStream os(TFilePath(saved.toStdWString())); os << &fx; }
  { TIStream is(TFilePath(saved.toStdWString())); TPersist *p = nullptr; is >> p;
    std::unique_ptr<TPersist> owner(p);
    auto *restored = dynamic_cast<GlbModelFx *>(p);
    require(restored, "Cannot reload color FX preset");
    expectColor<TPixelF>(*restored, 6, middle.r, middle.g, middle.b);
    TPixelParamP restoredColor = parameter<TParamSet>(*restored, "materialColors")->getParam(0);
    require(restoredColor->isKeyframe(0) && restoredColor->isKeyframe(12), "Saved color animation lost its keys"); }
  selector->setCurrentIndex(1);
  color = field->findChild<PixelParamField *>();
  color->setColor(TPixel32::White);
  expectColor<TPixel32>(fx, 12, 0, 0, 1);  // Another material must not recolor Body.
  require(actual->getParamCount() == 2, "Second material override missing");
  tree.refreshData(&sheet);
  require(materialChannels->getChildCount() == 2 && materialChannels->getChild(0) == bodyChannels &&
          selected.isValid() && QModelIndex(selected).internalPointer() == greenCurve && greenCurve->isActive(),
          "Adding a material disturbed an existing active curve or selection");
  { QFile f(path); require(f.open(QIODevice::ReadOnly), "Cannot verify source preservation");
    require(f.readAll() == QByteArray(reinterpret_cast<const char *>(bytes.data()), int(bytes.size())), "Color editing wrote to the GLB"); }
  writeModel(path);  // Different contents, same path, implicit default material.
  field->update(0); QApplication::processEvents();
  require(selector->count() == 1, "List did not follow the replacement model");
  require(fx.getMaterials()[0].key != materials[0].key, "Replacement reused material identity");
  expectColor<TPixel32>(fx, 0, 1, 1, 1);
  require(actual->getParamCount() == 2 && animated->isKeyframe(12), "Replacement discarded old animation");
  tree.refreshData(&sheet);
  require(bodyChannels->data(Qt::DisplayRole).toString().contains("Inactive") && selected.isValid(),
          "Replacement silently reassigned or removed saved material channels");
  { QFile f(path); require(f.open(QIODevice::WriteOnly), "Cannot restore model fixture");
    f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()); }
  field->update(12); QApplication::processEvents();
  expectColor<TPixel32>(fx, 12, 0, 0, 1);
  require(selector->count() == 2, "Restored GLB did not restore material listing");
  tree.refreshData(&sheet);
  require(bodyChannels->data(Qt::DisplayRole).toString().contains("Body"),
          "Restoring the model did not restore its channel labels");
  actual->removeAllParam();
  static_cast<TParamSet *>(current.getPointer())->removeAllParam();
  tree.refreshData(&sheet);
  require(materialChannels->getChildCount() == 0 && !selected.isValid() &&
          tree.getActiveChannelCount() == 0 && !tree.getCurrentChannel(),
          "Reset left stale selected/active material channels");
  field->update(0); QApplication::processEvents();
  require(actual->getParamCount() == 0, "Refreshing after reset resurrected detached overrides");
  color = field->findChild<PixelParamField *>();
  selector->setCurrentIndex(0);
  color = field->findChild<PixelParamField *>();
  color->setColor(TPixel32(255, 255, 0, 255));
  expectColor<TPixel32>(fx, 0, 1, 1, 0);
  TPixelParamP resetColor = actual->getParam(0);
  require(!resetColor->hasKeyframes(), "Reset editor resurrected old color animation");
  TUndoManager::manager()->reset();
}
}  // namespace

int main(int argc, char **argv) {
#ifndef _WIN32
  qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
  QApplication app(argc, argv);
  QTemporaryDir dir;
  try {
    require(dir.isValid(), "Temporary directory unavailable");
    const QString modelPath =
        dir.filePath(QString::fromUtf8("Model \xe8\x89\xb2.glb"));
    require(!QFile::exists(modelPath), "Missing-model fixture already exists");
    testDefaults();
    testCloneAndPersistence(dir.filePath("model.fx"), modelPath);
    testEmptyRendering();
    testLoadError(modelPath);
    require(!QFile::exists(modelPath), "Framework created a model file");

    // An intentionally invalid GLB must report failure and remain untouched.
    const QByteArray sentinel("not a GLB - framework-only fixture\n");
    QFile file(modelPath);
    require(file.open(QIODevice::WriteOnly), "Cannot create model fixture");
    require(file.write(sentinel) == sentinel.size(), "Cannot write fixture");
    file.close();
    testLoadError(modelPath);
    require(file.open(QIODevice::ReadOnly), "Model fixture was removed");
    require(file.readAll() == sentinel, "Framework modified the model file");
    file.close();
    testRenderingAndReload(modelPath);
    testMaterialControls(dir.filePath("materials.glb"), dir.filePath("materials.fx"));
    std::cout << "PASS: GLB FX registration, zero inputs, parameter defaults, "
                 "keyframes, clone, Unicode persistence, read-only loading, "
                 "8/16/float rendering, reload and explicit errors\n";
    return 0;
  } catch (const TException &error) {
    std::cerr << "FAIL: "
              << QString::fromStdWString(error.getMessage()).toStdString()
              << "\n";
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
  }
  return 1;
}
