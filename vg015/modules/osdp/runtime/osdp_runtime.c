#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "osdp.h"
#include "osdp_runtime.h"
#include "core/osdp_frame.h"
#include "handlers/osdp_internal_api.h"
#include "policy/osdp_policy.h"
#include "port/osdp_port.h"
#include "state/osdp_context.h"

#include "../../device/Include/K1921VG015.h"
#include "../config/config.h"
#include "../driver/w25q32/extflash_w25q32.h"
#include "../update/update_flag.h"

#define APP_FLASH_WAIT_ERASE_LOOPS ((uint32_t)2000000u)
#define APP_FLASH_WAIT_WRITE_LOOPS ((uint32_t)200000u)

typedef struct {
    uint32_t image_size;
    uint32_t crc32;
} bl_app_header_t;

static osdp_context_t g_runtime_ctx;

static uint16_t osdp_le_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t osdp_le_u32(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t osdp_build_header(uint8_t *tx, uint16_t dlen, uint8_t seq)
{
    return osdp_frame_build_header(tx, g_runtime_ctx.addr, dlen, seq);
}

static void osdp_build_crc_and_send(uint8_t *tx, uint16_t dlen)
{
    dlen = osdp_frame_append_crc(tx, dlen);
    osdp_port_send_blocking(tx, dlen);
}

static void set_led_state(uint8_t on)
{
    osdp_port_set_output(0u, on != 0u);
    osdp_port_set_output(1u, on != 0u);
    osdp_port_set_output(2u, on != 0u);
    osdp_port_set_output(3u, on != 0u);
}

static void update_flag_led_init(void)
{
    osdp_port_update_led_init();
}

static void update_flag_led_set(bool on)
{
    osdp_port_update_led_set(on);
}

static void osdp_load_addr_baud(void)
{
    config_storage_t cfg;
    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    g_runtime_ctx.addr = osdp_policy_normalize_addr(cfg.osdp_addr);
    g_runtime_ctx.baud = osdp_policy_normalize_baud(cfg.osdp_baud);
}

void set_uart_baud(uint32_t baud)
{
    osdp_port_set_uart_baud(baud);
}

void osdp_build_and_send_ack(uint8_t seq)
{
    uint8_t tx[16];
    uint16_t i = osdp_build_header(tx, OSDP_HEADER_LEN, seq);
    tx[i++] = osdp_ACK;
    osdp_build_crc_and_send(tx, i);
}

void osdp_build_and_send_nak(uint8_t seq, uint8_t reason)
{
    uint8_t tx[16];
    uint16_t i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + 1u), seq);
    tx[i++] = osdp_NAK;
    tx[i++] = reason;
    osdp_build_crc_and_send(tx, i);
}

void osdp_build_and_send_pdid(uint8_t seq)
{
    uint8_t tx[64];
    uint16_t i;
    config_storage_t cfg;

    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }

    i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + sizeof(cfg.osdp_pdid)), seq);
    tx[i++] = osdp_PDID;
    memcpy(&tx[i], cfg.osdp_pdid, sizeof(cfg.osdp_pdid));
    i += (uint16_t)sizeof(cfg.osdp_pdid);
    osdp_build_crc_and_send(tx, i);
}

void osdp_build_and_send_pdcap(uint8_t seq)
{
    uint8_t tx[64];
    uint16_t i;
    config_storage_t cfg;

    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }

    i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + sizeof(cfg.osdp_cap)), seq);
    tx[i++] = osdp_PDCAP;
    memcpy(&tx[i], cfg.osdp_cap, sizeof(cfg.osdp_cap));
    i += (uint16_t)sizeof(cfg.osdp_cap);
    osdp_build_crc_and_send(tx, i);
}

void osdp_build_and_send_com(uint8_t seq, uint8_t new_addr, uint32_t new_baud)
{
    uint8_t tx[16];
    uint16_t i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + 5u), seq);

    tx[i++] = osdp_COM;
    tx[i++] = (uint8_t)(new_addr & 0x7Fu);
    tx[i++] = (uint8_t)(new_baud & 0xFFu);
    tx[i++] = (uint8_t)((new_baud >> 8) & 0xFFu);
    tx[i++] = (uint8_t)((new_baud >> 16) & 0xFFu);
    tx[i++] = (uint8_t)((new_baud >> 24) & 0xFFu);

    osdp_build_crc_and_send(tx, i);
}

