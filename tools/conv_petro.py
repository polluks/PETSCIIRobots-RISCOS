#!/usr/bin/env python3
# petrobots.cpp -> C: rename C++ member accesses, expand C++ default args at call sites.
import sys

# Call padding table: function name -> (max_total_args, defaults_to_append_for_missing_tail)
# Derived from the original C++ declarations:
#   Platform.h:  writeToScreenMemory(address,value,color=10,yOffset=0)
#   petrobots.h: DECWRITE(dst,color=10); DECOMPRESS_SCREEN(src,color=10);
#                writeToScreenMemory(addr,value,color=10,yOffset=0)
#   Platform.h:  renderFrame(waitForNextFrame=false); renderTile(...,variant=0,transparent=false);
#                renderTiles(...,backgroundVariant=0,foregroundVariant=0); fadeScreen(intensity,immediate=true)
PAD = {
    'writeToScreenMemory':   (4, ['10', '0']),
    'DECWRITE':              (2, ['10']),
    'DECOMPRESS_SCREEN':     (2, ['10']),
    'platformRenderFrame':   (1, ['false']),
    'platformRenderTile':    (5, ['0', 'false']),
    'platformRenderTiles':   (6, ['0', '0']),
    'platformFadeScreen':    (2, ['true']),
}


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


def split_args(text):
    # split top-level commas (parens tracked); strips surrounding ws
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
        elif c == ',' and depth == 0:
            parts.append(''.join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(c)
        i += 1
    parts.append(''.join(cur).strip())
    return parts


def expand_calls(text):
    out = []
    i = 0
    n = len(text)
    while i < n:
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
        # identifier scan
        if c.isalpha() or c == '_':
            j = i + 1
            while j < n and (text[j].isalnum() or text[j] == '_'):
                j += 1
            word = text[i:j]
            k = j
            sk = ''
            while k < n and text[k] in ' \t':
                sk += text[k]
                k += 1
            if k < n and text[k] == '(' and word in PAD:
                maxlen, defaults = PAD[word]
                r = k + 1
                depth = 1
                while r < n and depth:
                    ch = text[r]
                    if ch == '"' or ch == "'":
                        r = skip_string(text, r, ch)
                        continue
                    if ch == '/' and r + 1 < n and text[r + 1] in '/*':
                        r = skip_string(text, r, '/')
                        continue
                    if ch == '(':
                        depth += 1
                    elif ch == ')':
                        depth -= 1
                    r += 1
                argtext = text[k + 1:r - 1] if depth == 0 else text[k + 1:r]
                args = split_args(argtext)
                nargs = 0 if args == [''] else len(args)
                if nargs < maxlen:
                    fill = defaults[nargs - (maxlen - len(defaults)):]
                    # nargs counts from the tail: args already supplied can only be the leading ones
                    fill = defaults
                    # append only the missing trailing defaults
                    needed = maxlen - nargs
                    fill = fill[len(defaults) - needed:] if needed > 0 else []
                    sep = ', ' if nargs > 0 else ''
                    out.append(word + text[j:k] + '(' + argtext + sep + ', '.join(fill) + ')')
                    i = r
                    continue
            out.append(text[i:j] + sk)
            i = k
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def convert(src):
    s = src.replace('#include "PlatformRISCOS.h"', '#include "Platform.h"')
    s = s.replace('Platform::', '')
    # remove PlatformClass object instantiation line in main
    lines = s.split('\n')
    out = []
    for ln in lines:
        if ln.strip().startswith('PlatformClass') or ln.strip().startswith('PlatformRISCOS '):
            # drop the bare local-instance line; if also assigning, keep nothing
            if '=' in ln and ln.strip().replace(' ', '').startswith('PlatformClass') and ln.strip().endswith(';'):
                out.append('')
                continue
            if ln.strip()[:len('PlatformClass')] == 'PlatformClass' and ln.strip().endswith(';'):
                out.append('')
                continue
        if '!platform)' in ln:
            ln = ln.replace('!platform)', '!platformInit())')
        out.append(ln)
    s = '\n'.join(out)
    # member-pointer style: platform->NAME -> platformName
    res = []
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if c == '"' or c == "'":
            e = skip_string(s, i, c)
            res.append(s[i:e])
            i = e
            continue
        if c == '/' and i + 1 < n and s[i + 1] in '/*':
            e = skip_string(s, i, '/')
            res.append(s[i:e])
            i = e
            continue
        if s.startswith('platform->', i):
            j = i + len('platform->')
            k = j
            while k < n and (s[k].isalnum() or s[k] == '_'):
                k += 1
            method = s[j:k]
            cand = 'platform' + (method[0].upper() + method[1:] if method else '')
            res.append(cand)
            i = k
            continue
        res.append(c)
        i += 1
    s = ''.join(res)
    return expand_calls(s)


if __name__ == '__main__':
    src = open(sys.argv[1]).read()
    sys.stdout.write(convert(src))