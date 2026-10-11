#include "lab02/inbox_model.h"

#include <assert.h>
#include <string.h>

static const char first_page[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":2,"
    "\"page_offset\":0,\"items\":[{\"message_id\":\"m1\",\"sender_name\":\"A\"},"
    "{\"message_id\":\"m2\",\"sender_name\":\"B\"}]}";
static const char removed_page[] =
    "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":2,\"total_count\":1,"
    "\"page_offset\":0,\"items\":[{\"message_id\":\"m2\",\"sender_name\":\"B\"}]}";

int main(void)
{
    lab02_inbox_model_t model = {0};
    lab02_inbox_page_t page;
    assert(lab02_inbox_model_parse_v2_page(first_page, strlen(first_page), 0, &page));
    assert(page.revision == 1 && page.total_count == 2 && page.item_count == 2);
    assert(lab02_inbox_model_apply_v2_page(&model, &page));
    assert(strcmp(lab02_inbox_model_item(&model, 1)->message_id, "m2") == 0);

    assert(lab02_inbox_model_parse_v2_page(removed_page, strlen(removed_page), 0, &page));
    assert(lab02_inbox_model_apply_v2_page(&model, &page));
    assert(model.count == 1);
    assert(strcmp(lab02_inbox_model_item(&model, 0)->message_id, "m2") == 0);
    assert(model.items[1].message_id[0] == 0);

    assert(!lab02_inbox_model_parse_v2_page(first_page, strlen(first_page), 16, &page));
    const char invalid[] =
        "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":1,"
        "\"page_offset\":0,\"items\":[{\"sender_name\":\"missing id\"}]}";
    assert(!lab02_inbox_model_parse_v2_page(invalid, strlen(invalid), 0, &page));
    return 0;
}
