#pragma once

#ifndef TTIO_EXR_INCLUDED
#define TTIO_EXR_INCLUDED

#include "tiio.h"
#include "tproperty.h"

#include <QCoreApplication>

#ifdef IMAGE_EXPORTS
#define TIIO_EXR_API DV_EXPORT_API
#else
#define TIIO_EXR_API DV_IMPORT_API
#endif

namespace Tiio {

//===========================================================================

class TIIO_EXR_API ExrWriterProperties final : public TPropertyGroup {
  Q_DECLARE_TR_FUNCTIONS(ExrWriterProperties)
public:
  TEnumProperty m_compressionType;
  TEnumProperty m_storageType;
  TEnumProperty m_bitsPerPixel;
  TDoubleProperty m_colorSpaceGamma;

  ExrWriterProperties();

  void updateTranslation() override;
};

//===========================================================================

TIIO_EXR_API Tiio::Reader* makeExrReader();
TIIO_EXR_API Tiio::Writer* makeExrWriter();
}  // namespace Tiio

#undef TIIO_EXR_API

#endif
