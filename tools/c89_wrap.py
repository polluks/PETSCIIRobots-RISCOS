#!/usr/bin/env python3
# Wrap `for (int X = ...; ...) { }` C99 loops into C89 by hoisting the loop
# variable declaration and its initializer(s) before the loop.
import re
import sys

_for_types = ('int', 'uint8_t', 'uint16_t', 'char', 'long', 'unsigned', 'bool')


def skip_string(text, p, q):
    n = len(text)
    if q == '/':
        if p + 1 < n and text[p + 1] == '*':
            e = text.find('*/', p + 2)
            return e + 2 if e >= 0 else n
        if p + 1 < n and text[p + 1] == '/':
            e = text.find('\n', p)
            return e if e >= 0 else n
        return p + 1
    i = p + 1
    while i < n:
        c = text[i]
        if c == '\\':
            i += 2
            continue
        if c == q:
            return i + 1
        if c == '\n':
            return i
        i += 1
    return n


def parse_ws(text, p):
    n = len(text)
    while p < n:
        c = text[p]
        if c in ' \t\r\n':
            p += 1
            continue
        if c == '/' and p + 1 < n and text[p + 1] in '/*':
            p = skip_string(text, p, '/')
            continue
        break
    return p


def prev_sig(text, p):
    j = p - 1
    while j >= 0:
        c = text[j]
        if c in ' \t\r\n':
            j -= 1
            continue
        if c == '/' and j - 1 >= 0 and text[j - 1] == '*':
            e = text.rfind('/*', 0, j - 1)
            if e < 0:
                return ''
            j = e - 1
            continue
        if c == '\n' and j - 1 >= 0 and text[j - 1] == '/':
            e = text.rfind('\n', 0, j - 1)
            j = e
            continue
        return c
    return ''


def directive_end(text, p):
    n = len(text)
    j = p
    while j < n:
        c = text[j]
        if c == '\n':
            k = j - 1
            while k >= p and text[k] in ' \t':
                k -= 1
            if k >= p and text[k] == '\\':
                j += 1
                continue
            return j + 1
        j += 1
    return j


def match_paren(text, p):
    depth = 1
    i = p + 1
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            i = skip_string(text, i, c)
            continue
        if c == '/' and i + 1 < n and text[i + 1] in '/*':
            i = skip_string(text, i, '/')
            continue
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return n


def match_brace(text, p):
    depth = 1
    n = len(text)
    i = p + 1
    cond = []
    outer = False
    start_of_line = True
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            i = skip_string(text, i, c)
            start_of_line = False
            continue
        if c == '/' and i + 1 < n and text[i + 1] in '/*':
            i = skip_string(text, i, '/')
            start_of_line = (i < n and text[i - 1] == '\n')
            continue
        if start_of_line and c in ' \t':
            i += 1
            continue
        if start_of_line and c == '#':
            de = directive_end(text, i)
            line = text[i:de]
            w = line.lstrip().split(None, 1)
            kw = w[0] if w else ''
            if kw in ('#if', '#ifdef', '#ifndef'):
                cond.append(0)
            elif kw == '#elif':
                if cond:
                    cond[-1] = 1
                else:
                    outer = True
            elif kw == '#else':
                if cond:
                    cond[-1] = 1
                else:
                    outer = True
            elif kw == '#endif':
                if cond:
                    cond.pop()
                elif outer:
                    outer = False
            i = de
            start_of_line = True
            continue
        if c == '\n':
            start_of_line = True
            i += 1
            continue
        if c == '{':
            if not outer and 1 not in cond:
                depth += 1
        elif c == '}':
            if not outer and 1 not in cond:
                depth -= 1
                if depth == 0:
                    return i + 1
        start_of_line = False
        i += 1
    return n


def contains_directive(text):
    line_start = True
    for ch in text:
        if ch == '\n':
            line_start = True
        elif line_start and ch in ' \t':
            pass
        elif line_start and ch == '#':
            return True
        else:
            line_start = False
    return False


