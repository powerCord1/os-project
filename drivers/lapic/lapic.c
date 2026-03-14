#include <cpu.h>
#include <debug.h>
#include <io.h>
#include <lapic.h>
#include <pit.h>
#include <vmm.h>

static volatile uint32_t *lapic_base;

static void lapic_calibrate_timer(uint32_t freq_hz);

void lapic_write(uint32_t reg, uint32_t val)
{
    *(volatile uint32_t *)((uintptr_t)lapic_base + reg) = val;
}

uint32_t lapic_read(uint32_t reg)
{
    return *(volatile uint32_t *)((uintptr_t)lapic_base + reg);
}

void lapic_init(void)
{
    if (!lapic_base) {
        lapic_base = (volatile uint32_t *)mmap_physical(
            NULL, (void *)0xFEE00000ULL, PAGE_SIZE,
            VMM_PRESENT | VMM_WRITE | VMM_CACHE_DISABLE);
        if (!lapic_base)
            return;
    }

    lapic_write(LAPIC_REG_SVR,
                LAPIC_SVR_ENABLE | LAPIC_SPURIOUS_VEC);

    lapic_write(LAPIC_REG_TPR, 0);

    lapic_eoi();

    log_info("LAPIC initialized, ID: %d", lapic_id());
}

void lapic_eoi(void)
{
    lapic_write(LAPIC_REG_EOI, 0);
}

uint32_t lapic_id(void)
{
    return lapic_read(LAPIC_REG_ID) >> 24;
}

void lapic_timer_init(uint32_t freq_hz)
{
    lapic_write(LAPIC_REG_LVT_TIMER, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_REG_TIMER_DCR, 0x03); // divisor 16

    lapic_calibrate_timer(freq_hz);

    log_info("LAPIC timer initialized at %d Hz", freq_hz);
}

static void lapic_calibrate_timer(uint32_t freq_hz)
{
    // Use PIT channel 2 for one-shot calibration (10ms)
    #define CALIBRATION_MS 10
    uint32_t pit_count = (PIT_BASE_FREQUENCY * CALIBRATION_MS) / 1000;

    lapic_write(LAPIC_REG_TIMER_ICR, 0xFFFFFFFF);

    // PIT channel 2, mode 0 (one-shot), lo/hi byte
    outb(PIT_CMD_PORT, 0xB0);
    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)(pit_count & 0xFF));
    outb(PIT_CHANNEL2_DATA_PORT, (uint8_t)((pit_count >> 8) & 0xFF));

    // Gate on channel 2
    uint8_t gate = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, (gate & 0xFC) | 0x01);

    // Wait for PIT to count down (bit 5 of port 0x61 goes high)
    while (!(inb(SPEAKER_PORT) & 0x20))
        ;

    uint32_t elapsed = 0xFFFFFFFF - lapic_read(LAPIC_REG_TIMER_CCR);

    // Calculate ticks per desired frequency
    uint32_t ticks_per_interval = (elapsed * (1000 / CALIBRATION_MS)) / freq_hz;

    lapic_write(LAPIC_REG_LVT_TIMER,
                LAPIC_TIMER_PERIODIC | LAPIC_TIMER_VECTOR);
    lapic_write(LAPIC_REG_TIMER_ICR, ticks_per_interval);
}

void lapic_timer_start(void)
{
    lapic_timer_init(1000);
}

void lapic_send_ipi(uint32_t target_lapic_id, uint8_t vector)
{
    lapic_write(LAPIC_REG_ICR_HIGH, target_lapic_id << 24);
    lapic_write(LAPIC_REG_ICR_LOW, LAPIC_ICR_LEVEL_ASSERT | vector);

    while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12))
        cpu_pause();
}

void lapic_send_ipi_all(uint8_t vector)
{
    lapic_write(LAPIC_REG_ICR_HIGH, 0);
    lapic_write(LAPIC_REG_ICR_LOW,
                LAPIC_ICR_DEST_ALL_EX_SELF | LAPIC_ICR_LEVEL_ASSERT | vector);

    while (lapic_read(LAPIC_REG_ICR_LOW) & (1 << 12))
        cpu_pause();
}
