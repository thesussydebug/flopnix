typedef struct { const char *label; u32 addr; } MePreset;

static inline int me_build_presets(u32 kern, u32 dma, u32 fb, u32 arena,
                                   u32 heap, u32 pool, u32 grow, MePreset *out, int cap)
{
    const MePreset all[8] = {
        { "Zero",  0     },
        { "Kern",  kern  },
        { "DMA",   dma   },
        { "Video", fb    },
        { "Kexts", arena },
        { "Heap",  heap  },
        { "Pool",  pool  },
        { "Grow",  grow  },
    };
    int n = 0;
    for (int i = 0; i < 8 && n < cap; i++) {
        if (i && !all[i].addr) continue;
        int dup = 0;
        for (int j = 0; j < n; j++)
            if (out[j].addr == all[i].addr) dup = 1;
        if (dup) continue;

        int at = n;
        while (at > 0 && out[at - 1].addr > all[i].addr) {
            out[at] = out[at - 1];
            at--;
        }
        out[at] = all[i];
        n++;
    }
    return n;
}