def else_arm_end(text, de):
    n = len(text)
    i = de
    depth = 0
    while i < n:
        j = parse_ws(text, i)
        if j < n and text[j] == '#':
            d2 = directive_end(text, j)
            line = text[j:d2]
            w = line.lstrip().split(None, 1)
            kw = w[0] if w else ''
            if kw in ('#if', '#ifdef', '#ifndef'):
                depth += 1
            elif kw == '#endif':
                if depth == 0:
                    return d2
                depth -= 1
            i = d2
        else:
            k = text.find('\n', i)
            if k < 0:
                return n
            i = k + 1
    return n


def statement_end(text, s):
    p = parse_ws(text, s)
    n = len(text)
    if p >= n:
        return p
    c = text[p]
    if c == '{':
        return match_brace(text, p)
    if text.startswith('if', p) or text.startswith('while', p) or text.startswith('for', p):
        k = 2 if text.startswith('if', p) else (5 if text.startswith('while', p) else 3)
        q = parse_ws(text, p + k)
        if q < n and text[q] == '(':
            r = match_paren(text, q)
            e = statement_end(text, r)
            nxt = parse_ws(text, e)
            if text.startswith('else', nxt):
                e = statement_end(text, parse_ws(text, nxt + 4))
            return e
    if text.startswith('switch', p):
        q = parse_ws(text, p + 6)
        if q < n and text[q] == '(':
            r = match_paren(text, q)
            return statement_end(text, r)
    if text.startswith('do', p):
        e = statement_end(text, parse_ws(text, p + 2))
        nxt = parse_ws(text, e)
        if text.startswith('while', nxt):
            r = match_paren(text, parse_ws(text, nxt + 5))
            sem = parse_ws(text, r)
            if sem < n and text[sem] == ';':
                return sem + 1
        return e
    depth = 0
    while p < n:
        c = text[p]
        if c == '"' or c == "'":
            p = skip_string(text, p, c)
            continue
        if c == '/' and p + 1 < n and text[p + 1] in '/*':
            p = skip_string(text, p, '/')
            continue
        if c == '}' and depth == 0:
            return p
        if c == ';' and depth == 0:
            return p + 1
        if c in '({[':
            depth += 1
        elif c in ')}]':
            depth -= 1
        p += 1
    return p


def split_top_level(text, sep):
    parts = []
    depth = 0
    cur = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            e = skip_string(text, i, c)
            cur.append(text[i:e])
            i = e
            continue
        if c == '/' and i + 1 < n and text[i + 1] in '/*':
            e = skip_string(text, i, '/')
            cur.append(text[i:e])
            i = e
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == sep and depth == 0:
            parts.append(''.join(cur))
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    parts.append(''.join(cur))
    return parts


def split_decls(head):
    # head like 'int X = 0, Y = CONTROLSTART[CONTROL]'
    items = []
    for d in split_top_level(head, ','):
        items.append(d.strip())
    return items


def parse_for_header(text, q):
    # text[q] == '(' of a for-header; return (decl_type, [ (name, star, init) ], new_header) or None
    r = match_paren(text, q)
    header = text[q + 1:r - 1]
    clauses = split_top_level(header, ';')
    if not clauses or not clauses[0].strip():
        return None
    first = clauses[0].strip()
    tname = None
    rest = None
    for t in _for_types:
        if first == t or first.startswith(t + ' ') or first.startswith(t + '*'):
            tname = t
            rest = first[len(t):].strip()
            break
    if tname is None:
        return None
    if not re.match(r'(?:\*\s*)?([A-Za-z_]\w*)', rest):
        return None
    vars_out = []
    for d0 in split_decls(rest):
        if not d0:
            continue
        eq = -1
        depth = 0
        for k, ch in enumerate(d0):
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == '=' and depth == 0:
                eq = k
                break
        if eq >= 0:
            varpart = d0[:eq].strip()
            init = d0[eq + 1:].strip()
        else:
            varpart = d0
            init = None
        nm = re.match(r'(?:\*\s*)?([A-Za-z_]\w*)', varpart)
        if not nm:
            return None
        vars_out.append((nm.group(1), varpart[:nm.start(1)], init))
    new_header = 'for (' + ';' + ';'.join(clauses[1:]) + ')'
    return tname, vars_out, new_header


