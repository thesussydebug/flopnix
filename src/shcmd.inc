/* Lists shell command names and usage text. */
static inline int sc_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static inline const char *sh_usage(const char *c)
{
    if(sc_eq(c,"uniq"))return "uniq [-c] file [> output] - collapse adjacent repeated lines";
    if(sc_eq(c,"strings"))return "strings [-n length] file [> output] - readable text from a binary";
    if(sc_eq(c,"crc32"))return "crc32 file [> output] - checksum a file";
    if(sc_eq(c,"cmp"))return "cmp file1 file2 [> output] - compare files byte by byte";

    if (sc_eq(c, "ls"))       return "ls [dir]      - list a directory on A: or U:";
    if (sc_eq(c, "cd"))       return "cd [dir]      - change directory (.. for up)";
    if (sc_eq(c, "tree"))     return "tree [dir]    - show the directory tree";
    if (sc_eq(c, "mkdir"))    return "mkdir <name>  - create a folder";
    if (sc_eq(c, "rmdir"))    return "rmdir <name>  - remove an empty folder";
    if (sc_eq(c, "du"))       return "du [dir]      - disk used under a folder";
    if (sc_eq(c, "map"))      return "map           - show which sectors each file owns";
    if (sc_eq(c, "cat"))      return "cat file [> output] - print or copy file contents";
    if (sc_eq(c, "rm"))       return "rm <file>     - delete a file";
    if (sc_eq(c, "cp"))       return "cp <src> <dst>- copy a file between paths";
    if (sc_eq(c, "mv"))       return "mv <src> <dst>- move a file between paths";
    if (sc_eq(c, "touch"))    return "touch <file>  - create/update a file";
    if (sc_eq(c, "echo"))     return "echo <text> [>|>> file] - print/write/append";
    if (sc_eq(c, "hexdump"))  return "hexdump <file>- hex+ascii dump";
    if (sc_eq(c, "wc"))       return "wc [-lwc] file [> output] - newline, word, byte counts";
    if (sc_eq(c, "head"))     return "head [-n count] file [> output] - first lines";
    if (sc_eq(c, "tail"))     return "tail [-n count] file [> output] - last lines";
    if (sc_eq(c, "grep"))     return "grep [-invc] pattern file [> output] - matching lines";
    if (sc_eq(c, "find"))     return "find <text>   - files whose name matches";
    if (sc_eq(c, "edit"))     return "edit <file>   - open a file for editing";
    if (sc_eq(c, "uls"))      return "uls [path]    - list USB (FAT) files";
    if (sc_eq(c, "ucat"))     return "ucat <path>   - print a USB file";
    if (sc_eq(c, "ucp"))      return "ucp <usb> <a> - copy USB file to A:";
    if (sc_eq(c, "urm"))      return "urm <path>    - delete a USB file";
    if (sc_eq(c, "ifconfig")) return "ifconfig [ip] - show or set the network interface";
    if (sc_eq(c, "open")) return "open <path>   - open a file or extension (A: or U:)";
    if (sc_eq(c, "confsec")) return "confsec [raw] - inspect current settings";
    if (sc_eq(c, "ping"))     return "ping <ip|host>- ICMP echo (resolves names)";
    if (sc_eq(c, "dns"))      return "dns <host>    - resolve a hostname to an IP";
    if (sc_eq(c, "ntp"))      return "ntp [+/-H:MM] - set the clock from the net";
    if (sc_eq(c, "pack"))     return "pack <file>   - compress to <file>.pz";
    if (sc_eq(c, "unpack"))   return "unpack <f.pz> - expand a .pz file";
    if (sc_eq(c, "wget"))     return "wget <url> [f]- fetch http (host or full URL)";
    if (sc_eq(c, "lspci"))    return "lspci         - list PCI devices";
    if (sc_eq(c, "set"))      return "set [video|mouse|net ...] - config";
    if (sc_eq(c, "uname"))    return "uname [-a]    - system name";
    if (sc_eq(c, "df"))       return "df            - disk space (A: + USB)";
    if (sc_eq(c, "cal"))      return "cal           - this month's calendar";
    if (sc_eq(c, "sort"))     return "sort [-nru] file [> output] - numeric, reverse, unique";
    if (sc_eq(c, "stat"))     return "stat <file>   - file details (A:/USB)";
    if (sc_eq(c, "history"))  return "history       - recent commands";
    if (sc_eq(c, "calc"))     return "calc <expr>   - integer math + - * / % ()";
    if (sc_eq(c, "ps"))       return "ps            - list open windows";
    if (sc_eq(c, "kill"))     return "kill <id>     - close a window (see ps)";
    if (sc_eq(c, "matrix"))   return "matrix        - enter the matrix";
    if (sc_eq(c, "rainbow"))  return "rainbow       - toggle rainbow text";
    if (sc_eq(c, "beep"))     return "beep [ms]     - PC speaker beep";
    if (sc_eq(c, "clear"))    return "clear / cls   - clear the screen";
    if (sc_eq(c, "usb"))      return "usb           - USB device info";
    if (sc_eq(c, "dmesg"))    return "dmesg         - boot diagnostics";
    if (sc_eq(c, "threads"))  return "threads       - kernel threads and stack use";
    if (sc_eq(c, "kupdate"))  return "kupdate <.ku> - install a kernel update + reboot";
    if (sc_eq(c, "crash"))    return "crash         - test the panic screen (halts!)";
    if (sc_eq(c, "kext"))     return "kext [load <file>] - list/load extensions";
    if (sc_eq(c, "defrag"))   return "defrag        - pack A:'s files contiguously";
    if (sc_eq(c, "fscan"))    return "fscan         - scan A: for bad sectors";
    if (sc_eq(c, "ring3"))    return "ring3         - run a payload at CPL 3 and report what the CPU did";
    if (sc_eq(c, "disk"))     return "disk [scan|seek|map] - drive health and surface map";
    if (sc_eq(c, "bootsec"))  return "bootsec       - hex dump of the boot sector";
    if (sc_eq(c, "bios"))     return "bios          - BIOS date/vendor/memory info";
    if (sc_eq(c, "shutdown")) return "shutdown      - power off (or safe-to-turn-off)";
    if (sc_eq(c, "testram"))  return "testram       - test RAM above 8 MB, 3 passes";

    if (sc_eq(c, "help"))     return "help [command] - list commands or explain one";
    if (sc_eq(c, "cls"))      return "cls           - clear the screen";
    if (sc_eq(c, "date"))     return "date          - current date and time";
    if (sc_eq(c, "uptime"))   return "uptime        - time since boot";
    if (sc_eq(c, "free"))     return "free          - memory used and free";
    if (sc_eq(c, "fetch"))    return "fetch         - system summary with logo";
    if (sc_eq(c, "ver"))      return "ver           - FLOPNIX version";
    if (sc_eq(c, "whoami"))   return "whoami        - the current user";
    if (sc_eq(c, "pwd"))      return "pwd           - the working directory";
    if (sc_eq(c, "settings")) return "settings      - open the Settings window";
    if (sc_eq(c, "about"))    return "about         - open the About window";
    if (sc_eq(c, "reboot"))   return "reboot        - restart FLOPNIX";
    return 0;
}

static const char *const sh_names[] = {"uniq","strings","crc32","cmp",
    "about", "beep", "bios", "bootsec", "cal", "calc", "cat", "cd", "clear",
    "cls", "cp", "crash", "date", "defrag", "disk", "dmesg", "dns", "du", "echo",
    "edit", "fetch", "find", "free", "fscan", "grep", "head", "help",
    "hexdump", "history", "ifconfig", "kext", "kill", "kupdate", "open", "confsec",
    "lspci",
    "ls", "map", "matrix", "mkdir", "mv", "ntp", "pack", "ping", "ps", "pwd",
    "rainbow",
    "reboot", "ring3", "rm", "rmdir", "set", "settings", "shutdown", "sort", "stat",
    "tail", "testram", "threads", "touch", "tree", "ucat", "ucp", "uls", "unpack",
    "uname", "uptime", "urm", "usb", "ver", "wc", "wget", "whoami"
};
#define SH_NNAMES ((int)(sizeof sh_names / sizeof sh_names[0]))