void osdp_build_and_send_istat(uint8_t seq)
{
    uint8_t tx[16];
    uint16_t i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + 1u), seq);
    uint8_t inputs = 0;

    tx[i++] = osdp_ISTATR;
    inputs |= (osdp_port_read_input(0u) ? 0u : 1u) << 0;
    inputs |= (osdp_port_read_input(1u) ? 0u : 1u) << 1;
    inputs |= (osdp_port_read_input(2u) ? 0u : 1u) << 2;
    inputs |= (osdp_port_read_input(3u) ? 0u : 1u) << 3;
    tx[i++] = inputs;

    osdp_build_crc_and_send(tx, i);
}

void osdp_build_and_send_ostat(uint8_t seq)
{
    uint8_t tx[16];
    uint16_t i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + 1u), seq);
    uint8_t outputs = 0;

    tx[i++] = osdp_OSTATR;
    outputs |= (osdp_port_read_output(0u) ? 1u : 0u) << 0;
    outputs |= (osdp_port_read_output(1u) ? 1u : 0u) << 1;
    outputs |= (osdp_port_read_output(2u) ? 1u : 0u) << 2;
    outputs |= (osdp_port_read_output(3u) ? 1u : 0u) << 3;
    tx[i++] = outputs;

    osdp_build_crc_and_send(tx, i);
}

void osdp_apply_comset(uint8_t new_addr, uint32_t new_baud)
{
    config_storage_t cfg;
    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    cfg.osdp_addr = (uint8_t)(new_addr & 0x7Fu);
    cfg.osdp_baud = new_baud;
    config_storage_save(&cfg);

    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    g_runtime_ctx.addr = osdp_policy_normalize_addr(cfg.osdp_addr);
    g_runtime_ctx.baud = osdp_policy_normalize_baud(cfg.osdp_baud);
}

void osdp_apply_factory_reset(void)
{
    config_storage_t cfg;
    config_storage_reset();
    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    g_runtime_ctx.addr = osdp_policy_normalize_addr(cfg.osdp_addr);
    g_runtime_ctx.baud = osdp_policy_normalize_baud(cfg.osdp_baud);
}

uint32_t osdp_get_baud(void)
{
    return g_runtime_ctx.baud;
}

uint8_t osdp_runtime_addr(void)
{
    return g_runtime_ctx.addr;
}

int osdp_validate_out_payload(const uint8_t *data, uint16_t data_len)
{
    uint16_t count;
    uint16_t n;

    if (!data || (data_len % 4u) != 0u || data_len < 4u) {
        return 0;
    }

    count = (uint16_t)(data_len / 4u);
    for (n = 0; n < count; ++n) {
        const uint8_t *p = &data[n * 4u];
        if (p[0] >= 4u || p[1] < 0x01u || p[1] > 0x06u) {
            return 0;
        }
    }
    return 1;
}

