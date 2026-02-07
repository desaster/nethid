/*
 * Device authentication for NetHID
 */

#include "auth.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <pico/rand.h>

static char session_token[AUTH_TOKEN_LEN + 1];
static uint8_t session_token_raw[AUTH_TOKEN_RAW_LEN];
static bool token_valid = false;

static void generate_token(void)
{
    uint32_t rand_bytes[4];
    for (int i = 0; i < 4; i++) {
        rand_bytes[i] = get_rand_32();
    }

    memcpy(session_token_raw, rand_bytes, AUTH_TOKEN_RAW_LEN);

    for (int i = 0; i < AUTH_TOKEN_RAW_LEN; i++) {
        snprintf(session_token + i * 2, 3, "%02x", session_token_raw[i]);
    }
    session_token[AUTH_TOKEN_LEN] = '\0';
    token_valid = true;

    printf("Auth: Session token generated\r\n");
}

void auth_init(void)
{
    token_valid = false;
    session_token[0] = '\0';
    memset(session_token_raw, 0, AUTH_TOKEN_RAW_LEN);

    if (settings_device_has_password()) {
        generate_token();
        printf("Auth: Enabled (password configured)\r\n");
    } else {
        printf("Auth: Disabled (no password)\r\n");
    }
}

bool auth_is_enabled(void)
{
    return token_valid;
}

bool auth_validate_password(const char *password)
{
    if (password == NULL) return false;

    char stored[DEVICE_PASSWORD_MAX_LEN + 1];
    if (!settings_get_device_password(stored)) {
        return false;
    }

    // Constant-time comparison to avoid timing attacks
    size_t stored_len = strlen(stored);
    size_t input_len = strlen(password);
    size_t max_len = stored_len > input_len ? stored_len : input_len;
    if (max_len == 0) return false;

    volatile uint8_t diff = 0;
    diff |= (stored_len != input_len);
    for (size_t i = 0; i < max_len; i++) {
        uint8_t a = i < stored_len ? (uint8_t)stored[i] : 0;
        uint8_t b = i < input_len ? (uint8_t)password[i] : 0;
        diff |= a ^ b;
    }

    return diff == 0;
}

bool auth_validate_token(const char *token)
{
    if (token == NULL || !token_valid) return false;

    // Constant-time comparison
    volatile uint8_t diff = 0;
    for (int i = 0; i < AUTH_TOKEN_LEN; i++) {
        diff |= (uint8_t)session_token[i] ^ (uint8_t)token[i];
    }

    return diff == 0;
}

bool auth_validate_token_raw(const uint8_t *token_raw)
{
    if (token_raw == NULL || !token_valid) return false;

    // Constant-time comparison
    volatile uint8_t diff = 0;
    for (int i = 0; i < AUTH_TOKEN_RAW_LEN; i++) {
        diff |= session_token_raw[i] ^ token_raw[i];
    }

    return diff == 0;
}

const char *auth_get_token(void)
{
    if (!token_valid) return NULL;
    return session_token;
}

void auth_regenerate_token(void)
{
    if (settings_device_has_password()) {
        generate_token();
    } else {
        token_valid = false;
        session_token[0] = '\0';
        memset(session_token_raw, 0, AUTH_TOKEN_RAW_LEN);
    }
}
