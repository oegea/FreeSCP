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
    # Split on top-level commas only (a value like `index = MASK_INDEX(a, b)` has commas
    # inside parentheses that must not split the property attribute list).
    parts, depth, cur = [], 0, ''
    for ch in body:
        if ch in '([{':
            depth += 1; cur += ch
        elif ch in ')]}':
            depth -= 1; cur += ch
        elif ch == ',' and depth == 0:
            parts.append(cur); cur = ''
        else:
            cur += ch
    if cur.strip():
        parts.append(cur)
    out = {}
    for part in parts:
        if '=' in part:
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
    index = body.get('index')   # Delphi `index=N`: getter/setter take this fixed value
    iarg = (index + (', ' if index else '')) if index else ''
    accessors = []
    get_fn = put_fn = None

    UNCONST = ('const_cast< ::std::remove_const< ::std::remove_reference<'
               'decltype(*this)>::type>::type *>(this)')

    # An indexed property:  TYPE Name[IDXDECL] = {read=G, write=S}  ->  obj->Name[i].
    # Forward through accessors so private getters/setters work; pass the index along.
    if indexed:
        idecl = m.group('index')[1:-1].strip()      # inside [...]
        toks = idecl.split()
        if len(toks) >= 2:
            iparam, iname = idecl, toks[-1]
        else:
            iparam, iname = (idecl + ' __i'), '__i'
        if read is not None:
            get_fn = '__pg_%s' % name
            accessors.append('%s __fastcall %s(%s) const { return (%s)(%s->%s(%s)); }'
                             % (typ, get_fn, iparam, typ, UNCONST, read, iname))
        if write is not None:
            put_fn = '__ps_%s' % name
            accessors.append('void __fastcall %s(%s, %s v) { %s->%s(%s, v); }'
                             % (put_fn, iparam, typ, UNCONST, write, iname))
        spec = []
        if get_fn: spec.append('get=%s' % get_fn)
        if put_fn: spec.append('put=%s' % put_fn)
        decl_line = '__declspec(property(%s)) %s %s[];' % (', '.join(spec), typ, name)
        prefix = (' '.join(accessors) + ' ') if accessors else ''
        return prefix + decl_line

    # Always emit a forwarding accessor (placed where __property was, i.e. its visibility —
    # usually public). It can legally read a private field or call a private getter/setter,
    # which __declspec(property) targeting the private member directly cannot from outside.
    if read is not None:
        get_fn = '__pg_%s' % name
        # Accessor is const so the property is readable on const objects. Field reads are
        # const-ok; method getters (often non-const) are reached by casting away constness of
        # *this without naming the class. `index=N` passes the fixed value.
        if is_field(read):
            expr = read
        else:
            call = '%s(%s)' % (read, index) if index else (read + '()')
            expr = '%s->%s' % (UNCONST, call)
        accessors.append('%s __fastcall %s() const { return (%s)(%s); }'
                         % (typ, get_fn, typ, expr))
    if write is not None:
        put_fn = '__ps_%s' % name
        if is_field(write):
            accessors.append('void __fastcall %s(%s v) { %s = v; }' % (put_fn, typ, write))
        else:
            accessors.append('void __fastcall %s(%s v) { %s(%sv); }' % (put_fn, typ, write, iarg))

    spec = []
    if get_fn:
        spec.append('get=%s' % get_fn)
    if put_fn:
        spec.append('put=%s' % put_fn)
    brackets = '[]' if indexed else ''
    decl_line = '__declspec(property(%s)) %s %s%s;' % (', '.join(spec), typ, name, brackets)

    prefix = (' '.join(accessors) + ' ') if accessors else ''
    return prefix + decl_line


# --- Borland try/finally -> RAII scope guard -------------------------------------------
# clang has no `try {} __finally {}`. Rewrite (brace/string/comment aware):
#   try { A } [catch(...){C}]* __finally { B }
#     no catch  -> { auto __finN = ::winscp::MakeFinally([&]() { B }); { A } }
#     w/ catch  -> { auto __finN = ::winscp::MakeFinally([&]() { B }); try { A } catch(...){C} }
# B runs on every exit path. Pure try/catch (no __finally) is left untouched (valid C++).

