#include <errno.h>
#include <string.h>

#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/ztest.h>

#include <ocs/at86rf215_zephyr.h>
#include <ocs/radio.h>

/* This chip model belongs only to this QEMU test executable. */
static struct {
    uint8_t memory[0x4000];
    uint8_t part_number;
    int init_error;
    int transfer_error;
    int delay_ms;
    unsigned int transfers;
    atomic_t active;
    atomic_t overlaps;
} chip;

static int chip_transfer(void *ctx, const uint8_t *tx,
                         uint8_t *rx, size_t length)
{
    ARG_UNUSED(ctx);

    if (length < 3 || length > 34) {
        return -EINVAL;
    }

    uint16_t address = ((uint16_t)(tx[0] & 0x3F) << 8) | tx[1];
    size_t count = length - 2;

    if (count > sizeof(chip.memory) - address) {
        return -EINVAL;
    }

    if (atomic_inc(&chip.active) != 0) {
        atomic_inc(&chip.overlaps);
    }

    chip.transfers++;

    if (chip.delay_ms != 0) {
        k_msleep(chip.delay_ms);
    }

    /* A failed transfer may have touched receive storage. */
    memset(rx, 0x99, length);
    int result = chip.transfer_error;

    if (result == 0) {
        if ((tx[0] & 0xC0) == 0x80) {
            memcpy(chip.memory + address, tx + 2, count);
        } else if ((tx[0] & 0xC0) == 0) {
            memcpy(rx + 2, chip.memory + address, count);
        } else {
            result = -EINVAL;
        }
    }

    atomic_dec(&chip.active);
    return result;
}

/* Replace only the physical board initialization in this executable. */
int at86rf215_zephyr_init(struct at86rf215 *radio)
{
    if (chip.init_error != 0) {
        return chip.init_error;
    }

    memset(chip.memory, 0, sizeof(chip.memory));
    chip.memory[0x000D] = chip.part_number;
    chip.memory[0x000E] = 0x03;
    radio->bus.ctx = &chip;
    radio->bus.transfer = chip_transfer;
    return 0;
}

static void before_each(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&chip, 0, sizeof(chip));
    chip.part_number = 0x34;
    zassert_ok(ocs_radio_init(), "Valid radio did not initialize");
    chip.transfers = 0;
}

ZTEST(radio_api, test_identity_and_register_access)
{
    uint8_t identity[2] = {0};
    const uint8_t expected[] = {0x34, 0x03};

    zassert_ok(ocs_radio_read(0x000D, identity, sizeof(identity)));
    zassert_mem_equal(identity, expected, sizeof(expected));

    uint8_t value = 0;
    zassert_ok(ocs_radio_write_reg(0x0100, 0xA5));
    zassert_ok(ocs_radio_read_reg(0x0100, &value));
    zassert_equal(value, 0xA5);
}

ZTEST(radio_api, test_string_and_binary_buffer_access)
{
    const char message[] = "Hello world";
    char actual[sizeof(message)] = {0};

    zassert_ok(ocs_radio_write(0x2800, message, sizeof(message) - 1));
    zassert_ok(ocs_radio_read(0x2800, actual, sizeof(message) - 1));
    zassert_mem_equal(actual, message, sizeof(message));

    const uint8_t binary[32] = {
        0x00, 0xFF, 0x55, 0xAA, 0x12, 0x34, 0x80, 0x01,
        0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
    };
    uint8_t bytes[32] = {0};

    zassert_ok(ocs_radio_write(0x2800, binary, sizeof(binary)));
    zassert_ok(ocs_radio_read(0x2800, bytes, sizeof(bytes)));
    zassert_mem_equal(bytes, binary, sizeof(binary));
}

ZTEST(radio_api, test_invalid_arguments_do_not_transfer)
{
    uint8_t bytes[33] = {0xA5};

    zassert_equal(ocs_radio_read(0, NULL, 1), -EINVAL);
    zassert_equal(ocs_radio_write(0, NULL, 1), -EINVAL);
    zassert_equal(ocs_radio_read_reg(0, NULL), -EINVAL);
    zassert_equal(ocs_radio_read(0, bytes, 0), -EINVAL);
    zassert_equal(ocs_radio_write(0, bytes, 0), -EINVAL);
    zassert_equal(ocs_radio_read(0, bytes, sizeof(bytes)), -EINVAL);
    zassert_equal(ocs_radio_write(0, bytes, sizeof(bytes)), -EINVAL);
    zassert_equal(ocs_radio_read(0x4000, bytes, 1), -EINVAL);
    zassert_equal(ocs_radio_write(0x4000, bytes, 1), -EINVAL);
    zassert_equal(ocs_radio_read(0x3FFF, bytes, 2), -EINVAL);
    zassert_equal(ocs_radio_write(0x3FFF, bytes, 2), -EINVAL);
    zassert_equal(bytes[0], 0xA5);
    zassert_equal(chip.transfers, 0, "Invalid request reached the bus");
}

