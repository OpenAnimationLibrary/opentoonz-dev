from pathlib import Path
import shutil
import subprocess
p = Path(__file__).resolve().with_name('prepare.py')
script = p.read_text()
fixes = p.with_name('corrections.py').read_text()
needle = "paths = out('git', 'diff', '--name-only', 'HEAD').splitlines()"
assert needle in script
script = script.replace(needle, 'exec(' + repr(fixes) + ')\n' + needle, 1)
# The native reader uses OpenToonz's TIFFReadRGBATile_64/Strip_64 extensions.
# Build the real bundled TIFF instead of substituting a system/mock reader.
root = Path.cwd()
report = Path('/tmp/trail-source-report')
report.mkdir(exist_ok=True)
tiff = Path('/tmp/trail-patched-tiff')
shutil.copytree(root / 'thirdparty/tiff-4.0.3', tiff)
with (report / 'patched-tiff.log').open('w') as stream:
    result = subprocess.run(['bash', '-c', 'set -e; CFLAGS=-fPIC CXXFLAGS=-fPIC ./configure --prefix=/tmp/trail-tiff --disable-jbig --disable-lzma; make -j4; make install'], cwd=tiff, stdout=stream, stderr=subprocess.STDOUT)
if result.returncode:
    print('\n'.join((report / 'patched-tiff.log').read_text(errors='replace').splitlines()[-80:]))
result.check_returncode()
print('Bundled patched TIFF built successfully', flush=True)
needle = "'-DTRAIL_LZO_BIN_DIR=' + str(helpers)"
assert needle in script
script = script.replace(needle, "'-DTRAIL_LZO_BIN_DIR=' + str(helpers), '-DTIFF_INCLUDE_DIR=/tmp/trail-tiff/include', '-DTIFF_LIBRARY=/tmp/trail-tiff/lib/libtiff.so', '-DTIFF_LIBRARY_RELEASE=/tmp/trail-tiff/lib/libtiff.so'")
exec(compile(script, str(p), 'exec'), {'__name__': '__main__', '__file__': str(p)})
