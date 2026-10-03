#include "palette_fit.h"
#include "lut_writer.h"
#include "toonz/lut3d.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
void roundTrip(const otlut::Lut3D &generated, const QString &path, bool threeDl,
               const std::vector<otlut::ColorPair> &pairs) {
  std::ostringstream output;
  if (threeDl)
    otlut::writeThreeDl(output, generated);
  else
    otlut::writeCube(output, generated);
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "could not create fixture");
  const QByteArray bytes = QByteArray::fromStdString(output.str());
  check(file.write(bytes) == bytes.size(), "write failed");
  file.close();
  Lut3D reader;
  QString error;
  check(reader.load(path, &error), error.toUtf8().constData());
  for (const auto &pair : pairs) {
    auto actual = pair.source;
    reader.convert(actual[0], actual[1], actual[2]);
    for (int c = 0; c < 3; ++c)
      check(std::fabs(actual[c] - pair.target[c]) <= 0.0012f,
            "OpenToonz reader disagrees with generated mapping");
  }
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory");
    const std::vector<otlut::ColorPair> example = {
        {{1, 0, 0}, {9 / 255.0f, 1, 0}, 0.1f, "example style 2"},
        {{0, 0, 0}, {0, 0, 0}, 0.1f, "example style 1"},
        {{0, 0, 1}, {0, 0, 1}, 0.1f, "protected blue"}};
    const auto palette = otlut::fitLutFromColorPairs(example, 33);
    roundTrip(palette, directory.path() + "/palette.cube", false, example);
    roundTrip(palette, directory.path() + "/palette.3dl", true, example);
    std::vector<otlut::ColorPair> samples;
    for (int i = 0; i < 100; ++i) {
      const std::array<float, 3> rgb = {(i * 17 % 101) / 100.0f,
                                        (i * 29 % 101) / 100.0f,
                                        (i * 43 % 101) / 100.0f};
      samples.push_back({rgb, rgb, 0, "identity"});
    }
    const auto identity = otlut::makeIdentityLut(33);
    roundTrip(identity, directory.path() + "/identity.cube", false, samples);
    roundTrip(identity, directory.path() + "/identity.3dl", true, samples);
    // Deliberately asymmetric transform exercises all channels and axes.
    auto asymmetric = identity;
    for (size_t i = 0; i < asymmetric.rgb.size(); i += 3) {
      asymmetric.rgb[i] *= 0.5f;
      asymmetric.rgb[i + 1] = 0.25f + asymmetric.rgb[i + 1] * 0.5f;
      asymmetric.rgb[i + 2] = 1 - asymmetric.rgb[i + 2];
    }
    for (auto &p : samples)
      p.target = {p.source[0] * 0.5f, 0.25f + p.source[1] * 0.5f,
                  1 - p.source[2]};
    roundTrip(asymmetric, directory.path() + "/asymmetric.cube", false,
              samples);
    roundTrip(asymmetric, directory.path() + "/asymmetric.3dl", true, samples);
    std::cout << "OpenToonz palette, identity and asymmetric LUT round trips "
                 "passed.\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
