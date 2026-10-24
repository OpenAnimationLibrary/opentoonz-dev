#pragma once

#ifndef TIIO_JXL_INCLUDED
#define TIIO_JXL_INCLUDED

#include "tiio.h"
#include "tproperty.h"
#include <QCoreApplication>

namespace Tiio {

class JxlWriterProperties final : public TPropertyGroup {
  Q_DECLARE_TR_FUNCTIONS(JxlWriterProperties)

public:
  TEnumProperty m_bitsPerPixel;
  TBoolProperty m_matte;
  TIntProperty m_effort;

  explicit JxlWriterProperties(bool alphaSupported = true);
  void updateTranslation() override;
};

Reader *makeJxlReader();
Writer *makeJxlWriter();
Reader *makeJxlRoutedReader(const std::string &ext, ReaderMaker *native);
Writer *makeJxlRoutedWriter(const std::string &ext, WriterMaker *native);

}  // namespace Tiio

#endif
