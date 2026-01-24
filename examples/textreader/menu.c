#include "menu.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>

static const char *k_fonts_dir = "./fonts";

static int ends_with_casei(const char *s, const char *suffix)
{
    size_t sl = strlen(s), su = strlen(suffix);
    if(sl < su) return 0;
    const char *p = s + (sl - su);
    for(size_t i = 0; i < su; i++){
        char a = p[i], b = suffix[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if(b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if(a != b) return 0;
    }
    return 1;
}

void menu_clear_actions(menu_t *m)
{
    m->action_close_menu = false;
    m->action_font_size_changed = false;
    m->action_goto_page = false;
    m->action_add_bookmark = false;
    m->action_select_bookmark = false;
    m->action_delete_bookmark = false;
    m->action_font_changed = false;
}

static void free_font_list(menu_t *m)
{
    for(int i = 0; i < m->font_count; i++){
        free(m->font_list[i]);
    }
    free(m->font_list);
    m->font_list = NULL;
    m->font_count = 0;
    m->font_cap = 0;
}

static int ensure_font_cap(menu_t *m, int need)
{
    if(need <= m->font_cap) return 0;
    int new_cap = m->font_cap ? m->font_cap : 16;
    while(new_cap < need) new_cap *= 2;
    char **p = (char **)realloc(m->font_list, (size_t)new_cap * sizeof(char *));
    if(!p) return -1;
    m->font_list = p;
    m->font_cap = new_cap;
    return 0;
}

static void scan_fonts(menu_t *m)
{
    free_font_list(m);

    DIR *d = opendir(k_fonts_dir);
    if(!d) return;
    struct dirent *e;
    while((e = readdir(d)) != NULL) {
        if(e->d_name[0] == '.') continue;
        if(!(ends_with_casei(e->d_name, ".ttf") || ends_with_casei(e->d_name, ".otf"))) continue;
        if(ensure_font_cap(m, m->font_count + 1) != 0) break;
        char full[768];
        snprintf(full, sizeof(full), "%s/%s", k_fonts_dir, e->d_name);
        m->font_list[m->font_count] = strdup(full);
        if(!m->font_list[m->font_count]) break;
        m->font_count++;
    }
    closedir(d);
}

void menu_init(menu_t *m)
{
    memset(m, 0, sizeof(*m));
    m->mode = MENU_NONE;
    m->selected = 0;
    m->new_font_px = 32.0f;
}

void menu_free(menu_t *m)
{
    if(!m) return;
    free_font_list(m);
}

void menu_open(menu_t *m,
               int total_pages,
               int current_page_1based,
               float current_font_px,
               const char *current_font_path,
               const size_t *bookmarks,
               int bookmark_count)
{
    menu_clear_actions(m);
    m->mode = MENU_ROOT;
    m->selected = 0;
    m->total_pages = total_pages;
    m->current_page_1based = current_page_1based;
    m->current_font_px = current_font_px;
    m->current_font_path = current_font_path;
    m->bookmarks = bookmarks;
    m->bookmark_count = bookmark_count;

    m->editor_active = false;
    m->editor_value = 0;
    m->editor_min = 0;
    m->editor_max = 0;
    m->editor_digits = 0;
    m->editor_cursor = 0;

    scan_fonts(m);
}

static void editor_start(menu_t *m, int value, int minv, int maxv, int digits)
{
    m->editor_active = true;
    m->editor_value = value;
    m->editor_min = minv;
    m->editor_max = maxv;
    m->editor_digits = digits;
    m->editor_cursor = 0; // 从最高位开始
}

static int clamp_int(int v, int lo, int hi)
{
    if(v < lo) return lo;
    if(v > hi) return hi;
    return v;
}

static int pow10_int(int n)
{
    int p = 1;
    for(int i = 0; i < n; i++) p *= 10;
    return p;
}

static void editor_inc_digit(menu_t *m, int delta)
{
    int v = clamp_int(m->editor_value, m->editor_min, m->editor_max);
    int pos_from_left = m->editor_cursor;
    int pos_from_right = (m->editor_digits - 1) - pos_from_left;
    int place = pow10_int(pos_from_right);
    int digit = (v / place) % 10;
    digit = (digit + delta) % 10;
    if(digit < 0) digit += 10;
    v = v - ((v / place) % 10) * place + digit * place;
    v = clamp_int(v, m->editor_min, m->editor_max);
    m->editor_value = v;
}

void menu_on_key(menu_t *m, int keycode, size_t current_position_for_bookmark)
{
    if(!m) return;
    menu_clear_actions(m);

    if(m->mode == MENU_NONE) return;

    // 数字编辑模式
    if(m->editor_active) {
        if(keycode == KEY_1) {
            editor_inc_digit(m, -1);
        } else if(keycode == KEY_2) {
            editor_inc_digit(m, +1);
        } else if(keycode == KEY_3) {
            if(m->editor_cursor + 1 < m->editor_digits) {
                m->editor_cursor++;
            } else {
                // confirm
                m->editor_value = clamp_int(m->editor_value, m->editor_min, m->editor_max);
                if(m->mode == MENU_EDIT_FONT_SIZE) {
                    m->new_font_px = (float)m->editor_value;
                    m->action_font_size_changed = true;
                } else if(m->mode == MENU_EDIT_GOTO_PAGE) {
                    m->goto_page_1based = m->editor_value;
                    m->action_goto_page = true;
                }
                m->editor_active = false;
                m->mode = MENU_ROOT;
            }
        } else if(keycode == KEY_4) {
            // cancel
            m->editor_active = false;
            m->mode = MENU_ROOT;
        }
        return;
    }

    if(m->mode == MENU_ROOT) {
        // 0: 字号 1: 跳页 2: 添加书签 3: 书签列表 4: 删除书签 5: 字体
        const int item_count = 6;
        if(keycode == KEY_1) {
            m->selected = (m->selected - 1 + item_count) % item_count;
        } else if(keycode == KEY_2) {
            m->selected = (m->selected + 1) % item_count;
        } else if(keycode == KEY_3) {
            if(m->selected == 0) {
                m->mode = MENU_EDIT_FONT_SIZE;
                editor_start(m, (int)(m->current_font_px + 0.5f), 12, 96, 2);
            } else if(m->selected == 1) {
                m->mode = MENU_EDIT_GOTO_PAGE;
                editor_start(m, m->current_page_1based, 1, (m->total_pages > 0 ? m->total_pages : 1), 3);
            } else if(m->selected == 2) {
                m->action_add_bookmark = true;
                (void)current_position_for_bookmark;
            } else if(m->selected == 3) {
                m->mode = MENU_BOOKMARKS;
                m->selected = 0;
            } else if(m->selected == 4) {
                m->mode = MENU_BOOKMARKS_DELETE;
                m->selected = 0;
            } else if(m->selected == 5) {
                m->mode = MENU_FONTS;
                m->selected = 0;
            }
        } else if(keycode == KEY_4) {
            m->action_close_menu = true;
            m->mode = MENU_NONE;
        }
        return;
    }

    if(m->mode == MENU_BOOKMARKS) {
        const int cnt = m->bookmark_count;
        if(keycode == KEY_1) {
            if(cnt > 0) m->selected = (m->selected - 1 + cnt) % cnt;
        } else if(keycode == KEY_2) {
            if(cnt > 0) m->selected = (m->selected + 1) % cnt;
        } else if(keycode == KEY_3) {
            if(cnt > 0) {
                m->selected_bookmark_index = m->selected;
                m->action_select_bookmark = true;
            }
        } else if(keycode == KEY_4) {
            m->mode = MENU_ROOT;
            m->selected = 0;
        }
        return;
    }

    if(m->mode == MENU_BOOKMARKS_DELETE) {
        const int cnt = m->bookmark_count;
        if(keycode == KEY_1) {
            if(cnt > 0) m->selected = (m->selected - 1 + cnt) % cnt;
        } else if(keycode == KEY_2) {
            if(cnt > 0) m->selected = (m->selected + 1) % cnt;
        } else if(keycode == KEY_3) {
            if(cnt > 0) {
                m->delete_bookmark_index = m->selected;
                m->action_delete_bookmark = true;
            }
        } else if(keycode == KEY_4) {
            m->mode = MENU_ROOT;
            m->selected = 0;
        }
        return;
    }

    if(m->mode == MENU_FONTS) {
        const int cnt = m->font_count;
        if(keycode == KEY_1) {
            if(cnt > 0) m->selected = (m->selected - 1 + cnt) % cnt;
        } else if(keycode == KEY_2) {
            if(cnt > 0) m->selected = (m->selected + 1) % cnt;
        } else if(keycode == KEY_3) {
            if(cnt > 0 && m->font_list[m->selected]) {
                strncpy(m->new_font_path, m->font_list[m->selected], sizeof(m->new_font_path) - 1);
                m->new_font_path[sizeof(m->new_font_path) - 1] = '\0';
                m->action_font_changed = true;
                m->mode = MENU_ROOT;
                m->selected = 0;
            }
        } else if(keycode == KEY_4) {
            m->mode = MENU_ROOT;
            m->selected = 0;
        }
        return;
    }
}

static void draw_line(fbdraw_fb_t *fb, const fbdraw_rect_t *rect, fbdraw_ttf_font_t *font,
                      int x, int y, const char *s, float px, uint32_t fg)
{
    fbdraw_rect_t r = { .x = rect->x + x, .y = rect->y + y, .w = rect->w - x, .h = rect->h - y };
    fbdraw_ttf_draw_text(fb, &r, font, (char *)s, px, (int)fg);
}

void menu_render(menu_t *m,
                 fbdraw_fb_t *fb,
                 const fbdraw_rect_t *rect,
                 fbdraw_ttf_font_t *font,
                 uint32_t fg,
                 uint32_t bg)
{
    if(!m || !fb || !rect || !font) return;
    fbdraw_rect_t rr = *rect;
    fbdraw_fill_rect(fb, &rr, bg);

    const float title_px = 48.0f;
    const float item_px = 32.0f;

    if(m->mode == MENU_ROOT) {
        draw_line(fb, rect, font, 8, 6, "菜单", title_px, fg);

        char buf[256];
        snprintf(buf, sizeof(buf), "%s 字号: %.0f", (m->selected == 0 ? ">" : " "), m->current_font_px);
        draw_line(fb, rect, font, 8, 80, buf, item_px, fg);
        snprintf(buf, sizeof(buf), "%s 跳转到页", (m->selected == 1 ? ">" : " "));
        draw_line(fb, rect, font, 8, 110, buf, item_px, fg);
        snprintf(buf, sizeof(buf), "%s 添加书签", (m->selected == 2 ? ">" : " "));
        draw_line(fb, rect, font, 8, 140, buf, item_px, fg);
        snprintf(buf, sizeof(buf), "%s 书签列表 (%d)", (m->selected == 3 ? ">" : " "), m->bookmark_count);
        draw_line(fb, rect, font, 8, 170, buf, item_px, fg);
        snprintf(buf, sizeof(buf), "%s 删除书签 (%d)", (m->selected == 4 ? ">" : " "), m->bookmark_count);
        draw_line(fb, rect, font, 8, 200, buf, item_px, fg);
        snprintf(buf, sizeof(buf), "%s 字体", (m->selected == 5 ? ">" : " "));
        draw_line(fb, rect, font, 8, 230, buf, item_px, fg);

        draw_line(fb, rect, font, 8, rect->h - 28, "1/2选择 3进入 4返回", 24.0f, fg);
        return;
    }

    if(m->editor_active) {
        const char *title = (m->mode == MENU_EDIT_FONT_SIZE) ? "设置字号" : "跳转到页";
        draw_line(fb, rect, font, 8, 6, title, title_px, fg);

        char buf[128];
        snprintf(buf, sizeof(buf), "当前值: %d", m->editor_value);
        draw_line(fb, rect, font, 8, 60, buf, 32.0f, fg);

        // 显示光标位
        char digits[16];
        snprintf(digits, sizeof(digits), "%0*d", m->editor_digits, m->editor_value);
        char show[64];
        snprintf(show, sizeof(show), "[%s]  位:%d", digits, m->editor_cursor + 1);
        draw_line(fb, rect, font, 8, 120, show, 32.0f, fg);

        draw_line(fb, rect, font, 8, rect->h - 56, "1减 2加 3下一位/确认 4取消", 24.0f, fg);
        return;
    }

    if(m->mode == MENU_BOOKMARKS) {
        draw_line(fb, rect, font, 8, 6, "书签列表", title_px, fg);
        if(m->bookmark_count <= 0) {
            draw_line(fb, rect, font, 8, 60, "（暂无书签）", item_px, fg);
        } else {
            int y = 52;
            for(int i = 0; i < m->bookmark_count && i < 10; i++){
                char buf[128];
                snprintf(buf, sizeof(buf), "%s #%d  offset=%zu", (i == m->selected ? ">" : " "), i + 1, m->bookmarks[i]);
                draw_line(fb, rect, font, 8, y, buf, 18.0f, fg);
                y += 22;
            }
        }
        draw_line(fb, rect, font, 8, rect->h - 28, "1/2选择 3跳转 4返回", 24.0f, fg);
        return;
    }

    if(m->mode == MENU_BOOKMARKS_DELETE) {
        draw_line(fb, rect, font, 8, 6, "删除书签", title_px, fg);
        if(m->bookmark_count <= 0) {
            draw_line(fb, rect, font, 8, 60, "（暂无书签）", item_px, fg);
        } else {
            int y = 120;
            for(int i = 0; i < m->bookmark_count && i < 10; i++){
                char buf[128];
                snprintf(buf, sizeof(buf), "%s #%d  offset=%zu", (i == m->selected ? ">" : " "), i + 1, m->bookmarks[i]);
                draw_line(fb, rect, font, 8, y, buf, 32.0f, fg);
                y += 40;
            }
        }
        draw_line(fb, rect, font, 8, rect->h - 28, "1/2选择 3删除 4返回", 24.0f, fg);
        return;
    }

    if(m->mode == MENU_FONTS) {
        draw_line(fb, rect, font, 8, 6, "选择字体", title_px, fg);
        if(m->font_count <= 0) {
            draw_line(fb, rect, font, 8, 60, "（未发现字体文件）", item_px, fg);
        } else {
            int y = 120;
            for(int i = 0; i < m->font_count && i < 12; i++){
                char buf[256];
                const char *name = m->font_list[i];
                if(strncmp(name, k_fonts_dir, strlen(k_fonts_dir)) == 0) {
                    const char *p = name + strlen(k_fonts_dir);
                    if(*p == '/') p++;
                    name = p;
                }
                snprintf(buf, sizeof(buf), "%s %s", (i == m->selected ? ">" : " "), name);
                draw_line(fb, rect, font, 8, y, buf, 32.0f, fg);
                y += 40;
            }
        }
        draw_line(fb, rect, font, 8, rect->h - 28, "1/2选择 3确认 4返回", 24.0f, fg);
        return;
    }
}

