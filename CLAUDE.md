# 电子通行证 (E-Pass) 开发指南

这是基于 F1C200S 的 Linux 开发板“电子通行证”的 C 语言开发环境。

## 1. 硬件规格与显示架构
- **LCD 屏幕**: 360x640 (纵向)，60fps，使用 Linux DRM 驱动。
- **硬件图层**: 支持 4 个硬件图层 (ID: 0-3)。
  - 图层 ID 越大，显示优先级越高（覆盖在下方图层上）。
  - 图层 0 通常被终端控制台占用，建议从图层 1 开始使用。
  - **透明度**: 全系统同时只能有一个图层支持透明度。
- **按键**: 四个物理按键。
  - `KEY_1`: 上翻/增加
  - `KEY_2`: 下翻/减少
  - `KEY_3`: 进入/确定
  - `KEY_4`: 退出/取消
  - 使用 `keyinput_get_key()` 获取，返回 `-1` 表示无按键。
- **拓展排母**: 两个 2x5P 的 2.54mm 拓展排母，可定义为 GPIO/SPI/I2C/UART 接口。
  - **I2C**: `/dev/i2c-0`
  - **SPI**: `/dev/spidev1.0`
  - **UART**: `/dev/ttyS1` (FIRST), `/dev/ttyS2` (SECOND)
  - **GPIO**: 使用 `gpiochip0`，具体引脚如 `GPIO_PE3_PIN` (131) 等，详见 `lib/epass_define.h`。

## 2. 核心库与 API 惯例
- **DRM 封装 (`lib/drm_warpper.h`)**:
  - 核心流程：`init` -> `init_layer` -> `allocate_buffer` -> `mount_layer` -> 循环 (`dequeue_free_item` -> 绘图 -> `enqueue_display_item`)。
  - **重要**: `buffer_object_t` 和 `drm_warpper_queue_item_t` 必须是 `static` 或生命周期覆盖整个运行期，严禁使用局部栈变量。
  - 若图层模式设置为DRM_WARPPER_LAYER_MODE_ARGB8888，且图层透明度为255，则默认使用像素alpha。
  - 如果你不希望图层是透明的，需要把每个像素的alpha设置为0xFF，如0xFF000000为黑色。
- **绘图库 (`lib/fbdraw.h`, `lib/fbdrawttf.h`)**:
  - 推荐使用 `fbdraw_fill_rect`, 进行渲染，而非手动操作像素。
  - 当用户要求显示中文时，使用fbdraw_ttf_font_t进行字体渲染，否则使用RREFont进行渲染。
    - 使用fbdraw_ttf，底层依赖为stb_truetype，需要添加Implementation。
  - 当用户要求显示图片时，使用fbdraw_image进行图片渲染，底层依赖为stb_image，需要添加Implementation。
- **日志 (`lib/log.h`)**:
  - 使用 `log_info()`, `log_error()`, `log_debug()` 输出。

## 3. STB Implementation使用约定

推荐方式：在单独的 .c 文件中定义（如 stb_impl.c）
```c
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
```

## 4. appconfig.json 程序声明文件定义
每个应用必须在同级目录下包含 `appconfig.json`，用于启动器识别：
```json
{
    "version": 1,
    "name": "应用名称",
    "uuid":"shell调用uuidgen生成一个随机的",
    "description": "应用描述",
    "icon": "icon.png", // 程序图标，可以为空。如果为空，则使用默认图标。
    "executable": {
        "file": "可执行文件名"
    },
    "type": "fg_ext", // bg：后台运行。fg：前台运行，fg_ext：前台运行，且只可以通过拓展名启动，拒绝从菜单直接启动。
    "extensions": [".txt", ".log"] // 关联的文件后缀名。
}
```

## 5. 程序启动与生命周期
- **启动方式**: 启动器会切换到程序所在目录，然后执行 `./executable_file`。
- **文件关联启动**: 当用户在启动器中选择关联文件时，启动器会执行 `./executable_file /abspath/to/file`。
- **开发建议**: 在 `main(int argc, char *argv[])` 中，通过 `argc > 1` 获取被打开文件的绝对路径。

## 6. 开发流程与规范
- **复制最小模板**: 在 `examples/` 下复制 `template` 文件夹，命名为你的程序名，如 `my_program`。
- **修改文件**: 修改appconfig.json, main.c, README.md等文件。
- **集成到examples**: 将你的程序添加到examples/CMakeLists.txt。

## 7. 若使用图形绘制，则推荐代码模式为
```c
// 阻塞等待空闲 buffer (自带 Vsync 效果)
drm_warpper_dequeue_free_item(&drm_warpper, layer_id, &curr_item);
uint32_t* vaddr = (uint32_t*)curr_item->mount.arg0;

// 使用 fbdraw 进行绘制
fbdraw_fb_t fb = { .vaddr = vaddr, .width = 360, .height = 640 };
fbdraw_fill_rect(&fb, &(fbdraw_rect_t){0, 0, 360, 640}, 0xFF000000); // 清屏

// 提交显示
drm_warpper_enqueue_display_item(&drm_warpper, layer_id, curr_item);
```

## 8.参考例程

* 按键输入+画图+RREFont渲染字体：examples/epniccc
* 双缓冲+ttf渲染：examples/textreader
* I2C读写：examples/i2c_test
* SPI读写：examples/spi_test
* UART读写：examples/uart_test
* GPIO读写：examples/libgpio_test