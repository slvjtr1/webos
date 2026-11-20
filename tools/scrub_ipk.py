#!/usr/bin/env python3
# scrub_ipk.py <file.ipk>: the archives record who built them (the user and group names, the ids); the package
# carries "root" and 0 instead, so nothing of the build machine travels with it. The contents are unchanged.
# The modes are set here too: a package made on Windows records every file as 666 (there is no execute bit there),
# and the TV then cannot start the app at all. Programs and libraries (ELF) 755, other files 644, folders 755.
import io, json, sys, tarfile

ELF = b'\x7fELF'

def scrub_tar(blob, gz):
    src = tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz' if gz else 'r:')
    out = io.BytesIO()
    dst = tarfile.open(fileobj=out, mode='w:gz' if gz else 'w', format=tarfile.GNU_FORMAT)
    for m in src.getmembers():
        m.uid = m.gid = 0; m.uname = m.gname = 'root'
        if m.isdir():
            m.mode = 0o755; dst.addfile(m)
        elif m.isfile():
            body = src.extractfile(m).read()
            m.mode = 0o755 if body[:4] == ELF else 0o644
            dst.addfile(m, io.BytesIO(body))
        else:
            dst.addfile(m)
    dst.close(); return out.getvalue()

def members_of(data):
    assert data[:8] == b'!<arch>\n', 'not an ar archive'
    i, out = 8, []
    while i < len(data):
        h = data[i:i + 60]; name = h[:16].decode().strip(); size = int(h[48:58].decode().strip())
        out.append((name, h, data[i + 60:i + 60 + size])); i += 60 + size + (size & 1)
    return out

def check(data):
    # checked again from the file: every entry says root, every program and library is executable, and the
    # program appinfo.json names is there and executable
    for name, _, body in members_of(data):
        n = name.rstrip('/')
        if not (n.endswith('.tar.gz') or n.endswith('.tar')): continue
        t = tarfile.open(fileobj=io.BytesIO(body), mode='r:gz' if n.endswith('.gz') else 'r:')
        for m in t.getmembers():
            if m.uname not in ('root', '') or m.gname not in ('root', '') or m.uid or m.gid: sys.exit('scrub failed: ' + m.name)
            if m.isfile() and t.extractfile(m).read(4) == ELF and m.mode & 0o755 != 0o755: sys.exit('not executable in the package: ' + m.name)
        if n.startswith('data'):
            infos = [m for m in t.getmembers() if m.name.endswith('/appinfo.json') and '/applications/' in m.name]
            if not infos: sys.exit('no appinfo.json in the package')
            for ai in infos:
                prog = ai.name.rsplit('/', 1)[0] + '/' + json.loads(t.extractfile(ai).read())['main']
                try: pm = t.getmember(prog)
                except KeyError: sys.exit('the app\'s program is missing from the package: ' + prog)
                if pm.mode & 0o755 != 0o755 or t.extractfile(pm).read(4) != ELF: sys.exit('the app\'s program is not an executable program: ' + prog)

def main(path):
    out = bytearray(b'!<arch>\n')
    for name, h, body in members_of(open(path, 'rb').read()):
        n = name.rstrip('/')
        if n.endswith('.tar.gz'): body = scrub_tar(body, True)
        elif n.endswith('.tar'): body = scrub_tar(body, False)
        hdr = (name.ljust(16) + h[16:28].decode() + '0'.ljust(6) + '0'.ljust(6) + h[40:48].decode() + str(len(body)).ljust(10) + '`\n').encode()
        out += hdr + body + (b'\n' if len(body) & 1 else b'')
    open(path, 'wb').write(out)
    check(bytes(out))

if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--check': check(open(sys.argv[2], 'rb').read()); print('ok:', sys.argv[2])
    else: main(sys.argv[1])
