/* Maps canvas coordinates and transforms image pixels. */
#pragma once
static int pv_limit(int size, int pixels, int zoom, int offset)
{
    int visible = (pixels + zoom - 1) / zoom;
    int max = size - visible;
    if (max < 0) max = 0;
    return offset < 0 ? 0 : offset > max ? max : offset;
}
static int pv_point(int pixel, int zoom, int offset, int size)
{
    int p = pixel < 0 ? offset : pixel / zoom + offset;
    return p >= size ? size - 1 : p;
}
static void pv_flip(u8 *pixels, int w, int h, int vertical)
{
    for (int y = 0; y < (vertical ? h / 2 : h); y++)
        for (int x = 0; x < (vertical ? w : w / 2); x++) {
            int a = y * w + x;
            int b = vertical ? (h - 1 - y) * w + x : y * w + w - 1 - x;
            u8 t = pixels[a]; pixels[a] = pixels[b]; pixels[b] = t;
        }
}
