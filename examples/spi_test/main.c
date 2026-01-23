#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdlib.h>
#include <gpiod.h>
#include <unistd.h>
#include "epass_define.h"
#include "log.h"
#include "keyinput.h"
#include "spi.h"
#include "adxl345_drv_basic.h"

// 为什么用信号处理函数来处理Ctrl-C?
// 因为当用户在终端按下Ctrl-C时，操作系统会向前台运行的程序发送SIGINT信号。
// 通过捕获和处理这个信号（而不是让程序直接被终止），我们可以优雅地执行一些清理工作，安全释放资源，正确关闭文件或设备，保证程序平滑退出，而不是“突然中断”。
static bool g_running = true;
void signal_handler(int signal){
    log_info("signal %d received, exiting", signal);
    g_running = false;
}

struct SpiDevice spidev = {
    .filename = SPI1_DEVICE_NAME,
    .mode = SPI_MODE_3,
    .bpw = 8,
    .speed = 1000000 / 8,
};


int main(int argc, char *argv[]) {
    int ret;
    keyinput_init();

    // 初始化adxl345
    ret = adxl345_basic_init(ADXL345_INTERFACE_SPI, ADXL345_ADDRESS_ALT_0);
    if(ret != 0) {
        log_error("Failed to init adxl345");
        return 1;
    }
    log_info("Adxl345 initialized");
    float g[3];

    while(g_running) {
        int key = keyinput_get_key();
        if(key == KEY_4) {
            log_info("KEY_4 pressed, exiting");
            g_running = false;
        }

        ret = adxl345_basic_read(g);
        if(ret != 0) {
            log_error("Failed to read adxl345");
            return 1;
        }
        log_info("Adxl345 read: %f, %f, %f", g[0], g[1], g[2]);

        usleep(1000000);
    }

    log_info("Exiting...");

    return 0;
}  