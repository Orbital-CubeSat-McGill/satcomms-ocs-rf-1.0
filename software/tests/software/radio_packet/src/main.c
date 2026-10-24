#include <errno.h>
#include <string.h>
#include <zephyr/irq_offload.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <ocs/at86rf215_zephyr.h>
#include <ocs/radio.h>

/* Only this test executable substitutes the physical chip and IRQ adapter. */
static struct {
    uint8_t memory[0x4000];
    uint8_t rx_data[2043];
    uint16_t rx_frame_length;
    uint8_t rx_header;
    uint8_t version;
    bool rx_pending;
    bool bad_crc;
    bool suppress_ready;
    bool suppress_txfe;
    bool pll_unlocked;
    bool trx_error;
    bool underrun;
    bool fail_stop;
    bool fail_payload;
    bool payload_timeout;
    bool fail_receive_read;
    uint16_t fail_tx_address;
    uint16_t fail_rx_address;
    bool slow_payload;
    int irq_error;
    int setup_error;
    int32_t tx_last_address;
    unsigned int tx_commands;
} chip;

static const struct ocs_radio_config config = {
    .frequency_hz = 915000000U,
    .tx_power_level = 0
};
K_SEM_DEFINE(receiver_waiting, 0, 1);

int at86rf215_zephyr_reset(void)
{
    memset(chip.memory, 0, sizeof(chip.memory));
    chip.tx_last_address = -1;
    chip.memory[0x000D] = 0x34;
    chip.memory[0x000E] = chip.version;
    chip.memory[0x0102] = 2;
    chip.memory[0] = 1;
    chip.memory[1] = 1;
    return 0;
}

int at86rf215_zephyr_irq_init(void) { return chip.setup_error; }
int at86rf215_zephyr_irq_level(void)
{
    return (chip.memory[0] | chip.memory[1] |
            chip.memory[2] | chip.memory[3]) != 0;
}

static int transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t length)
{
    ARG_UNUSED(ctx);
    uint16_t address = ((uint16_t)(tx[0] & 0x3F) << 8) | tx[1];
    size_t count = length - 2;
    zassert_true(count <= 32, "Packet transfer exceeded the wired SPI limit");
    memset(rx, 0, length);

    if ((tx[0] & 0xC0) == 0x80) {
        if (address == chip.fail_tx_address && address >= 0x2800) { return -EIO; }
        if (address >= 0x2800 && chip.slow_payload) { k_msleep(2); }
        if (address == 0x2800 && chip.fail_payload) {
            return -EIO;
        }
        if (address == 0x2800 && chip.payload_timeout) {
            return -ETIMEDOUT;
        }
        /* Datasheet 6.13.4: TXFLL resets ADDRSPI, even if RAM is filled. */
        if (address <= 0x0306 && address + count > 0x0306) {
            chip.tx_last_address = -1;
            chip.memory[0x0302] &= ~1;
        }
        if (address >= 0x2800 && address + count <= 0x3000) {
            int32_t last = (int32_t)(address + count - 0x2801);
            if (last > chip.tx_last_address) { chip.tx_last_address = last; }
        }
        if (address == 0x0103) {
            uint8_t command = tx[2];
            if (command == 2 && chip.fail_stop) {
                return -EIO;
            }
            chip.memory[0x0102] = command;
            if (command == 3) {
                chip.memory[0x0121] = chip.pll_unlocked ? 0 : 2;
                if (!chip.suppress_ready) { chip.memory[0] |= 2; }
                if (chip.trx_error) { chip.memory[0] |= 0x10; }
            } else if (command == 4) {
                chip.tx_commands++;
                chip.memory[0x0102] = 3;
                uint16_t frame_length = chip.memory[0x0306] |
                                       ((chip.memory[0x0307] & 7) << 8);
                chip.memory[0x0302] =
                    (chip.underrun || chip.tx_last_address < (int32_t)frame_length - 1)
                    ? 1 : 0;
                if (!chip.suppress_txfe) { chip.memory[2] |= 0x10; }
            }
        }
        memcpy(chip.memory + address, tx + 2, count);
    } else {
        if (address == chip.fail_rx_address && address >= 0x2000) { return -EIO; }
        if (address == 0x2000 && chip.fail_receive_read) {
            memset(rx, 0x99, length);
            return -EIO;
        }
        memcpy(rx + 2, chip.memory + address, count);
        for (size_t i = 0; i < count; i++) {
            if (address + i < 4) { chip.memory[address + i] = 0; }
        }
    }
    return 0;
}

