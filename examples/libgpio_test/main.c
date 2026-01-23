#include <stdio.h>
#include <stdlib.h>
#include <gpiod.h>
#include <unistd.h>
#include "epass_define.h"
#include "log.h"
#include "keyinput.h"


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

    // 打开GPIO芯片
    struct gpiod_chip *chip = gpiod_chip_open_by_name(GPIO_CHIP_NAME);
    if (!chip) {
        log_error("Failed to open %s", GPIO_CHIP_NAME);
        return 1;
    }

    // 获取GPIO_PE5_PIN。在linux里，一个GPIO脚称为一个“line”
    struct gpiod_line *line_pe5 = gpiod_chip_get_line(chip, GPIO_PE5_PIN);
    if (!line_pe5) {
        log_error("Failed to get line %d", GPIO_PE5_PIN);
        return 1;
    }

    // 获取GPIO_PE6_PIN
    struct gpiod_line *line_pe6 = gpiod_chip_get_line(chip, GPIO_PE6_PIN);
    if (!line_pe6) {
        log_error("Failed to get line %d", GPIO_PE6_PIN);
        return 1;
    }

    // 请求GPIO_PE5_PIN为输出
    log_info("Requesting output for GPIO_PE5_PIN: %d", GPIO_PE5_PIN);
    ret = gpiod_line_request_output(line_pe5, "gpio_test", 0);
    if(ret < 0) {
        log_error("Failed to request output for GPIO_PE5_PIN: %d", GPIO_PE5_PIN);
        return 1;
    }

    // 请求GPIO_PE6_PIN为输入，并上拉。有这些可用的flag：
    // GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP 上拉
    // GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN 下拉
    // GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE 无上拉/下拉
    // GPIOD_LINE_REQUEST_FLAG_ACTIVE_LOW 低电平有效
    // GPIOD_LINE_REQUEST_FLAG_OPEN_DRAIN 开漏输出
    // GPIOD_LINE_REQUEST_FLAG_OPEN_SOURCE 开源输出

    log_info("Requesting input_pullup for GPIO_PE6_PIN: %d", GPIO_PE6_PIN);
    ret = gpiod_line_request_input_flags(line_pe6, "gpio_test", GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP);
    if(ret < 0) {
        log_error("Failed to request input_pullup for GPIO_PE6_PIN: %d", GPIO_PE6_PIN);
        return 1;
    }
    
    log_info("Press key_4 to exit");

    while(g_running) {
        // 读取GPIO_PE6_PIN的值
        int value = gpiod_line_get_value(line_pe6);
        log_info("GPIO_PE6_PIN value: %d", value);
        if(value == 1) {
            log_info("GPIO_PE6_PIN is HIGH");
        } else {
            log_info("GPIO_PE6_PIN is LOW");
        }
        // 设置GPIO_PE5_PIN为高电平
        log_info("Set GPIO_PE5_PIN to HIGH: %d", GPIO_PE5_PIN);
        gpiod_line_set_value(line_pe5, 1);
        usleep(1000000);
        // 设置GPIO_PE5_PIN为低电平
        log_info("Set GPIO_PE5_PIN to LOW: %d", GPIO_PE5_PIN);
        gpiod_line_set_value(line_pe5, 0);
        usleep(1000000);

        // 读取按键输入
        int key = keyinput_get_key();
        if(key == KEY_4) {
            log_info("KEY_4 pressed, exiting");
            g_running = false;
        }
    }

    log_info("Exiting...");
    gpiod_line_release(line_pe5);
    gpiod_line_release(line_pe6);
    gpiod_chip_close(chip);

    return 0;
}  