/**
 * Host/IDF unit test skeleton for inbox_model (wire when Unity host runner is added).
 */
#include "lab02/inbox_model.h"

void test_inbox_model_placeholder(void)
{
    lab02_inbox_model_t m;
    const char json[] =
        "{\"schema\":\"inbox-summary/v1\",\"items\":[{\"message_id\":\"m1\",\"sender_name\":\"A\"}]}";
    (void)lab02_inbox_model_parse(json, sizeof(json) - 1, &m);
}