int at86rf215_zephyr_init(struct at86rf215 *radio)
{
    radio->bus.ctx = &chip;
    radio->bus.transfer = transfer;
    return at86rf215_zephyr_reset();
}

int at86rf215_zephyr_wait_irq(int32_t timeout_ms)
{
    if (chip.irq_error != 0) { return chip.irq_error; }
    if (chip.memory[0x0102] == 5 && chip.rx_pending) {
        chip.rx_pending = false;
        memcpy(chip.memory + 0x2000, chip.rx_data, sizeof(chip.rx_data));
        chip.memory[0x0304] = (uint8_t)chip.rx_frame_length;
        chip.memory[0x0305] = (uint8_t)(chip.rx_frame_length >> 8);
        chip.memory[0x036B] = chip.rx_header;
        if (!chip.bad_crc) { chip.memory[0x0301] |= 0x20; }
        else { chip.memory[0x0301] &= ~0x20; }
        chip.memory[0x0102] = 3;
        chip.memory[2] |= 2;
        return 0;
    }
    if (chip.memory[0x0102] == 5) { k_sem_give(&receiver_waiting); }
    k_msleep(timeout_ms);
    return -ETIMEDOUT;
}

static void before_each(void *fixture)
{
    ARG_UNUSED(fixture);
    memset(&chip, 0, sizeof(chip));
    chip.version = 3;
    zassert_ok(ocs_radio_init());
}

static void configure(void) { zassert_ok(ocs_radio_configure(&config)); }
static void queue_hello(void)
{
    memcpy(chip.rx_data, "Hello world", 11);
    chip.rx_frame_length = 15; /* Eleven payload bytes and four CRC bytes. */
    chip.rx_pending = true;
}

ZTEST(radio_packet, test_configuration_programs_version3_profile)
{
    configure();
    zassert_equal(chip.memory[0x0105], 0xF8);
    zassert_equal(chip.memory[0x0106], 0x8E);
    zassert_equal(chip.memory[0x0108], 0);
    zassert_equal(chip.memory[0x0113], 0x88);
    zassert_equal(chip.memory[0x010A], 0x2A);
    zassert_equal(chip.memory[0x0360], 0x16);
    zassert_equal(chip.memory[0x0365], 16);
    zassert_equal(chip.memory[0x0366], 0x09);
    zassert_equal(chip.memory[0x0367], 0x72);
    zassert_equal(chip.memory[0x0368], 0xF6);
    zassert_equal(chip.memory[0x0369], 0x72);
    zassert_equal(chip.memory[0x036A], 0);
    zassert_equal(chip.memory[0x0301], 0x55);
    zassert_equal(chip.memory[0x0102], 2);
    zassert_equal(chip.tx_commands, 0, "Configure emitted a packet");
}

ZTEST(radio_packet, test_requires_configuration_and_init_invalidates_it)
{
    const char data[] = "x";
    zassert_equal(ocs_radio_send(data, 1, 10), -EACCES);
    configure();
    zassert_ok(ocs_radio_init());
    zassert_equal(ocs_radio_send(data, 1, 10), -EACCES);
}

ZTEST(radio_packet, test_invalid_configuration_and_version)
{
    struct ocs_radio_config bad = config;
    zassert_equal(ocs_radio_configure(NULL), -EINVAL);
    bad.frequency_hz = 915000001;
    zassert_equal(ocs_radio_configure(&bad), -EINVAL);
    bad.frequency_hz = 433000000;
    zassert_equal(ocs_radio_configure(&bad), -EINVAL);
    bad = config; bad.tx_power_level = 32;
    zassert_equal(ocs_radio_configure(&bad), -EINVAL);
    chip.memory[0x000E] = 1;
    zassert_equal(ocs_radio_configure(&config), -ENOTSUP);
}