void osdp_handle_out(const uint8_t *data, uint16_t data_len)
{
    uint16_t count;
    uint16_t n;

    if (!osdp_validate_out_payload(data, data_len)) {
        return;
    }

    count = (uint16_t)(data_len / 4u);
    for (n = 0; n < count; ++n) {
        const uint8_t *p = &data[n * 4u];
        uint8_t idx = p[0];
        uint8_t code = p[1];
        uint16_t t = (uint16_t)p[2] | ((uint16_t)p[3] << 8);

        if (idx >= 4u) {
            continue;
        }

        switch (code) {
        case 0x01:
            g_runtime_ctx.output_ctrl[idx].permanent_state = 0;
            g_runtime_ctx.output_ctrl[idx].temp_active = 0;
            g_runtime_ctx.output_ctrl[idx].timer_ms_left = 0;
            osdp_port_set_output(idx, false);
            break;
        case 0x02:
            g_runtime_ctx.output_ctrl[idx].permanent_state = 1;
            g_runtime_ctx.output_ctrl[idx].temp_active = 0;
            g_runtime_ctx.output_ctrl[idx].timer_ms_left = 0;
            osdp_port_set_output(idx, true);
            break;
        case 0x03:
            g_runtime_ctx.output_ctrl[idx].permanent_state = 0;
            g_runtime_ctx.output_ctrl[idx].allow_completion = 1;
            if (!g_runtime_ctx.output_ctrl[idx].temp_active) {
                osdp_port_set_output(idx, false);
            }
            break;
        case 0x04:
            g_runtime_ctx.output_ctrl[idx].permanent_state = 1;
            g_runtime_ctx.output_ctrl[idx].allow_completion = 1;
            if (!g_runtime_ctx.output_ctrl[idx].temp_active) {
                osdp_port_set_output(idx, true);
            }
            break;
        case 0x05:
            g_runtime_ctx.output_ctrl[idx].temp_active = 1;
            g_runtime_ctx.output_ctrl[idx].temp_state = 1;
            g_runtime_ctx.output_ctrl[idx].allow_completion = 0;
            if (t == 0u) {
                g_runtime_ctx.output_ctrl[idx].permanent_state = 1;
                g_runtime_ctx.output_ctrl[idx].temp_active = 0;
                g_runtime_ctx.output_ctrl[idx].timer_ms_left = 0;
            } else {
                g_runtime_ctx.output_ctrl[idx].timer_ms_left = (uint32_t)t * 100u;
            }
            osdp_port_set_output(idx, true);
            break;
        case 0x06:
            g_runtime_ctx.output_ctrl[idx].temp_active = 1;
            g_runtime_ctx.output_ctrl[idx].temp_state = 0;
            g_runtime_ctx.output_ctrl[idx].allow_completion = 0;
            if (t == 0u) {
                g_runtime_ctx.output_ctrl[idx].permanent_state = 0;
                g_runtime_ctx.output_ctrl[idx].temp_active = 0;
                g_runtime_ctx.output_ctrl[idx].timer_ms_left = 0;
            } else {
                g_runtime_ctx.output_ctrl[idx].timer_ms_left = (uint32_t)t * 100u;
            }
            osdp_port_set_output(idx, false);
            break;
        default:
            break;
        }
    }
}

int osdp_validate_led_payload(const uint8_t *data, uint16_t data_len)
{
    uint16_t count;
    uint16_t rec;

    if (!data || data_len < 14u || (data_len % 14u) != 0u) {
        return 0;
    }

    count = (uint16_t)(data_len / 14u);
    for (rec = 0; rec < count; ++rec) {
        const uint8_t *p = &data[rec * 14u];
        if (!(p[0] == 0u && p[1] == 0u)) {
            return 0;
        }
        if (!(p[2] == 0x01u || p[2] == 0x02u)) {
            return 0;
        }
        if (!(p[9] == 0x00u || p[9] == 0x01u)) {
            return 0;
        }
    }
    return 1;
}

