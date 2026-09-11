'Check API parsing, argument lists, and browsing categories.'

import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import kapidoc

fails = []
def check(cond, what):
    if not cond:
        fails.append(what)

check(kapidoc.split_args("") == [], "empty arg list")
check(kapidoc.split_args("void") == [], "(void) means no arguments")
check(kapidoc.split_args("int a") == ["int a"], "one argument")
check(kapidoc.split_args("const char *a, int b") == ["const char *a", "int b"],
      "two plain arguments")

check(kapidoc.split_args("void (*cb)(int, int), void *ctx")
      == ["void (*cb)(int, int)", "void *ctx"], "callback commas do not split")
check(len(kapidoc.split_args(
      "const char *title, const char *ext, int dirs_only, "
      "void (*cb)(const char *path, void *ctx), void *ctx")) == 5,
      "file_picker has 5 arguments")
check(len(kapidoc.split_args(
      "void (*f)(void (*g)(int, int), int), int x")) == 2, "nested callbacks")
check(kapidoc.split_args("const char *const *items, int n")
      == ["const char *const *items", "int n"], "const char *const * survives")

m = kapidoc.parse_member("void (*klog)(const char *s);")
check(m and m["name"] == "klog", "klog name")
check(m and m["ret"] == "void", "klog return type")
check(m and m["args"] == ["const char *s"], "klog args")
check(m and m["is_fn"], "klog is a function")

m = kapidoc.parse_member("const char *(*strstr)(const char *hay, const char *needle);")
check(m and m["name"] == "strstr", "strstr name")
check(m and m["ret"] == "const char *", "pointer return type kept")

m = kapidoc.parse_member("int  (*strcasecmp)(const char *a, const char *b);")
check(m and m["name"] == "strcasecmp" and len(m["args"]) == 2, "strcasecmp")

m = kapidoc.parse_member("u8   (*palette_nearest)(u8 r, u8 g, u8 b);")
check(m and m["name"] == "palette_nearest" and len(m["args"]) == 3,
      "palette_nearest 3 args")

m = kapidoc.parse_member("void (*busy_end)(void);")
check(m and m["name"] == "busy_end" and m["args"] == [], "(void) -> no args")

m = kapidoc.parse_member("u8  *iobuf;")
check(m and m["name"] == "iobuf" and not m["is_fn"], "iobuf is data")
m = kapidoc.parse_member("u32  iobuf_size;")
check(m and m["name"] == "iobuf_size" and not m["is_fn"], "iobuf_size is data")
m = kapidoc.parse_member("FCfg *cfg;")
check(m and m["name"] == "cfg" and not m["is_fn"], "cfg is data")
m = kapidoc.parse_member("const char *os_build_date;")
check(m and m["name"] == "os_build_date" and not m["is_fn"], "os_build_date")

check(kapidoc.parse_member("} Kapi;") is None, "struct close is not a member")
check(kapidoc.parse_member("/* just a comment */") is None, "comment")
check(kapidoc.parse_member("") is None, "blank line")
check(kapidoc.parse_member("#define KAPI_VERSION 20") is None, "define")

check(kapidoc.split_declarators("const int *screen_w, *screen_h;")
      == ["const int *screen_w;", "const int *screen_h;"], "two declarators split")
check(kapidoc.split_declarators("u8 *iobuf;") == ["u8 *iobuf;"], "single declarator")

check(kapidoc.split_declarators("void (*cb)(int a, int b);")
      == ["void (*cb)(int a, int b);"], "function pointer left alone")

check(kapidoc.section_of("/* ---- v11: name resolution + HTTP ---- */")
      == "name resolution + HTTP", "banner with version tag")
check(kapidoc.section_of("    /* ---- palette ---- */") == "palette", "plain banner")

s = kapidoc.section_of("/* ---- bitmap loader: a BMP (8bpp indexed or 24bpp)")
check(s and s.startswith("bitmap loader"), "unterminated banner keeps its text")
check(kapidoc.section_of("    int x;") is None, "code is not a banner")
check(kapidoc.section_of("/* ---- ---- */") is None, "dashes only is not a section")

check(kapidoc.section_of("    /* graphics (draw only inside your app's draw handler) */")
      == "graphics", "plain one-line group header")
check(kapidoc.section_of("    /* USB hot-swap */") == "USB hot-swap", "short plain header")

check(kapidoc.section_of("    int x;  /* not a section */") is None,
      "trailing comment is not a section")

check(kapidoc.section_of("    /* Is this address mapped? Anything that dereferences") is None,
      "unterminated plain comment is a doc, not a section")

check(kapidoc.section_of("/* ---- bitmap loader: a BMP (8bpp indexed or 24bpp) decoded")
      == "bitmap loader", "long banner cut at the colon")