ZTEST(radio_packet, test_send_writes_payload_and_crc_inclusive_length)
{
    configure();
    memset(chip.memory + 0x2800, 0xA5, 15);
    zassert_ok(ocs_radio_send("Hello world", 11, 20));
    zassert_mem_equal(chip.memory + 0x2800, "Hello world", 11);
    const uint8_t padding[4] = {0};
    zassert_mem_equal(chip.memory + 0x280B, padding, sizeof(padding));
    zassert_equal(chip.tx_last_address, 14);
    zassert_equal(chip.memory[0x0306], 15);
    zassert_equal(chip.memory[0x0307], 0);
    zassert_equal(chip.tx_commands, 1);
    zassert_equal(chip.memory[0x0102], 2);
}

ZTEST(radio_packet, test_stale_txfe_does_not_satisfy_new_transmit)
{
    configure();
    chip.memory[2] = 0x10;
    chip.suppress_txfe = true;
    zassert_equal(ocs_radio_send("x", 1, 10), -ETIMEDOUT);
    zassert_equal(chip.memory[0x0102], 2);
}

ZTEST(radio_packet, test_missing_ready_never_starts_transmit)
{
    configure();
    chip.suppress_ready = true;
    zassert_equal(ocs_radio_send("x", 1, 10), -ETIMEDOUT);
    zassert_equal(chip.tx_commands, 0);
    zassert_equal(chip.memory[0x0102], 2);
}

ZTEST(radio_packet, test_configuration_checks_pll_and_irq_setup)
{
    chip.pll_unlocked = true;
    zassert_equal(ocs_radio_configure(&config), -EIO);
    chip.pll_unlocked = false;
    chip.setup_error = -EIO;
    zassert_equal(ocs_radio_configure(&config), -EIO);
}

ZTEST(radio_packet, test_transport_radio_and_underrun_faults)
{
    configure(); chip.fail_payload = true;
    zassert_equal(ocs_radio_send("x", 1, 10), -EIO);
    zassert_equal(ocs_radio_send("x", 1, 10), -EACCES);
    chip.fail_payload = false; configure(); chip.trx_error = true;
    zassert_equal(ocs_radio_send("x", 1, 10), -EIO);
    chip.trx_error = false; configure(); chip.underrun = true;
    zassert_equal(ocs_radio_send("x", 1, 10), -EIO);
}

ZTEST(radio_packet, test_receive_returns_payload_without_crc)
{
    configure(); queue_hello();
    char data[12] = {0}; size_t count = 99;
    zassert_ok(ocs_radio_receive(data, sizeof(data), &count, 20));
    zassert_equal(count, 11);
    zassert_mem_equal(data, "Hello world", 12);
    zassert_equal(chip.memory[0x0102], 2);
}

ZTEST(radio_packet, test_receive_timeout_preserves_output_and_recovers)
{
    configure();
    uint8_t data = 0xA5; size_t count = 99;
    zassert_equal(ocs_radio_receive(&data, 1, &count, 10), -ETIMEDOUT);
    zassert_equal(data, 0xA5); zassert_equal(count, 99);
    zassert_equal(chip.memory[0x0102], 2);
    zassert_ok(ocs_radio_send("x", 1, 20));
}

ZTEST(radio_packet, test_bad_crc_and_unexpected_fcs_type)
{
    configure(); queue_hello(); chip.bad_crc = true;
    char data[12]; memset(data, 0xA5, sizeof(data)); size_t count = 99;
    zassert_equal(ocs_radio_receive(data, sizeof(data), &count, 20), -EBADMSG);
    zassert_equal((uint8_t)data[0], 0xA5); zassert_equal(count, 99);
    queue_hello(); chip.bad_crc = false; chip.rx_header = 0x80;
    zassert_equal(ocs_radio_receive(data, sizeof(data), &count, 20), -EBADMSG);
    zassert_equal((uint8_t)data[0], 0xA5); zassert_equal(count, 99);
}

ZTEST(radio_packet, test_frame_length_and_capacity_limits)
{
    configure();
    uint8_t data[32]; memset(data, 0xA5, sizeof(data)); size_t count = 99;
    const uint16_t bad_lengths[] = {0, 4, 37, 2047, 2048};
    for (size_t i = 0; i < ARRAY_SIZE(bad_lengths); i++) {
        queue_hello(); chip.rx_frame_length = bad_lengths[i];
        zassert_equal(ocs_radio_receive(data, sizeof(data), &count, 20), -EMSGSIZE);
        zassert_equal(data[0], 0xA5); zassert_equal(count, 99);
    }
    queue_hello();
    zassert_equal(ocs_radio_receive(data, 10, &count, 20), -EMSGSIZE);
    zassert_equal(data[0], 0xA5); zassert_equal(count, 99);
}