def _skip_trivia(s, i):
    n = len(s)
    while i < n:
        c = s[i]
        if c in ' \t\r\n':
            i += 1
        elif c == '/' and i + 1 < n and s[i+1] == '/':
            while i < n and s[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and s[i+1] == '*':
            i += 2
            while i + 1 < n and not (s[i] == '*' and s[i+1] == '/'):
                i += 1
            i += 2
        else:
            break
    return i


def _read_block(s, i):
    # s[i] must be '{'; return index just past the matching '}', string/comment aware.
    assert s[i] == '{'
    n = len(s)
    depth = 0
    while i < n:
        c = s[i]
        if c == '"' or c == "'":
            q = c
            i += 1
            while i < n and s[i] != q:
                if s[i] == '\\':
                    i += 1
                i += 1
            i += 1
        elif c == '/' and i + 1 < n and s[i+1] == '/':
            while i < n and s[i] != '\n':
                i += 1
        elif c == '/' and i + 1 < n and s[i+1] == '*':
            i += 2
            while i + 1 < n and not (s[i] == '*' and s[i+1] == '/'):
                i += 1
            i += 2
        else:
            if c == '{':
                depth += 1
            elif c == '}':
                depth -= 1
                if depth == 0:
                    return i + 1
            i += 1
    return -1


WORD = re.compile(r'\w')


def transform_try_finally(s):
    out = []
    i = 0
    n = len(s)
    counter = [0]
    while i < n:
        c = s[i]
        # copy over strings/comments verbatim so we never match inside them
        if c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c:
                if s[j] == '\\':
                    j += 1
                j += 1
            out.append(s[i:j+1]); i = j + 1; continue
        if c == '/' and i + 1 < n and s[i+1] == '/':
            j = s.find('\n', i)
            j = n if j < 0 else j
            out.append(s[i:j]); i = j; continue
        if c == '/' and i + 1 < n and s[i+1] == '*':
            j = s.find('*/', i)
            j = n if j < 0 else j + 2
            out.append(s[i:j]); i = j; continue
        # match `try` as a whole word
        if s.startswith('try', i) and (i == 0 or not WORD.match(s[i-1])) \
           and (i + 3 >= n or not WORD.match(s[i+3])):
            k = _skip_trivia(s, i + 3)
            if k < n and s[k] == '{':
                a_end = _read_block(s, k)
                if a_end > 0:
                    block_a = s[k:a_end]
                    catches = []
                    m = _skip_trivia(s, a_end)
                    while s.startswith('catch', m) and (m + 5 >= n or not WORD.match(s[m+5])):
                        p = s.index('{', m)
                        c_end = _read_block(s, p)
                        catches.append(s[m:c_end])
                        m = _skip_trivia(s, c_end)
                    if s.startswith('__finally', m) and (m + 9 >= n or not WORD.match(s[m+9])):
                        fb = _skip_trivia(s, m + 9)
                        b_end = _read_block(s, fb)
                        block_b = s[fb:b_end]
                        counter[0] += 1
                        # Recurse so nested try/finally inside A/B/catches are also rewritten.
                        block_a = transform_try_finally(block_a)
                        block_b = transform_try_finally(block_b)
                        catches = [transform_try_finally(c) for c in catches]
                        guard = '{ auto __fin%d = ::winscp::MakeFinally([&]() %s); ' % (counter[0], block_b)
                        if catches:
                            out.append(guard + 'try ' + block_a + ' ' + ' '.join(catches) + ' }')
                        else:
                            out.append(guard + block_a + ' }')
                        i = b_end
                        continue
        out.append(c)
        i += 1
    return ''.join(out)


# --- Delphi __closure events -> std::function + implicit-this handler binds ----------------
# 1) typedef RET __fastcall (__closure * NAME)(ARGS);  ->  typedef std::function<RET(ARGS)> NAME;
CLOSURE_TYPEDEF_RE = re.compile(
    r'typedef\s+(?P<ret>[\w:\*\s&<>,]+?)\s*(?:__fastcall\s*)?\(\s*__closure\s*\*\s*'
    r'(?P<name>\w+)\s*\)\s*\((?P<args>[^;]*?)\)\s*;')

def _closure_typedef(m):
    ret = m.group('ret').strip()
    return 'typedef ::std::function<%s(%s)> %s;' % (ret, m.group('args').strip(), m.group('name'))

# 2) <lhs ending in OnXxx> = <bare member-fn name> ;  ->  bind via decltype(*this) (no class name)
CLOSURE_BIND_RE = re.compile(
    r'(?<![A-Za-z0-9_>.])'                              # not mid-chain/mid-identifier
    r'(?P<lhs>(?:[A-Za-z_]\w*\s*(?:->|\.)\s*)*'
    r'(?<![A-Za-z0-9_])On[A-Z]\w*)\s*=\s*'              # OnXxx event (capital after On)
    r'(?P<rhs>[A-Za-z_]\w*)\s*;')
_BIND_SKIP = {'NULL', 'nullptr', 'True', 'False', 'true', 'false'}

def _closure_bind(m):
    rhs = m.group('rhs')
    # Skip: nulls/bools; event-to-event copies (OnXxx); and F-prefixed members which are
    # event-holding *fields* (WinSCP convention) not handler methods — those are plain copies.
    if rhs in _BIND_SKIP or rhs.startswith('On') or re.match(r'F[A-Z]', rhs):
        return m.group(0)
    return ('%s = ::winscp::MakeClosure(this, '
            '&::std::remove_reference<decltype(*this)>::type::%s);'
            % (m.group('lhs'), rhs))


# 2b) <lhs ending in OnXxx> = &recv.Method ;  ->  bind with an explicit receiver object.
#     (e.g. CopyAlias.OnSubmit = &ClipboardHandler.Copy;)
CLOSURE_BIND_ADDR_RE = re.compile(
    r'(?<![A-Za-z0-9_>.])'
    r'(?P<lhs>(?:[A-Za-z_]\w*\s*(?:->|\.)\s*)*'
    r'(?<![A-Za-z0-9_])On[A-Z]\w*)\s*=\s*'
    r'&\s*(?P<recv>[A-Za-z_]\w*)\s*\.\s*(?P<meth>[A-Za-z_]\w*)\s*;')

def _closure_bind_addr(m):
    return ('%s = ::winscp::MakeClosure(&%s, '
            '&::std::remove_reference<decltype(%s)>::type::%s);'
            % (m.group('lhs'), m.group('recv'), m.group('recv'), m.group('meth')))


# 3) closure passed as a call argument to a known closure-taking method: f(Method) -> bind.
#    (WinSCP funcs that take a Delphi event/closure by a member-method name, optionally with an
#    explicit receiver: f(Method) -> this-bound, f(obj.Method) -> obj-bound.)
CLOSURE_ARG_FUNCS = ['RunAction', 'TimeoutPrompt', 'RegisterReceiveHandler', 'UnregisterReceiveHandler']
CLOSURE_ARG_RE = re.compile(
    r'\b(?P<fn>' + '|'.join(CLOSURE_ARG_FUNCS) + r')\(\s*'
    r'(?:(?P<recv>[A-Za-z_]\w*)\s*\.\s*)?'
    r'(?P<rhs>[A-Za-z_]\w*)\s*\)')

def _closure_arg(m):
    rhs = m.group('rhs')
    if rhs in _BIND_SKIP or re.match(r'F[A-Z]', rhs):
        return m.group(0)
    recv = m.group('recv')
    if recv:
        return ('%s(::winscp::MakeClosure(&%s, '
                '&::std::remove_reference<decltype(%s)>::type::%s))'
                % (m.group('fn'), recv, recv, rhs))
    return ('%s(::winscp::MakeClosure(this, '
            '&::std::remove_reference<decltype(*this)>::type::%s))' % (m.group('fn'), rhs))


# 4) closures passed as call args in known closure-taking call shapes that the simple
#    single-arg rule (3) can't reach: an explicit-receiver method in a fixed argument slot, or
#    the address-of-bound-member form. Each is specific to a function whose signature takes a
#    Delphi event there, so wrapping is unambiguous (these recur in Terminal/Scp/Sftp).
def _mc_recv(recv, meth):
    return ('::winscp::MakeClosure(%s, &::std::remove_reference<decltype(*%s)>::type::%s)'
            % (recv, recv, meth))
def _mc_obj(obj, meth):
    return ('::winscp::MakeClosure(&%s, &::std::remove_reference<decltype(%s)>::type::%s)'
            % (obj, obj, meth))
def _mc_this(meth):
    return ('::winscp::MakeClosure(this, &::std::remove_reference<decltype(*this)>::type::%s)' % meth)

# 4a) TFileOperationProgressType Local(&recv->M1, &recv->M2);
PROGRESS_CTOR_RE = re.compile(
    r'TFileOperationProgressType\s+(?P<var>\w+)\s*\(\s*'
    r'&\s*(?P<r1>\w+)\s*->\s*(?P<m1>\w+)\s*,\s*'
    r'&\s*(?P<r2>\w+)\s*->\s*(?P<m2>\w+)\s*\)')
def _progress_ctor(m):
    return 'TFileOperationProgressType %s(%s, %s)' % (
        m.group('var'), _mc_recv(m.group('r1'), m.group('m1')), _mc_recv(m.group('r2'), m.group('m2')))

# 4b) ProcessDirectory(<arg1>, recv->Method, ...) — the 2nd arg is a TProcessFileEvent.
PROCESSDIR_RE = re.compile(
    r'ProcessDirectory\(\s*(?P<a1>[^,]+?)\s*,\s*(?P<recv>\w+)\s*->\s*(?P<meth>\w+)\s*,')
def _processdir(m):
    return 'ProcessDirectory(%s, %s,' % (m.group('a1'), _mc_recv(m.group('recv'), m.group('meth')))

# 4c) FileOperationLoop(Method, ...) — the 1st arg is the operation's callback event (this-bound).
FILEOPLOOP_RE = re.compile(r'FileOperationLoop\(\s*(?P<meth>[A-Za-z_]\w*)\s*,')
def _fileoploop(m):
    if re.match(r'F[A-Z]', m.group('meth')) or m.group('meth') in _BIND_SKIP:
        return m.group(0)
    return 'FileOperationLoop(%s,' % _mc_this(m.group('meth'))


def process(text):
    text = transform_try_finally(text)
    text = CLOSURE_TYPEDEF_RE.sub(_closure_typedef, text)
    text = CLOSURE_BIND_ADDR_RE.sub(_closure_bind_addr, text)
    text = CLOSURE_BIND_RE.sub(_closure_bind, text)
    text = PROGRESS_CTOR_RE.sub(_progress_ctor, text)
    text = PROCESSDIR_RE.sub(_processdir, text)
    text = FILEOPLOOP_RE.sub(_fileoploop, text)
    text = CLOSURE_ARG_RE.sub(_closure_arg, text)
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
