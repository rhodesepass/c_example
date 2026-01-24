#include "state.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int ensure_textstate_dir(void)
{
    struct stat st;
    if(stat("./textstate", &st) == 0) {
        if(S_ISDIR(st.st_mode)) return 0;
        return -1;
    }
    if(mkdir("./textstate", 0755) == 0) return 0;
    if(errno == EEXIST) return 0;
    return -1;
}

static const char *path_basename(const char *path)
{
    if(!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? (slash + 1) : path;
}

int textreader_state_make_path(const char *text_path, char *out_path, size_t out_path_cap)
{
    if(!text_path || !out_path || out_path_cap == 0) return -1;
    const char *base = path_basename(text_path);
    // 使用“文件名（含扩展名）”作为 key，避免同名不同扩展冲突
    const int n = snprintf(out_path, out_path_cap, "./textstate/%s.state", base);
    if(n < 0 || (size_t)n >= out_path_cap) return -1;
    return 0;
}

void textreader_state_init(textreader_state_t *st)
{
    if(!st) return;
    st->position = 0;
    st->font_px = 32.0f;
    st->font_path[0] = '\0';
    st->bookmarks = NULL;
    st->bookmark_count = 0;
    st->bookmark_cap = 0;
}

void textreader_state_free(textreader_state_t *st)
{
    if(!st) return;
    free(st->bookmarks);
    st->bookmarks = NULL;
    st->bookmark_count = 0;
    st->bookmark_cap = 0;
}

static int cmp_size_t(const void *a, const void *b)
{
    const size_t aa = *(const size_t *)a;
    const size_t bb = *(const size_t *)b;
    if(aa < bb) return -1;
    if(aa > bb) return 1;
    return 0;
}

static int ensure_bookmark_cap(textreader_state_t *st, int need_cap)
{
    if(need_cap <= st->bookmark_cap) return 0;
    int new_cap = st->bookmark_cap ? st->bookmark_cap : 8;
    while(new_cap < need_cap) new_cap *= 2;
    size_t *p = (size_t *)realloc(st->bookmarks, (size_t)new_cap * sizeof(size_t));
    if(!p) return -1;
    st->bookmarks = p;
    st->bookmark_cap = new_cap;
    return 0;
}

int textreader_state_add_bookmark(textreader_state_t *st, size_t position)
{
    if(!st) return -1;
    // 去重
    for(int i = 0; i < st->bookmark_count; i++){
        if(st->bookmarks[i] == position) return 0;
    }
    if(ensure_bookmark_cap(st, st->bookmark_count + 1) != 0) return -1;
    st->bookmarks[st->bookmark_count++] = position;
    qsort(st->bookmarks, (size_t)st->bookmark_count, sizeof(size_t), cmp_size_t);
    return 0;
}

int textreader_state_remove_bookmark_at(textreader_state_t *st, int idx)
{
    if(!st) return -1;
    if(idx < 0 || idx >= st->bookmark_count) return -1;
    for(int i = idx; i + 1 < st->bookmark_count; i++){
        st->bookmarks[i] = st->bookmarks[i + 1];
    }
    st->bookmark_count--;
    return 0;
}

static void trim_inplace(char *s)
{
    if(!s) return;
    // rtrim
    size_t n = strlen(s);
    while(n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || isspace((unsigned char)s[n - 1]))) {
        s[n - 1] = '\0';
        n--;
    }
    // ltrim
    size_t i = 0;
    while(s[i] && isspace((unsigned char)s[i])) i++;
    if(i > 0) memmove(s, s + i, strlen(s + i) + 1);
}

int textreader_state_load(const char *state_path, textreader_state_t *st)
{
    if(!state_path || !st) return -1;
    if(ensure_textstate_dir() != 0) return -1;

    FILE *fp = fopen(state_path, "r");
    if(!fp) {
        // 不存在时视为首次打开
        return 0;
    }

    char line[1024];
    while(fgets(line, sizeof(line), fp)) {
        trim_inplace(line);
        if(line[0] == '\0' || line[0] == '#') continue;

        char *eq = strchr(line, '=');
        if(!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;
        trim_inplace(key);
        trim_inplace(val);

        if(strcmp(key, "position") == 0) {
            st->position = (size_t)strtoull(val, NULL, 10);
        } else if(strcmp(key, "font_px") == 0) {
            st->font_px = (float)strtod(val, NULL);
            if(st->font_px < 8.0f) st->font_px = 8.0f;
            if(st->font_px > 96.0f) st->font_px = 96.0f;
        } else if(strcmp(key, "font_path") == 0) {
            strncpy(st->font_path, val, sizeof(st->font_path) - 1);
            st->font_path[sizeof(st->font_path) - 1] = '\0';
        } else if(strcmp(key, "bookmark") == 0) {
            size_t pos = (size_t)strtoull(val, NULL, 10);
            (void)textreader_state_add_bookmark(st, pos);
        }
    }

    fclose(fp);
    return 0;
}

int textreader_state_save(const char *state_path, const textreader_state_t *st)
{
    if(!state_path || !st) return -1;
    if(ensure_textstate_dir() != 0) return -1;

    // 直接覆盖写入（简单可靠）
    FILE *fp = fopen(state_path, "w");
    if(!fp) return -1;

    fprintf(fp, "# textreader state\n");
    fprintf(fp, "position=%zu\n", st->position);
    fprintf(fp, "font_px=%.2f\n", st->font_px);
    fprintf(fp, "font_path=%s\n", st->font_path[0] ? st->font_path : "");
    for(int i = 0; i < st->bookmark_count; i++){
        fprintf(fp, "bookmark=%zu\n", st->bookmarks[i]);
    }

    fclose(fp);
    return 0;
}

