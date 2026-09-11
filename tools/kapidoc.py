#!/usr/bin/env python3
'Generate API Explorer entries from src/kapi.h.'
import re, sys

FN_RE   = re.compile(r'^(.*?)\(\s*\*\s*([A-Za-z_]\w*)\s*\)\s*\((.*)\)\s*;\s*$', re.S)
DATA_RE = re.compile(r'^([A-Za-z_][\w\s\*]*?[\s\*])([A-Za-z_]\w*)\s*(\[\s*\w*\s*\])?\s*;\s*$')

SECT_RE = re.compile(r'/\*+\s*-+\s*(.*?)\s*-+')

def split_args(s):
    'Split arguments at commas outside nested parentheses.'
    s = s.strip()
    if not s or s == "void":
        return []
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out

def split_declarators(decl):
    "Split shared declarations while keeping each member's type."
    d = decl.strip()
    if '(' in d or not d.endswith(';'):
        return [d]
    parts = split_args(d[:-1])
    if len(parts) < 2:
        return [d]

    first = parts[0]
    i = len(first)
    while i > 0 and (first[i - 1].isalnum() or first[i - 1] == '_'):
        i -= 1
    typ = first[:i].rstrip('*').rstrip()
    out = [first + ';']
    for p in parts[1:]:
        out.append(("%s %s;" % (typ, p)) if not p.startswith(typ) else p + ';')
    return out

SECT_MAX = 40

CATS = [
    ("filesystem",  ("fs_", "disk_", "flopfs")),
    ("usb",         ("fat_", "usb")),
    ("network",     ("net_", "lspci")),
    ("terminal",    ("term_", "shell_", "register_shell", "cmd_")),
    ("windows",     ("win_", "app_", "gui_")),
    ("graphics",    ("draw_", "fill_", "hline", "vline", "blit", "pixel",
                     "getpixel", "line", "rect", "circle", "palette_",
                     "set_clip", "clear_clip", "clip_rect_get", "read_rect",
                     "present", "set_dither", "surface_", "screen_", "bmp_")),
    ("widgets",     ("panel", "bevel", "sbar_", "focus_rect", "text_",
                     "font_", "cursor_", "busy_", "anim_")),
    ("strings",     ("str", "kfmt", "atoi", "human_", "b64_", "crc32",
                     "tolower", "toupper", "uuid", "date_", "time_")),
    ("util",        ("path_", "hash_", "ksort", "rand", "dos_fmt")),
    ("memory",      ("mem", "kmalloc", "kfree", "heap", "ms_", "iobuf")),
    ("hardware",    ("outb", "inb", "outw", "inw", "outl", "inl", "insw",
                     "outsw", "pci_", "irq_", "cpuid", "tsc_")),
    ("system",      ("version", "os_version", "os_build_date", "service_get",
                     "ext_type", "open_with")),
    ("threads",     ("thr", "preempt", "mtx", "threads")),
    ("extensions",  ("kext_", "kupdate", "kernel_update", "register_",
                     "on_event", "off_event", "broadcast")),
    ("clipboard",   ("clip_",)),
    ("dialogs",     ("msgbox", "file_picker", "file_save", "progress_",
                     "notify", "menu_", "set_overlay")),
    ("time",        ("timer_", "ticks", "uptime", "rtc", "cpu_now",
                     "cpu_mhz", "cpu_brand", "cpu_usage", "sleep_ms")),
    ("diagnostics", ("klog", "ktrace", "dmesg", "boot_", "fault", "panic",
                     "irq_count", "plat")),
    ("input",       ("mouse_", "kbd_", "drag_", "dclick", "key_")),
    ("audio",       ("speaker", "snd_", "beep", "midi")),
    ("config",      ("cfg", "config_")),
    ("power",       ("reboot", "shutdown")),
]

def category_of(name):
    'Choose a browsing category from the member name.'
    if not name:
        return "misc"
    best, blen = "misc", 0
    for cat, prefixes in CATS:
        for p in prefixes:
            if name.startswith(p) and len(p) > blen:
                best, blen = cat, len(p)
    return best

def section_of(line):
    'Read a group label from a standalone header comment.'
    s = line.strip()
    if not s.startswith('/*'):
        return None
    banner = re.match(r'/\*+\s*-{3,}\s*(.+)$', s)
    if banner:
        t = banner.group(1)
    else:
        if not s.endswith('*/'):
            return None
        t = s[2:-2]
    t = t.split('*/')[0].strip().strip('-').strip()
    t = re.sub(r'^v\d+:\s*', '', t)
    if not t:
        return None

    cut = min((i for i in (t.find(s) for s in (':', '(', ';', ' - '))
               if 2 <= i <= 32), default=-1)
    if cut > 0:
        t = t[:cut].strip()
    t = " ".join(t.split())
    if len(t) > SECT_MAX:
        t = t[:SECT_MAX].rstrip()

    if not banner and (len(t) > SECT_MAX or t.endswith('.')):
        return None
    return t or None

