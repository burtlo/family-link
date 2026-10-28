#include "lab02/inbox_model.h"

#include "cJSON.h"
#include <stdio.h>
#include <string.h>

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    snprintf(dst, cap, "%s", src);
}

bool lab02_inbox_model_parse(const char *json, size_t len, lab02_inbox_model_t *out)
{
    if (!json || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return false;
    }
    cJSON *schema = cJSON_GetObjectItem(root, "schema");
    cJSON *revision = cJSON_GetObjectItem(root, "revision");
    cJSON *page_size = cJSON_GetObjectItem(root, "page_size");
    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (schema && cJSON_IsString(schema)) {
        copy_str(out->schema, sizeof(out->schema), schema->valuestring);
    }
    if (revision && cJSON_IsString(revision)) {
        copy_str(out->revision, sizeof(out->revision), revision->valuestring);
    }
    if (page_size && cJSON_IsNumber(page_size)) {
        out->page_size = (uint16_t)page_size->valueint;
    } else {
        out->page_size = LAB02_DEFAULT_PAGE_SIZE;
    }
    if (!items || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(items);
    if (n > LAB02_INBOX_MAX_ITEMS) {
        n = LAB02_INBOX_MAX_ITEMS;
    }
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(items, i);
        if (!it) {
            continue;
        }
        lab02_inbox_item_t *dst = &out->items[i];
        cJSON *v;
        v = cJSON_GetObjectItem(it, "message_id");
        if (v && cJSON_IsString(v)) {
            copy_str(dst->message_id, sizeof(dst->message_id), v->valuestring);
        }
        v = cJSON_GetObjectItem(it, "sender_id");
        if (v && cJSON_IsString(v)) {
            copy_str(dst->sender_id, sizeof(dst->sender_id), v->valuestring);
        }
        v = cJSON_GetObjectItem(it, "sender_name");
        if (v && cJSON_IsString(v)) {
            copy_str(dst->sender_name, sizeof(dst->sender_name), v->valuestring);
        }
        v = cJSON_GetObjectItem(it, "preview");
        if (v && cJSON_IsString(v)) {
            copy_str(dst->preview, sizeof(dst->preview), v->valuestring);
        }
        v = cJSON_GetObjectItem(it, "duration_ms");
        if (v && cJSON_IsNumber(v)) {
            dst->duration_ms = (uint32_t)v->valueint;
        }
        v = cJSON_GetObjectItem(it, "seq");
        if (v && cJSON_IsNumber(v)) {
            dst->seq = (uint32_t)v->valueint;
        }
        v = cJSON_GetObjectItem(it, "unread");
        if (v && cJSON_IsBool(v)) {
            dst->unread = cJSON_IsTrue(v);
        }
    }
    out->count = (uint16_t)n;
    cJSON_Delete(root);
    return out->count > 0;
}

static bool parse_v2_item(cJSON *it, lab02_inbox_item_t *dst)
{
    if (!it || !dst) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    cJSON *v;
    v = cJSON_GetObjectItem(it, "message_id");
    if (v && cJSON_IsString(v)) {
        copy_str(dst->message_id, sizeof(dst->message_id), v->valuestring);
    }
    v = cJSON_GetObjectItem(it, "sender_user_id");
    if (v && cJSON_IsString(v)) {
        copy_str(dst->sender_id, sizeof(dst->sender_id), v->valuestring);
    }
    v = cJSON_GetObjectItem(it, "sender_id");
    if (v && cJSON_IsString(v)) {
        copy_str(dst->sender_id, sizeof(dst->sender_id), v->valuestring);
    }
    v = cJSON_GetObjectItem(it, "sender_name");
    if (v && cJSON_IsString(v)) {
        copy_str(dst->sender_name, sizeof(dst->sender_name), v->valuestring);
    }
    v = cJSON_GetObjectItem(it, "preview");
    if (v && cJSON_IsString(v)) {
        copy_str(dst->preview, sizeof(dst->preview), v->valuestring);
    }
    v = cJSON_GetObjectItem(it, "duration_ms");
    if (v && cJSON_IsNumber(v)) {
        dst->duration_ms = (uint32_t)v->valueint;
    }
    v = cJSON_GetObjectItem(it, "recipient_sequence");
    if (v && cJSON_IsNumber(v)) {
        dst->seq = (uint32_t)v->valueint;
    }
    v = cJSON_GetObjectItem(it, "seq");
    if (v && cJSON_IsNumber(v)) {
        dst->seq = (uint32_t)v->valueint;
    }
    v = cJSON_GetObjectItem(it, "read");
    if (v && cJSON_IsBool(v)) {
        dst->unread = !cJSON_IsTrue(v);
    }
    v = cJSON_GetObjectItem(it, "unread");
    if (v && cJSON_IsBool(v)) {
        dst->unread = cJSON_IsTrue(v);
    }
    return dst->message_id[0] != 0;
}

