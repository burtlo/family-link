#include "lab02/inbox_page_metadata.h"
#include "lab02/inbox_model.h"

void test_page_metadata_placeholder(void)
{
    lab02_inbox_model_t m = {0};
    m.count = 64;
    m.page_size = 16;
    lab02_page_metadata_t meta;
    lab02_page_metadata_build(&m, &meta);
    (void)lab02_page_metadata_page_for_index(&meta, 17);
}
