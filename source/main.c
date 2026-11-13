/*
 * Flexcomm1 SPI hardware slave-select test for FRDM-RW612 (scope test, no SPI device needed).
 *
 * Question: with SCK/MISO/MOSI on GPIO7/8/9, can GPIO6 (SSEL0) and GPIO10 (SSEL1) be driven
 * separately using only the unmodified SDK?
 *
 * A 4-byte burst (55 AA 0F F0, 1 MHz, mode 0) is sent every 5 ms. Send any key on the serial
 * console to step to the next mode:
 *
 *   mode  pin mux (SDK macros)         SSEL used   expected on the scope
 *   1     FC1_SPI_SS0 only             SSEL0       GPIO6 pulses low,  GPIO10 stays high
 *   2     FC1_SPI_SS1 only             SSEL1       GPIO10 pulses low, GPIO6 stays high
 *   3     FC1_SPI_SS0 + FC1_SPI_SS1    SSEL0       GPIO6 pulses low,  GPIO10 stays high  (to verify)
 *   4     FC1_SPI_SS0 + FC1_SPI_SS1    SSEL1       GPIO10 pulses low, GPIO6 stays high  (to verify)
 *
 * Modes 1 and 2 re-mux the pins for each device. Modes 3 and 4 keep one pin mux and only change
 * the slave select number, which is the interesting case for two devices on one bus.
 *
 * Probe points: SCK GPIO7 = J2 pin 12, MOSI GPIO9 = J1 pin 2, SSEL0 GPIO6 = J2 pin 6,
 * SSEL1 GPIO10 = mikroBUS J6 CS.
 */
#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_debug_console.h"
#include "fsl_device_registers.h"
#include "fsl_io_mux.h"
#include "fsl_spi.h"
#include "fsl_usart.h"

#include "app.h"
#include "board.h"

#define TEST_SPI           SPI1
#define TEST_SPI_FC_INDEX  1U
#define TEST_SPI_HZ        1000000U
#define TEST_BURST_GAP_US  5000U
#define TEST_MODE_COUNT    4U

typedef struct
{
    bool muxSs0;         /* IO_MUX_FC1_SPI_SS0: GPIO6/7/8/9 */
    bool muxSs1;         /* IO_MUX_FC1_SPI_SS1: GPIO7/8/9/10 */
    spi_ssel_t ssel;
    const char *muxText;
    const char *expectText;
} test_mode_t;

static const test_mode_t s_modes[TEST_MODE_COUNT] = {
    {true, false, kSPI_Ssel0, "FC1_SPI_SS0 only (GPIO6/7/8/9)", "GPIO6 pulses low, GPIO10 stays high"},
    {false, true, kSPI_Ssel1, "FC1_SPI_SS1 only (GPIO7/8/9/10)", "GPIO10 pulses low, GPIO6 stays high"},
    {true, true, kSPI_Ssel0, "FC1_SPI_SS0 + FC1_SPI_SS1 (GPIO6/7/8/9/10)", "GPIO6 pulses low, GPIO10 stays high"},
    {true, true, kSPI_Ssel1, "FC1_SPI_SS0 + FC1_SPI_SS1 (GPIO6/7/8/9/10)", "GPIO10 pulses low, GPIO6 stays high"},
};

