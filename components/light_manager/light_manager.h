#ifndef LIGHT_MANAGER_H
#define LIGHT_MANAGER_H

#include "cob_light.h"
#include "driver/ledc.h"

// 初始化所有 COB 灯带
void light_manager_init(void);

// 控制接口（字符串命令方式）
void light_manager_control(int id, const char *cmd);

void light_manager_set_duty_range(int id, int min_duty, int max_duty);

#endif
