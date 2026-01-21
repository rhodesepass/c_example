#pragma once

#include <stdint.h>


// 在 DRAW_WIDTH×DRAW_HEIGHT 的 ARGB8888 缓冲区 `vaddr` 上绘制指定帧（0-based）。
// 本项目约定像素值为 0xAARRGGBB（与 fbdraw/crrefont 一致）。
void niccc_draw_frame(uint32_t* vaddr,int frame_idx);