void osdp_handle_led(const uint8_t *data, uint16_t data_len)
{
    uint16_t count;
    uint16_t rec;

    if (!osdp_validate_led_payload(data, data_len)) {
        return;
    }

    count = (uint16_t)(data_len / 14u);
    for (rec = 0; rec < count; ++rec) {
        const uint8_t *p = &data[rec * 14u];
        uint8_t tcode = p[2];
        uint8_t tOn = p[3];
        uint8_t tOff = p[4];
        uint8_t tOnC = p[5];
        uint8_t tOffC = p[6];
        uint16_t timer = (uint16_t)p[7] | ((uint16_t)p[8] << 8);
        uint8_t pcode = p[9];
        uint8_t pOn = p[10];
        uint8_t pOff = p[11];
        uint8_t pOnC = p[12];
        uint8_t pOffC = p[13];

        if (!(p[0] == 0u && p[1] == 0u)) {
            continue;
        }

        if (pcode == 0x01) {
            uint32_t on = (uint32_t)pOn * 100u;
            uint32_t off = (uint32_t)pOff * 100u;
            g_runtime_ctx.led_ctrl.perm_on_ms = on;
            g_runtime_ctx.led_ctrl.perm_off_ms = off;
            g_runtime_ctx.led_ctrl.perm_on_color_is_on = (pOnC != 0u);
            g_runtime_ctx.led_ctrl.perm_off_color_is_on = (pOffC != 0u);

            if (on > 0u && off == 0u) {
                g_runtime_ctx.led_ctrl.perm_state = (pOnC != 0u) ? 1u : 0u;
            } else if (on == 0u && off > 0u) {
                g_runtime_ctx.led_ctrl.perm_state = (pOffC != 0u) ? 1u : 0u;
            }

            if (!g_runtime_ctx.led_ctrl.temp_active) {
                if (on > 0u && off > 0u) {
                    g_runtime_ctx.led_ctrl.current_state = 1u;
                    set_led_state(g_runtime_ctx.led_ctrl.perm_on_color_is_on ? 1u : 0u);
                    g_runtime_ctx.led_ctrl.phase_ms_left = on;
                } else {
                    set_led_state(g_runtime_ctx.led_ctrl.perm_state);
                }
            }
        }

        if (tcode == 0x01) {
            g_runtime_ctx.led_ctrl.temp_active = 0u;
            if (g_runtime_ctx.led_ctrl.perm_on_ms > 0u && g_runtime_ctx.led_ctrl.perm_off_ms > 0u) {
                g_runtime_ctx.led_ctrl.current_state = 1u;
                set_led_state(g_runtime_ctx.led_ctrl.perm_on_color_is_on ? 1u : 0u);
                g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.perm_on_ms;
            } else {
                g_runtime_ctx.led_ctrl.current_state = g_runtime_ctx.led_ctrl.perm_state;
                set_led_state(g_runtime_ctx.led_ctrl.perm_state);
                g_runtime_ctx.led_ctrl.phase_ms_left = 0u;
            }
        } else if (tcode == 0x02) {
            uint32_t on = (uint32_t)tOn * 100u;
            uint32_t off = (uint32_t)tOff * 100u;
            uint32_t period;
            uint32_t total;
            uint32_t cycles;

            g_runtime_ctx.led_ctrl.on_ms = on;
            g_runtime_ctx.led_ctrl.off_ms = off;
            g_runtime_ctx.led_ctrl.temp_on_color_is_on = (tOnC != 0u);
            g_runtime_ctx.led_ctrl.temp_off_color_is_on = (tOffC != 0u);

            if (timer == 0u) {
                g_runtime_ctx.led_ctrl.cycles_left = 0u;
            } else {
                period = g_runtime_ctx.led_ctrl.on_ms + g_runtime_ctx.led_ctrl.off_ms;
                if (period == 0u) {
                    period = 100u;
                }
                total = (uint32_t)timer * 100u;
                cycles = total / period;
                if (cycles == 0u) {
                    cycles = 1u;
                }
                g_runtime_ctx.led_ctrl.cycles_left = (uint16_t)((cycles > 0xFFFFu) ? 0xFFFFu : cycles);
            }

            if (g_runtime_ctx.led_ctrl.on_ms > 0u) {
                g_runtime_ctx.led_ctrl.current_state = 1u;
                set_led_state(g_runtime_ctx.led_ctrl.temp_on_color_is_on ? 1u : 0u);
                g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.on_ms;
            } else {
                g_runtime_ctx.led_ctrl.current_state = 0u;
                set_led_state(g_runtime_ctx.led_ctrl.temp_off_color_is_on ? 1u : 0u);
                g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.off_ms;
            }
            g_runtime_ctx.led_ctrl.temp_active = 1u;
        }
    }
}

int osdp_vendor_is_prs(const uint8_t *v)
{
    return v && v[0] == 'P' && v[1] == 'R' && v[2] == 'S';
}

osdp_mfg_result_t osdp_handle_mfg(const uint8_t *data, uint16_t data_len)
{
    config_storage_t cfg;

    if (!data) {
        return osdp_mfg_result_invalid;
    }
    if (!config_storage_load(&cfg)) {
        config_storage_default(&cfg);
    }
    if (data_len == 16u && osdp_vendor_is_prs(&data[0]) && data[3] == osdp_MFG_WRITE_PDID) {
        memcpy(cfg.osdp_pdid, &data[4], sizeof(cfg.osdp_pdid));
        config_storage_save(&cfg);
        return osdp_mfg_result_ok;
    }
    return osdp_mfg_result_invalid;
}

