#pragma once

#include <QString>

// A Qt bridge to the command-line tools shipped with libjxl. No
// application preferences, image readers, or global process state are required.
namespace JxlCodec {

struct Settings {
  QString toolsPath;  // Directory containing cjxl, djxl, and jxlinfo.
  int timeoutSeconds = 600;
  int effort         = 7;
};

struct ImageInfo {
  int width       = 0;
  int height      = 0;
  int bits        = 0;
  int channels    = 0;  // Total channels, including alpha when present.
  bool alpha      = false;
  int orientation = 1;  // Stored orientation; width/height describe its output.
};

enum class Result { Success, Unsupported, Failed };

// Capabilities are read from the selected tools' help, then restricted to the
// still, integer, sRGB formats this bridge can preserve. Missing tools disable
// the bridge. Format arguments are extensions, optionally with a leading dot.
bool supports(const Settings &settings, const QString &inputFormat,
              const QString &outputFormat);

Result inspect(const Settings &settings, const QString &input, ImageInfo *info,
               QString *error = nullptr);

// Destinations are replaced atomically only after successful conversion and
// validation. Existing destinations survive every failure. Input/output paths
// may contain Unicode; the tools only receive ASCII relative working names.
// Encoding is lossless and forces a container so callers can add metadata
// boxes.
Result encode(const Settings &settings, const QString &input,
              const QString &stagedOutput, QString *error = nullptr);

Result decode(const Settings &settings, const QString &input,
              const QString &stagedOutput,
              const QString &outputFormat = QStringLiteral("png"),
              ImageInfo *info = nullptr, QString *error = nullptr);

// Converts a supported native file through an intermediate JXL. The caller
// owns native fallback. JPEG output requires reversible JPEG reconstruction,
// so a native JPEG does not undergo another lossy encoding pass.
Result roundTrip(const Settings &settings, const QString &input,
                 const QString &stagedOutput, const QString &outputFormat,
                 ImageInfo *info = nullptr, QString *error = nullptr);

}  // namespace JxlCodec
