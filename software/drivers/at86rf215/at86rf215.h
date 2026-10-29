#ifndef ZEPHYR_DRIVERS_AT86RF215_H
#define ZEPHYR_DRIVERS_AT86RF215_H

struct rf215_api {
    //api from tutorial might need to remove later depending on if we want to have a public function get the state of each transceiver 
    int(*get)(const struct device *dev, uint8_t *state);
    //api we might need
    int(*validate)(const struct device *dev, uint8_t *rtn);
    int(*set_param)(const struct device *dev, const struct rf215_param);
    int(*
};

struct rf215_config {
    struct gpio_dt_spec btn;
    uint8_t id;
};

#endif /*ZEPHYR_DRIVERS_AT86RF215_H
