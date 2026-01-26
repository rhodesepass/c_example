#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ipc_client.h"
#include "ipc_common.h"
#include "log.h"
#include "uuid.h"
#include "icons.h"

static int build_asset_path(char *out, size_t out_len, const char *filename) {
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        log_error("getcwd failed");
        return -1;
    }
    int written = snprintf(out, out_len, "%s/assets/%s", cwd, filename);
    if (written < 0 || (size_t)written >= out_len) {
        log_error("asset path too long: %s", filename);
        return -1;
    }
    return 0;
}

static void print_settings(const ipc_settings_data_t *settings) {
    log_info(
        "settings: brightness=%d interval=%d mode=%d usb=%d ctrl_lowbat=%u ctrl_no_intro=%u ctrl_no_overlay=%u",
        settings->brightness,
        settings->switch_interval,
        settings->switch_mode,
        settings->usb_mode,
        settings->ctrl_word.lowbat_trip,
        settings->ctrl_word.no_intro_block,
        settings->ctrl_word.no_overlay_block
    );
}

void notice_with_warning(ipc_client_t *client, const char *title, const char *desc) {
    if (ipc_client_ui_warning(client, title, desc, UI_ICON_CAT, 0xFF004400) < 0) {
        log_error("ipc_client_ui_warning failed");
    }
}