check(len(kapidoc.section_of(
      "/* ---- UUID (version-4 style, from the PRNG); out needs 37 bytes ---- */")) <= 40,
      "label kept short")

check(kapidoc.section_of("/* ---- FLOPFS (A:) the boot floppy ---- */") == "FLOPFS",
      "earliest separator wins")

for name, cat in (("fs_read", "filesystem"), ("fs_write", "filesystem"),
                  ("fat_read", "usb"), ("net_dns", "network"),
                  ("term_clear", "terminal"), ("win_close", "windows"),
                  ("fill_rect", "graphics"), ("draw_text", "graphics"),
                  ("blit", "graphics"), ("palette_nearest", "graphics"),
                  ("surface_lock", "graphics"),
                  ("strlcpy", "strings"), ("kfmt", "strings"),
                  ("memcpy", "memory"), ("kmalloc", "memory"),
                  ("kext_load", "extensions"), ("register_app", "extensions"),
                  ("clip_set", "clipboard"), ("msgbox", "dialogs"),
                  ("file_picker", "dialogs"), ("timer_add", "time"),
                  ("klog", "diagnostics"), ("ktrace", "diagnostics"),
                  ("mouse_x", "input"), ("kbd_mods", "input"),
                  ("speaker_tone", "audio"),

                  ("outb", "hardware"), ("pci_find", "hardware"),
                  ("irq_register", "hardware"), ("tsc_read", "hardware"),
                  ("pixel", "graphics"), ("circle", "graphics"),
                  ("font_height", "widgets"), ("cursor_hide", "widgets"),
                  ("panel", "widgets"), ("gui_dirty", "windows"),
                  ("path_base", "util"), ("ksort", "util"), ("rand", "util"),
                  ("ms_open", "memory"), ("iobuf", "memory"),
                  ("sleep_ms", "time"), ("os_build_date", "system"),
                  ("service_get", "system")):
    check(kapidoc.category_of(name) == cat,
          "%s -> %s (got %s)" % (name, cat, kapidoc.category_of(name)))

check(kapidoc.category_of("zzz_unknown_thing") == "misc", "unknown -> misc")
check(kapidoc.category_of("") == "misc", "empty -> misc")

d, rest = kapidoc.strip_doc("void (*klog)(const char *s);  /* appended */")
check(d == "appended", "trailing doc extracted")
check(rest.strip() == "void (*klog)(const char *s);", "declaration left clean")
d, rest = kapidoc.strip_doc("u32  iobuf_size;")
check(d == "", "no doc is empty, not None")

here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ents = kapidoc.parse_header(os.path.join(here, "src", "kapi.h"))
def one(nm):
    hits = [e for e in ents if e["name"] == nm]
    return hits[0] if hits else None
check(len(ents) >= 243, "found the whole Kapi (got %d)" % len(ents))
names = [e["name"] for e in ents]
for want in ("klog", "fill_rect", "fs_read", "kmalloc", "ktrace", "file_picker",
             "iobuf", "cfg", "threads", "mem_mapped",

             "screen_w", "screen_h", "mouse_x", "mouse_y"):
    check(want in names, "real header contains %s" % want)
check(len(names) == len(set(names)), "no duplicate names")

check(all(e["name"] and e["section"] for e in ents), "every entry has name+section")

misc = sum(1 for e in ents if e["section"] == "misc")
check(misc < 45, "most entries get a real category (misc=%d)" % misc)
check(all(len(e["section"]) <= 40 for e in ents), "every label is short enough")
big = max((sum(1 for e in ents if e["section"] == s), s)
          for s in set(e["section"] for e in ents))
check(big[0] <= 45, "no runaway category (%s has %d)" % (big[1], big[0]))

check(one("surface_lock")["section"] != one("net_dns")["section"],
      "unrelated calls are not in the same group")
nsect = len(set(e["section"] for e in ents))
check(nsect >= 18, "enough distinct sections to browse (got %d)" % nsect)

leak = [e["name"] for e in ents if e["doc"] and e["doc"].startswith(e["section"])
        and len(e["doc"]) > len(e["section"])]
check(not leak, "group header not reused as a member doc (%s)" % leak[:3])
fr = one("fill_rect")
check(fr and fr["section"] == "graphics", "fill_rect grouped under graphics")

fp, ms = one("file_picker"), one("menu_show")
check(fp and len(fp["args"]) == 5, "file_picker rejoined across lines")
check(ms and len(ms["args"]) == 6, "menu_show rejoined across lines")

if fails:
    print("FAIL (%d of %d):" % (len(fails), len(fails)))
    for f in fails:
        print("  -", f)
    sys.exit(1)
print("kapidoc: all checks pass")
