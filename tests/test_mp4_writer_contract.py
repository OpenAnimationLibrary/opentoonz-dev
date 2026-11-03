"""Source-level regression checks for MP4 writer concurrency safety.

Run: python tests/test_mp4_writer_contract.py <repository-root>
These checks do not replace compiling or executing OpenToonz.
"""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1]
sys.argv = sys.argv[:1]

class MP4WriterSafetyContract(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = (ROOT / 'toonz/sources/image/ffmpeg/tiio_mp4.h').read_text(encoding='utf-8')
        cls.impl = (ROOT / 'toonz/sources/image/ffmpeg/tiio_mp4.cpp').read_text(encoding='utf-8')

    def test_writer_owns_mutex(self):
        self.assertIn('QMutex m_frameWriteMutex;', self.header)

    def test_save_locks_before_shared_state(self):
        body = self.impl.split('void TLevelWriterMp4::save(const TImageP &img, int frameIndex) {', 1)[1].split('\n}', 1)[0]
        lock = body.index('QMutexLocker lock(&m_frameWriteMutex);')
        for expression in ['m_lx =', 'm_ly =', 'ffmpegWriter->createIntermediateImage(']:
            self.assertLess(lock, body.index(expression))

    def test_reject_invalid_raster_before_dereference(self):
        body = self.impl.split('void TLevelWriterMp4::save(const TImageP &img, int frameIndex) {', 1)[1].split('\n}', 1)[0]
        self.assertLess(body.index('if (!image || !image->getRaster())'), body.index('image->getRaster()->getLx()'))

if __name__ == '__main__':
    unittest.main(verbosity=2)
