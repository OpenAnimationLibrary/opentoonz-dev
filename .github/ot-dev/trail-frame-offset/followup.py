from pathlib import Path
root = Path.cwd()
p = root / 'toonz/sources/image/pli/tiio_pli.cpp'
s = p.read_text()
a = '''      if (marker.m_type != TStyleParam::SP_STRING || value.m_type != TStyleParam::SP_INT)
        return;
      const bool cycle = marker.m_string == "trail-cycle-v1";
      const bool offset = marker.m_string == "trail-frame-offset-v1";
      if (!cycle && !offset) return;
      const double number = value.m_numericVal;
      const double maximum = cycle ? 3 : (std::numeric_limits<int>::max)();
      if (std::isfinite(number) && number >= 0 && number <= maximum && number == int(number)) {
        if (cycle) TrailStyles::setMode(style, TrailCycle::modeFromValue(int(number)));
        else TrailStyles::setFrameOffset(style, int(number));
      }
'''
b = '''      if (marker.m_type != TStyleParam::SP_STRING) return;
      const bool cycle = marker.m_string == "trail-cycle-v1";
      const bool offset = marker.m_string == "trail-frame-offset-v1";
      if (!cycle && !offset) return;
      if (cycle) {
        if (value.m_type != TStyleParam::SP_INT) return;
        const double number = value.m_numericVal;
        if (std::isfinite(number) && number >= 0 && number <= 3 && number == int(number))
          TrailStyles::setMode(style, TrailCycle::modeFromValue(int(number)));
      } else {
        if (value.m_type != TStyleParam::SP_STRING) return;
        // PLI numeric style parameters have only a 16-bit integer part.
        // A tagged decimal string preserves the full source-frame ID range.
        bool ok = false;
        const int frame = QString::fromStdString(value.m_string).toInt(&ok);
        if (ok && frame >= 0) TrailStyles::setFrameOffset(style, frame);
      }
'''
assert a in s
s = s.replace(a,b,1)
s = s.replace('#include <limits>\n','',1)
a = 'if (offset > 0) stream << std::string("trail-frame-offset-v1") << offset;'
assert a in s
s = s.replace(a,'if (offset > 0) stream << std::string("trail-frame-offset-v1") << std::to_string(offset);',1)
p.write_text(s)
p = root / 'toonz/tests/trail_settings/trailoffset_test.cpp'
s = p.read_text()
s = s.replace('int rasterOffset, int vectorOffset) {','int rasterOffset, int vectorOffset, const char *phase) {',1)
a = '''  require(TrailStyles::getFrameOffset(palette->getStyle(1)) == rasterOffset &&
              TrailStyles::getFrameOffset(palette->getStyle(2)) == vectorOffset,
          "Palette round trip lost offset");'''
b = '''  const int actualRaster = TrailStyles::getFrameOffset(palette->getStyle(1));
  const int actualVector = TrailStyles::getFrameOffset(palette->getStyle(2));
  if (actualRaster != rasterOffset || actualVector != vectorOffset)
    throw std::runtime_error(std::string(phase) + ": expected offsets " +
        std::to_string(rasterOffset) + "," + std::to_string(vectorOffset) +
        ", got " + std::to_string(actualRaster) + "," + std::to_string(actualVector));'''
assert a in s
s = s.replace(a,b,1)
s = s.replace('{0, 70, (std::numeric_limits<int>::max)()}', '{0, 70, 32767, 32768, 65536, (std::numeric_limits<int>::max)()}',1)
s = s.replace('checkPalette(palette, mode, offset, 10);','checkPalette(palette, mode, offset, 10, "original");',1)
s = s.replace('checkPalette(loaded, mode, offset, 10);','checkPalette(loaded, mode, offset, 10, "TPL");',1)
s = s.replace('checkPalette(TPaletteP(info->getPalette()), mode, offset, 10);','checkPalette(TPaletteP(info->getPalette()), mode, offset, 10, "PLI");',1)
p.write_text(s)
p = root / 'doc/trail_style_settings.md'
s = p.read_text()
s += '''\nFrame Offset PLI metadata uses a tagged decimal string rather than a numeric\nstyle parameter: PLI's numeric style encoding has a 16-bit integer part. The\nstring is parsed with range checking, preserving source IDs above 32767 without\nchanging the legacy style encoding. Regression cases include 32767, 32768, 65536\nand INT_MAX, with Cycle Off as well as all active modes.\n'''
p.write_text(s)
