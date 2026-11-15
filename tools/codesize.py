# Code size of the examples compared with OMSI Pascal, see docs/DESIGN.md section 7.11.
#
# Usage: python tools/codesize.py [OMSI_DIR]
#   OMSI_DIR - folder with the uknc-various repository (KINGOM-PASCAL, STALK-PASCAL with the OMSI .MAC files);
#              without it only the Pesto/11 numbers are printed.
#   Examples written for both compilers (leibniz, sieve, qsort, lz77, tpk, mandel; the file opening line is replaced by the OMSI form)
#   and the OMSI variants in tools/omsi (wumpus) are compiled by OMSI Pascal on the spot: PASCAL.SAV from
#   OMSI_DIR/KINGOM-PASCAL under the RT-11 emulator rt11.exe (environment variable RT11EXE,
#   default C:/bin/rt11/rt11.exe). Without it these examples are skipped in the comparison.
#
# Two comparisons:
#  - program code, words: instruction lines of the assembler listing; .WORD right after CALL/JSR (inline
#    arguments) counts as code too. Strings, variables and the runtime are not counted;
#  - whole image, bytes: the top address of the .SAV minus 1000 -- code, strings, runtime and its buffers.
#    OMSI Pascal allocates the global variables at the start, so ours (.BLKW of the program) are subtracted.
# Procedures that have no counterpart in OMSI code are excluded: RANDU (FORTRAN library in the original)
# and the high score module KINGM6 (L65754, L64564, L65504: ported in another way).
import os, re, sys, struct, subprocess, tempfile, shutil, collections

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMPILER = os.path.join(ROOT, 'Debug', 'pesto11.exe') if os.name == 'nt' else os.path.join(ROOT, 'pesto11')
MACRO11 = os.path.join(ROOT, 'x-tools', 'macro11.exe' if os.name == 'nt' else 'macro11')
EXCLUDE = {'RANDU', 'L65754', 'L64564', 'L65504'}
LINE = re.compile(r"^\s*\d+\s+(?:([0-7]{6})\s+)?((?:[0-7]{6}[G']?\s+){0,3})(.*)$")

def assemble(mac, workdir):
    name = os.path.splitext(os.path.basename(mac))[0]
    subprocess.run([MACRO11, os.path.basename(mac), '-l', name + '.LST', '-o', name + '.OBJ', '-rt11'],
                   cwd=workdir, capture_output=True)
    return os.path.join(workdir, name + '.LST')

def count(lst, ours, skip=0):
    """Code words per procedure. Ours: segments start at Pnnnnn:/START::; OMSI: at a "; procedure X" comment.
    skip: the first procedures of an OMSI listing (up to that many RTS) are not counted."""
    res = collections.OrderedDict()
    cur, lastname, prevop = ('SKIP' if skip else 'MAIN'), None, ''
    for l in open(lst, 'rb').read().decode('koi8-r', 'replace').replace('\r', '').split('\n'):
        m = LINE.match(l)
        if not m:
            continue
        rest = m.group(3)
        body = rest.split(';')[0].strip()
        comment = rest[len(rest.split(';')[0]) + 1:].strip() if ';' in rest else ''
        mm = re.match(r'(?i)(procedure|function)\s+(\w+)', comment)
        if mm:
            lastname = mm.group(2).upper()
            if not ours and not re.search(r'(?i)external|fortran', comment):
                cur = lastname
        lab = re.match(r'^(\w+\$?)::?', body)
        if lab:
            name = lab.group(1).upper()
            if ours and re.match(r'P\d{5}$', name):
                cur = lastname
            elif ours and name == 'START':
                cur = 'MAIN'
            elif ours and re.match(r'[VCK]\d{5}$', name):
                cur = 'DATA'
            elif not ours and name == '$BEGIN':
                cur = 'MAIN'
        words = len(m.group(2).split())
        if words == 0:
            continue
        f = re.sub(r'^\w+\$?:+\s*', '', body).split()
        op = f[0].upper() if f else ''
        if op.startswith('.'):
            if not (op == '.WORD' and prevop in ('CALL', 'JSR')):
                continue
        else:
            prevop = op
        res[cur] = res.get(cur, 0) + words
        if cur == 'SKIP' and op == 'RTS':
            skip -= 1
            if skip == 0:
                cur = 'MAIN'
    return sum(v for k, v in res.items() if k not in ('DATA', 'SKIP') and k not in EXCLUDE)

