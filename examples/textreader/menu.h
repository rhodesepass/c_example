#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "fbdraw.h"
#include "fbdrawttf.h"

typedef enum {
    MENU_NONE = 0,
    MENU_ROOT,
    MENU_EDIT_FONT_SIZE,
    MENU_EDIT_GOTO_PAGE,
    MENU_BOOKMARKS,
    MENU_BOOKMARKS_DELETE,
    MENU_FONTS,
} menu_mode_t;

typedef struct {
    // 菜单状态
    menu_mode_t mode;
    int selected;

    // 外部注入的数据
    int total_pages;
    int current_page_1based;
    float current_font_px;
    const char *current_font_path;
    const size_t *bookmarks;
    int bookmark_count;

    // 字体列表
    char **font_list;
    int font_count;
    int font_cap;

    // 数字编辑器（跳页/字号）
    bool editor_active;
    int editor_value;
    int editor_min;
    int editor_max;
    int editor_digits;   // 位数
    int editor_cursor;   // 当前编辑位 0..digits-1（从高位到低位）

    // 输出动作（由 main 读取并清零）
    bool action_close_menu;
    bool action_font_size_changed;
    float new_font_px;
    bool action_goto_page;
    int goto_page_1based;
    bool action_add_bookmark;
    bool action_select_bookmark;
    int selected_bookmark_index;
    bool action_delete_bookmark;
    int delete_bookmark_index;
    bool action_font_changed;
    char new_font_path[512];
} menu_t;

void menu_init(menu_t *m);
void menu_free(menu_t *m);
void menu_clear_actions(menu_t *m);

void menu_open(menu_t *m,
               int total_pages,
               int current_page_1based,
               float current_font_px,
               const char *current_font_path,
               const size_t *bookmarks,
               int bookmark_count);

// 传入 keycode（KEY_1/KEY_2/KEY_3/KEY_4）
void menu_on_key(menu_t *m, int keycode, size_t current_position_for_bookmark);

void menu_render(menu_t *m,
                 fbdraw_fb_t *fb,
                 const fbdraw_rect_t *rect,
                 fbdraw_ttf_font_t *font,
                 uint32_t fg,
                 uint32_t bg);

