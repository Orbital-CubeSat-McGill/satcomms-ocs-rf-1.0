#ifndef ZEPHYR_DRIVERS_AT86RF215_H
#define ZEPHYR_DRIVERS_AT86RF215_H

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>

struct rf215_api {
    //api from tutorial might need to remove later depending on if we want to have a public function get the state of each transceiver 
    int(*get)(const struct device *dev, uint8_t *state);
    //api we might need
    int(*validate)(const struct device *dev, uint8_t *rtn);
    int(*set_param)(const struct device *dev, const struct rf215_param);
    int(*
};

struct rf215_config {
    struct spi_dt_spec rf215_spi;
    struct gpio_dt_spec rf215_rst;
    struct gpio_dt_spec rf215_irq;
    uint8_t id;
};

// Set struct for chip irq triggers and lock spi access
struct rf215_data {
    const struct device *dev;
    struct gpio_callback irq_cb;
    struct k_work irq_work;
    struct k_mutex bus_lock;   /* if other threads also touch the chip */
};

#endif /*ZEPHYR_DRIVERS_AT86RF215_H