def rebuild_statement(text, start):
    st = statement_end(text, start)
    n = len(text)
    pieces = []
    last = start
    depth = 0
    i = start
    while i < st:
        c = text[i]
        if c == '"' or c == "'":
            i = skip_string(text, i, c)
            continue
        if c == '/' and i + 1 < n and text[i + 1] in '/*':
            i = skip_string(text, i, '/')
            continue
        if c == '{':
            if depth == 0 and prev_sig(text, i) not in ('=', ',', ':', '('):
                pieces.append(text[last:i])
                pieces.append(rebuild_block(text, i))
                i = match_brace(text, i)
                last = i
                continue
            depth += 1
        elif c in '([':
            depth += 1
        elif c in ')]}':
            depth -= 1
        i += 1
    pieces.append(text[last:st])
    return ''.join(pieces)


def rebuild_block(text, op):
    close = match_brace(text, op)
    out = []
    decls = []
    declared = {}
    if_stack = []
    pos = op + 1
    end = close
    while True:
        start = parse_ws(text, pos)
        if start >= end:
            break
        c = text[start]
        if c == '}':
            break
        if c == '#':
            de = directive_end(text, start)
            line = text[start:de]
            w = line.lstrip().split(None, 1)
            kw = w[0] if w else ''
            if kw in ('#if', '#ifdef', '#ifndef'):
                if_stack.append('if')
                out.append('\n' + text[start:de])
                pos = de
                continue
            if kw in ('#else', '#elif'):
                if if_stack:
                    out.append('\n' + text[start:de])
                    pos = de
                    continue
                eae = else_arm_end(text, de)
                if eae > de:
                    out.append('\n' + text[start:eae])
                    pos = eae
                    continue
                out.append('\n' + text[start:de])
                pos = de
                continue
            if kw == '#endif':
                if if_stack:
                    if_stack.pop()
                out.append('\n' + text[start:de])
                pos = de
                continue
            out.append('\n' + text[start:de])
            pos = de
            continue
        se = statement_end(text, start)
        if se <= start:
            break
        stmt = text[start:se]
        rewritten = None
        if text.startswith('for', start):
            q = parse_ws(text, start + 3)
            if q < len(text) and text[q] == '(':
                res = parse_for_header(text, q)
                if res is not None:
                    rewritten = res
        if rewritten:
            tname, vars_out, new_header = rewritten
            q0 = start + 3
            qq = parse_ws(text, q0)
            r = match_paren(text, qq)
            bopen = parse_ws(text, r)
            for (name, star, init) in vars_out:
                if name and name not in declared:
                    decls.append('    ' + tname + star + ' ' + name + ';')
                    declared[name] = True
            lines = ['\n']
            for (name, star, init) in vars_out:
                if init is not None:
                    lines.append('    ' + name + ' = ' + init + ';')
            lines.append('    ' + new_header + ' ' + rebuild_block(text, bopen))
            out.append('\n'.join(lines))
            pos = se
            continue
        if c == '{':
            out.append('\n' + rebuild_block(text, start))
        else:
            out.append('\n' + rebuild_statement(text, start))
        pos = se
    head = ''.join('\n' + d for d in decls)
    return '{' + head + ''.join(out) + '\n}'


def wrap_blocks(text, a, b):
    out = []
    i = a
    n = len(text)
    while i < b:
        c = text[i]
        if c == '"' or c == "'":
            e = skip_string(text, i, c)
            out.append(text[i:e])
            i = e
            continue
        if c == '/' and i + 1 < n and text[i + 1] in '/*':
            e = skip_string(text, i, '/')
            out.append(text[i:e])
            i = e
            continue
        if c == '{':
            ps = prev_sig(text, i)
            if ps not in ('=', ','):
                out.append(rebuild_block(text, i))
                i = match_brace(text, i)
                continue
        out.append(c)
        i += 1
    return ''.join(out)


if __name__ == '__main__':
    src = open(sys.argv[1]).read()
    sys.stdout.write(wrap_blocks(src, 0, len(src)))