#include <signal.h>
#include <unistd.h>
#include "fbdraw.h"
#include "crrefont.h"
#include "config.h"
#include "RREFont/rre_chicago_20x24.h"
#include "log.h"
#include "drm_warpper.h"
#include "niccc.h"
#include "epniccc_bg.h"
#include "keyinput.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


// 字体渲染  
static fbdraw_fb_t font_draw_fb;
static CRREFont *font = NULL;
// RREFont 字体渲染回调
void rrefont_rect_cb(int x, int y, int w, int h, int c){
    static fbdraw_rect_t dst_rect;
    dst_rect.x = x;
    dst_rect.y = y;
    dst_rect.w = w;
    dst_rect.h = h;
    fbdraw_fill_rect(&font_draw_fb, &dst_rect, c);
}

// 初始化 RREFont 字体渲染
void rrefont_init(){
    font = CRREFont_new();
    CRREFont_init(font,rrefont_rect_cb, DRAW_WIDTH, DRAW_HEIGHT);
    CRREFont_setFont(font, &rre_chicago_20x24);

    CRREFont_setFg(font, 0xffffffff);
    CRREFont_setBg(font, 0xff000000);

    font_draw_fb.height = DRAW_HEIGHT;
    font_draw_fb.width = DRAW_WIDTH;
}


