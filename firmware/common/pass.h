#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** UART line grep-able from the host: `-- PASS h02` */
void demo_pass(const char *id);

/** UART line: `-- FAIL h02 reason` */
void demo_fail(const char *id, const char *reason);

#ifdef __cplusplus
}
#endif