ZTEST(radio_api, test_failed_init_disables_access_and_can_recover)
{
    chip.init_error = -EIO;
    zassert_equal(ocs_radio_init(), -EIO);

    uint8_t value = 0xA5;
    zassert_equal(ocs_radio_read_reg(0x000D, &value), -ENODEV);
    zassert_equal(ocs_radio_write_reg(0x0100, 1), -ENODEV);
    zassert_equal(value, 0xA5);
    zassert_equal(chip.transfers, 0);

    chip.init_error = 0;
    zassert_ok(ocs_radio_init());
    zassert_ok(ocs_radio_read_reg(0x000D, &value));
    zassert_equal(value, 0x34);
}

ZTEST(radio_api, test_wrong_chip_is_rejected)
{
    chip.part_number = 0;
    zassert_equal(ocs_radio_init(), -ENODEV);

    uint8_t value = 0xA5;
    zassert_equal(ocs_radio_read_reg(0x000D, &value), -ENODEV);
    zassert_equal(value, 0xA5);
}

ZTEST(radio_api, test_identification_transport_error_is_preserved)
{
    chip.transfer_error = -ETIMEDOUT;
    zassert_equal(ocs_radio_init(), -ETIMEDOUT);

    uint8_t value = 0xA5;
    zassert_equal(ocs_radio_read_reg(0x000D, &value), -ENODEV);
    zassert_equal(value, 0xA5);
}

ZTEST(radio_api, test_transfer_errors_preserve_read_output)
{
    chip.transfer_error = -ETIMEDOUT;
    uint8_t value = 0xA5;

    zassert_equal(ocs_radio_read_reg(0x000D, &value), -ETIMEDOUT);
    zassert_equal(value, 0xA5);
    zassert_equal(ocs_radio_write_reg(0x0100, 1), -ETIMEDOUT);

    chip.transfer_error = 0;
    zassert_ok(ocs_radio_read_reg(0x000D, &value));
    zassert_equal(value, 0x34, "Error path did not release the mutex");
}

static int isr_results[5];

static void access_from_isr(const void *argument)
{
    ARG_UNUSED(argument);
    uint8_t byte = 0;

    isr_results[0] = ocs_radio_init();
    isr_results[1] = ocs_radio_read(0x000D, &byte, 1);
    isr_results[2] = ocs_radio_write(0x0100, &byte, 1);
    isr_results[3] = ocs_radio_read_reg(0x000D, &byte);
    isr_results[4] = ocs_radio_write_reg(0x0100, 1);
}

ZTEST(radio_api, test_interrupt_calls_are_rejected)
{
    irq_offload(access_from_isr, NULL);

    for (size_t i = 0; i < ARRAY_SIZE(isr_results); i++) {
        zassert_equal(isr_results[i], -EWOULDBLOCK);
    }

    zassert_equal(chip.transfers, 0);
    uint8_t value = 0;
    zassert_ok(ocs_radio_read_reg(0x000D, &value));
    zassert_equal(value, 0x34, "ISR call damaged initialization state");
}

K_THREAD_STACK_DEFINE(worker_stack_a, 1024);
K_THREAD_STACK_DEFINE(worker_stack_b, 1024);
static struct k_thread worker_a;
static struct k_thread worker_b;
static atomic_t worker_errors;
K_SEM_DEFINE(workers_start, 0, 2);

static void read_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);
    k_sem_take(&workers_start, K_FOREVER);

    for (unsigned int i = 0; i < 8; i++) {
        uint8_t value = 0;
        if (ocs_radio_read_reg(0x000D, &value) != 0 || value != 0x34) {
            atomic_inc(&worker_errors);
        }
    }
}

ZTEST(radio_api, test_concurrent_callers_do_not_overlap)
{
    chip.delay_ms = 2;
    atomic_clear(&worker_errors);
    k_sem_reset(&workers_start);

    k_thread_create(&worker_a, worker_stack_a,
                    K_THREAD_STACK_SIZEOF(worker_stack_a), read_worker,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_create(&worker_b, worker_stack_b,
                    K_THREAD_STACK_SIZEOF(worker_stack_b), read_worker,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_sem_give(&workers_start);
    k_sem_give(&workers_start);

    int joined_a = k_thread_join(&worker_a, K_SECONDS(2));
    int joined_b = k_thread_join(&worker_b, K_SECONDS(2));
    zassert_ok(joined_a);
    zassert_ok(joined_b);
    zassert_equal(atomic_get(&worker_errors), 0);
    zassert_equal(chip.transfers, 16);
    zassert_equal(atomic_get(&chip.overlaps), 0,
                  "Two callers entered the transport at the same time");
}

ZTEST_SUITE(radio_api, NULL, NULL, before_each, NULL, NULL);
