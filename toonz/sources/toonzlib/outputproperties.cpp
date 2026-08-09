

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include "toutputproperties.h"

// TnzLib includes
#include "toonz/boardsettings.h"

// TnzBase includes
#include "trasterfx.h"

// TnzCore includes
#include "tiio.h"
#include "tproperty.h"

//**********************************************************************************
//    Local namespace  stuff
//**********************************************************************************

namespace {

inline void deleteValue(const std::pair<std::string, TPropertyGroup *> &p) {
  delete p.second;
}

}  // namespace

//**********************************************************************************
//    TOutputProperties  implementation
//**********************************************************************************

TOutputProperties::TOutputProperties()
    : m_path(TFilePath("+outputs") + ".tif")
    , m_renderSettings()
    , m_frameRate(24)
    , m_from(0)
    , m_to(-1)
    , m_offset(0)
    , m_step(1)
    , m_whichLevels(false)
    , m_multimediaRendering(0)
    , m_maxTileSizeIndex(0)
    , m_threadIndex(2)
    , m_subcameraPreview(false)
    , m_boardSettings(new BoardSettings())
    , m_formatTemplateFId()
    , m_syncColorSettings(true) {
  m_renderSettings = new TRenderSettings();
  m_nonlinearBpp   = m_renderSettings->m_bpp;
}

//-------------------------------------------------------------------

TOutputProperties::TOutputProperties(const TOutputProperties &src)
    : m_path(src.m_path)
    , m_formatProperties(src.m_formatProperties)
    , m_renderSettings(new TRenderSettings(*src.m_renderSettings))
    , m_frameRate(src.m_frameRate)
    , m_frameRanges(src.m_frameRanges)
    , m_from(src.m_from)
    , m_to(src.m_to)
    , m_whichLevels(src.m_whichLevels)
    , m_offset(src.m_offset)
    , m_step(src.m_step)
    , m_multimediaRendering(src.m_multimediaRendering)
    , m_maxTileSizeIndex(src.m_maxTileSizeIndex)
    , m_threadIndex(src.m_threadIndex)
    , m_subcameraPreview(src.m_subcameraPreview)
    , m_boardSettings(new BoardSettings(*src.m_boardSettings))
    , m_formatTemplateFId(src.m_formatTemplateFId)
    , m_syncColorSettings(src.m_syncColorSettings)
    , m_nonlinearBpp(src.m_nonlinearBpp) {
  std::map<std::string, TPropertyGroup *>::iterator ft,
      fEnd = m_formatProperties.end();
  for (ft = m_formatProperties.begin(); ft != fEnd; ++ft) {
    if (ft->second) ft->second = ft->second->clone();
  }
}

//-------------------------------------------------------------------

TOutputProperties::~TOutputProperties() {
  delete m_renderSettings;

  std::for_each(m_formatProperties.begin(), m_formatProperties.end(),
                ::deleteValue);
}

//-------------------------------------------------------------------

TOutputProperties &TOutputProperties::operator=(const TOutputProperties &src) {
  m_path        = src.m_path;
  m_frameRanges = src.m_frameRanges;
  m_from        = src.m_from;
  m_to          = src.m_to;
  m_frameRate   = src.m_frameRate;
  m_whichLevels = src.m_whichLevels;
  m_offset      = src.m_offset;
  m_step        = src.m_step;

  m_multimediaRendering = src.m_multimediaRendering;
  m_maxTileSizeIndex    = src.m_maxTileSizeIndex;
  m_threadIndex         = src.m_threadIndex;
  m_subcameraPreview    = src.m_subcameraPreview;

  delete m_renderSettings;
  m_renderSettings = new TRenderSettings(*src.m_renderSettings);

  std::for_each(m_formatProperties.begin(), m_formatProperties.end(),
                ::deleteValue);

  std::map<std::string, TPropertyGroup *>::const_iterator sft,
      sfEnd = src.m_formatProperties.end();
  for (sft = src.m_formatProperties.begin(); sft != sfEnd; ++sft)
    m_formatProperties[sft->first] = sft->second->clone();

  delete m_boardSettings;
  m_boardSettings = new BoardSettings(*src.m_boardSettings);

  m_formatTemplateFId = src.m_formatTemplateFId;

  return *this;
}

