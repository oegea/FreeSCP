#!/usr/bin/env python3
"""genprops.py — rewrite Embarcadero __property declarations into clang-compatible
__declspec(property(...)), used to port WinSCP engine headers without editing the source.

  __property UnicodeString Name = { read = FName, write = SetName };
becomes:
  <generated field accessors if a target is a field>
  __declspec(property(get=__pg_Name, put=SetName)) UnicodeString Name;

Rules (match WinSCP conventions):
  * a target matching /^F[A-Z_]/ is a data member  -> wrap in a generated inline accessor
  * any other target is a method                   -> referenced directly
  * persistence attrs (stored/default/index/nodefault) are stripped (runtime-irrelevant)
  * indexed properties  Name[IndexType I]  -> __declspec(property(...)) TYPE Name[];
    (their read/write must be methods; clang forwards the index args)

Read-only -> only get=, write-only -> only put=. Idempotent on already-converted files.
"""
import os
import re
import sys

PROP_RE = re.compile(
    r'__property\s+(?P<decl>[^=;{}]+?)\s*=\s*\{(?P<body>[^}]*)\}\s*;')
DECL_RE = re.compile(
    r'^(?P<type>.*?)\b(?P<name>[A-Za-z_]\w*)\s*(?P<index>\[[^\]]*\])?\s*$', re.DOTALL)
FIELD_RE = re.compile(r'^F[A-Z_]')


def is_field(target):
    return bool(FIELD_RE.match(target))


def parse_body(body):
    out = {}
    for part in body.split(','):
        if '=' not in part:
            continue
        k, v = part.split('=', 1)
        out[k.strip()] = v.strip()
    return out


def convert(match):
    decl = match.group('decl').strip()
    body = parse_body(match.group('body'))
    m = DECL_RE.match(decl)
    if not m:
        return match.group(0)  # leave untouched; will surface as a compile error to fix
    typ = m.group('type').strip()
    name = m.group('name')
    indexed = m.group('index') is not None

    read = body.get('read')
    write = body.get('write')
    accessors = []
    get_fn = put_fn = None

    if read is not None:
        if is_field(read) and not indexed:
            get_fn = '__pg_%s' % name
            accessors.append('%s __fastcall %s() { return %s; }' % (typ, get_fn, read))
        else:
            get_fn = read
    if write is not None:
        if is_field(write) and not indexed:
            put_fn = '__ps_%s' % name
            accessors.append('void __fastcall %s(%s v) { %s = v; }' % (put_fn, typ, write))
        else:
            put_fn = write

    spec = []
    if get_fn:
        spec.append('get=%s' % get_fn)
    if put_fn:
        spec.append('put=%s' % put_fn)
    brackets = '[]' if indexed else ''
    decl_line = '__declspec(property(%s)) %s %s%s;' % (', '.join(spec), typ, name, brackets)

    prefix = (' '.join(accessors) + ' ') if accessors else ''
    return prefix + decl_line


def process(text):
    return PROP_RE.sub(convert, text)


def main():
    if len(sys.argv) != 3:
        sys.stderr.write('usage: genprops.py <in.h> <out.h>\n')
        return 2
    out_dir = os.path.dirname(sys.argv[2])
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    with open(sys.argv[1], 'r', encoding='utf-8', errors='surrogateescape') as f:
        src = f.read()
    with open(sys.argv[2], 'w', encoding='utf-8', errors='surrogateescape') as f:
        f.write(process(src))
    return 0


if __name__ == '__main__':
    sys.exit(main())
