/*
 * Device authentication for NetHID
 *
 * Optional password-based auth with session tokens. If no password is
 * configured, auth is disabled and all requests are allowed.
 */

#ifndef NETHID_AUTH_H
#define NETHID_AUTH_H

#include <stdbool.h>
#include <stdint.h>

// Token is 16 random bytes, hex-encoded to 32 chars + null
#define AUTH_TOKEN_LEN 32
#define AUTH_TOKEN_RAW_LEN 16

// Initialize auth module (call after settings are available)
void auth_init(void);

// Check if authentication is enabled (password is configured)
bool auth_is_enabled(void);

// Validate a plaintext password
bool auth_validate_password(const char *password);

// Validate a hex token string
bool auth_validate_token(const char *token);

// Validate raw token bytes (for UDP packets)
bool auth_validate_token_raw(const uint8_t *token_raw);

// Get current session token (NULL if auth disabled)
const char *auth_get_token(void);

// Regenerate token (call after password change)
void auth_regenerate_token(void);

#endif // NETHID_AUTH_H
