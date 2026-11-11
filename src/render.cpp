// 小工具：截图写出 PPM（不引第三方图片库）
#include <cstdio>

#include <SDL3/SDL.h>

bool writePPM(const char* path, int width, int height, const unsigned char* rgba) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char* p = rgba + (size_t(y) * size_t(width) + size_t(x)) * 4;
            const unsigned char rgb[3] = {p[0], p[1], p[2]};
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    return true;
}