static bool app_flash_wait_ready(uint32_t loops)
{
    while (loops > 0u) {
        if ((FLASH->STAT & FLASH_STAT_BUSY_Msk) == 0u) {
            return true;
        }
        --loops;
    }
    return false;
}

static uint32_t app_flash_offs(uint32_t abs_addr)
{
    return abs_addr - MEM_FLASH_BASE;
}

static bool app_flash_erase_page(uint32_t abs_addr)
{
    FLASH->ADDR = app_flash_offs(abs_addr);
    FLASH->CMD = ((uint32_t)FLASH_CMD_KEY_Access << FLASH_CMD_KEY_Pos) | FLASH_CMD_ERSEC_Msk;
    return app_flash_wait_ready(APP_FLASH_WAIT_ERASE_LOOPS);
}

static bool app_flash_write16(uint32_t abs_addr, const uint8_t *data16)
{
    uint32_t w0;
    uint32_t w1;
    uint32_t w2;
    uint32_t w3;

    if (!data16) {
        return false;
    }

    w0 = (uint32_t)data16[0] | ((uint32_t)data16[1] << 8) | ((uint32_t)data16[2] << 16) | ((uint32_t)data16[3] << 24);
    w1 = (uint32_t)data16[4] | ((uint32_t)data16[5] << 8) | ((uint32_t)data16[6] << 16) | ((uint32_t)data16[7] << 24);
    w2 = (uint32_t)data16[8] | ((uint32_t)data16[9] << 8) | ((uint32_t)data16[10] << 16) | ((uint32_t)data16[11] << 24);
    w3 = (uint32_t)data16[12] | ((uint32_t)data16[13] << 8) | ((uint32_t)data16[14] << 16) | ((uint32_t)data16[15] << 24);

    FLASH->DATA[0].DATA = w0;
    FLASH->DATA[1].DATA = w1;
    FLASH->DATA[2].DATA = w2;
    FLASH->DATA[3].DATA = w3;

    FLASH->ADDR = app_flash_offs(abs_addr);
    FLASH->CMD = ((uint32_t)FLASH_CMD_KEY_Access << FLASH_CMD_KEY_Pos) | FLASH_CMD_WR_Msk;
    return app_flash_wait_ready(APP_FLASH_WAIT_WRITE_LOOPS);
}

static bool app_shared_flag_set_pending(uint32_t slot_base, uint32_t total_size, const bl_app_header_t *hdr)
{
    update_flag_t f;
    uint8_t b0[16];
    uint8_t b1[16];

    if (!hdr || total_size < 8u) {
        return false;
    }

    memset(&f, 0xFF, sizeof(f));
    f.magic = UPDATE_FLAG_MAGIC;
    f.version = UPDATE_FLAG_VERSION;
    f.state = UPDATE_FLAG_STATE_PENDING;
    f.slot_base = slot_base;
    f.total_size = total_size;
    f.image_size = hdr->image_size;
    f.image_crc32 = hdr->crc32;

    memcpy(b0, ((const uint8_t *)&f), 16u);
    memcpy(b1, ((const uint8_t *)&f) + 16u, 16u);

    if (!app_flash_erase_page(UPDATE_FLAG_ADDR_ABS)) {
        return false;
    }
    if (!app_flash_write16(UPDATE_FLAG_ADDR_ABS, b0)) {
        return false;
    }
    if (!app_flash_write16(UPDATE_FLAG_ADDR_ABS + 16u, b1)) {
        return false;
    }
    update_flag_led_set(true);
    return true;
}

static void osdp_build_and_send_ftstat(uint8_t seq, int16_t status_detail)
{
    uint8_t tx[32];
    uint16_t i = osdp_build_header(tx, (uint16_t)(OSDP_HEADER_LEN + 7u), seq);
    uint16_t det = (uint16_t)status_detail;

    tx[i++] = osdp_FTSTAT;
    tx[i++] = 0u;
    tx[i++] = 0u;
    tx[i++] = 0u;
    tx[i++] = (uint8_t)(det & 0xFFu);
    tx[i++] = (uint8_t)((det >> 8) & 0xFFu);
    tx[i++] = 0x80u;
    tx[i++] = 0x00u;

    osdp_build_crc_and_send(tx, i);
}

