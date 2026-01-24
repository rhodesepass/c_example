#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t position;          // 当前阅读位置（UTF-8 byte offset）
    float font_px;            // 正文字号
    char font_path[512];      // 字体路径（相对/绝对）

    size_t *bookmarks;        // 书签列表（UTF-8 byte offset）
    int bookmark_count;
    int bookmark_cap;
} textreader_state_t;

// 从文本文件路径生成状态文件路径：./textstate/<basename>.state
// out_path 必须是可写缓冲区。
int textreader_state_make_path(const char *text_path, char *out_path, size_t out_path_cap);

// 初始化/销毁
void textreader_state_init(textreader_state_t *st);
void textreader_state_free(textreader_state_t *st);

// 读取/保存状态文件（不存在则返回 0 并保持默认值）
int textreader_state_load(const char *state_path, textreader_state_t *st);
int textreader_state_save(const char *state_path, const textreader_state_t *st);

// 书签管理（会去重并保持升序）
int textreader_state_add_bookmark(textreader_state_t *st, size_t position);
int textreader_state_remove_bookmark_at(textreader_state_t *st, int idx);

