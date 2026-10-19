#pragma once

// In-memory bridge between the Palette Gizmo dialog and the Paint Brush tool.
// The two live in different binaries, so avoid a direct link-time dependency.
// No property means the Gizmo has not supplied a value in this session.
#include <QCoreApplication>
#include <QVariant>

namespace PaintBrushGizmoSettings {

constexpr char Fade[]            = "OpenToonz_PaintBrushGizmoFade";
constexpr char ShadowColor[]     = "OpenToonz_PaintBrushGizmoShadowColor";
constexpr char ValueScale[]      = "OpenToonz_PaintBrushGizmoValueScale";
constexpr char ValueShift[]      = "OpenToonz_PaintBrushGizmoValueShift";
constexpr char SaturationScale[] = "OpenToonz_PaintBrushGizmoSaturationScale";
constexpr char SaturationShift[] = "OpenToonz_PaintBrushGizmoSaturationShift";
constexpr char HueShift[]        = "OpenToonz_PaintBrushGizmoHueShift";
constexpr char AlphaScale[]      = "OpenToonz_PaintBrushGizmoAlphaScale";
constexpr char AlphaShift[]      = "OpenToonz_PaintBrushGizmoAlphaShift";

inline void publish(const char *key, const QVariant &value) {
  if (QCoreApplication *app = QCoreApplication::instance())
    app->setProperty(key, value);
}

inline QVariant read(const char *key) {
  QCoreApplication *app = QCoreApplication::instance();
  return app ? app->property(key) : QVariant();
}

inline double number(const char *key, double fallback = 0.0) {
  bool valid          = false;
  const double result = read(key).toDouble(&valid);
  return valid ? result : fallback;
}

}  // namespace PaintBrushGizmoSettings