ZTEST(radio_packet, test_failed_receive_read_preserves_output)
{
    configure(); queue_hello(); chip.fail_receive_read = true;
    uint8_t data[32]; memset(data, 0xA5, sizeof(data)); size_t count = 99;
    zassert_equal(ocs_radio_receive(data, sizeof(data), &count, 20), -EIO);
    zassert_equal(data[0], 0xA5); zassert_equal(count, 99);
}

ZTEST(radio_packet, test_cleanup_failure_invalidates_configuration)
{
    configure(); queue_hello(); chip.fail_stop = true;
    uint8_t data[32]; memset(data, 0xA5, sizeof(data)); size_t count = 99;
    zassert_equal(ocs_radio_receive(data, sizeof(data), &count, 20), -EIO);
    zassert_equal(data[0], 0xA5); zassert_equal(count, 99);
    zassert_equal(ocs_radio_send("x", 1, 10), -EACCES);
}

ZTEST(radio_packet, test_invalid_packet_arguments)
{
    configure(); static uint8_t data[2044]; size_t count = 99;
    zassert_equal(ocs_radio_send(NULL, 1, 10), -EINVAL);
    zassert_equal(ocs_radio_send(data, 0, 10), -EINVAL);
    zassert_equal(ocs_radio_send(data, sizeof(data), 10), -EINVAL);
    zassert_equal(ocs_radio_send(data, 1, 0), -EINVAL);
    zassert_equal(ocs_radio_receive(NULL, 1, &count, 10), -EINVAL);
    zassert_equal(ocs_radio_receive(data, 0, &count, 10), -EINVAL);
    zassert_equal(ocs_radio_receive(data, sizeof(data), NULL, 10), -EINVAL);
    zassert_equal(ocs_radio_receive(data, sizeof(data), &count, -1), -EINVAL);
    zassert_equal(chip.tx_commands, 0);
}

ZTEST(radio_packet, test_maximum_binary_payload)
{
    configure();
    static uint8_t data[2043];
    for (size_t i = 0; i < sizeof(data); i++) { data[i] = (uint8_t)(i * 9); }
    zassert_ok(ocs_radio_send(data, sizeof(data), 1000));
    zassert_equal(chip.memory[0x0306], 0xFF);
    zassert_equal(chip.memory[0x0307], 0x07);
    zassert_mem_equal(chip.memory + 0x2800, data, sizeof(data));
    const uint8_t padding[4] = {0};
    zassert_mem_equal(chip.memory + 0x2FFB, padding, sizeof(padding));
    zassert_equal(chip.tx_last_address, 2046, "FCS placeholders must follow the payload");
    memcpy(chip.rx_data, data, sizeof(data));
    chip.rx_frame_length = 2047; chip.rx_pending = true;
    static uint8_t received[2043]; size_t count = 0;
    zassert_ok(ocs_radio_receive(received, sizeof(received), &count, 1000));
    zassert_equal(count, 2043);
    zassert_mem_equal(received, data, sizeof(data));
}

ZTEST(radio_packet, test_payloads_cross_spi_chunk_boundaries)
{
    configure();
    uint8_t data[65], received[65];
    for (size_t i = 0; i < sizeof(data); i++) { data[i] = (uint8_t)(i + 1); }
    const size_t sizes[] = {31, 32, 33, 63, 64, 65};
    for (size_t i = 0; i < ARRAY_SIZE(sizes); i++) {
        size_t size = sizes[i], count = 0;
        zassert_ok(ocs_radio_send(data, size, 1000));
        zassert_mem_equal(chip.memory + 0x2800, data, size);
        zassert_equal(chip.tx_last_address, size + 3);
        memcpy(chip.rx_data, data, size);
        chip.rx_frame_length = size + 4; chip.rx_pending = true;
        memset(received, 0, sizeof(received));
        zassert_ok(ocs_radio_receive(received, sizeof(received), &count, 1000));
        zassert_equal(count, size);
        zassert_mem_equal(received, data, size);
    }
}

