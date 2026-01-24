#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include "log.h"
#include "keyinput.h"
#include "drm_warpper.h"
#include "fbdraw.h"
#include "fbdrawttf.h"

#include "reader.h"
#include "state.h"
#include "menu.h"

#define DRAW_WIDTH 360
#define DRAW_HEIGHT 640
#define STATUS_BAR_H 36

// 为什么用信号处理函数来处理Ctrl-C?
// 因为当用户在终端按下Ctrl-C时，操作系统会向前台运行的程序发送SIGINT信号。
// 通过捕获和处理这个信号（而不是让程序直接被终止），我们可以优雅地执行一些清理工作，安全释放资源，正确关闭文件或设备，保证程序平滑退出，而不是“突然中断”。
static bool g_running = true;
void signal_handler(int signal){
    log_info("signal %d received, exiting", signal);
    g_running = false;
}
void setup_signal_handler(){
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}



// 双缓冲相关数据结构
// 这四个变量会在之后的生命周期里一直使用。记得不要把他放在栈上。
// 如果你新建了一个函数，记得把这个变量写成static或者做全局变量。
static buffer_object_t buf_1,buf_2;
static drm_warpper_queue_item_t item_1,item_2;

typedef enum {
    UI_READING = 0,
    UI_MENU,
    UI_EXIT_CONFIRM,
} ui_mode_t;

static int key_edge(int key, int *last_key)
{
    if(key == -1) {
        *last_key = -1;
        return -1;
    }
    if(*last_key == -1) {
        *last_key = key;
        return key;
    }
    // 按住重复：忽略
    return -1;
}

static int find_page_by_offset(const size_t *pages, int page_count, size_t pos)
{
    if(!pages || page_count <= 0) return 0;
    int lo = 0, hi = page_count - 1, ans = 0;
    while(lo <= hi) {
        int mid = (lo + hi) / 2;
        if(pages[mid] <= pos) { ans = mid; lo = mid + 1; }
        else hi = mid - 1;
    }
    return ans;
}

static void draw_status_bar(fbdraw_fb_t *fb,
                            fbdraw_ttf_font_t *font,
                            int page_0based,
                            int page_count,
                            uint32_t fg,
                            uint32_t bg)
{
    fbdraw_rect_t r = { .x = 0, .y = DRAW_HEIGHT - STATUS_BAR_H, .w = DRAW_WIDTH, .h = STATUS_BAR_H };
    fbdraw_fill_rect(fb, &r, bg);
    char buf[64];
    snprintf(buf, sizeof(buf), "第 %d / %d 页", page_0based + 1, page_count);
    fbdraw_rect_t tr = { .x = 8, .y = DRAW_HEIGHT - STATUS_BAR_H + 6, .w = DRAW_WIDTH - 16, .h = STATUS_BAR_H - 6 };
    fbdraw_ttf_draw_text(fb, &tr, font, buf, 18.0f, (int)fg);
}

static void render_reading(fbdraw_fb_t *fb,
                           fbdraw_ttf_font_t *font,
                           const char *text,
                           size_t text_len,
                           const size_t *pages,
                           int page_count,
                           int page_idx,
                           float font_px)
{
    fbdraw_rect_t full = { .x = 0, .y = 0, .w = DRAW_WIDTH, .h = DRAW_HEIGHT };
    fbdraw_fill_rect(fb, &full, 0xff000000);
    fbdraw_rect_t content = { .x = 0, .y = 0, .w = DRAW_WIDTH, .h = DRAW_HEIGHT - STATUS_BAR_H };
    size_t off = 0;
    if(pages && page_count > 0 && page_idx >= 0 && page_idx < page_count) off = pages[page_idx];
    textreader_render_page(fb, &content, font, text, text_len, off, font_px, 0xffffffff);
    draw_status_bar(fb, font, page_idx, page_count, 0xffffffff, 0xff101010);
}

static void render_exit_confirm(fbdraw_fb_t *fb, fbdraw_ttf_font_t *font)
{
    fbdraw_rect_t full = { .x = 0, .y = 0, .w = DRAW_WIDTH, .h = DRAW_HEIGHT };
    fbdraw_fill_rect(fb, &full, 0xff000000);
    fbdraw_rect_t r = { .x = 20, .y = 220, .w = DRAW_WIDTH - 40, .h = 200 };
    fbdraw_fill_rect(fb, &r, 0xff202020);
    fbdraw_rect_t t = { .x = r.x + 10, .y = r.y + 20, .w = r.w - 20, .h = r.h - 40 };
    fbdraw_ttf_draw_text(fb, &t, font, "确认退出？\nKEY_3 确认\nKEY_4 取消", 28.0f, 0xffffffff);
}

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

