#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include "fbdraw.h"
#include "crrefont.h"
#include "log.h"
#include "drm_warpper.h"
#include "keyinput.h"
#include "RREFont/rre_chicago_20x24.h"

#define DRAW_WIDTH 360
#define DRAW_HEIGHT 640
#define DRAW_LAYER 1

// Game constants
#define GRAVITY 0.5f
#define JUMP_STRENGTH -8.0f
#define PIPE_SPEED 4.0f
#define PIPE_WIDTH 60
#define PIPE_GAP 180
#define BIRD_X 80
#define BIRD_SIZE 30
#define MAX_PIPES 3

typedef struct {
    float x, y;
    float velocity;
} Bird;

typedef struct {
    float x;
    float gap_y;
    bool active;
    bool passed;
} Pipe;

typedef enum {
    STATE_START,
    STATE_PLAYING,
    STATE_GAMEOVER
} GameState;

static bool g_running = true;
void signal_handler(int signal) {
    log_info("signal %d received, exiting...", signal);
    g_running = false;
}

// Global game variables
static Bird bird;
static Pipe pipes[MAX_PIPES];
static int score = 0;
static GameState game_state = STATE_START;

// RREFont setup
static fbdraw_fb_t font_draw_fb;
static CRREFont *font = NULL;

void rrefont_rect_cb(int x, int y, int w, int h, int c) {
    static fbdraw_rect_t dst_rect;
    dst_rect.x = x;
    dst_rect.y = y;
    dst_rect.w = w;
    dst_rect.h = h;
    fbdraw_fill_rect(&font_draw_fb, &dst_rect, c);
}

void init_game() {
    bird.y = DRAW_HEIGHT / 2;
    bird.velocity = 0;
    score = 0;
    
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].x = DRAW_WIDTH + i * (DRAW_WIDTH / 2 + PIPE_WIDTH);
        pipes[i].gap_y = 150 + rand() % (DRAW_HEIGHT - 300);
        pipes[i].active = true;
        pipes[i].passed = false;
    }
}

void update_game() {
    if (game_state != STATE_PLAYING) return;

    // Bird physics
    bird.velocity += GRAVITY;
    bird.y += bird.velocity;

    // Floor/Ceiling collision
    if (bird.y < 0) {
        bird.y = 0;
        bird.velocity = 0;
    }
    if (bird.y + BIRD_SIZE > DRAW_HEIGHT) {
        game_state = STATE_GAMEOVER;
    }

    // Pipes movement
    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].x -= PIPE_SPEED;

        if (pipes[i].x + PIPE_WIDTH < 0) {
            pipes[i].x += MAX_PIPES * (DRAW_WIDTH / 2 + PIPE_WIDTH);
            pipes[i].gap_y = 150 + rand() % (DRAW_HEIGHT - 300);
            pipes[i].passed = false;
        }

        // Collision detection
        if (BIRD_X + BIRD_SIZE > pipes[i].x && BIRD_X < pipes[i].x + PIPE_WIDTH) {
            if (bird.y < pipes[i].gap_y - PIPE_GAP / 2 || 
                bird.y + BIRD_SIZE > pipes[i].gap_y + PIPE_GAP / 2) {
                game_state = STATE_GAMEOVER;
            }
        }

        // Scoring
        if (!pipes[i].passed && pipes[i].x + PIPE_WIDTH < BIRD_X) {
            score++;
            pipes[i].passed = true;
        }
    }
}

void draw_game(fbdraw_fb_t *fb) {
    // Clear background (Sky blue)
    fbdraw_fill_rect(fb, &(fbdraw_rect_t){0, 0, DRAW_WIDTH, DRAW_HEIGHT}, 0xFF87CEEB);

    // Draw pipes
    for (int i = 0; i < MAX_PIPES; i++) {
        // Top pipe
        fbdraw_fill_rect(fb, &(fbdraw_rect_t){(int)pipes[i].x, 0, PIPE_WIDTH, (int)(pipes[i].gap_y - PIPE_GAP / 2)}, 0xFF228B22);
        // Bottom pipe
        fbdraw_fill_rect(fb, &(fbdraw_rect_t){(int)pipes[i].x, (int)(pipes[i].gap_y + PIPE_GAP / 2), PIPE_WIDTH, (int)(DRAW_HEIGHT - (pipes[i].gap_y + PIPE_GAP / 2))}, 0xFF228B22);
    }

    // Draw bird (Yellow square)
    fbdraw_fill_rect(fb, &(fbdraw_rect_t){BIRD_X, (int)bird.y, BIRD_SIZE, BIRD_SIZE}, 0xFFFFFF00);

    // Draw UI
    font_draw_fb.vaddr = fb->vaddr;
    CRREFont_setBg(font, 0xFF000000); 
    CRREFont_setFg(font, 0xFFFFFFFF);

    if (game_state == STATE_START) {
        CRREFont_printf(font, 80, 250, "FLAPPY BIRD");
        CRREFont_printf(font, 60, 300, "Press KEY_3 to Jump");
    } else if (game_state == STATE_PLAYING) {
        CRREFont_printf(font, 10, 10, "Score: %d", score);
    } else if (game_state == STATE_GAMEOVER) {
        CRREFont_printf(font, 100, 250, "GAME OVER");
        CRREFont_printf(font, 120, 300, "Score: %d", score);
        CRREFont_printf(font, 60, 350, "Press KEY_3 to Restart");
    }
}