def runtime_words(lst):
    """Words of the runtime module: code and initialized data (tables), .BLKW not counted"""
    total = 0
    for l in open(lst, 'rb').read().decode('koi8-r', 'replace').replace(chr(13), '').split(chr(10)):
        m = LINE.match(l)
        if m:
            total += len(m.group(2).split())
    return total

PCLINK11 = os.path.join(ROOT, 'x-tools', 'pclink11.exe' if os.name == 'nt' else 'pclink11')

def image_bytes(sav):
    """Bytes of the program image in a .SAV: the top address (word at 50) minus the start 1000"""
    data = open(sav, 'rb').read()
    return struct.unpack('<H', data[0o50:0o52])[0] - 0o1000

def ours(path):
    """Code words of the program, words of its runtime, and the image without the global variables.
    OMSI Pascal allocates the global variables at the start, so they are not in its .SAV image;
    ours are .BLKW in the program and are subtracted."""
    work = tempfile.mkdtemp()
    try:
        shutil.copy(path, work)
        name = os.path.splitext(os.path.basename(path))[0]
        subprocess.run([COMPILER, '-q', name + '.PAS'], cwd=work, check=True)
        code = count(assemble(os.path.join(work, name + '.MAC'), work), True)
        runtime = runtime_words(assemble(os.path.join(work, 'PESTO.MAC'), work))
        subprocess.run([PCLINK11, name + '.OBJ', 'PESTO.OBJ', '-EXECUTE:' + name + '.SAV'], cwd=work, capture_output=True)
        mac = open(os.path.join(work, name + '.MAC'), 'rb').read().decode('koi8-r', 'replace')
        globals_ = 2 * sum(int(n) for n in re.findall(r'^V\d{5}:\t\.BLKW\t(\d+)\.', mac, re.M))
        image = image_bytes(os.path.join(work, name + '.SAV')) - globals_
        return code, runtime, image
    finally:
        shutil.rmtree(work, ignore_errors=True)

RT11EXE = os.environ.get('RT11EXE', r'C:/bin/rt11/rt11.exe')

# Lines that differ between Turbo Pascal 3.0 and OMSI Pascal: opening a text file
OMSI_PATCHES = [
    ("ASSIGN(F, 'LZDATA.TXT'); RESET(F); V := 1;", "RESET(F, 'LZDATA', 'TXT', V);"),
]

def omsi_build(src, omsidir, work):
    """Compile, assemble and link the source by OMSI Pascal under rt11.exe: (MAC, SAV) or None"""
    pascal = os.path.join(omsidir, 'KINGOM-PASCAL', 'PASCAL.SAV')
    library = os.path.join(omsidir, 'KINGOM-PASCAL', 'PASCAL.OBJ')
    if not os.path.exists(RT11EXE) or not os.path.exists(pascal):
        return None
    text = open(src, 'rb').read().decode('koi8-r')
    for old, new in OMSI_PATCHES:
        text = text.replace(old, new)
    open(os.path.join(work, os.path.basename(src)), 'wb').write(text.encode('koi8-r'))
    shutil.copy(pascal, work)
    shutil.copy(library, work)
    name = os.path.splitext(os.path.basename(src))[0]
    subprocess.run([RT11EXE, 'RU', 'PASCAL', '%s,%s=%s.PAS' % (name, name, name)], cwd=work, capture_output=True)
    mac = os.path.join(work, name + '.MAC')
    if not os.path.exists(mac):
        return None
    subprocess.run([RT11EXE, 'MACRO', name + '.MAC'], cwd=work, capture_output=True)
    subprocess.run([RT11EXE, 'LINK/STACK:1000', name + ',PASCAL'], cwd=work, capture_output=True)
    sav = os.path.join(work, name + '.SAV')
    return mac, (sav if os.path.exists(sav) else None)