// 在 ./fonts 下挑一个字体路径（优先 SourceHanSansSC-Regular.ttf）
// 返回 0 成功；-1 表示无字体
static int pick_font_from_fonts_dir(char *out_path, size_t out_cap)
{
    if(!out_path || out_cap == 0) return -1;
    out_path[0] = '\0';

    DIR *d = opendir("./fonts");
    if(!d) return -1;
    struct dirent *e;
    char first_match[768] = {0};
    char preferred[768] = {0};

    while((e = readdir(d)) != NULL) {
        if(e->d_name[0] == '.') continue;
        if(!(ends_with_casei(e->d_name, ".ttf") || ends_with_casei(e->d_name, ".otf"))) continue;
        if(first_match[0] == '\0') {
            snprintf(first_match, sizeof(first_match), "./fonts/%s", e->d_name);
        }
        if(strcmp(e->d_name, "SourceHanSansSC-Regular.ttf") == 0) {
            snprintf(preferred, sizeof(preferred), "./fonts/%s", e->d_name);
        }
    }
    closedir(d);

    const char *sel = preferred[0] ? preferred : first_match;
    if(!sel[0]) return -1;
    snprintf(out_path, out_cap, "%s", sel);
    return 0;
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        log_error("Usage: %s <text file>", argv[0]);
        return 1;
    }

    char *text = NULL;
    size_t text_len = 0;
    if(textreader_load_file(argv[1], &text, &text_len) != 0) {
        log_error("Failed to read text file: %s", argv[1]);
        return 1;
    }

    // 读取/恢复状态（同时决定字体路径）
    textreader_state_t st;
    textreader_state_init(&st);
    char state_path[1024];
    state_path[0] = '\0';
    if(textreader_state_make_path(argv[1], state_path, sizeof(state_path)) == 0) {
        (void)textreader_state_load(state_path, &st);
    }

    // 字体改为运行目录下的 ./fonts/；如果没有任何字体，直接报错退出（不开始渲染）
    char picked_font[768];
    if(st.font_path[0] == '\0' || access(st.font_path, R_OK) != 0) {
        if(pick_font_from_fonts_dir(picked_font, sizeof(picked_font)) != 0) {
            log_error("No font found. Please put .ttf/.otf into ./fonts/ then rerun.");
            textreader_state_free(&st);
            free(text);
            return 1;
        }
        strncpy(st.font_path, picked_font, sizeof(st.font_path) - 1);
        st.font_path[sizeof(st.font_path) - 1] = '\0';
    }
    // 兜底：确保当前字体可读
    if(access(st.font_path, R_OK) != 0) {
        log_error("Font not readable: %s", st.font_path);
        textreader_state_free(&st);
        free(text);
        return 1;
    }

    keyinput_init();
    setup_signal_handler();

    // 初始化drm warpper
    drm_warpper_t drm_warpper;
    drm_warpper_init(&drm_warpper);

    drm_warpper_init_layer(
        &drm_warpper, 
        1, //图层ID。取值0-3.数字大的图层覆盖在数字小的图层上。 图层0一般会被终端使用。
        DRAW_WIDTH, DRAW_HEIGHT, 
        DRM_WARPPER_LAYER_MODE_ARGB8888 // 图层模式。取值见drm_warpper_layer_mode_t枚举。FBdraw是对argb8888写的。
    );

    // 双缓冲 申请buffer
    drm_warpper_allocate_buffer(&drm_warpper, 1, &buf_1);
    drm_warpper_allocate_buffer(&drm_warpper, 1, &buf_2);

    fbdraw_fb_t fbdst;
    fbdraw_rect_t drect;

    //先清空buffer

    fbdst.vaddr = (uint32_t*)buf_1.vaddr;
    fbdst.width = DRAW_WIDTH;
    fbdst.height = DRAW_HEIGHT;
    drect.x = 0;
    drect.y = 0;
    drect.w = DRAW_WIDTH;
    drect.h = DRAW_HEIGHT;
    fbdraw_fill_rect(&fbdst, &drect, 0xff000000);
    fbdst.vaddr = (uint32_t*)buf_2.vaddr;
    fbdraw_fill_rect(&fbdst, &drect, 0xff000000);


    // 双缓冲 快速挂载请求 初始化
    item_1.mount.type = DRM_SRGN_ATOMIC_COMMIT_MOUNT_FB_NORMAL;
    item_1.mount.arg0 = (uint32_t)buf_1.vaddr;
    item_1.mount.arg1 = 0;
    item_1.mount.arg2 = 0;
    item_1.userdata = (void*)&buf_1;
    item_1.on_heap = false;
    item_2.mount.type = DRM_SRGN_ATOMIC_COMMIT_MOUNT_FB_NORMAL;
    item_2.mount.arg0 = (uint32_t)buf_2.vaddr;
    item_2.mount.arg1 = 0;
    item_2.mount.arg2 = 0;
    item_2.userdata = (void*)&buf_2;
    item_2.on_heap = false;
    // 第一次使用这个图层 需要通过这个DRM的封装先挂载一次实现modeset
    //之后就可以走快速挂载路径了
    drm_warpper_mount_layer(&drm_warpper, 1, 0, 0, &buf_1);

    // 先把两个buffer都提交一次，形成队列的初始状态（一个显示中，一个等待取回）
    drm_warpper_enqueue_display_item(&drm_warpper, 1, &item_1);
    drm_warpper_enqueue_display_item(&drm_warpper, 1, &item_2);

    // 也就是说，现在正在显示item2，也就是buf2地址内的内容。
    // buf1现在正在取回队列，下次调用drm_warpper_dequeue_free_itm的时候，会获得item1。
    
    
    // 加载字体
    log_info("load font...");
    fbdraw_ttf_font_t font;
    fbdraw_ttf_load_font(&font, st.font_path);
    log_info("font loaded");

    // 分页（正文区域）
    fbdraw_rect_t content_rect = { .x = 0, .y = 0, .w = DRAW_WIDTH, .h = DRAW_HEIGHT - STATUS_BAR_H };
    size_t *pages = NULL;
    int page_count = 0;
    if(textreader_paginate(text, text_len, &font.font, st.font_px, content_rect.w, content_rect.h, &pages, &page_count) != 0) {
        log_error("paginate failed");
        page_count = 1;
    }

    int page_idx = find_page_by_offset(pages, page_count, st.position);
    ui_mode_t ui = UI_READING;
    menu_t menu;
    menu_init(&menu);
    int last_key = -1;

    while(g_running) {
        // 获取按键
        int key = keyinput_get_key();
        int press = key_edge(key, &last_key);

        // 获取一个可以用来显示的buffer
        drm_warpper_queue_item_t* curr_item = NULL;
        drm_warpper_dequeue_free_item(&drm_warpper, 1, &curr_item);
        fbdst.vaddr = (uint32_t*)curr_item->mount.arg0;

        if(ui == UI_READING) {
            if(press == KEY_1) {
                if(page_idx > 0) page_idx--;
            } else if(press == KEY_2) {
                if(page_idx + 1 < page_count) page_idx++;
            } else if(press == KEY_3) {
                ui = UI_MENU;
                menu_open(&menu, page_count, page_idx + 1, st.font_px, st.font_path, st.bookmarks, st.bookmark_count);
            } else if(press == KEY_4) {
                ui = UI_EXIT_CONFIRM;
            }
            render_reading(&fbdst, &font, text, text_len, pages, page_count, page_idx, st.font_px);
        } else if(ui == UI_MENU) {
            if(press != -1) {
                // 用当前页起始 offset 作为书签位置
                size_t curr_pos = (pages && page_count > 0) ? pages[page_idx] : 0;
                menu_on_key(&menu, press, curr_pos);
            }

            // 处理菜单动作
            if(menu.action_close_menu) {
                ui = UI_READING;
            }
            if(menu.action_add_bookmark) {
                size_t curr_pos = (pages && page_count > 0) ? pages[page_idx] : 0;
                (void)textreader_state_add_bookmark(&st, curr_pos);
                // 刷新菜单引用
                menu.bookmarks = st.bookmarks;
                menu.bookmark_count = st.bookmark_count;
            }
            if(menu.action_delete_bookmark) {
                int idx = menu.delete_bookmark_index;
                if(idx >= 0 && idx < st.bookmark_count) {
                    (void)textreader_state_remove_bookmark_at(&st, idx);
                    // 刷新菜单引用
                    menu.bookmarks = st.bookmarks;
                    menu.bookmark_count = st.bookmark_count;
                    // 修正选择项，避免越界
                    if(st.bookmark_count <= 0) {
                        menu.selected = 0;
                    } else if(menu.selected >= st.bookmark_count) {
                        menu.selected = st.bookmark_count - 1;
                    }
                }
            }
            if(menu.action_select_bookmark) {
                int idx = menu.selected_bookmark_index;
                if(idx >= 0 && idx < st.bookmark_count) {
                    page_idx = find_page_by_offset(pages, page_count, st.bookmarks[idx]);
                    ui = UI_READING;
                }
            }
            if(menu.action_goto_page) {
                int p1 = menu.goto_page_1based;
                if(p1 < 1) p1 = 1;
                if(p1 > page_count) p1 = page_count;
                page_idx = p1 - 1;
                ui = UI_READING;
            }
            if(menu.action_font_size_changed) {
                st.font_px = menu.new_font_px;
                free(pages);
                pages = NULL;
                page_count = 0;
                if(textreader_paginate(text, text_len, &font.font, st.font_px, content_rect.w, content_rect.h, &pages, &page_count) != 0) {
                    page_count = 1;
                }
                if(page_idx >= page_count) page_idx = page_count - 1;
                // 更新菜单显示
                menu.current_font_px = st.font_px;
                menu.total_pages = page_count;
            }
            if(menu.action_font_changed) {
                // 切换字体并重新分页
                strncpy(st.font_path, menu.new_font_path, sizeof(st.font_path) - 1);
                st.font_path[sizeof(st.font_path) - 1] = '\0';
                fbdraw_ttf_free_font(&font);
                fbdraw_ttf_load_font(&font, st.font_path);
                free(pages);
                pages = NULL;
                page_count = 0;
                if(textreader_paginate(text, text_len, &font.font, st.font_px, content_rect.w, content_rect.h, &pages, &page_count) != 0) {
                    page_count = 1;
                }
                if(page_idx >= page_count) page_idx = page_count - 1;
                // 更新菜单显示
                menu.current_font_path = st.font_path;
                menu.total_pages = page_count;
            }

            fbdraw_rect_t menu_rect = { .x = 0, .y = 0, .w = DRAW_WIDTH, .h = DRAW_HEIGHT };
            menu.current_page_1based = page_idx + 1;
            menu.current_font_px = st.font_px;
            menu.current_font_path = st.font_path;
            menu.bookmarks = st.bookmarks;
            menu.bookmark_count = st.bookmark_count;
            menu.total_pages = page_count;
            menu_render(&menu, &fbdst, &menu_rect, &font, 0xffffffff, 0xff000000);

            // 处理完所有动作后清空标志位，防止重复触发
            menu_clear_actions(&menu);
        } else if(ui == UI_EXIT_CONFIRM) {
            if(press == KEY_3) {
                // 保存状态并退出
                if(pages && page_count > 0) st.position = pages[page_idx];
                if(state_path[0]) (void)textreader_state_save(state_path, &st);
                g_running = false;
            } else if(press == KEY_4) {
                ui = UI_READING;
            }
            render_exit_confirm(&fbdst, &font);
        }

        // 提交buffer到显示队列
        drm_warpper_enqueue_display_item(&drm_warpper, 1, curr_item);

        usleep(16000); // 限制刷新频率，减少按住重复与 CPU 占用
    }

    log_info("Exiting...");

    // 收尾及清理工作。
    drm_warpper_destroy_layer(&drm_warpper, 1);
    drm_warpper_free_buffer(&drm_warpper, 1, &buf_1);
    drm_warpper_free_buffer(&drm_warpper, 1, &buf_2);
    drm_warpper_destroy(&drm_warpper);
    fbdraw_ttf_free_font(&font);
    if(pages) free(pages);
    if(pages && page_count > 0) st.position = pages[page_idx];
    if(state_path[0]) (void)textreader_state_save(state_path, &st);
    menu_free(&menu);
    textreader_state_free(&st);
    free(text);

    return 0;
}  