def strip_doc(line):
    'Separate a trailing comment from its declaration.'
    i = line.find('/*')
    if i < 0:
        return ("", line)
    body = line[i + 2:]
    j = body.find('*/')
    if j >= 0:
        body = body[:j]
    doc = " ".join(body.replace('*', ' ').split())
    return (doc, line[:i])

def parse_member(decl):
    'Parse one member declaration, or return None.'
    d = decl.strip()
    if not d or not d.endswith(';') or d.startswith('#') or d.startswith('}'):
        return None
    if d.startswith('/*') or d.startswith('*'):
        return None
    m = FN_RE.match(d)
    if m:
        ret = " ".join(m.group(1).split())
        return {"name": m.group(2), "ret": ret, "args": split_args(m.group(3)),
                "is_fn": True, "doc": "", "section": ""}
    m = DATA_RE.match(d)
    if m:
        typ = " ".join((m.group(1) + (m.group(3) or "")).split())
        if typ in ("return", "typedef", "struct", "enum"):
            return None
        return {"name": m.group(2), "ret": typ, "args": [],
                "is_fn": False, "doc": "", "section": ""}
    return None

def parse_header(path):
    'Read Kapi members in declaration order.'
    lines = open(path, encoding="utf-8", errors="replace").read().split('\n')

    end = next(i for i, l in enumerate(lines) if l.startswith('} Kapi;'))
    start = max(i for i, l in enumerate(lines[:end])
                if l.startswith('typedef struct {'))

    ents, section, pending, buf = [], "misc", "", ""
    in_block = block_keep = False
    for raw in lines[start + 1:end]:

        if in_block:
            if block_keep:
                pending += " " + raw.replace('*/', ' ').replace('*', ' ').strip()
            if '*/' in raw:
                in_block = False
            continue
        if not buf:
            s = section_of(raw)
            if s:
                section = s
                pending = ""
                if raw.lstrip().startswith('/*') and '*/' not in raw:
                    in_block, block_keep = True, False
                continue
        if raw.lstrip().startswith('/*') and '*/' not in raw and not buf:
            in_block, block_keep = True, True
            pending = ""
            continue

        doc, code = strip_doc(raw)
        code = code.strip()
        if not code:
            if doc and not buf:
                pending = doc
            continue
        buf = (buf + " " + code).strip() if buf else code
        if not buf.endswith(';'):
            continue
        decls = split_declarators(buf)
        buf = ""
        hit = 0
        for d in decls:
            m = parse_member(d)
            if not m:
                continue
            m["doc"] = doc or pending

            m["section"] = category_of(m["name"])
            ents.append(m)
            hit += 1
        pending = ""

    seen, uniq = set(), []
    for e in ents:
        if e["name"] in seen:
            continue
        seen.add(e["name"])
        uniq.append(e)
    return uniq

def c_str(s, cap=0):
    s = s.replace('\\', '\\\\').replace('"', '\\"')
    if cap and len(s) > cap:
        s = s[:cap - 1] + "\xe2\x80\xa6" if False else s[:cap]
    return '"%s"' % s

def emit(ents, out):
    'Write one API Explorer row for each member.'
    w = open(out, "w", encoding="utf-8", newline="\n")
    w.write("/* API Explorer data generated by tools/kapidoc.py. */\n"
            "#pragma once\n\n")
    w.write("typedef struct {\n"
            "    const char *name, *ret, *args, *doc, *sect;\n"
            "    unsigned char is_fn, nargs;\n"
            "} ApiEnt;\n\n")
    w.write("static const ApiEnt api_tab[] = {\n")
    for e in ents:
        args = ", ".join(e["args"])
        w.write("    { %s, %s, %s, %s, %s, %d, %d },\n" % (
            c_str(e["name"]), c_str(e["ret"]), c_str(args),
            c_str(e["doc"]), c_str(e["section"]),
            1 if e["is_fn"] else 0, len(e["args"])))
    w.write("};\n\n")
    w.write("#define API_NENT ((int)(sizeof api_tab / sizeof api_tab[0]))\n")
    w.close()

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else "src/kapi.h"
    dst = sys.argv[2] if len(sys.argv) > 2 else "kexts/apidoc_data.inc"
    ents = parse_header(src)

    if len(ents) < 240:
        sys.exit("kapidoc: only parsed %d entries from %s - refusing to write a "
                 "truncated table (parser regression?)" % (len(ents), src))
    emit(ents, dst)
    print("kapidoc: %d entries -> %s" % (len(ents), dst))
