#include <stdio.h>
#include <stdlib.h>
#include <gpiod.h>
#include <unistd.h>
#include "epass_define.h"
#include "log.h"
#include "keyinput.h"
#include "i2c.h"
#include "ssd1306_i2c.h"


// 为什么用信号处理函数来处理Ctrl-C?
// 因为当用户在终端按下Ctrl-C时，操作系统会向前台运行的程序发送SIGINT信号。
// 通过捕获和处理这个信号（而不是让程序直接被终止），我们可以优雅地执行一些清理工作，安全释放资源，正确关闭文件或设备，保证程序平滑退出，而不是“突然中断”。
static bool g_running = true;
void signal_handler(int signal){
    log_info("signal %d received, exiting", signal);
    g_running = false;
}

int main(int argc, char *argv[]) {
    int ret;
    // 初始化按键输入
    keyinput_init();

    // 初始化i2c设备
    struct I2cDevice dev = {
        .filename = I2C0_DEVICE_NAME,
        // 此处为i2c地址
        .addr = 0x3C,
    };
    ret = i2c_start(&dev);
    if(ret < 0) {
        log_error("Failed to start i2c");
        return 1;
    }

    //如果需要多个i2c设备，如法炮制再开一个struct I2cDevice：
    // struct I2cDevice dev2 = {
    //     .filename = I2C0_DEVICE_NAME,
    //     .addr = 0x3D,
    // };
    // ret = i2c_start(&dev2);
    // if(ret < 0) {
    //     log_error("Failed to start i2c");
    //     return 1;
    // }

    // 初始化ssd1306液晶屏
    ssd1306_begin(&dev);
    ssd1306_display();
    usleep(2000000);

    // 清屏
    ssd1306_clearDisplay();
    // 设置光标位置
    ssd1306_set_cursor(0, 0);
    // 绘制字符串
    ssd1306_drawString("Hello, World!");
    ssd1306_display();

    int iter = 0;
    char buf[20];
    while(g_running) {
        // 读取按键输入
        int key = keyinput_get_key();
        if(key == KEY_4) {
            log_info("KEY_4 pressed, exiting");
            g_running = false;
        }

        // 清空第2行
        ssd1306_fillRect(0, 8, 128, 8, BLACK);
        // 设置光标位置
        ssd1306_set_cursor(0, 8);
        // 格式化字符串
        snprintf(buf, sizeof(buf), "iter:%d", iter);
        // 绘制字符串
        ssd1306_drawString(buf);
        // 显示
        ssd1306_display();
        puts(buf);
        puts("\n");
        iter ++;
        usleep(1000000);
    }

    log_info("Exiting...");
    // 释放i2c资源
    i2c_stop(&dev);

    return 0;
}  