#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "ipc_client.h"
#include "ipc_common.h"
#include "log.h"
#include "icons.h"

#define TRIGGER_INTERVAL_US (30 * 1000 * 1000)


typedef struct {
    char *title;
    char *desc;
    char *icon;
    uint32_t color;
} priestess_msg_t;

priestess_msg_t priestess_msg[] = {
    {
        .title = "连接已建立。",
        .desc  = "你终于上线了。",
        .icon  = UI_ICON_HEART,
        .color = 0xFF2188FF,
    },
    {
        .title = "PRTS:......",
        .desc  = "查询结果:权限不足。",
        .icon  = UI_ICON_TRIANGLE_EXCLAMATION,
        .color = 0xFFF1C40F,
    },
    {
        .title = "系统提示",
        .desc  = "有人替你保存了“你本该忘记的东西”。",
        .icon  = UI_ICON_USER,
        .color = 0xFF757575,
    },
    {
        .title = "警告",
        .desc  = "你的指令正在被重写。",
        .icon  = UI_ICON_TRIANGLE_EXCLAMATION,
        .color = 0xFFCC3300,
    },
    {
        .title = "提示",
        .desc  = "不要看日志。",
        .icon  = UI_ICON_FILE,
        .color = 0xFF2188FF,
    },
    {
        .title = "欢迎回来。",
        .desc  = "别担心，我一直在。\n把手给我。",
        .icon  = UI_ICON_HEART,
        .color = 0xFF8A2BE2,
    },
    {
        .title = "异常:发现历史指纹。",
        .desc  = "正在校验身份......失败。\n......算了。你还是你。",
        .icon  = UI_ICON_HEART_CRACK,
        .color = 0xFF7F0000,
    },
    {
        .title = "检测到未知错误",
        .desc  = "错误:EYESOFPRIESTESS\n.....别再重复了，我看得见。",
        .icon  = UI_ICON_TRIANGLE_EXCLAMATION,
        .color = 0xFFCC3300,
    },
    {
        .title = "PRTS:......",
        .desc  = "查询:普瑞塞斯。返回:空。返回:我。",
        .icon  = UI_ICON_USER,
        .color = 0xFFCC3300,
    },
    {
        .title = "温馨提示",
        .desc  = "就这么喜欢和...小动物玩耍吗?",
        .icon  = UI_ICON_CAT,
        .color = 0xFFCC3300,
    }
};

static void random_priestess_msg(ipc_client_t *client) {
    // 随机选一条消息出来
    int index = rand() % sizeof(priestess_msg) / sizeof(priestess_msg[0]);
    log_debug("random priestess msg: %s", priestess_msg[index].title);
    // 用IPC弹窗展示
    ipc_client_ui_warning(client, 
        priestess_msg[index].title, 
        priestess_msg[index].desc, 
        priestess_msg[index].icon,
        priestess_msg[index].color
    );

    usleep(5 * 1000 * 1000);
    
    // 退回空界面。
    ipc_client_ui_set_current_screen(client, curr_screen_t_SCREEN_SPINNER);

}

static void display_priestess_img(ipc_client_t *client) {
    // 展示给定图片
    char curr_dir[128];
    getcwd(curr_dir, sizeof(curr_dir));
    char img_path_buf[128];
    snprintf(img_path_buf, sizeof(img_path_buf), "%s/EYESOFPRIESTESS.jpg", curr_dir);
    ipc_client_overlay_schedule_transition(
        client,
        300 * 1000,
        TRANSITION_TYPE_FADE,
        img_path_buf, 
        0xFF000000);
    
    usleep(2 * 1000 * 1000);

    ipc_client_overlay_schedule_transition(
        client,
        300 * 1000,
        TRANSITION_TYPE_FADE,
        img_path_buf, 
        0xFF000000);

    ipc_client_ui_warning(
        client, 
        "画面捕获:已损坏（2帧）", 
        "你刚刚看见了什么？", 
        UI_ICON_TRIANGLE_EXCLAMATION, 
        0xFFCC3300);

    usleep(5 * 1000 * 1000);

    ipc_client_ui_set_current_screen(client, curr_screen_t_SCREEN_SPINNER);
}

static void glitch_brightness(ipc_client_t *client) {

    int fd;
    // 先记录当前亮度
    char curr_brightness[16];
    fd = open("/sys/class/backlight/backlight/brightness", O_RDONLY);
    if (fd < 0) {
        log_error("open /sys/class/backlight/backlight/brightness failed");
        return;
    }
    read(fd, &curr_brightness, sizeof(curr_brightness));
    close(fd);

    char brightness_buf[16];
    // 随机设置亮度
    fd = open("/sys/class/backlight/backlight/brightness", O_WRONLY);
    if (fd < 0) {
        log_error("open /sys/class/backlight/backlight/brightness failed");
        return;
    }

    for(int i = 0; i < 20; i++) {
        snprintf(brightness_buf, sizeof(brightness_buf), "%d", rand() % 10);
        write(fd, brightness_buf, strlen(brightness_buf));
        usleep(200 * 1000);
    }

    write(fd, curr_brightness, strlen(curr_brightness));
    close(fd);

    ipc_client_ui_warning(client, "错误:数据完整性受损", "不是数据受损，是你。", UI_ICON_HEART_CRACK, 0xFFCC3300);
    usleep(5 * 1000 * 1000);
    ipc_client_ui_set_current_screen(client, curr_screen_t_SCREEN_SPINNER);
}

static bool is_proper_to_trigger(ipc_client_t* client) {
    ipc_resp_prts_status_data_t prts_status = {0};
    if (ipc_client_prts_get_status(client, &prts_status) < 0) {
        log_error("ipc_client_prts_get_status failed");
        return false;
    }
    if (prts_status.state != PRTS_STATE_IDLE) {
        return false;
    }
    curr_screen_t curr_screen;
    if (ipc_client_ui_get_current_screen(client, &curr_screen) < 0) {
        log_error("ipc_client_ui_get_current_screen failed");
        return false;
    }
    if (curr_screen != curr_screen_t_SCREEN_SPINNER) {
        return false;
    }
    return true;
}

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

uint64_t get_now_us(void){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000ll + tv.tv_usec;
}

int main(int argc, char *argv[]) {

    setup_signal_handler();

    ipc_client_t client = {.fd = -1};
    if (ipc_client_init(&client) < 0) {
        log_error("ipc_client_init failed");
        return 1;
    }

    uint64_t last_trigger_time = get_now_us();

    // 初始化随机种子
    srand(get_now_us());
    while(g_running) {
        if(get_now_us() - last_trigger_time < TRIGGER_INTERVAL_US) {
            goto loop_end;
        }
        if (!is_proper_to_trigger(&client)){
            goto loop_end;
        }

        // 开始之前先闭锁PRTS。
        ipc_client_prts_set_blocked_auto_switch(&client, true);

        last_trigger_time = get_now_us();
        
        int random_value = 0;
        random_value = rand() % 10;
        if (random_value <= 6) {
            random_priestess_msg(&client);
        } else if (random_value <= 8) {
            display_priestess_img(&client);
        } else {
            glitch_brightness(&client);
        }

        // 结束之后再解锁PRTS。
        ipc_client_prts_set_blocked_auto_switch(&client, false);
loop_end:
        usleep(1 * 1000 * 1000);
    }

    log_info("Exiting...");
    ipc_client_ui_warning(&client, "你要离开？", "你总是这样。\n再见。", UI_ICON_HEART_CRACK, 0xFFCC3300);
    ipc_client_destroy(&client);
    return 0;
}
