#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <gpiod.h>
#include <unistd.h>
#include "epass_define.h"
#include "fbdrawttf.h"
#include "log.h"
#include "keyinput.h"
#include "uart.h"
#include "drm_warpper.h"
#include "fbdraw.h"

// 在这里定义你的wifi的ssid和密码。一定要是2.4G的
#define WIFI_SSID "aaaaaaaaaaaa"
#define WIFI_PASSWORD "vvvvvvvv"

#define HITOKOTO_API_HOST "v1.hitokoto.cn"
#define HITOKOTO_API_IP "104.21.63.38"
#define HITOKOTO_API_PATH "/?encode=text"
#define HITOKOTO_API_PORT "80"

// 别忘记http的请求本质上就是字符串哦。
// 我们这里直接手写http请求。
#define HITOKOTO_HTTP_REQUEST \
    "GET " HITOKOTO_API_PATH " HTTP/1.1\r\n" \
    "Host: " HITOKOTO_API_HOST "\r\n" \
    "Connection: close\r\n" \
    "User-Agent: shirogane-handwritten/1.0\r\n" \
    "Content-Length: 0\r\n\r\n"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define DRAW_WIDTH 360
#define DRAW_HEIGHT 640

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


// 等待UART退回OK或ERROR。
void at_wait_ok(struct UartDevice* uart_dev) {
    char buf[1024];
    while(1) {
        uart_reads(uart_dev, buf, 1024);
        log_info("wait ok: %s", buf);
        if(strstr(buf, "OK") || strstr(buf, "ERROR")) {
            break;
        }
    }
    uart_flush(uart_dev);

}


// 双缓冲相关数据结构
// 这四个变量会在之后的生命周期里一直使用。记得不要把他放在栈上。
// 如果你新建了一个函数，记得把这个变量写成static或者做全局变量。
static buffer_object_t buf_1,buf_2;
static drm_warpper_queue_item_t item_1,item_2;