int main(int argc, char *argv[]) {

    // 初始化IPC对象

    ipc_client_t client = {.fd = -1};
    if (ipc_client_init(&client) < 0) {
        log_error("ipc_client_init failed");
        return 1;
    }

    int rc = 0;
    char dispimg_path[PATH_MAX] = {0};
    char transition_img_path[PATH_MAX] = {0};
    char video_path[PATH_MAX] = {0};
    bool have_dispimg = (build_asset_path(dispimg_path, sizeof(dispimg_path), "test_force_dispimg.png") == 0);
    bool have_transition_img = (build_asset_path(transition_img_path, sizeof(transition_img_path), "testtransition_ov.jpg") == 0);
    bool have_video = (build_asset_path(video_path, sizeof(video_path), "testvideo.mp4") == 0);

    log_info("Starting IPC Test.....");

    notice_with_warning(&client, "你好！", "我是白银，很高兴认识你！");
    usleep(3 * 1000 * 1000);
    notice_with_warning(&client, "白银", "我来演示一下电子通行证后台进程请求(IPC)的能力~");
    usleep(3 * 1000 * 1000);
    
    // UI warning（弹窗）
    log_info("UI warning");
    if (ipc_client_ui_warning(&client, "弹窗", "可以设置颜色，图标，标题，描述", UI_ICON_TRIANGLE_EXCLAMATION, 0xFFCC3300) < 0) {
        log_error("ipc_client_ui_warning failed");
        rc = 1;
    }
    usleep(3 * 1000 * 1000);
    log_info("UI get current screen");
    // UI get/set current screen
    curr_screen_t current_screen = curr_screen_t_SCREEN_MAINMENU;
    if (ipc_client_ui_get_current_screen(&client, &current_screen) == 0) {
        log_info("current screen: %d", current_screen);
    } else {
        log_error("ipc_client_ui_get_current_screen failed");
        rc = 1;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "当前界面是 %d 马上切到设备信息页", current_screen);
    notice_with_warning(&client, "获取/切换界面", buf);

    usleep(3 * 1000 * 1000);
    log_info("UI set current screen");
    if (ipc_client_ui_set_current_screen(&client, curr_screen_t_SCREEN_SYSINFO) < 0) {
        log_error("ipc_client_ui_set_current_screen failed");
        rc = 1;
    }

    usleep(3 * 1000 * 1000);
    if (ipc_client_ui_set_current_screen(&client, curr_screen_t_SCREEN_SPINNER) < 0) {
        log_error("ipc_client_ui_set_current_screen failed");
        rc = 1;
    }

    notice_with_warning(&client, "展示给定图片", "使用扩列图模块，将展示test_force_dispimg.png");
    usleep(3 * 1000 * 1000); 
    log_info("UI force display image");
    // UI force display image
    if (have_dispimg) {
        // 先设置图片url
        if (ipc_client_ui_force_dispimg(&client, dispimg_path) < 0) {
            log_error("ipc_client_ui_force_dispimg failed");
            rc = 1;
        }
        // 然后把当前界面设置为显示图片界面
        ipc_client_ui_set_current_screen(&client, curr_screen_t_SCREEN_DISPLAYIMG);
    } else {
        log_warn("dispimg asset missing, skip ui_force_dispimg");
    }

    usleep(5 * 1000 * 1000);
    log_info("PRTS status");
    // PRTS status
    ipc_resp_prts_status_data_t prts_status = {0};
    if (ipc_client_prts_get_status(&client, &prts_status) == 0) {
        log_info("prts status: state=%d count=%d index=%d",
                 prts_status.state, prts_status.operator_count, prts_status.operator_index);
    } else {
        log_error("ipc_client_prts_get_status failed");
        rc = 1;
    }

    snprintf(buf, sizeof(buf), "PRTS中有%d个干员，当前正在播放%d号干员", prts_status.operator_count, prts_status.operator_index);
    notice_with_warning(&client, "PRTS状态", buf);

    if(prts_status.operator_count > 0) {
        log_info("PRTS get operator info:dump operator info");
        // PRTS get operator info
        ipc_prts_operator_info_data_t operator_info = {0};
        for(int i = 0; i < prts_status.operator_count; i++) {
            if (ipc_client_prts_get_operator_info(&client, i, &operator_info) == 0) {
                log_info("operator info: index=%d name=%s desc=%s icon=%s source=%d",
                            operator_info.operator_index,
                            operator_info.operator_name,
                            operator_info.description,
                            operator_info.icon_path,
                            operator_info.source);
                uuid_print(&operator_info.uuid);
            } else {
                log_error("ipc_client_prts_get_operator_info failed");
                rc = 1;
            }
        }

        notice_with_warning(&client, "最后一个干员的名字是", operator_info.operator_name);
    }

    usleep(5 * 1000 * 1000);
    log_info("Settings get/set");
    notice_with_warning(&client, "读写设置", "设置模块可以获取/设置设备设置");
    usleep(5 * 1000 * 1000);

    if(ipc_client_ui_set_current_screen(&client, curr_screen_t_SCREEN_MAINMENU) < 0) {
        log_error("ipc_client_ui_set_current_screen failed");
        rc = 1;
    }
    // Settings get/set
    ipc_settings_data_t settings = {0};
    if (ipc_client_settings_get(&client, &settings) == 0) {
        print_settings(&settings);
        for(int i = 1; i < 10; i++) {
            log_info("brightness set to : %d", i);
            settings.brightness = i;
            if (ipc_client_settings_set(&client, &settings) < 0) {
                log_error("ipc_client_settings_set failed");
                rc = 1;
            }
            usleep(1 * 1000 * 1000);
        }
    } else {
        log_error("ipc_client_settings_get failed");
        rc = 1;
    }
    usleep(1 * 1000 * 1000);

    notice_with_warning(&client, "PRTS闭锁自动切换", "PRTS闭锁自动切换后，将不再自动切换干员");

    log_info("PRTS set blocked auto switch");
    // PRTS set blocked auto switch
    // 建议在对PRTS、Mediaplayer、Overlay等模块写操作前，先闭锁PRTS自动切换，
    // 以免造成竞争冒险。
    if (ipc_client_prts_set_blocked_auto_switch(&client, true) < 0) {
        log_error("ipc_client_prts_set_blocked_auto_switch failed");
        rc = 1;
    }

    usleep(5 * 1000 * 1000);

    notice_with_warning(&client, "切换干员", "切换到随机干员");

    log_info("PRTS set operator: random operator");
    int target_index = rand() % prts_status.operator_count;
    log_info("PRTS set operator: %d", target_index);
    // PRTS set operator
    if (ipc_client_prts_set_operator(&client, target_index) < 0) {
        log_error("ipc_client_prts_set_operator failed");
        rc = 1;
    }

    usleep(5 * 1000 * 1000);
    notice_with_warning(&client, "等待切换完毕", "等待PRTS切换到目标干员");

    // 注意:set Operator 是prts一段时间去看一次的，因此要延时一下，保证PRTS开始看到请求以后
    // 再来等待状态
    // 循环获取PRTS状态，检查切换完毕后，再进行下一个操作
    while(true){
        if (ipc_client_prts_get_status(&client, &prts_status) == 0) {
            log_info("WAIT PRTS IDLE: status: state=%d count=%d index=%d",
                     prts_status.state, prts_status.operator_count, prts_status.operator_index);
            if (prts_status.state == PRTS_STATE_IDLE) {
                break;
            }
        } else {
            log_error("ipc_client_prts_get_status failed");
            rc = 1;
            break;
        }
        usleep(3 * 1000 * 1000);
    }

    if(ipc_client_ui_set_current_screen(&client, curr_screen_t_SCREEN_SPINNER) < 0) {
        log_error("ipc_client_ui_set_current_screen failed");
        rc = 1;
    }
    usleep(2 * 1000 * 1000);
    notice_with_warning(&client, "Mediaplayer", "直接设置播放视频为testvideo.mp4");
    // MediaPlayer get/set
    usleep(3 * 1000 * 1000);
    log_info("MediaPlayer get/set");
    char current_video[128] = {0};
    if (ipc_client_mediaplayer_get_video_path(&client, current_video, sizeof(current_video)) == 0) {
        log_info("current video path: %s", current_video);
        const char *next_video = NULL;
        if (current_video[0] != '\0') {
            next_video = current_video;
        } else if (have_video) {
            next_video = video_path;
        }
        if (next_video && ipc_client_mediaplayer_set_video_path(&client, next_video) < 0) {
            log_error("ipc_client_mediaplayer_set_video_path failed");
            rc = 1;
        }
    } else {
        log_error("ipc_client_mediaplayer_get_video_path failed");
        rc = 1;
    }

    usleep(3 * 1000 * 1000);
    notice_with_warning(&client, "Overlay过渡功能", "进行纯过渡排期，支持fade、swipe、move三种类型");
    log_info("Overlay schedule transition");
    char* transition_optional_image = NULL;
    if (have_transition_img) {
        transition_optional_image = transition_img_path;
    }
    usleep(4 * 1000 * 1000);

    // Overlay schedule transition
    log_info("fade transition");
    notice_with_warning(&client, "Overlay过渡功能", "进行渐变过渡，可设置背景色/过渡时长/图片");
    if (ipc_client_overlay_schedule_transition(&client, 300 * 1000, TRANSITION_TYPE_FADE, NULL, 0xFF000000) < 0) {
        log_error("ipc_client_overlay_schedule_transition failed");
        rc = 1;
    }
    usleep(4 * 1000 * 1000);

    log_info("fade transition with image(if any)");
    notice_with_warning(&client, "Overlay过渡功能", "进行渐变过渡，测试带图片过渡");
    if (ipc_client_overlay_schedule_transition(&client, 300 * 1000, TRANSITION_TYPE_FADE, transition_optional_image, 0xFF000000) < 0) {
        log_error("ipc_client_overlay_schedule_transition failed");
        rc = 1;
    }
    usleep(4 * 1000 * 1000);

    log_info("swipe transition");
    notice_with_warning(&client, "Overlay过渡功能", "进行滑动过渡，可设置背景色/过渡时长/图片");
    if (ipc_client_overlay_schedule_transition(&client, 500 * 1000, TRANSITION_TYPE_SWIPE, NULL, 0xFFFF0000) < 0) {
        log_error("ipc_client_overlay_schedule_transition failed");
        rc = 1;
    }
    usleep(4 * 1000 * 1000);

    log_info("move transition");
    notice_with_warning(&client, "Overlay过渡功能", "进行移动过渡，可设置背景色/过渡时长/图片");
    if (ipc_client_overlay_schedule_transition(&client, 300 * 1000, TRANSITION_TYPE_MOVE, NULL, 0xFF00FF00) < 0) {
        log_error("ipc_client_overlay_schedule_transition failed");
        rc = 1;
    }
    usleep(4 * 1000 * 1000);

    // Overlay schedule transition video
    log_info("overlay schedule transition with video");
    notice_with_warning(&client, "Overlay过渡功能", "进行带视频的过渡排期，可设置背景色/过渡时长/图片/切换到的视频");
    if (have_video) {
        const char *overlay_img = have_transition_img ? transition_img_path : NULL;
        if (ipc_client_overlay_schedule_transition_video(&client, video_path, 300 * 1000, TRANSITION_TYPE_FADE, overlay_img, 0xFF0000FF) < 0) {
            log_error("ipc_client_overlay_schedule_transition_video failed");
            rc = 1;
        }
    } else {
        log_warn("video asset missing, skip overlay_schedule_transition_video");
    }
    usleep(4*1000*1000);

    // 恢复PRTS自动切换
    if (ipc_client_prts_set_blocked_auto_switch(&client, false) < 0) {
        log_error("ipc_client_prts_set_blocked_auto_switch failed");
        rc = 1;
    }

    // 退出APP
    // usleep(5 * 1000 * 1000);
    // log_info("App exit: %d", EXITCODE_SRGN_CONFIG);
    // if (ipc_client_app_exit(&client, EXITCODE_SRGN_CONFIG) < 0) {
    //     log_error("ipc_client_app_exit failed");
    //     rc = 1;
    // }


    notice_with_warning(&client, "白银", "以上功能都有比较简单的C语言封装，可以方便地调用");
    usleep(4*1000*1000);
    notice_with_warning(&client, "白银", "希望你能喜欢~我要走啦，再见！");
    usleep(4*1000*1000);

    ipc_client_ui_set_current_screen(&client, curr_screen_t_SCREEN_SPINNER);

    ipc_client_destroy(&client);
    return rc;
}