// Static buffers for DRM
static buffer_object_t buf_1, buf_2;
static drm_warpper_queue_item_t item_1, item_2;

int main(int argc, char *argv[]) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    srand(time(NULL));
    keyinput_init();

    drm_warpper_t drm_warpper;
    drm_warpper_init(&drm_warpper);
    drm_warpper_init_layer(&drm_warpper, DRAW_LAYER, DRAW_WIDTH, DRAW_HEIGHT, DRM_WARPPER_LAYER_MODE_ARGB8888);

    drm_warpper_allocate_buffer(&drm_warpper, DRAW_LAYER, &buf_1);
    drm_warpper_allocate_buffer(&drm_warpper, DRAW_LAYER, &buf_2);

    item_1.mount.type = DRM_SRGN_ATOMIC_COMMIT_MOUNT_FB_NORMAL;
    item_1.mount.arg0 = (uint32_t)buf_1.vaddr;
    item_1.userdata = &buf_1;
    item_1.on_heap = false;

    item_2.mount.type = DRM_SRGN_ATOMIC_COMMIT_MOUNT_FB_NORMAL;
    item_2.mount.arg0 = (uint32_t)buf_2.vaddr;
    item_2.userdata = &buf_2;
    item_2.on_heap = false;

    drm_warpper_mount_layer(&drm_warpper, DRAW_LAYER, 0, 0, &buf_1);
    drm_warpper_enqueue_display_item(&drm_warpper, DRAW_LAYER, &item_1);
    drm_warpper_enqueue_display_item(&drm_warpper, DRAW_LAYER, &item_2);

    font = CRREFont_new();
    CRREFont_init(font, rrefont_rect_cb, DRAW_WIDTH, DRAW_HEIGHT);
    CRREFont_setFont(font, &rre_chicago_20x24);
    font_draw_fb.width = DRAW_WIDTH;
    font_draw_fb.height = DRAW_HEIGHT;

    init_game();

    drm_warpper_queue_item_t *curr_item = NULL;
    int last_key = -1;
    while (g_running) {
        int key = keyinput_get_key();
        if (key == KEY_4) g_running = false;
        
        bool jump_pressed = (key == KEY_3 || key == KEY_1) && (last_key != KEY_3 && last_key != KEY_1);
        last_key = key;

        if (jump_pressed) {
            if (game_state == STATE_START) {
                game_state = STATE_PLAYING;
                bird.velocity = JUMP_STRENGTH;
            } else if (game_state == STATE_PLAYING) {
                bird.velocity = JUMP_STRENGTH;
            } else if (game_state == STATE_GAMEOVER) {
                init_game();
                game_state = STATE_START;
            }
        }

        update_game();

        drm_warpper_dequeue_free_item(&drm_warpper, DRAW_LAYER, &curr_item);
        fbdraw_fb_t fb = {(uint32_t*)curr_item->mount.arg0, DRAW_WIDTH, DRAW_HEIGHT};
        draw_game(&fb);
        drm_warpper_enqueue_display_item(&drm_warpper, DRAW_LAYER, curr_item);
    }

    log_info("cleaning up...");
    drm_warpper_destroy_layer(&drm_warpper, DRAW_LAYER);
    drm_warpper_free_buffer(&drm_warpper, DRAW_LAYER, &buf_1);
    drm_warpper_free_buffer(&drm_warpper, DRAW_LAYER, &buf_2);
    drm_warpper_destroy(&drm_warpper);

    return 0;
}