static void apply_mode(uint32_t index)
{
    const test_mode_t *mode = &s_modes[index];

    /*
     * Start from a known state: both pins plain GPIO (inputs, pulled up). IO_MUX_GPIO6 clears the
     * SS0 select bit and IO_MUX_GPIO10 clears the SS1 select bit of the FC1 mux register.
     */
    IO_MUX_SetPinMux(IO_MUX_GPIO6);
    IO_MUX_SetPinMux(IO_MUX_GPIO10);

    /* Order matters: FC1_SPI_SS0 clears the SS1 bit, FC1_SPI_SS1 does not clear the SS0 bit. */
    if (mode->muxSs0)
    {
        IO_MUX_SetPinMux(IO_MUX_FC1_SPI_SS0);
    }
    if (mode->muxSs1)
    {
        IO_MUX_SetPinMux(IO_MUX_FC1_SPI_SS1);
    }

    /* Pull-ups so an undriven select line reads high on the scope. */
    IO_MUX_SetPinConfig(6U, IO_MUX_PinConfigPullUp);
    IO_MUX_SetPinConfig(10U, IO_MUX_PinConfigPullUp);

    /* Public SDK API: the driver takes the slave select of each transfer from this structure. */
    ((spi_config_t *)SPI_GetConfig(TEST_SPI))->sselNum = mode->ssel;

    PRINTF("\r\nMode %u/%u\r\n", index + 1U, TEST_MODE_COUNT);
    PRINTF("  pin mux : %s\r\n", mode->muxText);
    PRINTF("  SSEL    : SSEL%u\r\n", (uint32_t)mode->ssel);
    PRINTF("  expect  : %s\r\n", mode->expectText);
    PRINTF("  MCI_IO_MUX FC1 = 0x%03X (0x008 = SS0 set, 0x020 = SS1 set), GPIO_GRP0[6]=%u [10]=%u\r\n",
           MCI_IO_MUX->FC1, (MCI_IO_MUX->GPIO_GRP0 >> 6) & 1U, (MCI_IO_MUX->GPIO_GRP0 >> 10) & 1U);
    PRINTF("Send any key (or Enter) for the next mode.\r\n");
}

/*
 * Non-blocking console check. Any received byte steps the mode (CR+LF counts once) and is printed,
 * so the log shows whether the terminal really sent something.
 */
static bool key_pressed(void)
{
    static uint8_t last;
    USART_Type *uart = (USART_Type *)BOARD_DEBUG_UART_BASEADDR;
    bool pressed     = false;

    while ((USART_GetStatusFlags(uart) & (uint32_t)kUSART_RxFifoNotEmptyFlag) != 0U)
    {
        uint8_t ch = USART_ReadByte(uart);

        PRINTF("[rx 0x%02X]", ch);
        if (!((ch == 0x0AU) && (last == 0x0DU))) /* LF right after CR is the same key press */
        {
            pressed = true;
        }
        last = ch;
    }
    return pressed;
}

int main(void)
{
    static const uint8_t pattern[4] = {0x55U, 0xAAU, 0x0FU, 0xF0U};
    spi_master_config_t config;
    spi_transfer_t xfer = {0};
    uint8_t rx[sizeof(pattern)];
    uint32_t mode = 0U;

    BOARD_InitHardware();

    PRINTF("\r\n*** FC1 SPI slave-select test on FRDM-RW612 ***\r\n");
    PRINTF("Burst: 55 AA 0F F0 at %u Hz, every %u ms. SSEL is released after each burst.\r\n", TEST_SPI_HZ,
           TEST_BURST_GAP_US / 1000U);
    PRINTF("Probe: SCK GPIO7 (J2-12), MOSI GPIO9 (J1-2), SSEL0 GPIO6 (J2-6), SSEL1 GPIO10 (mikroBUS CS)\r\n");

    CLOCK_AttachClk(kFFRO_to_FLEXCOMM1);
    SPI_MasterGetDefaultConfig(&config); /* mode 0, 8-bit, MSB first, all SSEL active low */
    config.baudRate_Bps = TEST_SPI_HZ;
    (void)SPI_MasterInit(TEST_SPI, &config, CLOCK_GetFlexCommClkFreq(TEST_SPI_FC_INDEX));

    apply_mode(mode);

    xfer.txData   = pattern;
    xfer.rxData   = rx;
    xfer.dataSize = sizeof(pattern);
    /* End-of-transfer flag: the selected SSEL goes high again after the last byte. */
    xfer.configFlags = (uint32_t)kSPI_FrameAssert;

    while (1)
    {
        (void)SPI_MasterTransferBlocking(TEST_SPI, &xfer);
        SDK_DelayAtLeastUs(TEST_BURST_GAP_US, SystemCoreClock);

        if (key_pressed())
        {
            mode = (mode + 1U) % TEST_MODE_COUNT;
            apply_mode(mode);
        }
    }
}
