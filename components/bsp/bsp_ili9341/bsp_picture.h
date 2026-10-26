// bsp_picture.h
#ifndef BSP_PICTURE_H  // 防止头文件重复包含
#define BSP_PICTURE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp_ili9341.h"

#ifdef BSP_picture_yinwu

#include <stdint.h>

extern const unsigned char gImage_yingwu[]; 

#endif

#ifdef __cplusplus
}
#endif

#endif // bsp_picture.h



