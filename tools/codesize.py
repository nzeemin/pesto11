# Code size of the examples compared with OMSI Pascal, see docs/DESIGN.md section 7.11.
#
# Usage: python tools/codesize.py [OMSI_DIR]
#   OMSI_DIR - folder with the uknc-various repository (KINGOM-PASCAL, STALK-PASCAL with the OMSI .MAC files);
#              without it only the Pesto/11 numbers are printed.
#   Examples written for both compilers (leibniz, sieve) are compiled by OMSI Pascal on the spot: PASCAL.SAV from
#   OMSI_DIR/KINGOM-PASCAL under the RT-11 emulator rt11.exe (environment variable RT11EXE,
#   default C:/bin/rt11/rt11.exe). Without it these examples are skipped in the comparison.
#
# Code words: instruction lines of the assembler listing; .WORD right after CALL/JSR (inline arguments)
# counts as code too. Strings, variables and the runtime are not counted.
# Procedures that have no counterpart in OMSI code are excluded: RANDU (FORTRAN library in the original)
# and the high score module KINGM6 (L65754, L64564, L65504: ported in another way).
import os, re, sys, subprocess, tempfile, shutil, collections

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

def count(lst, ours):
    """Code words per procedure. Ours: segments start at Pnnnnn:/START::; OMSI: at a "; procedure X" comment."""
    res = collections.OrderedDict()
    cur, lastname, prevop = 'MAIN', None, ''
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
    return sum(v for k, v in res.items() if k != 'DATA' and k not in EXCLUDE)

def ours(path):
    work = tempfile.mkdtemp()
    try:
        shutil.copy(path, work)
        name = os.path.basename(path)
        subprocess.run([COMPILER, '-q', name], cwd=work, check=True)
        return count(assemble(os.path.join(work, os.path.splitext(name)[0] + '.MAC'), work), True)
    finally:
        shutil.rmtree(work, ignore_errors=True)

RT11EXE = os.environ.get('RT11EXE', r'C:/bin/rt11/rt11.exe')

def omsi_compile(src, omsidir, work):
    """MAC of the source by OMSI Pascal; None if the compiler cannot be run"""
    pascal = os.path.join(omsidir, 'KINGOM-PASCAL', 'PASCAL.SAV')
    if not os.path.exists(RT11EXE) or not os.path.exists(pascal):
        return None
    shutil.copy(src, work)
    shutil.copy(pascal, work)
    name = os.path.splitext(os.path.basename(src))[0]
    subprocess.run([RT11EXE, 'RU', 'PASCAL', '%s,%s=%s.PAS' % (name, name, name)], cwd=work, capture_output=True)
    mac = os.path.join(work, name + '.MAC')
    return mac if os.path.exists(mac) else None

def omsi(macs):
    work = tempfile.mkdtemp()
    try:
        total = 0
        for mac in macs:
            shutil.copy(mac, work)
            total += count(assemble(os.path.join(work, os.path.basename(mac)), work), False)
        return total
    finally:
        shutil.rmtree(work, ignore_errors=True)

programs = [
    ('kingdom', os.path.join(ROOT, 'examples', 'kingdom', 'KINGOM.PAS'),
     ['KINGOM-PASCAL/KINGOM.MAC', 'KINGOM-PASCAL/KINGM2.MAC', 'KINGOM-PASCAL/KINGM3.MAC',
      'KINGOM-PASCAL/KINGM4.MAC', 'KINGOM-PASCAL/KINGM5.MAC']),
    ('stalker', os.path.join(ROOT, 'examples', 'stalker', 'STALK.PAS'), ['STALK-PASCAL/STALK1.MAC']),
    ('leibniz', os.path.join(ROOT, 'examples', 'leibniz', 'LEIBNZ.PAS'), None),  # Compiled by OMSI here
    ('sieve', os.path.join(ROOT, 'examples', 'sieve', 'SIEVE.PAS'), None),
]
omsidir = sys.argv[1] if len(sys.argv) > 1 else None
sumours = sumomsi = 0
for name, src, macs in programs:
    a = ours(src)
    if omsidir:
        if macs is None:
            work = tempfile.mkdtemp()
            try:
                mac = omsi_compile(src, omsidir, work)
                if mac is None:
                    print('%-8s Pesto/11 %6d  (OMSI Pascal not available)' % (name, a))
                    continue
                b = omsi([mac])
            finally:
                shutil.rmtree(work, ignore_errors=True)
        else:
            b = omsi([os.path.join(omsidir, m) for m in macs])
        sumours += a
        sumomsi += b
        print('%-8s Pesto/11 %6d  OMSI %6d  %5.1f%%' % (name, a, b, 100.0 * a / b))
    else:
        print('%-8s Pesto/11 %6d' % (name, a))
if omsidir:
    print('%-8s Pesto/11 %6d  OMSI %6d  %5.1f%%' % ('total', sumours, sumomsi, 100.0 * sumours / sumomsi))
