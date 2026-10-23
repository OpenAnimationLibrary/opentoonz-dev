#include "tsimplecolorstyles.h"
#include "timage_io.h"
#include "texception.h"
#include "tstream.h"
#include "tvectorimage.h"

TInputStreamInterface &TInputStreamInterface::operator>>(TPixel32 &pixel) {
  return *this >> pixel.r >> pixel.g >> pixel.b >> pixel.m;
}
TOutputStreamInterface &TOutputStreamInterface::operator<<(
    const TPixel32 &pixel) {
  return *this << pixel.r << pixel.g << pixel.b << pixel.m;
}

// Real palette, color-style metadata, and stream code are linked. Only drawing
// services and unused image-codec entry points are excluded from this harness.
TOutlineStyle::TOutlineStyle() : m_regionOutlineModifier(nullptr) {}
TOutlineStyle::TOutlineStyle(const TOutlineStyle &other)
    : TColorStyle(other), m_regionOutlineModifier(nullptr) {}
TOutlineStyle::~TOutlineStyle() { delete m_regionOutlineModifier; }
TStrokeProp *TOutlineStyle::makeStrokeProp(const TStroke *) {
  throw TException("Unexpected rendering request.");
}
TRegionProp *TOutlineStyle::makeRegionProp(const TRegion *) {
  throw TException("Unexpected rendering request.");
}
void TOutlineStyle::computeOutline(const TStroke *, TStrokeOutline &,
                                   TOutlineUtil::OutlineParameter) const {
  throw TException("Unexpected rendering request.");
}
TSolidColorStyle::TSolidColorStyle(const TPixel32 &color)
    : m_color(color), m_tessellator(nullptr) {}
TSolidColorStyle::TSolidColorStyle(const TSolidColorStyle &other)
    : TOutlineStyle(other), m_color(other.m_color), m_tessellator(nullptr) {}
TSolidColorStyle::~TSolidColorStyle() {}
TColorStyle *TSolidColorStyle::clone() const {
  return new TSolidColorStyle(*this);
}
QString TSolidColorStyle::getDescription() const { return "SolidColorStyle"; }
std::string TSolidColorStyle::getBrushIdName() const {
  return "SolidColorStyle";
}
int TSolidColorStyle::getTagId() const { return 3; }
void TSolidColorStyle::loadData(TInputStreamInterface &is) { is >> m_color; }
void TSolidColorStyle::saveData(TOutputStreamInterface &os) const {
  os << m_color;
}
void TSolidColorStyle::makeIcon(const TDimension &) {
  throw TException("Unexpected rendering request.");
}
void TSolidColorStyle::drawRegion(const TColorFunction *, const bool,
                                  TRegionOutline &) const {
  throw TException("Unexpected rendering request.");
}
void TSolidColorStyle::doDrawStroke(const TColorFunction *, TStrokeOutline *,
                                    const TStroke *, bool) const {
  throw TException("Unexpected rendering request.");
}
bool TImageReader::load(const TFilePath &, TRasterP &) {
  throw TException("Image codecs are not part of this test.");
}
void TImageWriter::save(const TFilePath &, TRasterP) {
  throw TException("Image codecs are not part of this test.");
}