def omsi(macs, skip=0):
    work = tempfile.mkdtemp()
    try:
        total = 0
        for mac in macs:
            shutil.copy(mac, work)
            total += count(assemble(os.path.join(work, os.path.basename(mac)), work), False, skip)
        return total
    finally:
        shutil.rmtree(work, ignore_errors=True)

# Name, our source, OMSI .MAC files and .SAV in OMSI_DIR (None: the source is compiled by OMSI here;
# a tuple: the OMSI variant of the source in tools/omsi, compiled here, and the number of its first
# procedures not counted -- helpers for what TP3 has built in)
programs = [
    ('kingdom', os.path.join(ROOT, 'examples', 'kingdom', 'KINGOM.PAS'),
     ['KINGOM-PASCAL/KINGOM.MAC', 'KINGOM-PASCAL/KINGM2.MAC', 'KINGOM-PASCAL/KINGM3.MAC',
      'KINGOM-PASCAL/KINGM4.MAC', 'KINGOM-PASCAL/KINGM5.MAC'], 'KINGOM-PASCAL/KINGOM.SAV'),
    ('stalker', os.path.join(ROOT, 'examples', 'stalker', 'STALK.PAS'), ['STALK-PASCAL/STALK1.MAC'],
     'STALK-PASCAL/STALKP.SAV'),
    ('leibniz', os.path.join(ROOT, 'examples', 'leibniz', 'LEIBNZ.PAS'), None, None),
    ('sieve', os.path.join(ROOT, 'examples', 'sieve', 'SIEVE.PAS'), None, None),
    ('qsort', os.path.join(ROOT, 'examples', 'qsort', 'QSORT.PAS'), None, None),
    ('lz77', os.path.join(ROOT, 'examples', 'lz77', 'LZ77.PAS'), None, None),
    ('tpk', os.path.join(ROOT, 'examples', 'tpk', 'TPK.PAS'), None, None),
    ('mandel', os.path.join(ROOT, 'examples', 'mandel', 'MANDEL.PAS'), None, None),
    # OMSI: the cave filled by INITCAVE (TP3: a typed constant) and RANDOM are not counted
    ('wumpus', os.path.join(ROOT, 'examples', 'wumpus', 'WUMPUS.PAS'),
     (os.path.join(ROOT, 'tools', 'omsi', 'WUMPUS.PAS'), 3), None),
]

def percent(a, b):
    return '%5.1f%%' % (100.0 * a / b) if b else '     -'

omsidir = sys.argv[1] if len(sys.argv) > 1 else None
print('Program code, words (no runtime, strings, variables) | whole image, bytes (code, strings, runtime,')
print('runtime buffers; no global variables)')
print('%-8s %8s %8s %7s | %8s %8s %7s | %s' % ('', 'Pesto/11', 'OMSI', '', 'Pesto/11', 'OMSI', '', 'runtime'))
sums = [0, 0, 0, 0, 0]
for name, src, macs, sav in programs:
    code, runtime, image = ours(src)
    omsicode = omsiimage = 0
    if omsidir:
        if macs is None or isinstance(macs, tuple):
            omsisrc, skip = (src, 0) if macs is None else macs
            work = tempfile.mkdtemp()
            try:
                built = omsi_build(omsisrc, omsidir, work)
                if built is not None:
                    omsicode = omsi([built[0]], skip)
                    if built[1] is not None:
                        omsiimage = image_bytes(built[1])
            finally:
                shutil.rmtree(work, ignore_errors=True)
        else:
            omsicode = omsi([os.path.join(omsidir, m) for m in macs])
            omsiimage = image_bytes(os.path.join(omsidir, sav))
    if omsicode and omsiimage:
        for i, v in enumerate((code, omsicode, image, omsiimage, runtime)):
            sums[i] += v
    print('%-8s %8d %8s %7s | %8d %8s %7s | %5d' % (name, code, omsicode or '-', percent(code, omsicode),
          image, omsiimage or '-', percent(image, omsiimage), runtime))
if omsidir:
    print('%-8s %8d %8d %7s | %8d %8d %7s | %5d' % ('total', sums[0], sums[1], percent(sums[0], sums[1]),
          sums[2], sums[3], percent(sums[2], sums[3]), sums[4]))