int main(int argc, char *argv[]) {
    int ret;
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
    fbdraw_ttf_load_font(&font, "./SourceHanSansSC-Regular.ttf");
    log_info("font loaded");
    // 初始化uart。注意这里的filename，/dev/ttyS1 这个1说的是第二个出现的uart的意思。（第一个是console）
    // 单独启动了uart1/uart2功能，那么uart1/uart2的设备文件都是/dev/ttyS1
    // 如果同时启动了uart1和uart2功能，那么uart1的设备文件是/dev/ttyS1，uart2的设备文件是/dev/ttyS2
    struct UartDevice uart_dev = {
        .filename = FIRST_UART_DEVICE_NAME,
        .rate = B115200,
    };
    ret = uart_start(&uart_dev, true);
    if(ret != 0) {
        log_error("Failed to start uart");
        return 1;
    }
    log_info("Uart started");

    // 先直接显示一点东西
    fbdraw_fill_rect(&fbdst, &drect, 0xff000000);
    fbdraw_ttf_draw_text(&fbdst, &drect, &font, "初始化！", 60, 0xffffffff);

    // 重置ESP-01S模块
    log_info("AT+RST");
    uart_writes(&uart_dev, "AT+RST\r\n");
    at_wait_ok(&uart_dev);
    
    usleep(3 * 1000 * 1000);

    // 获取模块版本
    log_info("AT+GMR");
    uart_writes(&uart_dev, "AT+GMR\r\n");
    at_wait_ok(&uart_dev);

    // 设置模块模式为Station模式
    log_info("AT+CWMODE=1");
    uart_writes(&uart_dev, "AT+CWMODE=1\r\n");
    at_wait_ok(&uart_dev);

    fbdraw_fill_rect(&fbdst, &drect, 0xff000000);
    fbdraw_ttf_draw_text(&fbdst, &drect, &font, "连接热点...", 60, 0xffffffff);

    // 连接热点
    log_info("AT+CWJAP=\"" WIFI_SSID "\",\"************\"");
    uart_writes(&uart_dev, "AT+CWJAP=\"" WIFI_SSID "\",\"" WIFI_PASSWORD "\"\r\n");
    at_wait_ok(&uart_dev);

    // 获取IP地址
    log_info("AT+CIPSTA?");
    uart_writes(&uart_dev, "AT+CIPSTA?\r\n");
    at_wait_ok(&uart_dev);

    while(g_running) {
        // 获取一个可以用来显示的buffer
        drm_warpper_queue_item_t* curr_item = NULL;
        drm_warpper_dequeue_free_item(&drm_warpper, 1, &curr_item);
        fbdst.vaddr = (uint32_t*)curr_item->mount.arg0;

        // 打开TCP连接。
        log_info("send http request");
        uart_writes(&uart_dev, "AT+CIPSTART=\"TCP\",\"" HITOKOTO_API_IP "\"," HITOKOTO_API_PORT "\r\n");
        at_wait_ok(&uart_dev);

        // 这里需要一个足够大的缓冲区来存储HTTP响应。
        char buf[2048];
        // 发送HTTP请求长度。先借用一下这个buffer——
        snprintf(buf, sizeof(buf), "AT+CIPSEND=%d\r\n", strlen(HITOKOTO_HTTP_REQUEST));
        log_info("send http request length: %s", buf);
        uart_writes(&uart_dev, buf);
        at_wait_ok(&uart_dev);

        // 发送HTTP请求。
        log_info("send http request");
        uart_writes(&uart_dev, HITOKOTO_HTTP_REQUEST);
        at_wait_ok(&uart_dev);
        log_info("http request sent, waiting for response");

        // 等待HTTP响应。
        int curr_response_len = 0;
        while(g_running) {
            curr_response_len += uart_reads(&uart_dev, buf+curr_response_len, sizeof(buf)-curr_response_len);
            if(curr_response_len >= sizeof(buf)) {
                break;
            }
            // 我们请求包的header 有Connection: close。
            // 服务端侧会自动关闭请求。对于我们来说，一旦收到CLOSED
            // 就说明响应结束了。
            if(strstr(buf, "CLOSED")) {
                break;
            }
            usleep(100000);
        }

        // 以防万一，我们再关闭一次TCP连接。
        log_info("AT+CIPCLOSE");
        uart_writes(&uart_dev, "AT+CIPCLOSE\r\n");
        at_wait_ok(&uart_dev);

        // HTTP的响应头和响应体之间，有一个空行。
        // 我们找到这个空行，然后从空行后面开始，就是响应体。
        char * http_body = strstr(buf, "\r\n\r\n");
        if(http_body) {
            http_body += 4;
        }

        // 去掉最后的CLOSED字符串。
        char * http_body_end = strstr(http_body, "CLOSED");
        *http_body_end = '\0';
        log_info("http body: %s", http_body);

        // 把HTTP响应体，也就是“一言“画到屏幕上
        char textbuf[1024];
        snprintf(textbuf, sizeof(textbuf), "一言 Hitokoto:\n%s\n\n 按4退出。", http_body);
        fbdraw_fill_rect(&fbdst, &drect, 0xff000000);
        fbdraw_ttf_draw_text(&fbdst, &drect, &font, textbuf, 60, 0xffffffff);
       
        // 提交buffer到显示队列
        drm_warpper_enqueue_display_item(&drm_warpper, 1, curr_item);

        // 获取按键
        int key = keyinput_get_key();
        if(key == KEY_4) {
            log_info("KEY_4 pressed, exiting");
            g_running = false;
        }
        usleep(5000000);

    }

    log_info("Exiting...");

    // 收尾及清理工作。
    drm_warpper_destroy_layer(&drm_warpper, 1);
    drm_warpper_free_buffer(&drm_warpper, 1, &buf_1);
    drm_warpper_free_buffer(&drm_warpper, 1, &buf_2);
    drm_warpper_destroy(&drm_warpper);
    fbdraw_ttf_free_font(&font);
    uart_stop(&uart_dev);

    return 0;
}  