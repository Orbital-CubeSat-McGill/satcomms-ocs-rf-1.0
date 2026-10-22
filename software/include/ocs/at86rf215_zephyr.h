#pragma once

#include <ocs/at86rf215.h>

int at86rf215_zephyr_init(struct at86rf215 *radio);

int at86rf215_zephyr_reset(void);

int at86rf215_zephyr_irq_init(void);

int at86rf215_zephyr_wait_irq(int32_t timeout_ms);

int at86rf215_zephyr_irq_level(void);