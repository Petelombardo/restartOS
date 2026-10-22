#!/usr/bin/env python3
"""Turn a package list into the list of host paths that make up those packages.

usage: collect.py <closure.txt> <filelist.out>

- Paths come from `dpkg -L`, canonicalised through merged-/usr symlinks.
- Docs, manpages, locales, info and lintian overrides are dropped.
- update-alternatives links are added when their final target is in the set.
"""
import os, subprocess, sys

closure, listout = sys.argv[1:3]
pkgs = open(closure).read().split()

EXCLUDE = ('/usr/share/doc/', '/usr/share/man/', '/usr/share/info/',
           '/usr/share/locale/', '/usr/share/lintian/', '/usr/share/bug/',
           # nmap's NSE scripts (13 MB): only needed for -sC/--script/-A; plain,
           # -sV and -O scans just need the nmap-* data files
           '/usr/share/nmap/scripts/', '/usr/share/nmap/nselib/')
TOPLEVEL_LINKS = {'/bin', '/sbin', '/lib', '/lib64'}

raw = subprocess.run(['dpkg', '-L'] + pkgs, capture_output=True, text=True).stdout.split('\n')

def canon(p):
    if p in ('/', '/.'):
        return None
    return os.path.join(os.path.realpath(os.path.dirname(p)), os.path.basename(p))

paths = set()
for p in raw:
    if not p.startswith('/'):
        continue
    c = canon(p)
    if not c or c in TOPLEVEL_LINKS:
        continue
    if any(c.startswith(e) or c + '/' == e for e in EXCLUDE):
        continue
    if os.path.lexists(c):
        paths.add(c)

# update-alternatives: link -> /etc/alternatives/x -> real target. Links come from
# the dpkg admin db (master and slaves), not just /usr/bin, so library alternatives
# such as libblas.so.3 / liblapack.so.3 are included too.
# admin file: mode, master link, then (slave name, slave link) pairs up to a blank line
ADMIN = '/var/lib/dpkg/alternatives'
for name in os.listdir(ADMIN):
    lines = open(os.path.join(ADMIN, name)).read().split('\n')
    links = {name: lines[1]}
    i = 2
    while i + 1 < len(lines) and lines[i]:
        links[lines[i]] = lines[i + 1]
        i += 2
    for n, link in links.items():
        alt = '/etc/alternatives/' + n
        if os.path.islink(link) and os.readlink(link) == alt and os.path.realpath(link) in paths:
            paths.update({canon(link), alt, '/etc/alternatives'})

# make sure every parent directory is listed so tar --no-recursion recreates it
for p in list(paths):
    d = os.path.dirname(p)
    while d not in ('/', '') and d not in paths and d not in TOPLEVEL_LINKS:
        paths.add(d)
        d = os.path.dirname(d)

with open(listout, 'w') as f:
    for p in sorted(paths):
        f.write(p.lstrip('/') + '\n')

print(f'{len(paths)} paths', file=sys.stderr)