void osdp_handle_filetransfer(uint8_t seq, const uint8_t *data, uint16_t data_len)
{
    const uint16_t min_len = 11u;
    bl_app_header_t hdr;

    if (!data || data_len < min_len) {
        osdp_build_and_send_nak(seq, 0x02u);
        return;
    }

    {
        uint8_t ft_type = data[0];
        uint32_t ft_size_total = osdp_le_u32(&data[1]);
        uint32_t ft_offset = osdp_le_u32(&data[5]);
        uint16_t ft_fragment_size = osdp_le_u16(&data[9]);
        uint16_t ft_data_len = (uint16_t)(data_len - min_len);
        const uint8_t *ft_data = &data[min_len];

        if (ft_fragment_size != ft_data_len) {
            osdp_build_and_send_nak(seq, 0x02u);
            return;
        }
        if (ft_type != 1u) {
            osdp_build_and_send_ftstat(seq, (int16_t)-2);
            g_runtime_ctx.file_tx.active = 0u;
            return;
        }
        if (ft_size_total < 8u ||
            ft_size_total > EXTFLASH_FW_SLOT_SIZE ||
            ft_offset > EXTFLASH_FW_SLOT_SIZE ||
            (ft_offset + (uint32_t)ft_fragment_size) > ft_size_total) {
            osdp_build_and_send_ftstat(seq, (int16_t)-3);
            g_runtime_ctx.file_tx.active = 0u;
            return;
        }

        if (ft_offset == 0u) {
            g_runtime_ctx.file_tx.active = 1u;
            g_runtime_ctx.file_tx.ft_type = ft_type;
            g_runtime_ctx.file_tx.ft_size_total = ft_size_total;
            g_runtime_ctx.file_tx.expected_offset = 0u;
            if (!osdp_port_extflash_erase_range_4k(EXTFLASH_FW_SLOT_BASE, ft_size_total)) {
                osdp_build_and_send_ftstat(seq, (int16_t)-4);
                g_runtime_ctx.file_tx.active = 0u;
                return;
            }
        }

        if (!g_runtime_ctx.file_tx.active || ft_offset != g_runtime_ctx.file_tx.expected_offset) {
            osdp_build_and_send_ftstat(seq, (int16_t)-3);
            g_runtime_ctx.file_tx.active = 0u;
            return;
        }
        if (ft_fragment_size > 0u &&
            !osdp_port_extflash_write(EXTFLASH_FW_SLOT_BASE + ft_offset, ft_data, ft_fragment_size)) {
            osdp_build_and_send_ftstat(seq, (int16_t)-4);
            g_runtime_ctx.file_tx.active = 0u;
            return;
        }

        g_runtime_ctx.file_tx.expected_offset += (uint32_t)ft_fragment_size;
        if (g_runtime_ctx.file_tx.expected_offset == g_runtime_ctx.file_tx.ft_size_total) {
            if (osdp_port_extflash_read(EXTFLASH_FW_SLOT_BASE, (uint8_t *)&hdr, 8u) &&
                (hdr.image_size > 0u) &&
                ((uint32_t)8u + hdr.image_size == g_runtime_ctx.file_tx.ft_size_total)) {
                if (!app_shared_flag_set_pending(EXTFLASH_FW_SLOT_BASE, g_runtime_ctx.file_tx.ft_size_total, &hdr)) {
                    osdp_build_and_send_ftstat(seq, (int16_t)-4);
                    g_runtime_ctx.file_tx.active = 0u;
                    return;
                }
                osdp_build_and_send_ftstat(seq, (int16_t)1);
                g_runtime_ctx.file_tx.active = 0u;
                osdp_port_delay_ms(50u);
                osdp_port_do_reset();
                return;
            }

            osdp_build_and_send_ftstat(seq, (int16_t)-2);
            g_runtime_ctx.file_tx.active = 0u;
            return;
        }
    }

    osdp_build_and_send_ftstat(seq, (int16_t)0);
}

