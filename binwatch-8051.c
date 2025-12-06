#include <8051.h>   // SDCC: AT89C52 = семейство 8051

// ======================= ПИНЫ ИНДИКАЦИИ =======================
// P0: часы (4 бита) + AM/PM
__sbit __at(0x80) H0;     // P0.0 -> 1
__sbit __at(0x81) H1;     // P0.1 -> 2
__sbit __at(0x82) H2;     // P0.2 -> 4
__sbit __at(0x83) H3;     // P0.3 -> 8
__sbit __at(0x84) LED_AM; // P0.4 -> AM
__sbit __at(0x85) LED_PM; // P0.5 -> PM

// P1: минуты (6 бит)
__sbit __at(0x90) M0;     // P1.0 -> 1
__sbit __at(0x91) M1;     // P1.1 -> 2
__sbit __at(0x92) M2;     // P1.2 -> 4
__sbit __at(0x93) M3;     // P1.3 -> 8
__sbit __at(0x94) M4;     // P1.4 -> 16
__sbit __at(0x95) M5;     // P1.5 -> 32

// P2: секунды (6 бит)
__sbit __at(0xA0) S0;     // P2.0 -> 1
__sbit __at(0xA1) S1;     // P2.1 -> 2
__sbit __at(0xA2) S2;     // P2.2 -> 4
__sbit __at(0xA3) S3;     // P2.3 -> 8
__sbit __at(0xA4) S4;     // P2.4 -> 16
__sbit __at(0xA5) S5;     // P2.5 -> 32

// Кнопки (активный 0)
__sbit __at(0xB0) BTN_INC;  // P3.0
__sbit __at(0xB1) BTN_DEC;  // P3.1
__sbit __at(0xB2) BTN_OK;   // P3.2

// ======================= I2C на P1.6/P1.7 ======================
__sbit __at(0x96) I2C_SCL;  // P1.6
__sbit __at(0x97) I2C_SDA;  // P1.7

static void i2c_delay(void) {  // ~2-3 мкс при 12 МГц
    unsigned char i; for (i = 0;i < 15;i++) { __asm nop __endasm; }
}
static void sda_release(void) { I2C_SDA = 1; }
static void sda_low(void) { I2C_SDA = 0; }
static void scl_release(void) { I2C_SCL = 1; }
static void scl_low(void) { I2C_SCL = 0; }

static void i2c_start(void) {
    sda_release(); scl_release(); i2c_delay();
    sda_low(); i2c_delay();
    scl_low(); i2c_delay();
}
static void i2c_stop(void) {
    sda_low(); i2c_delay();
    scl_release(); i2c_delay();
    sda_release(); i2c_delay();
}
static unsigned char i2c_write(unsigned char byte) {
    unsigned char i, ack;
    for (i = 0;i < 8;i++) {
        if (byte & 0x80) sda_release(); else sda_low();
        i2c_delay();
        scl_release(); i2c_delay();
        scl_low(); i2c_delay();
        byte <<= 1;
    }
    // ACK
    sda_release(); i2c_delay();
    scl_release(); i2c_delay();
    ack = !I2C_SDA;
    scl_low(); i2c_delay();
    return ack;
}
static unsigned char i2c_read(unsigned char ack) {
    unsigned char i, data = 0;
    sda_release();
    for (i = 0;i < 8;i++) {
        data <<= 1;
        scl_release(); i2c_delay();
        if (I2C_SDA) data |= 1;
        scl_low(); i2c_delay();
    }
    // отправить ACK/NACK
    if (ack) sda_low(); else sda_release();
    i2c_delay();
    scl_release(); i2c_delay();
    scl_low(); i2c_delay();
    sda_release();
    return data;
}

// ======================= DS1307 ===============================
#define DS1307_ADDR_W  0xD0
#define DS1307_ADDR_R  0xD1

static unsigned char bcd2bin(unsigned char x) { return (x & 0x0F) + 10 * (x >> 4); }
static unsigned char bin2bcd(unsigned char x) { return (x % 10) | ((x / 10) << 4); }

