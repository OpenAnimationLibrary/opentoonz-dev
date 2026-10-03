"""Exercise the provided TPL fixture through the public color-pair CLI."""
import argparse
import pathlib
import subprocess
import xml.etree.ElementTree as ET


def sample(values, size, rgb):
    position = [v * (size - 1) for v in rgb]
    lo = [min(int(v), size - 2) for v in position]
    t = [position[c] - lo[c] for c in range(3)]
    out = [0.0, 0.0, 0.0]
    for r in range(2):
        for g in range(2):
            for b in range(2):
                weight = ((t[0] if r else 1 - t[0]) *
                          (t[1] if g else 1 - t[1]) *
                          (t[2] if b else 1 - t[2]))
                index = ((lo[2] + b) * size + lo[1] + g) * size + lo[0] + r
                for c in range(3):
                    out[c] += values[index][c] * weight
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True)
    parser.add_argument('--workdir', required=True)
    args = parser.parse_args()
    work = pathlib.Path(args.workdir)
    work.mkdir(parents=True, exist_ok=True)
    tpl = ET.parse(pathlib.Path(__file__).with_name('example_keys.tpl')).getroot()
    styles = tpl.findall('./styles/style')
    keyed = tpl.find('./animation/style[@id="2"]')
    source = tuple(int(v) / 255 for v in keyed.find('./keyframe[@frame="0"]').text.split()[2:5])
    target = tuple(int(v) / 255 for v in keyed.find('./keyframe[@frame="1"]').text.split()[2:5])
    black = tuple(int(v) / 255 for v in styles[1].text.split()[2:5])
    assert source == (1, 0, 0) and target == (9 / 255, 1, 0)
    assert int(styles[0].text.split()[-1]) == 0
    pair_file = work / 'palette color pairs.txt'
    pair_file.write_text(' '.join(map(str, source + target + (0.1,))) + ' style_2\n' +
                         ' '.join(map(str, black + black + (0.1,))) + ' style_1\n')
    output = work / 'palette result.cube'
    command = [args.exe, '--color-pairs', str(pair_file), '--output', str(output), '--size', '33']
    run = subprocess.run(command, capture_output=True, text=True, check=True)
    assert 'Changed colors: 1' in run.stdout and 'Preserved colors: 1' in run.stdout
    values = []
    for line in output.read_text().splitlines():
        if line and (line[0].isdigit() or line[0] == '-'):
            values.append(tuple(map(float, line.split())))
    assert len(values) == 33 ** 3
    for before, after in [(source, target), (black, black), ((0, 0, 1), (0, 0, 1))]:
        assert max(abs(a - b) for a, b in zip(sample(values, 33, before), after)) < 0.251 / 255
    # Conflicts must fail before the output is opened or overwritten.
    old = output.read_bytes()
    pair_file.write_text('1 0 0 0 1 0 0.1 change\n1 0 0 1 0 0 0.1 preserve\n')
    bad = subprocess.run(command, capture_output=True, text=True)
    assert bad.returncode != 0 and 'Conflicting' in bad.stderr
    assert output.read_bytes() == old
    print('TPL fixture, CLI, cube ordering, preservation and conflict handling passed.')


if __name__ == '__main__':
    main()
