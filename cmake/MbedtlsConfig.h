// mbedTLS options layered on the default configuration (MBEDTLS_USER_CONFIG_FILE).
#pragma once

// libcurl shares one entropy source and DRBG across connections; without these, handshakes on
// concurrent threads corrupt that state.
#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_PTHREAD