// 为什么用信号处理函数来处理Ctrl-C?
// 因为当用户在终端按下Ctrl-C时，操作系统会向前台运行的程序发送SIGINT信号。
// 通过捕获和处理这个信号（而不是让程序直接被终止），我们可以优雅地执行一些清理工作，安全释放资源，正确关闭文件或设备，保证程序平滑退出，而不是“突然中断”。
static bool g_running = true;
void signal_handler(int signal){
    log_info("signal %d received, exiting...", signal);
    g_running = false;
}
void setup_signal_handler(){
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

// ST-NICCC的demo会显示，一整次1800帧绘图所用的时长。
// 他们是用来比较谁的优化更好的，我们这里就做着玩保留一下（）
int get_time_ms(){
    return time(NULL) * 1000 + clock() / (CLOCKS_PER_SEC / 1000);
}

static int g_start_time_ms = 0;
static int g_stop_time_ms = 0;
static bool g_is_timing = true;

int get_timing_time_ms(){
    if(g_is_timing){
        return get_time_ms() - g_start_time_ms;
    }
    return g_stop_time_ms - g_start_time_ms;
}


// 双缓冲相关数据结构
// 这四个变量会在之后的生命周期里一直使用。记得不要把他放在栈上。
// 如果你新建了一个函数，记得把这个变量写成static或者做全局变量。
static buffer_object_t buf_1,buf_2;
static drm_warpper_queue_item_t item_1,item_2;

int main(int argc, char *argv[]) {
    setup_signal_handler();
    keyinput_init();
    rrefont_init();
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

    // 把背景图画到两个buffer中

    // 你可以使用img2lcd对图片进行取模，
    // 绘图目标
    fbdst.vaddr = (uint32_t*)buf_1.vaddr;
    fbdst.width = DRAW_WIDTH;
    fbdst.height = DRAW_HEIGHT;
    //目标矩形（范围)
    drect.x = 0;
    drect.y = 0;
    drect.w = DRAW_WIDTH;
    drect.h = DRAW_HEIGHT;
    // 这个rgb565绘制函数是img2lcd专用的。。
    fbdraw_draw_rgb565(&fbdst, (uint8_t*)gImage_epniccc_bg, 360, 640);
    fbdst.vaddr = (uint32_t*)buf_2.vaddr;
    fbdraw_draw_rgb565(&fbdst, (uint8_t*)gImage_epniccc_bg, 360, 640);


    // 或者用fbdraw_image函数，直接加载文件系统里的图片
    // fbdst.vaddr = (uint32_t*)buf_1.vaddr;
    // fbdst.width = DRAW_WIDTH;
    // fbdst.height = DRAW_HEIGHT;

    // drect.x = 0;
    // drect.y = 0;
    // drect.w = DRAW_WIDTH;
    // drect.h = DRAW_HEIGHT;
    // fbdraw_image(&fbdst, &drect, "/root/epniccc/epniccc.jpg");
    // fbdst.vaddr = (uint32_t*)buf_2.vaddr;
    // fbdraw_image(&fbdst, &drect, "/root/epniccc/epniccc.jpg");



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
    drm_warpper_mount_layer(&drm_warpper, 1, 0, DRAW_HEIGHT, &buf_1);
    
    // 先把两个buffer都提交一次，形成队列的初始状态（一个显示中，一个等待取回）
    drm_warpper_enqueue_display_item(&drm_warpper, 1, &item_1);
    drm_warpper_enqueue_display_item(&drm_warpper, 1, &item_2);

    // 也就是说，现在正在显示item2，也就是buf2地址内的内容。
    // buf1现在正在取回队列，下次调用drm_warpper_dequeue_free_itm的时候，会获得item1。

    // 图层滑入。
    for(int y=DRAW_HEIGHT; y>=0; y-=10){
        drm_warpper_set_layer_coord(&drm_warpper, 1, 0, y);
        usleep(10000); // 10ms
    }

    drm_warpper_set_layer_coord(&drm_warpper, 1, 0, 0);


    // 开始主循环。
    drm_warpper_queue_item_t* curr_item = NULL;
    int frame_count = 0;
    g_start_time_ms = get_time_ms();
    while(g_running){
        char* pressed_key_str;
        // 处理按键输入
        int key = keyinput_get_key();
        switch(key){
            case KEY_4:
                g_running = false;
                break;
            case KEY_3:
                pressed_key_str = "KEY_3";
                break;
            case KEY_2:
                pressed_key_str = "KEY_2";
                break;
            case KEY_1:
                pressed_key_str = "KEY_1";
                break;
            case KEY_0:
                pressed_key_str = "KEY_0";
                break;
            default:
                pressed_key_str = " ";
                break;
        }

        if(!g_running){
            break;
        }

        // 程序会堵塞获取一个可以用来显示的buffer。
        // 空buffer是需要在vblank的时候，有新的buffer被换上以后，才会被换下来。
        // 因此此函数还有等待Vsync的功效。
        drm_warpper_dequeue_free_item(&drm_warpper, 1, &curr_item);
        
        // 我们只需要把图画到这个vaddr里面去就可以了。
        // vaddr是一个指针，指向一个32位 DRAW_WIDTH * DRAW_HEIGHT的图形缓冲区
        // 数组中的每个元素是一个32位整数，表示一个像素。
        // 数组中的每个元素的格式是ARGB。
        uint32_t* vaddr = (uint32_t*)curr_item->mount.arg0;
        font_draw_fb.vaddr = vaddr;

        // 先清空字体下面的空白区域
        fbdst.vaddr = vaddr;
        drect.x = 0;
        drect.y = 110;
        drect.w = DRAW_WIDTH;
        drect.h = 60;
        fbdraw_fill_rect(&fbdst, &drect, 0xff000000);
        // 把字体画到这个buffer里面去。
        if(g_is_timing){
            CRREFont_printf(font, 10, 110, "Frame: %d TIME:%dms", frame_count,get_timing_time_ms());
        }else{
            //闪烁显示时间。
            if(frame_count % 100 < 50){
                CRREFont_printf(font, 10, 110, "Frame: %d TIME:",frame_count);
            }else{
                CRREFont_printf(font, 10, 110, "Frame: %d TIME:%dms", frame_count, get_timing_time_ms());
            }
        }
        CRREFont_printf(font, 10, 130, "Pressed Key: %s", pressed_key_str);

        // 画这个帧的内容。
        niccc_draw_frame(vaddr, frame_count);

        // 把buffer提交给drm_warpper，让他在下一个vblank的时候显示出来。
        drm_warpper_enqueue_display_item(&drm_warpper, 1, curr_item);
        
        frame_count++;
        if(frame_count >= 1800){
            frame_count = 0;

            if(g_is_timing){
                g_stop_time_ms = get_time_ms();
                g_is_timing = false;
            }
        }
    }

    log_info("exiting...");

    // 图层滑出。
    for(int y=0; y<DRAW_HEIGHT; y+=10){
        drm_warpper_set_layer_coord(&drm_warpper, 1, 0, y);
        usleep(10000); // 10ms
    }

    drm_warpper_set_layer_coord(&drm_warpper, 1, 0, DRAW_HEIGHT);

    // 释放资源
    drm_warpper_destroy_layer(&drm_warpper, 1);
    drm_warpper_free_buffer(&drm_warpper, 1, &buf_1);
    drm_warpper_free_buffer(&drm_warpper, 1, &buf_2);
    drm_warpper_destroy(&drm_warpper);
    return 0;
}