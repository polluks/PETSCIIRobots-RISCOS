#!/usr/bin/env python3
# Hoist mid-block declarations in C99 source to the top of their enclosing block,
# converting `type name = init;` into `type name;` (at block top) + `name = init;`
# (at the original position), preserving execution order of the initializers.
# Preprocessor directives are respected: declarations in #ifdef-branched blocks are
# hoisted within their own block, macros produce verbatim copies, and matching braces
# ignores the non-compiled conditional arms.
import re
import sys

TYPE_RE = re.compile(
    r'(?:const\s+|static\s+|register\s+|volatile\s+)*'
    r'(?:unsigned\s+|signed\s+|short\s+|long\s+)*'
    r'(?:uint32_t|uint16_t|uint8_t|int32_t|int16_t|int8_t|char|float|double|bool|void|'
    r'_kernel_oserror|_kernel_swi_regs|_kernel_osfile_block|_kernel_osgbpb_block|'
    r'_kernel_HandledOrNot|_kernel_languagedescription|FILE|size_t|off_t|CursorShape|'
    r'Module|Map|Image|ModPlayerStatus_t|ModPlayer_t|int|long|short|unsigned|signed)\b')


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
    # p at '{'; return index just past the matching '}'.
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
    n = len(text)
    p = 0
    line_start = True
    while p < n:
        c = text[p]
        if c == '\n':
            line_start = True
        elif line_start and c in ' \t':
            pass
        elif line_start and c == '#':
            return True
        else:
            line_start = False
        p += 1
    return False


def else_arm_end(text, de):
    # de = past the '#else'/'#elif' directive line; return past the matching '#endif' line.
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


def split_items(text):
    items = []
    cur = []
    depth = 0
    p = 0
    n = len(text)
    while p < n:
        c = text[p]
        if c == '"' or c == "'":
            np = skip_string(text, p, c)
            cur.append(text[p:np])
            p = np
            continue
        if c == '/' and p + 1 < n and text[p + 1] in '/*':
            np = skip_string(text, p, '/')
            cur.append(text[p:np])
            p = np
            continue
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ',' and depth == 0:
            items.append(''.join(cur))
            cur = []
            p += 1
            continue
        cur.append(c)
        p += 1
    items.append(''.join(cur))
    return items


def parse_decl_stmt(text):
    t = text.strip()
    if not t.endswith(';'):
        return None
    m = TYPE_RE.match(t)
    if not m:
        return None
    tp = t[:m.end()]
    rest = t[m.end():].lstrip()
    items = split_items(rest)
    last = items[-1] if items else ''
    if last.endswith(';'):
        items[-1] = last[:-1]
    assignments = []
    for it in items:
        it0 = it.strip()
        if not it0:
            continue
        eeq = -1
        ddep = 0
        for kk, chch in enumerate(it0):
            if chch in '([{':
                ddep += 1
            elif chch in ')]}':
                ddep -= 1
            elif chch == '=' and ddep == 0:
                eeq = kk
                break
        decor = it0[:eeq] if eeq >= 0 else it0
        if '[' in decor:
            return ('verbatim', None, None)
        eidx = -1
        depth = 0
        for k, ch in enumerate(it0):
            if ch in '([{':
                depth += 1
            elif ch in ')]}':
                depth -= 1
            elif ch == '=' and depth == 0:
                eidx = k
                break
        nm = re.match(r'(?:\*\s*)*(?:\w+\s*:\s*)?([A-Za-z_]\w*)', it0)
        name = nm.group(1) if nm else None
        if not name:
            return ('verbatim', None, None)
        if eidx >= 0:
            declpart = it0[:eidx].strip()
            expr = it0[eidx + 1:].strip()
            assignments.append((name, declpart, expr, it0))
        else:
            assignments.append((name, it0.strip(), None, it0))
    return ('split', tp, assignments)


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
    # returns rebuilt text of the block starting at text[op]=='{'
    close = match_brace(text, op)
    results = []  # (kind, text, start)
    pos = op + 1
    end = close
    has_hash = False
    if_stack = []
    while True:
        start = parse_ws(text, pos)
        if start >= end:
            break
        c = text[start]
        if c == '}':
            break
        if c == '#':
            has_hash = True
            de = directive_end(text, start)
            line = text[start:de]
            w = line.lstrip().split(None, 1)
            kw = w[0] if w else ''
            if kw in ('#if', '#ifdef', '#ifndef'):
                if_stack.append('if')
            elif kw in ('#else', '#elif'):
                if not if_stack:
                    eae = else_arm_end(text, de)
                    if eae > de:
                        results.append(('dir', text[start:eae], start))
                        pos = eae
                        continue
            elif kw == '#endif':
                if if_stack:
                    if_stack.pop()
            results.append(('dir', text[start:de], start))
            pos = de
            continue
        se = statement_end(text, start)
        if se <= start:
            break
        child = text[start:se]
        pc = parse_ws(text, start)
        if pc < len(text) and text[pc] == '{':
            results.append(('stmt', rebuild_block(text, pc), start))
        else:
            kind = 'decl' if TYPE_RE.match(child.strip()) else 'stmt'
            results.append((kind, child, start))
        pos = se

    for idx in range(len(results)):
        kind, child, start = results[idx]
        if kind == 'stmt':
            if text[start] != '{':
                results[idx] = ('stmt', rebuild_statement(text, start), start)
        elif kind == 'decl':
            if not contains_directive(child):
                results[idx] = ('decl', process_range(child, 0, len(child)), start)

    if has_hash:
        prel = []
        pre_assigned = {}
        body_parts = []
        for k, child, _start in results:
            if k == 'dir':
                body_parts.append('\n' + child)
                continue
            if k == 'decl' and not contains_directive(child):
                parsed = parse_decl_stmt(child)
                if parsed:
                    mode, top, assignments = parsed
                    if mode == 'split':
                        for (name, declpart, expr, orig) in assignments:
                            if name and name not in pre_assigned:
                                pre_assigned[name] = True
                                prel.append((top + ' ' + declpart).strip() + ';')
                            if expr is not None and name:
                                body_parts.append('\n    ' + name + ' = ' + expr + ';')
                        continue
            body_parts.append('\n' + child)
        body = ''
        for d in prel:
            body += '\n    ' + d
        for part in body_parts:
            body += part
        return '{' + body + '\n}'

    decls = []
    stmts = []
    for kind, child, _start in results:
        if kind == 'dir':
            stmts.append(child)
            continue
        if kind == 'decl':
            parsed = parse_decl_stmt(child)
            if parsed:
                mode, top, assignments = parsed
                if mode == 'verbatim':
                    stmts.append(('RELOC_VERBATIM', child))
                elif mode == 'split':
                    if stmts:
                        for (name, declpart, expr, orig) in assignments:
                            decls.append((top + ' ' + declpart).strip() + ';')
                            if name and expr is not None:
                                stmts.append(name + ' = ' + expr + ';')
                    else:
                        stmts.append(child)
            else:
                stmts.append(child)
        else:
            stmts.append(child)

    mid_decls = []
    reloc_stmts = []
    for s in stmts:
        if isinstance(s, tuple) and s[0] == 'RELOC_VERBATIM':
            mid_decls.append(s[1])
        else:
            reloc_stmts.append(s)

    body = ''
    for d in mid_decls:
        body += '\n    ' + d
    for d in decls:
        body += '\n    ' + d
    for s in reloc_stmts:
        body += '\n    ' + s
    return '{' + body + '\n}'


def process_range(text, a, b):
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
    sys.stdout.write(process_range(src, 0, len(src)))