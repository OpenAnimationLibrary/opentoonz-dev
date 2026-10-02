from pathlib import Path
p = Path(__file__).resolve().with_name('prepare.py')
script = p.read_text()
fixes = p.with_name('corrections.py').read_text()
needle = "paths = out('git', 'diff', '--name-only', 'HEAD').splitlines()"
assert needle in script
script = script.replace(needle, 'exec(' + repr(fixes) + ')\n' + needle, 1)
exec(compile(script, str(p), 'exec'), {'__name__': '__main__', '__file__': str(p)})