bool lab02_inbox_model_merge_v2_page(lab02_inbox_model_t *model, int page_offset, const char *json,
                                     size_t len, uint32_t *inbox_revision_out)
{
    lab02_inbox_page_t page;
    if (!lab02_inbox_model_parse_v2_page(json, len, page_offset, &page) ||
        !lab02_inbox_model_apply_v2_page(model, &page)) {
        return false;
    }
    if (inbox_revision_out) {
        *inbox_revision_out = page.revision;
    }
    return true;
}

bool lab02_inbox_model_parse_v2_page(const char *json, size_t len, int page_offset,
                                     lab02_inbox_page_t *out)
{
    if (!json || !out || page_offset < 0 || page_offset >= LAB02_INBOX_MAX_ITEMS) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return false;
    }
    cJSON *schema = cJSON_GetObjectItem(root, "schema");
    if (!schema || !cJSON_IsString(schema) ||
        strcmp(schema->valuestring, "fl.inbox-page/2") != 0) {
        cJSON_Delete(root);
        return false;
    }
    cJSON *rev = cJSON_GetObjectItem(root, "inbox_revision");
    cJSON *total = cJSON_GetObjectItem(root, "total_count");
    cJSON *offset = cJSON_GetObjectItem(root, "page_offset");
    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!rev || !cJSON_IsNumber(rev) || rev->valueint < 0 || !total ||
        !cJSON_IsNumber(total) || total->valueint < 0 ||
        total->valueint > LAB02_INBOX_MAX_ITEMS || !offset || !cJSON_IsNumber(offset) ||
        offset->valueint != page_offset || !items || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(items);
    if (n < 0 || n > LAB02_DEFAULT_PAGE_SIZE || page_offset + n > LAB02_INBOX_MAX_ITEMS) {
        cJSON_Delete(root);
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->revision = (uint32_t)rev->valueint;
    out->total_count = (uint16_t)total->valueint;
    out->item_count = (uint16_t)n;
    out->page_offset = page_offset;
    cJSON *receipt = cJSON_GetObjectItem(root, "page_receipt_id");
    cJSON *run_id = cJSON_GetObjectItem(root, "run_id");
    cJSON *completed = cJSON_GetObjectItem(root, "page_completed_ns");
    if (receipt && cJSON_IsString(receipt) && strlen(receipt->valuestring) < sizeof(out->page_receipt_id)) {
        snprintf(out->page_receipt_id, sizeof(out->page_receipt_id), "%s", receipt->valuestring);
    }
    if (run_id && cJSON_IsString(run_id) && strlen(run_id->valuestring) < sizeof(out->run_id)) {
        snprintf(out->run_id, sizeof(out->run_id), "%s", run_id->valuestring);
    }
    if (completed && cJSON_IsNumber(completed) && completed->valuedouble >= 0) {
        out->server_completed_ns = (uint64_t)completed->valuedouble;
    }
    for (int i = 0; i < n; i++) {
        if (!parse_v2_item(cJSON_GetArrayItem(items, i), &out->items[i])) {
            cJSON_Delete(root);
            return false;
        }
    }
    cJSON_Delete(root);
    return true;
}

bool lab02_inbox_model_apply_v2_page(lab02_inbox_model_t *model,
                                     const lab02_inbox_page_t *page)
{
    if (!model || !page || page->item_count > LAB02_DEFAULT_PAGE_SIZE ||
        page->page_offset < 0 || page->page_offset + page->item_count > LAB02_INBOX_MAX_ITEMS) {
        return false;
    }
    copy_str(model->schema, sizeof(model->schema), "fl.inbox-page/2");
    if (model->page_size == 0) {
        model->page_size = LAB02_DEFAULT_PAGE_SIZE;
    }
    int clear_count = (int)model->page_size;
    if (page->page_offset + clear_count > LAB02_INBOX_MAX_ITEMS) {
        clear_count = LAB02_INBOX_MAX_ITEMS - page->page_offset;
    }
    memset(&model->items[page->page_offset], 0,
           (size_t)clear_count * sizeof(model->items[0]));
    for (uint16_t i = 0; i < page->item_count; i++) {
        model->items[page->page_offset + i] = page->items[i];
    }
    model->count = page->total_count;
    return true;
}

const lab02_inbox_item_t *lab02_inbox_model_item(const lab02_inbox_model_t *m, int index)
{
    if (!m || index < 0 || index >= (int)m->count) {
        return NULL;
    }
    if (m->items[index].message_id[0] == 0) {
        return NULL;
    }
    return &m->items[index];
}