void osdp_runtime_tick_1ms(void)
{
    uint8_t i;

    for (i = 0; i < 4u; ++i) {
        if (g_runtime_ctx.output_ctrl[i].temp_active && g_runtime_ctx.output_ctrl[i].timer_ms_left > 0u) {
            --g_runtime_ctx.output_ctrl[i].timer_ms_left;
            if (g_runtime_ctx.output_ctrl[i].timer_ms_left == 0u) {
                g_runtime_ctx.output_ctrl[i].temp_active = 0u;
                osdp_port_set_output(i, g_runtime_ctx.output_ctrl[i].permanent_state != 0u);
            }
        }
    }

    if (g_runtime_ctx.led_ctrl.phase_ms_left > 0u) {
        --g_runtime_ctx.led_ctrl.phase_ms_left;
        return;
    }

    if (g_runtime_ctx.led_ctrl.temp_active) {
        if (g_runtime_ctx.led_ctrl.current_state) {
            g_runtime_ctx.led_ctrl.current_state = 0u;
            set_led_state(g_runtime_ctx.led_ctrl.temp_off_color_is_on ? 1u : 0u);
            g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.off_ms;
        } else {
            g_runtime_ctx.led_ctrl.current_state = 1u;
            set_led_state(g_runtime_ctx.led_ctrl.temp_on_color_is_on ? 1u : 0u);
            g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.on_ms;
            if (g_runtime_ctx.led_ctrl.cycles_left > 0u) {
                --g_runtime_ctx.led_ctrl.cycles_left;
                if (g_runtime_ctx.led_ctrl.cycles_left == 0u) {
                    g_runtime_ctx.led_ctrl.temp_active = 0u;
                    if (g_runtime_ctx.led_ctrl.perm_on_ms > 0u && g_runtime_ctx.led_ctrl.perm_off_ms > 0u) {
                        g_runtime_ctx.led_ctrl.current_state = 1u;
                        set_led_state(g_runtime_ctx.led_ctrl.perm_on_color_is_on ? 1u : 0u);
                        g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.perm_on_ms;
                    } else {
                        set_led_state(g_runtime_ctx.led_ctrl.perm_state);
                        g_runtime_ctx.led_ctrl.phase_ms_left = 0u;
                    }
                }
            }
        }
    } else if (g_runtime_ctx.led_ctrl.perm_on_ms > 0u && g_runtime_ctx.led_ctrl.perm_off_ms > 0u) {
        if (g_runtime_ctx.led_ctrl.current_state) {
            g_runtime_ctx.led_ctrl.current_state = 0u;
            set_led_state(g_runtime_ctx.led_ctrl.perm_off_color_is_on ? 1u : 0u);
            g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.perm_off_ms;
        } else {
            g_runtime_ctx.led_ctrl.current_state = 1u;
            set_led_state(g_runtime_ctx.led_ctrl.perm_on_color_is_on ? 1u : 0u);
            g_runtime_ctx.led_ctrl.phase_ms_left = g_runtime_ctx.led_ctrl.perm_on_ms;
        }
    }
}

void osdp_runtime_init(void)
{
    uint8_t i;

    osdp_load_addr_baud();
    osdp_port_extflash_init();
    update_flag_led_init();
    update_flag_led_set(osdp_port_update_flag_is_pending());

    g_runtime_ctx.file_tx.active = 0u;
    g_runtime_ctx.file_tx.ft_type = 0u;
    g_runtime_ctx.file_tx.ft_size_total = 0u;
    g_runtime_ctx.file_tx.expected_offset = 0u;

    for (i = 0; i < 4u; ++i) {
        g_runtime_ctx.output_ctrl[i].permanent_state = 0u;
        g_runtime_ctx.output_ctrl[i].temp_active = 0u;
        g_runtime_ctx.output_ctrl[i].temp_state = 0u;
        g_runtime_ctx.output_ctrl[i].timer_ms_left = 0u;
        g_runtime_ctx.output_ctrl[i].allow_completion = 0u;
    }
}
