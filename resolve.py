#!/usr/bin/env python3
"""Resolve the Depends/Pre-Depends closure of seed packages against this host's dpkg db.

usage: resolve.py [--provided FILE] seed [seed ...]

--provided FILE  packages already shipped by lower restartOS layers (one name per
                 line, extra columns ignored, e.g. a manifest). Dependencies they
                 satisfy are not followed, and they are left out of the output.
Prints one package per line. Exits 1 if a seed isn't installed on this host.
"""
import re, subprocess, sys

args = sys.argv[1:]
provided = set()
if args[:1] == ['--provided']:
    provided = {l.split()[0] for l in open(args[1]) if l.strip()}
    args = args[2:]
seeds = args

fmt = '${Package}\t${Status}\t${Depends}\t${Pre-Depends}\t${Provides}\n'
out = subprocess.run(['dpkg-query', '-W', '-f', fmt], capture_output=True, text=True).stdout
installed, provides = {}, {}
for line in out.splitlines():
    pkg, status, dep, pre, prov = (line.split('\t') + [''] * 5)[:5]
    if not status.endswith('installed') or 'not-installed' in status:
        continue
    installed[pkg] = ', '.join(x for x in (dep, pre) if x)
    for p in filter(None, (s.strip() for s in prov.split(','))):
        provides.setdefault(re.split(r'[ (]', p)[0], []).append(pkg)

def name(atom):
    return re.split(r'[ (:]', atom.strip())[0]

def candidates(atom):
    n = name(atom)
    return ([n] if n in installed else []) + provides.get(n, [])

def in_base(alts):
    return any(c in provided or name(a) in provided for a in alts for c in candidates(a))

def pick(alts):
    for a in alts:
        c = candidates(a)
        if c:
            return c[0]
    return None

absent = [p for p in seeds if p not in installed]
if absent:
    print('Seed packages not installed on this host: ' + ' '.join(absent), file=sys.stderr)
    print('Install them with: sudo apt install ' + ' '.join(absent), file=sys.stderr)
    sys.exit(1)

todo, seen, missing = [p for p in seeds if p not in provided], set(), []
for p in seeds:
    if p in provided:
        print(f'{p}: already in a lower layer', file=sys.stderr)
while todo:
    p = todo.pop()
    if p in seen:
        continue
    seen.add(p)
    for clause in filter(None, (c.strip() for c in installed.get(p, '').split(','))):
        alts = clause.split('|')
        if in_base(alts):
            continue
        chosen = pick(alts)
        if chosen:
            todo.append(chosen)
        else:
            missing.append(f'{p}: {clause}')
for p in sorted(seen):
    print(p)
for m in missing:
    print('MISSING', m, file=sys.stderr)
