#include <signal.h>
#include <unistd.h>
#include "log.h"
#include "keyinput.h"


// 为什么用信号处理函数来处理Ctrl-C?
// 因为当用户在终端按下Ctrl-C时，操作系统会向前台运行的程序发送SIGINT信号。
// 通过捕获和处理这个信号（而不是让程序直接被终止），我们可以优雅地执行一些清理工作，
// 安全释放资源，正确关闭文件或设备，保证程序平滑退出，而不是“突然中断”。
static bool g_running = true;
void signal_handler(int signal){
    log_info("signal %d received, exiting", signal);
    g_running = false;
}
void setup_signal_handler(){
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

int main(int argc, char *argv[]) {
    // 设置信号处理函数
    setup_signal_handler();
    // 初始化按键输入
    keyinput_init();

    // 在这里添加其他初始化操作
    log_info("Starting main loop....");

    while(g_running) {
        // 在这里添加主循环逻辑

        // 读取按键输入
        int key = keyinput_get_key();
        // 进行按键处理，如
        if(key == KEY_4) {
            log_info("KEY_4 pressed, exiting");
            g_running = false;
        }

    }

    log_info("Exiting...");
    // 在这里释放资源
    // 如关闭文件、释放内存、关闭设备等

    return 0;
}  