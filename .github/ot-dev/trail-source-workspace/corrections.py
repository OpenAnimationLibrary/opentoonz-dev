from pathlib import Path
p = Path('toonz/sources/common/tvrender/tsimplecolorstyles.cpp')
s = p.read_text()
old = '''    if (!TRaster64P(input) && !TRasterFP(input) && !TRasterGR8P(input) &&
        !TRasterGR16P(input))
      return TRaster32P();
'''
new = '''    if (TRasterGR16P gray = input) {
      // The generic converter has no GR16 -> RGBA path.
      TRaster32P rgba(input->getSize());
      gray->lock();
      rgba->lock();
      for (int y = 0; y < gray->getLy(); ++y) {
        const TPixelGR16 *source = gray->pixels(y);
        TPixel32 *target         = rgba->pixels(y);
        for (int x = 0; x < gray->getLx(); ++x) {
          const int value = (int(source[x].value) + 128) / 257;
          target[x] = TPixel32(value, value, value, 255);
        }
      }
      rgba->setLinear(input->isLinear());
      rgba->unlock();
      gray->unlock();
      return rgba;
    }
    if (!TRaster64P(input) && !TRasterFP(input) && !TRasterGR8P(input))
      return TRaster32P();
'''
assert old in s
p.write_text(s.replace(old, new, 1))
p = Path('toonz/tests/trail_settings/trailsource_test.cpp')
s = p.read_text()
assert 'extractT(TRect(8, 8, 23, 23))' in s
p.write_text(s.replace('extractT(TRect(8, 8, 23, 23))', 'extractT(8, 8, 23, 23)'))
