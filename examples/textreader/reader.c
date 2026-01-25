#include "reader.h"
#include "log.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// reader 内部使用与 lib/fbdrawttf.c 相同的 UTF-8 解码规则（非法序列 -> U+FFFD，至少消耗 1 字节）
static int utf8_decode_1(const char *s, int *out_codepoint)
{
    const unsigned char c0 = (unsigned char)s[0];
    if(c0 == 0) { *out_codepoint = 0; return 0; }
    if(c0 < 0x80) { *out_codepoint = (int)c0; return 1; }

    if((c0 & 0xE0u) == 0xC0u) {
        const unsigned char c1 = (unsigned char)s[1];
        if((c1 & 0xC0u) != 0x80u) goto invalid;
        const int cp = ((int)(c0 & 0x1Fu) << 6) | (int)(c1 & 0x3Fu);
        if(cp < 0x80) goto invalid;
        *out_codepoint = cp;
        return 2;
    }

    if((c0 & 0xF0u) == 0xE0u) {
        const unsigned char c1 = (unsigned char)s[1];
        const unsigned char c2 = (unsigned char)s[2];
        if((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u) goto invalid;
        const int cp = ((int)(c0 & 0x0Fu) << 12) | ((int)(c1 & 0x3Fu) << 6) | (int)(c2 & 0x3Fu);
        if(cp < 0x800) goto invalid;
        if(cp >= 0xD800 && cp <= 0xDFFF) goto invalid;
        *out_codepoint = cp;
        return 3;
    }

    if((c0 & 0xF8u) == 0xF0u) {
        const unsigned char c1 = (unsigned char)s[1];
        const unsigned char c2 = (unsigned char)s[2];
        const unsigned char c3 = (unsigned char)s[3];
        if((c1 & 0xC0u) != 0x80u || (c2 & 0xC0u) != 0x80u || (c3 & 0xC0u) != 0x80u) goto invalid;
        const int cp = ((int)(c0 & 0x07u) << 18) | ((int)(c1 & 0x3Fu) << 12) | ((int)(c2 & 0x3Fu) << 6) | (int)(c3 & 0x3Fu);
        if(cp < 0x10000) goto invalid;
        if(cp > 0x10FFFF) goto invalid;
        *out_codepoint = cp;
        return 4;
    }

invalid:
    *out_codepoint = 0xFFFD;
    return 1;
}

int textreader_load_file(const char *path, char **out_buf, size_t *out_len)
{
    if(!path || !out_buf || !out_len) return -1;

    FILE *fp = fopen(path, "rb");
    if(!fp) return -1;
    if(fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return -1; }
    long sz = ftell(fp);
    if(sz < 0) { fclose(fp); return -1; }
    if(fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return -1; }

    char *buf = (char *)malloc((size_t)sz + 1);
    if(!buf) { fclose(fp); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';

    *out_buf = buf;
    *out_len = n;
    return 0;
}

static void get_line_metrics(const stbtt_fontinfo *font, float font_px,
                             int *out_ascent_px, int *out_descent_px, int *out_line_height_px,
                             float *out_scale)
{
    const float scale = stbtt_ScaleForPixelHeight((stbtt_fontinfo *)font, font_px);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics((stbtt_fontinfo *)font, &ascent, &descent, &lineGap);

    int ascent_px = (int)ceilf((float)ascent * scale);
    int descent_px = (int)ceilf((float)(-descent) * scale);
    int line_gap_px = (int)ceilf((float)lineGap * scale);
    int line_height_px = ascent_px + descent_px + line_gap_px;
    if(line_height_px <= 0) line_height_px = (int)ceilf(font_px);
    if(ascent_px <= 0) ascent_px = (int)ceilf(font_px);

    *out_ascent_px = ascent_px;
    *out_descent_px = descent_px;
    *out_line_height_px = line_height_px;
    *out_scale = scale;
}

// 分页：从 start_offset 开始，计算这一页能容纳到哪个 byte offset（返回 next_offset）。
static size_t layout_one_page(const char *text,
                              size_t len,
                              size_t start_offset,
                              const stbtt_fontinfo *font,
                              float font_px,
                              int area_w,
                              int area_h)
{
    if(start_offset >= len) return len;

    int ascent_px = 0, descent_px = 0, line_height_px = 0;
    float scale = 0.0f;
    get_line_metrics(font, font_px, &ascent_px, &descent_px, &line_height_px, &scale);

    int pen_x = 0;
    int baseline_y = ascent_px;
    const int area_x1 = area_w;
    const int area_y1 = area_h;

    if(baseline_y + descent_px > area_y1) {
        // 一行都放不下：强制至少前进 1 个 codepoint（避免死循环）
        int cp = 0;
        int n = utf8_decode_1(text + start_offset, &cp);
        if(n <= 0) return len;
        return (start_offset + (size_t)n > len) ? len : (start_offset + (size_t)n);
    }

    int prev_cp = 0;
    size_t off = start_offset;
    size_t last_good = start_offset;

    while(off < len && text[off] != '\0') {
        int cp = 0;
        int n = utf8_decode_1(text + off, &cp);
        if(n <= 0) break;

        if(cp == '\r') { off += (size_t)n; continue; }
        if(cp == '\n') {
            off += (size_t)n;
            pen_x = 0;
            baseline_y += line_height_px;
            prev_cp = 0;
            if(baseline_y + descent_px > area_y1) { last_good = off; break; }
            last_good = off;
            continue;
        }

        // kerning
        if(prev_cp != 0) {
            int kern = stbtt_GetCodepointKernAdvance((stbtt_fontinfo *)font, prev_cp, cp);
            if(kern != 0) pen_x += (int)floorf((float)kern * scale + 0.5f);
        }

        int ix0 = 0, iy0 = 0, ix1 = 0, iy1 = 0;
        stbtt_GetCodepointBitmapBox((stbtt_fontinfo *)font, cp, scale, scale, &ix0, &iy0, &ix1, &iy1);

        // 超宽换行（与 fbdraw_ttf_draw_text 对齐）：若不在行首且该字形会超出右边界，则先换行
        if(pen_x != 0 && (pen_x + ix1) > area_x1) {
            pen_x = 0;
            baseline_y += line_height_px;
            prev_cp = 0;
            if(baseline_y + descent_px > area_y1) { last_good = off; break; }
            // 换行后重新获取 box（无需 kerning）
            stbtt_GetCodepointBitmapBox((stbtt_fontinfo *)font, cp, scale, scale, &ix0, &iy0, &ix1, &iy1);
        }

        // 前进笔位置
        int advanceWidth = 0, leftSideBearing = 0;
        stbtt_GetCodepointHMetrics((stbtt_fontinfo *)font, cp, &advanceWidth, &leftSideBearing);
        pen_x += (int)floorf((float)advanceWidth * scale + 0.5f);

        off += (size_t)n;
        prev_cp = cp;
        last_good = off;
    }

    if(last_good == start_offset) {
        // 兜底：至少前进一个 codepoint
        int cp = 0;
        int n = utf8_decode_1(text + start_offset, &cp);
        if(n <= 0) return len;
        return (start_offset + (size_t)n > len) ? len : (start_offset + (size_t)n);
    }

    return last_good;
}

int textreader_paginate(const char *text,
                        size_t len,
                        const stbtt_fontinfo *font,
                        float font_px,
                        int area_w,
                        int area_h,
                        size_t **out_pages,
                        int *out_page_count)
{
    // log_info("paginate: len=%zu, font_px=%f, area_w=%d, area_h=%d", len, font_px, area_w, area_h);
    if(!text || !font || !out_pages || !out_page_count) return -1;
    if(area_w <= 0 || area_h <= 0) return -1;
    if(font_px <= 0.0f) return -1;

    size_t cap = 64;
    size_t *pages = (size_t *)malloc(cap * sizeof(size_t));
    if(!pages) return -1;
    int count = 0;

    pages[count++] = 0;
    size_t off = 0;

    while(off < len && text[off] != '\0') {
        // log_debug("paginate: off=%zu", off);
        size_t next = layout_one_page(text, len, off, font, font_px, area_w, area_h);
        if(next <= off) {
            // 防死循环
            next = off + 1;
            if(next > len) next = len;
        }
        off = next;
        if(off >= len) break;

        if((size_t)count >= cap) {
            cap *= 2;
            size_t *p = (size_t *)realloc(pages, cap * sizeof(size_t));
            if(!p) { free(pages); return -1; }
            pages = p;
        }
        pages[count++] = off;
    }

    // log_info("paginate: count=%d", count);
    *out_pages = pages;
    *out_page_count = count > 0 ? count : 1;
    return 0;
}

void textreader_render_page(fbdraw_fb_t *fb,
                            const fbdraw_rect_t *content_rect,
                            fbdraw_ttf_font_t *font,
                            const char *text,
                            size_t len,
                            size_t page_offset,
                            float font_px,
                            uint32_t color)
{
    if(!fb || !content_rect || !font || !text) return;
    if(page_offset >= len) page_offset = len;
    fbdraw_rect_t r = *content_rect;
    // 直接复用库的渲染：会在 rect 内自动换行并在高度不够时停止
    fbdraw_ttf_draw_text(fb, &r, font, (char *)(text + page_offset), font_px, (int)color);
}