//-------------------------------------------------------------------

TFilePath TOutputProperties::getPath() const { return m_path; }

//-------------------------------------------------------------------

void TOutputProperties::setPath(const TFilePath &fp) { m_path = fp; }

//-------------------------------------------------------------------

void TOutputProperties::setOffset(int off) { m_offset = off; }

//-----------------------------------------------------------------------------

void TOutputProperties::setFrameRanges(
    const std::vector<std::pair<int, int>> &ranges) {
  m_frameRanges = ranges;
}

//-----------------------------------------------------------------------------

/*-- Expand the selection into ascending, de-duplicated frame numbers. Step is
 * applied across the whole selection rather than restarting per range, so a
 * step of 2 over "1-4, 7-10" behaves like a single strided pass. --*/
std::vector<int> TOutputProperties::getFrameList(int sceneLength) const {
  std::vector<int> frames;
  std::vector<std::pair<int, int>> ranges = m_frameRanges;
  if (ranges.empty()) {
    int r0, r1, step;
    if (!getRange(r0, r1, step)) {
      r0 = 0;
      r1 = sceneLength - 1;
    }
    ranges.push_back(std::make_pair(r0, r1));
  }

  int step = m_step > 0 ? m_step : 1;
  std::vector<int> expanded;
  for (int i = 0; i < (int)ranges.size(); i++) {
    int from = std::max(0, ranges[i].first);
    int to   = std::min(sceneLength - 1, ranges[i].second);
    for (int f = from; f <= to; f++) expanded.push_back(f);
  }
  std::sort(expanded.begin(), expanded.end());
  expanded.erase(std::unique(expanded.begin(), expanded.end()),
                 expanded.end());

  for (int i = 0; i < (int)expanded.size(); i += step)
    frames.push_back(expanded[i]);
  return frames;
}

//-----------------------------------------------------------------------------

/*-- Frame numbers in the text are one-based because that is what the Output
 * Settings fields show; the stored pairs are zero-based like getRange(). --*/
bool TOutputProperties::parseFrameRanges(
    const std::string &text, std::vector<std::pair<int, int>> &ranges,
    std::string *error) {
  std::vector<std::pair<int, int>> parsed;
  std::string token;
  std::vector<std::string> tokens;
  for (size_t i = 0; i <= text.size(); i++) {
    if (i == text.size() || text[i] == ',') {
      tokens.push_back(token);
      token.clear();
    } else if (!isspace((unsigned char)text[i]))
      token += text[i];
  }

  bool anyContent = false;
  for (size_t i = 0; i < tokens.size(); i++) {
    const std::string &tk = tokens[i];
    if (tk.empty()) continue;
    anyContent = true;

    size_t dash = tk.find('-');
    std::string fromStr =
        (dash == std::string::npos) ? tk : tk.substr(0, dash);
    std::string toStr =
        (dash == std::string::npos) ? tk : tk.substr(dash + 1);
    if (fromStr.empty() || toStr.empty()) {
      if (error) *error = "Incomplete frame range: " + tk;
      return false;
    }
    for (size_t c = 0; c < fromStr.size(); c++)
      if (!isdigit((unsigned char)fromStr[c])) {
        if (error) *error = "Frame numbers must be positive integers: " + tk;
        return false;
      }
    for (size_t c = 0; c < toStr.size(); c++)
      if (!isdigit((unsigned char)toStr[c])) {
        if (error) *error = "Frame numbers must be positive integers: " + tk;
        return false;
      }

    int from = std::atoi(fromStr.c_str());
    int to   = std::atoi(toStr.c_str());
    if (from < 1 || to < 1) {
      if (error) *error = "Frame numbers start at 1: " + tk;
      return false;
    }
    if (from > to) {
      if (error) *error = "Range start is after its end: " + tk;
      return false;
    }
    parsed.push_back(std::make_pair(from - 1, to - 1));
  }

  if (!anyContent) {
    if (error) *error = "No frames were specified.";
    return false;
  }

  /*-- Normalise so overlapping and unordered input still describes exactly one
   * ascending selection. --*/
  std::sort(parsed.begin(), parsed.end());
  std::vector<std::pair<int, int>> merged;
  for (size_t i = 0; i < parsed.size(); i++) {
    if (!merged.empty() && parsed[i].first <= merged.back().second + 1)
      merged.back().second = std::max(merged.back().second, parsed[i].second);
    else
      merged.push_back(parsed[i]);
  }

  ranges = merged;
  return true;
}

