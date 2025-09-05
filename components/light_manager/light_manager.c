#include "light_manager.h"
#include <string.h>

static cob_light_t cob1;
static cob_light_t cob2;

void light_manager_init(void)
{
    cob_light_init(&cob1, 15, LEDC_CHANNEL_0);
    cob_light_init(&cob2, 16, LEDC_CHANNEL_1);

    // cob_light_breath(&cob1, 1000);
    // cob_light_wave(&cob2, 800);
}

void light_manager_control(int id, const char *cmd)
{
    printf("light_manager_control.cmd = %s\n", cmd);
    cob_light_t *target = (id == 1) ? &cob1 : &cob2;

    if (strcmp(cmd, "off") == 0)
        cob_light_off(target);
    else if (strcmp(cmd, "breath") == 0)
    {
        printf("strcmp.breath \n");
        cob_light_breath(target, 1000);
    }
    else if (strcmp(cmd, "wave") == 0)
        cob_light_wave(target, 800);
    else if (strcmp(cmd, "fire") == 0)
        cob_light_fire(target, 200);
}

void light_manager_set_duty_range(int id, int min_duty, int max_duty)
{
    reset_duty_range((id == 1) ? &cob1 : &cob2, min_duty, max_duty);
}