static unsigned char ds1307_write_reg(unsigned char reg, unsigned char val) {
    i2c_start();
    if (!i2c_write(DS1307_ADDR_W)) { i2c_stop(); return 0; }
    if (!i2c_write(reg)) { i2c_stop(); return 0; }
    if (!i2c_write(val)) { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}
static unsigned char ds1307_read_reg(unsigned char reg, unsigned char* val) {
    i2c_start();
    if (!i2c_write(DS1307_ADDR_W)) { i2c_stop(); return 0; }
    if (!i2c_write(reg)) { i2c_stop(); return 0; }
    i2c_start();
    if (!i2c_write(DS1307_ADDR_R)) { i2c_stop(); return 0; }
    *val = i2c_read(0); // NACK
    i2c_stop();
    return 1;
}

// Запуск осциллятора: очистить бит CH (bit7 в секундах)
static void ds1307_start_osc_if_needed(void) {
    unsigned char sec;
    if (!ds1307_read_reg(0x00, &sec)) return;
    if (sec & 0x80) {
        sec &= 0x7F;
        ds1307_write_reg(0x00, sec);
    }
}

// Чтение H:M:S в 12-часовом формате
// hh: 1..12, pm: 0/1
static unsigned char ds1307_read_time(unsigned char* hh, unsigned char* mm, unsigned char* ss, unsigned char* pm) {
    unsigned char rs, rm, rh;

    if (!ds1307_read_reg(0x00, &rs)) return 0;
    if (!ds1307_read_reg(0x01, &rm)) return 0;
    if (!ds1307_read_reg(0x02, &rh)) return 0;

    rs &= 0x7F; // CH=0
    *ss = bcd2bin(rs);
    *mm = bcd2bin(rm & 0x7F);

    // нормализация минут/секунд
    if (*ss > 59) *ss = 0;
    if (*mm > 59) *mm = 0;

    if (rh & 0x40) {
        // 12h формат DS1307
        *pm = (rh & 0x20) ? 1 : 0;
        *hh = bcd2bin(rh & 0x1F); // 1..12
        if (*hh < 1 || *hh > 12) *hh = 12;
    }
    else {
        // 24h формат DS1307 -> представление 12h без операций %
        unsigned char h24 = bcd2bin(rh & 0x3F);
        if (h24 > 23) h24 = 0;

        if (h24 == 0) {
            *hh = 12; *pm = 0;
        }
        else if (h24 < 12) {
            *hh = h24; *pm = 0;
        }
        else if (h24 == 12) {
            *hh = 12; *pm = 1;
        }
        else {
            *hh = (unsigned char)(h24 - 12); *pm = 1;
        }
    }
    return 1;
}

// Запись H:M:S в 12-часовом формате, CH=0
// hh: 1..12, pm: 0/1
static unsigned char ds1307_write_time(unsigned char hh, unsigned char mm, unsigned char ss, unsigned char pm) {
    unsigned char rs = bin2bcd(ss) & 0x7F; // CH=0
    unsigned char rm = bin2bcd(mm) & 0x7F;

    // 12h: bit6=1, bit5=PM, bits4..0 = BCD(1..12)
    unsigned char rh = bin2bcd(hh) & 0x1F;
    rh |= 0x40;
    if (pm) rh |= 0x20;

    i2c_start();
    if (!i2c_write(DS1307_ADDR_W)) { i2c_stop(); return 0; }
    if (!i2c_write(0x00)) { i2c_stop(); return 0; }
    if (!i2c_write(rs)) { i2c_stop(); return 0; }
    if (!i2c_write(rm)) { i2c_stop(); return 0; }
    if (!i2c_write(rh)) { i2c_stop(); return 0; }
    i2c_stop();
    return 1;
}

// ======================= ЛОГИКА ЧАСОВ =========================
volatile unsigned char hh = 1, mm = 58, ss = 0;   // локальные копии (12h)
volatile unsigned char is_pm = 0;                 // 0=AM, 1=PM
volatile __bit have_rtc = 0;

typedef enum { ST_SHOW = 0, ST_SET_H, ST_SET_M, ST_SET_S } state_t;
volatile state_t ui = ST_SHOW;

volatile unsigned int ms = 0;
volatile __bit tick_1s = 0;

// состояния кнопок (без дебаунса)
volatile __bit prev_inc = 0, prev_dec = 0, prev_ok = 0;

// ===== Вспомогательная логика 12h =====
static void hour_inc_12(void) {
    if (hh == 11) {
        hh = 12;
        is_pm = !is_pm;
    }
    else if (hh == 12) {
        hh = 1;
    }
    else {
        hh++;
    }
}
static void hour_dec_12(void) {
    if (hh == 12) {
        hh = 11;
        is_pm = !is_pm;
    }
    else if (hh == 1) {
        hh = 12;
    }
    else {
        hh--;
    }
}

// ===== Индикация (катоды на МК: 0=ВКЛ, 1=ВЫКЛ) =====
static void set_hour_leds(unsigned char v4) {
    H0 = (v4 & 0x01) ? 0 : 1;
    H1 = (v4 & 0x02) ? 0 : 1;
    H2 = (v4 & 0x04) ? 0 : 1;
    H3 = (v4 & 0x08) ? 0 : 1;
}
static void set_min_leds(unsigned char v6) {
    M0 = (v6 & 0x01) ? 0 : 1;
    M1 = (v6 & 0x02) ? 0 : 1;
    M2 = (v6 & 0x04) ? 0 : 1;
    M3 = (v6 & 0x08) ? 0 : 1;
    M4 = (v6 & 0x10) ? 0 : 1;
    M5 = (v6 & 0x20) ? 0 : 1;
}
static void set_sec_leds(unsigned char v6) {
    S0 = (v6 & 0x01) ? 0 : 1;
    S1 = (v6 & 0x02) ? 0 : 1;
    S2 = (v6 & 0x04) ? 0 : 1;
    S3 = (v6 & 0x08) ? 0 : 1;
    S4 = (v6 & 0x10) ? 0 : 1;
    S5 = (v6 & 0x20) ? 0 : 1;
}
static void set_ampm(void) {
    LED_AM = (is_pm == 0) ? 0 : 1;
    LED_PM = (is_pm == 1) ? 0 : 1;
}
static void refresh_display(void) {
    unsigned char hdisp = hh & 0x0F;  // hh уже 1..12
    set_hour_leds(hdisp);
    set_min_leds(mm);
    set_sec_leds(ss);
    set_ampm();
}

// ===== Таймер0: 1 мс тик =====
void timer0_isr(void) __interrupt(1) {
    TH0 = 0xFC; TL0 = 0x18;
    if (++ms >= 1000) { ms = 0; tick_1s = 1; }
}

// ===== Обработка кнопок (edge, без дебаунса) =====
static void apply_inc(void) {
    if (ui == ST_SET_H) {
        hour_inc_12();
    }
    else if (ui == ST_SET_M) {
        if (mm < 59) mm++;
        else mm = 0;
    }
    else if (ui == ST_SET_S) {
        if (ss < 59) ss++;
        else ss = 0;
    }
    refresh_display();
}

static void apply_dec(void) {
    if (ui == ST_SET_H) {
        hour_dec_12();
    }
    else if (ui == ST_SET_M) {
        if (mm > 0) mm--;
        else mm = 59;
    }
    else if (ui == ST_SET_S) {
        if (ss > 0) ss--;
        else ss = 59;
    }
    refresh_display();
}

static void process_keys(void) {
    __bit inc_now = !BTN_INC;
    __bit dec_now = !BTN_DEC;
    __bit ok_now = !BTN_OK;

    // короткое OK: по отпусканию
    if (!ok_now && prev_ok) {
        if (ui == ST_SHOW) ui = ST_SET_H;
        else if (ui == ST_SET_H) ui = ST_SET_M;
        else if (ui == ST_SET_M) ui = ST_SET_S;
        else ui = ST_SHOW;
    }

    if (inc_now && !prev_inc) apply_inc();
    if (dec_now && !prev_dec) apply_dec();

    prev_inc = inc_now; prev_dec = dec_now; prev_ok = ok_now;
}

// ===== Инициализация =====
static void timer0_init(void) {
    TMOD = (TMOD & ~0x0F) | 0x01; // T0 mode1
    TH0 = 0xFC; TL0 = 0x18; ET0 = 1; TR0 = 1;
}

void main(void) {
    P0 = 0xFF; P1 = 0xFF; P2 = 0xFF; P3 = 0xFF;

    I2C_SCL = 1; I2C_SDA = 1;

    timer0_init(); EA = 1;

    ds1307_start_osc_if_needed();

    {
        unsigned char rh, rm, rs, rpm;
        if (ds1307_read_time(&rh, &rm, &rs, &rpm)) {
            hh = rh; mm = rm; ss = rs; is_pm = rpm;
            have_rtc = 1;
        }
        else {
            have_rtc = 0;
        }
    }

    refresh_display();

    state_t prev_ui = ui;

    prev_inc = !BTN_INC; prev_dec = !BTN_DEC; prev_ok = !BTN_OK;

    for (;;) {
        if (tick_1s) {
            tick_1s = 0;

            if (ui == ST_SHOW) {
                if (have_rtc) {
                    unsigned char rh, rm, rs, rpm;
                    if (ds1307_read_time(&rh, &rm, &rs, &rpm)) {
                        hh = rh; mm = rm; ss = rs; is_pm = rpm;
                    }
                }
                else {
                    // внутренний ход времени
                    if (++ss >= 60) {
                        ss = 0;
                        if (++mm >= 60) {
                            mm = 0;
                            hour_inc_12();
                        }
                    }
                }
                refresh_display();
            }
        }

        process_keys();

        // Если выходим из режима SET_* в SHOW — записываем в RTC
        if (prev_ui != ui && ui == ST_SHOW && have_rtc) {
            ds1307_write_time(hh, mm, ss, is_pm);
        }
        prev_ui = ui;
    }
}