ZTEST(radio_packet, test_later_tx_chunk_failure_never_transmits)
{
    configure();
    uint8_t data[65] = {0};
    chip.fail_tx_address = 0x2820;
    zassert_equal(ocs_radio_send(data, sizeof(data), 1000), -EIO);
    zassert_equal(chip.tx_commands, 0);
    zassert_equal(ocs_radio_send("x", 1, 1000), -EACCES);
}

ZTEST(radio_packet, test_later_rx_chunk_failure_preserves_entire_output)
{
    configure();
    memset(chip.rx_data, 0x11, 65);
    chip.rx_frame_length = 69; chip.rx_pending = true;
    chip.fail_rx_address = 0x2020;
    uint8_t received[65], expected[65]; size_t count = 99;
    memset(received, 0xA5, sizeof(received));
    memset(expected, 0xA5, sizeof(expected));
    zassert_equal(ocs_radio_receive(received, sizeof(received), &count, 1000), -EIO);
    zassert_mem_equal(received, expected, sizeof(expected));
    zassert_equal(count, 99);
}

ZTEST(radio_packet, test_deadline_expires_while_loading_large_payload)
{
    configure();
    static uint8_t data[2043];
    chip.slow_payload = true;
    zassert_equal(ocs_radio_send(data, sizeof(data), 5), -ETIMEDOUT);
    zassert_equal(chip.tx_commands, 0);
}

ZTEST(radio_packet, test_irq_wait_error_invalidates_configuration)
{
    configure(); chip.irq_error = -EIO;
    uint8_t data = 0xA5; size_t count = 99;
    zassert_equal(ocs_radio_receive(&data, 1, &count, 20), -EIO);
    zassert_equal(data, 0xA5); zassert_equal(count, 99);
    zassert_equal(ocs_radio_send("x", 1, 10), -EACCES);
}

ZTEST(radio_packet, test_transport_timeout_requires_reconfiguration)
{
    configure(); chip.payload_timeout = true;
    zassert_equal(ocs_radio_send("x", 1, 20), -ETIMEDOUT);
    chip.payload_timeout = false;
    zassert_equal(ocs_radio_send("x", 1, 20), -EACCES);
}

ZTEST(radio_packet, test_raw_write_invalidates_packet_configuration)
{
    configure();
    zassert_ok(ocs_radio_write_reg(0x0301, 0));
    zassert_equal(ocs_radio_send("x", 1, 20), -EACCES);
    zassert_equal(chip.tx_commands, 0);
}

K_THREAD_STACK_DEFINE(receiver_stack, 1024);
static struct k_thread receiver_thread;
static int receiver_result;
static void receive_worker(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    uint8_t byte; size_t count;
    receiver_result = ocs_radio_receive(&byte, 1, &count, 100);
}

ZTEST(radio_packet, test_timeout_includes_mutex_contention)
{
    configure(); k_sem_reset(&receiver_waiting);
    k_thread_create(&receiver_thread, receiver_stack,
                    K_THREAD_STACK_SIZEOF(receiver_stack), receive_worker,
                    NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    int started = k_sem_take(&receiver_waiting, K_SECONDS(1));
    int result = ocs_radio_send("x", 1, 5);
    int joined = k_thread_join(&receiver_thread, K_SECONDS(2));
    zassert_ok(started); zassert_ok(joined);
    zassert_equal(result, -ETIMEDOUT);
    zassert_equal(receiver_result, -ETIMEDOUT);
    zassert_equal(chip.tx_commands, 0);
}

static int isr_results[3];
static void from_isr(const void *argument)
{
    ARG_UNUSED(argument); uint8_t byte = 0; size_t count = 0;
    isr_results[0] = ocs_radio_configure(&config);
    isr_results[1] = ocs_radio_send(&byte, 1, 10);
    isr_results[2] = ocs_radio_receive(&byte, 1, &count, 10);
}

ZTEST(radio_packet, test_packet_calls_reject_interrupt_context)
{
    irq_offload(from_isr, NULL);
    for (size_t i = 0; i < ARRAY_SIZE(isr_results); i++) {
        zassert_equal(isr_results[i], -EWOULDBLOCK);
    }
}

ZTEST_SUITE(radio_packet, NULL, NULL, before_each, NULL, NULL);