//-----------------------------------------------------------------------------

std::string TOutputProperties::formatFrameRanges(
    const std::vector<std::pair<int, int>> &ranges) {
  std::string text;
  for (size_t i = 0; i < ranges.size(); i++) {
    if (i > 0) text += ", ";
    text += std::to_string(ranges[i].first + 1);
    if (ranges[i].second != ranges[i].first)
      text += "-" + std::to_string(ranges[i].second + 1);
  }
  return text;
}

//-----------------------------------------------------------------------------

bool TOutputProperties::getRange(int &r0, int &r1, int &step) const {
  step = m_step;
  if (m_from > m_to) {
    r0 = 0;
    r1 = -1;
    return false;
  } else {
    r0 = m_from;
    r1 = m_to;
    return true;
  }
}

//-----------------------------------------------------------------------------

void TOutputProperties::setRange(int r0, int r1, int step) {
  assert(0 <= r0 && r0 <= r1 || r0 == 0 && r1 == -1);
  m_from = r0;
  m_to   = r1;
  m_step = step;
}

//-----------------------------------------------------------------------------

void TOutputProperties::setFrameRate(double fps) { m_frameRate = fps; }

//-------------------------------------------------------------------

TPropertyGroup *TOutputProperties::getFileFormatProperties(std::string ext) {
  std::map<std::string, TPropertyGroup *>::const_iterator it;
  it = m_formatProperties.find(ext);
  if (it == m_formatProperties.end()) {
    TPropertyGroup *ret     = Tiio::makeWriterProperties(ext);
    m_formatProperties[ext] = ret;
    return ret;
  } else if (ext == "mov" || ext == "3gp") {
    return it->second;
  } else {
    // Try to merge settings instead of overriding them
    TPropertyGroup *ret = Tiio::makeWriterProperties(ext);
    ret->setProperties(it->second);
    m_formatProperties[ext] = ret;
    return ret;
  }
}

//-------------------------------------------------------------------

void TOutputProperties::getFileFormatPropertiesExtensions(
    std::vector<std::string> &v) const {
  v.reserve(m_formatProperties.size());
  std::map<std::string, TPropertyGroup *>::const_iterator it;
  for (it = m_formatProperties.begin(); it != m_formatProperties.end(); ++it)
    v.push_back(it->first);
}

//-------------------------------------------------------------------

void TOutputProperties::setRenderSettings(
    const TRenderSettings &renderSettings) {
  assert(renderSettings.m_bpp == 32 || renderSettings.m_bpp == 64 ||
         renderSettings.m_bpp == 128);
  assert(renderSettings.m_gamma > 0);
  assert(renderSettings.m_quality == TRenderSettings::StandardResampleQuality ||
         renderSettings.m_quality == TRenderSettings::ImprovedResampleQuality ||
         renderSettings.m_quality == TRenderSettings::HighResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Triangle_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Mitchell_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Cubic5_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Cubic75_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Cubic1_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Hann2_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Hann3_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Hamming2_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Hamming3_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Lanczos2_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Lanczos3_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Gauss_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::ClosestPixel_FilterResampleQuality ||
         renderSettings.m_quality ==
             TRenderSettings::Bilinear_FilterResampleQuality);

  *m_renderSettings = renderSettings;
}
