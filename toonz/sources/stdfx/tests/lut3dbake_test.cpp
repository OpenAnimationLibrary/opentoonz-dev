// Exercise the production FX directly with a deterministic raster source. This
// target intentionally does not link tnzstdfx: it compiles the same FX source.
#include "../lut3dbakefx.cpp"
#include "tparamcontainer.h"
#include "tstream.h"
#include "tfilepath.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void expectClose(float actual, float expected, float tolerance = 0.00002f) {
  require(std::fabs(actual - expected) <= tolerance, "Unexpected LUT result");
}
void writeFile(const QString &path, const QString &contents) {
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Text), "Write failed");
  QTextStream stream(&file);
  stream << contents;
}
QString cube(int size, int look = 0, const QString &domain = QString()) {
  QString result;
  QTextStream stream(&result);
  stream << "TITLE \"LUT Bake regression fixture\"\nLUT_3D_SIZE " << size
         << "\n"
         << domain;
  for (int b = 0; b < size; ++b)
    for (int g = 0; g < size; ++g)
      for (int r = 0; r < size; ++r) {
        float rr = float(r) / (size - 1), gg = float(g) / (size - 1),
              bb = float(b) / (size - 1);
        if (look == 1) {
          rr *= gg;
          gg = 0.2f + 0.5f * gg;
          bb = 1.0f - bb;
        }
        if (look == 2) {
          rr = 1.0f - rr;
          gg = 1.0f - gg;
          bb = 1.0f - bb;
        }
        if (look == 3) {
          rr = -1.0f;
          gg = 2.0f;
          bb = 0.5f;
        }
        stream << rr << " " << gg << " " << bb << "\n";
      }
  return result;
}
void setPath(Lut3DBakeFx &fx, const QString &path) {
  TStringParamP param = TParamP(fx.getParams()->getParam("lutFile"));
  require(bool(param), "Missing LUT file parameter");
  param->setValue(path.toStdWString());
}
class Source final : public TStandardRasterFx {
  TRasterP m_raster;

public:
  explicit Source(const TRasterP &raster) : m_raster(raster) {}
  const TPersistDeclaration *getDeclaration() const override { return nullptr; }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool doGetBBox(double, TRectD &bbox, const TRenderSettings &) override {
    bbox = TRectD(0, 0, m_raster->getLx(), m_raster->getLy());
    return true;
  }
  std::string getAlias(double, const TRenderSettings &) const override {
    return "lut-test-source";
  }
  void compute(TTile &tile, double, const TRenderSettings &) override {
    tile.getRaster()->copy(m_raster);
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &ri) override {
    compute(tile, frame, ri);
  }
};
void connectSource(Lut3DBakeFx &fx, const TRasterP &raster) {
  fx.getInputPort(0)->setFx(new Source(raster));
}
void testParser(const QString &path) {
  QString error;
  for (int size : {2, 3, 17}) {
    writeFile(path, cube(size, 1));
    Lut3D lut;
    require(lut.load(path, &error), "Cube load failed");
    for (int i = 0; i < 100; ++i) {
      float r = float(i) / 99, g = float((i * 37) % 100) / 99,
            b        = float((i * 59) % 100) / 99;
      const float er = r * g, eg = 0.2f + 0.5f * g, eb = 1.0f - b;
      lut.convert(r, g, b);
      expectClose(r, er);
      expectClose(g, eg);
      expectClose(b, eb);
    }
  }
  writeFile(path, cube(2, 0, "DOMAIN_MIN -1 0 2\nDOMAIN_MAX 1 2 6\n"));
  Lut3D lut;
  require(lut.load(path), "Domain load failed");
  float r = 0, g = 0.5f, b = 5;
  lut.convert(r, g, b);
  expectClose(r, 0.5f);
  expectClose(g, 0.25f);
  expectClose(b, 0.75f);
  r = -10;
  g = 10;
  b = 1;
  lut.convert(r, g, b);
  expectClose(r, 0);
  expectClose(g, 1);
  expectClose(b, 0);
  writeFile(path, cube(2, 0, "LUT_3D_INPUT_RANGE -1 1\n"));
  require(lut.load(path), "Range load failed");
  r = g = b = 0;
  lut.convert(r, g, b);
  expectClose(r, 0.5f);
  writeFile(
      path,
      cube(2, 0, "DOMAIN_MIN -3e38 -3e38 -3e38\nDOMAIN_MAX 3e38 3e38 3e38\n"));
  require(lut.load(path), "Wide domain load failed");
  r = g = b = 0;
  lut.convert(r, g, b);
  expectClose(r, 0.5f);
  writeFile(path, cube(2));
  require(lut.load(path), "Identity load failed");
  r = std::numeric_limits<float>::quiet_NaN();
  g = std::numeric_limits<float>::infinity();
  b = -g;
  lut.convert(r, g, b);
  expectClose(r, 0);
  expectClose(g, 1);
  expectClose(b, 0);

  const QStringList invalid = {
      "LUT_3D_SIZE 2\n0 0 0\n",
      "LUT_3D_SIZE 1000000\n",
      "LUT_1D_SIZE 2\n",
      "LUT_3D_SIZE 2\nNaN 0 0\n",
      cube(2, 0, "DOMAIN_MIN 1 0 0\nDOMAIN_MAX 1 1 1\n"),
      cube(2) + "0 0 0\n"};
  for (const QString &text : invalid) {
    writeFile(path, text);
    error.clear();
    require(!lut.load(path, &error) && !error.isEmpty() && !lut.isValid(),
            "Malformed LUT accepted or stale data retained");
  }
  require(!lut.load(path + ".missing", &error), "Missing file accepted");
}
void test3dl(const QString &path) {
  QString text;
  QTextStream stream(&text);
  stream << "3DMESH\nMesh 1 16\n0 512 1023\n";
  for (int r = 0; r < 3; ++r)
    for (int g = 0; g < 3; ++g)
      for (int b = 0; b < 3; ++b)
        stream << qRound(r * 65535.0 / 2) << " " << qRound(g * 65535.0 / 2)
               << " " << qRound(b * 65535.0 / 2) << "\n";
  writeFile(path, text);
  Lut3D lut;
  require(lut.load(path), "3dl load failed");
  float r = 0.25f, g = 0.5f, b = 0.75f;
  lut.convert(r, g, b);
  expectClose(r, 0.25f);
  expectClose(g, 0.5f);
  expectClose(b, 0.75f);
}
template <class PIXEL>
void testRaster(const QString &path) {
  const float max = float(PIXEL::maxChannelValue);
  TRasterPT<PIXEL> input(8, 1), output(8, 1);
  for (int i = 0; i < 8; ++i) {
    const int a         = qRound(max * i / 7.0f);
    input->pixels(0)[i] = PIXEL(a / 2, a / 4, a * 3 / 4, a);
  }
  Lut3DBakeFx fx;
  connectSource(fx, input);
  setPath(fx, path);
  TTile tile;
  tile.setRaster(output);
  TRenderSettings ri;
  writeFile(path, cube(2, 1));
  fx.doCompute(tile, 0, ri);
  for (int i = 1; i < 8; ++i) {
    const PIXEL &src = input->pixels(0)[i], &dst = output->pixels(0)[i];
    expectClose(float(dst.r), float(src.r) * float(src.g) / src.m, 1.0f);
    expectClose(float(dst.g), 0.2f * src.m + 0.5f * src.g, 1.0f);
    expectClose(float(dst.b), float(src.m - src.b), 1.0f);
    require(dst.m == src.m, "Alpha changed");
  }
  require(output->pixels(0)[0].m == 0 && output->pixels(0)[0].r == 0,
          "Transparent pixel changed");
  setPath(fx, QString());
  fx.doCompute(tile, 0, ri);
  for (int i = 0; i < 8; ++i)
    require(input->pixels(0)[i] == output->pixels(0)[i],
            "Empty path did not bypass");
}
void testFloatAndReload(const QString &path) {
  TRasterFP input(1, 1), output(1, 1);
  auto &src = input->pixels(0)[0];
  src.r     = 0.25f;
  src.g     = 0.125f;
  src.b     = 0.375f;
  src.m     = 0.5f;
  Lut3DBakeFx fx;
  connectSource(fx, input);
  setPath(fx, path);
  writeFile(path, cube(2, 1));
  TTile tile;
  tile.setRaster(output);
  TRenderSettings ri;
  ri.m_bpp = 128;
  fx.doCompute(tile, 0, ri);
  expectClose(output->pixels(0)[0].r, 0.0625f);
  expectClose(output->pixels(0)[0].g, 0.1625f);
  expectClose(output->pixels(0)[0].b, 0.125f);
  expectClose(output->pixels(0)[0].m, 0.5f);
  require(!fx.toBeComputedInLinearColorSpace(true, true),
          "LUT sampled in linear RGB");
  const auto oldAlias = fx.getAlias(0, ri);
  writeFile(path, cube(2, 2) + "# changed file size\n");
  require(oldAlias != fx.getAlias(0, ri), "Stale render cache alias");
  fx.doCompute(tile, 0, ri);
  expectClose(output->pixels(0)[0].r, 0.25f);
  expectClose(output->pixels(0)[0].g, 0.375f);
  writeFile(path, cube(2, 3) + "# clipping fixture\n");
  fx.doCompute(tile, 0, ri);
  expectClose(output->pixels(0)[0].r, 0);
  expectClose(output->pixels(0)[0].g, 0.5f);
  expectClose(output->pixels(0)[0].b, 0.25f);
  std::vector<std::future<void>> tasks;
  for (int i = 0; i < 8; ++i)
    tasks.push_back(std::async(std::launch::async, [&]() {
      TRasterFP raster(1, 1);
      TTile t;
      t.setRaster(raster);
      fx.doCompute(t, 0, ri);
      expectClose(raster->pixels(0)[0].g, 0.5f);
    }));
  for (auto &task : tasks) task.get();
  const auto validAlias = fx.getAlias(0, ri);
  require(QFile::remove(path), "Could not remove fixture");
  require(validAlias != fx.getAlias(0, ri), "Missing LUT kept cached alias");
  bool failed = false;
  try {
    fx.doCompute(tile, 0, ri);
  } catch (const TException &) {
    failed = true;
  }
  require(failed, "Missing LUT silently rendered");
}
void testPersistence(const QString &file, const QString &lutPath) {
  {
    Lut3DBakeFx fx;
    setPath(fx, lutPath);
    TOStream os(TFilePath(file.toStdWString()));
    os << fx;
  }
  TIStream is(TFilePath(file.toStdWString()));
  TPersist *loaded = nullptr;
  is >> loaded;
  std::unique_ptr<TPersist> owner(loaded);
  auto *fx = dynamic_cast<Lut3DBakeFx *>(loaded);
  require(fx != nullptr, "FX type failed to reload");
  TStringParamP path = TParamP(fx->getParams()->getParam("lutFile"));
  require(path->getValue() == lutPath.toStdWString(),
          "LUT path did not persist");
}
}  // namespace
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  try {
    require(dir.isValid(), "Temporary directory unavailable");
    const QString path =
        dir.filePath(QString::fromUtf8("LUT \xe8\x89\xb2.cube"));
    testParser(path);
    test3dl(dir.filePath("identity.3dl"));
    testRaster<TPixel32>(path);
    testRaster<TPixel64>(path);
    testFloatAndReload(path);
    testPersistence(dir.filePath("bake.fx"), path);
    std::cout
        << "PASS: LUT formats, domains, interpolation, invalid files, "
           "8/16/float RGB, alpha, cache reload, concurrency and persistence\n";
    return 0;
  } catch (const TException &e) {
    std::cerr << "FAIL: "
              << QString::fromStdWString(e.getMessage()).toStdString() << "\n";
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << "\n";
  }
  return 1;
}
