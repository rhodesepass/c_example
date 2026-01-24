#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fbdraw.h"
#include "fbdrawttf.h"
#include "stb_truetype.h"

// 读取整本书到内存（以 '\0' 结尾，len 不含 '\0'）
int textreader_load_file(const char *path, char **out_buf, size_t *out_len);

// 计算分页索引：返回 page_starts 数组（每页起始 UTF-8 byte offset），第 0 页必为 0。
// out_page_count >= 1。调用方负责 free(*out_pages)。
int textreader_paginate(const char *text,
                        size_t len,
                        const stbtt_fontinfo *font,
                        float font_px,
                        int area_w,
                        int area_h,
                        size_t **out_pages,
                        int *out_page_count);

// 渲染一页：从 page_offset 开始绘制到 content_rect 内。
void textreader_render_page(fbdraw_fb_t *fb,
                            const fbdraw_rect_t *content_rect,
                            fbdraw_ttf_font_t *font,
                            const char *text,
                            size_t len,
                            size_t page_offset,
                            float font_px,
                            uint32_t color);

