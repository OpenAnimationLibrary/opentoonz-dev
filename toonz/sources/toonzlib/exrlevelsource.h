// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifndef EXR_LEVEL_SOURCE_H
#define EXR_LEVEL_SOURCE_H

#include "tfilepath.h"
#include "timageinfo.h"
#include "trasterimage.h"

#include <string>
#include <vector>

#ifdef TOONZLIB_EXPORTS
#define EXR_LEVEL_SOURCE_API DV_EXPORT_API
#else
#define EXR_LEVEL_SOURCE_API DV_IMPORT_API
#endif

// Metadata and decoding shared by the dedicated EXR level loader and EXR AOV
// consumers.  All coordinates in PartInfo retain their original OpenEXR
// top-down coordinate system.  load() returns an OpenToonz bottom-up raster.
namespace ExrLevelSource {

struct Window {
  int xMin = 0;
  int yMin = 0;
  int xMax = -1;
  int yMax = -1;

  int width() const { return xMax >= xMin ? xMax - xMin + 1 : 0; }
  int height() const { return yMax >= yMin ? yMax - yMin + 1 : 0; }
};

struct ChannelInfo {
  std::string name;
  std::string pixelType;
  int xSampling = 1;
  int ySampling = 1;
};

struct PartInfo {
  int index = -1;
  std::string name;
  // Named parts use "name:<name>". Unnamed parts use "part:<ordinal>".
  std::string stableId;
  std::string type;
  bool tiled     = false;
  bool deep      = false;
  bool supported = false;
  Window displayWindow;
  Window dataWindow;
  bool hasBaseLayer = false;
  std::vector<std::string> layers;
  std::vector<std::string> channels;
  std::vector<ChannelInfo> channelInfo;
};

struct Selection {
  // A stableId is preferred. Raw part names are accepted for convenience.
  std::string part;
  int partIndexFallback = 0;
  // Empty selects the base layer. Named layers must be selected explicitly.
  std::string layer;
};

EXR_LEVEL_SOURCE_API std::vector<PartInfo> inspect(const TFilePath &path);

EXR_LEVEL_SOURCE_API PartInfo resolve(const std::vector<PartInfo> &parts,
                                      const Selection &selection);

// Checks whether a part/layer can be represented by the flat RGBA decoder.
// This performs no pixel reads and reports deep, unsupported, unrecognized,
// and subsampled selections through error.
EXR_LEVEL_SOURCE_API bool validateLayer(const PartInfo &part,
                                        const std::string &layer,
                                        std::string *error = nullptr);

// Loads the selected flat part and layer into a display-window-sized float
// raster. Areas outside the data window are transparent. Pass gamma 1.0 for
// raw data such as normals; positive RGB values otherwise receive 1/gamma.
EXR_LEVEL_SOURCE_API TRasterImageP load(const TFilePath &path,
                                        const Selection &selection = {},
                                        double colorSpaceGamma     = 2.2);

// Supplies display dimensions and the data-window content rectangle. Errors
// are returned through error when supplied; load() and inspect() throw them.
EXR_LEVEL_SOURCE_API bool getInfo(const TFilePath &path, TImageInfo &info,
                                  const Selection &selection = {},
                                  std::string *error         = nullptr);

}  // namespace ExrLevelSource

#undef EXR_LEVEL_SOURCE_API

#endif  // EXR_LEVEL_SOURCE_H
