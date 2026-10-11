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
    if (!model || !json || page_offset < 0) {
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
    copy_str(model->schema, sizeof(model->schema), "fl.inbox-page/2");
    cJSON *rev = cJSON_GetObjectItem(root, "inbox_revision");
    if (rev && cJSON_IsNumber(rev) && inbox_revision_out) {
        *inbox_revision_out = (uint32_t)rev->valueint;
    }
    cJSON *total = cJSON_GetObjectItem(root, "total_count");
    int total_count = total && cJSON_IsNumber(total) ? total->valueint : 0;
    cJSON *items = cJSON_GetObjectItem(root, "items");
    if (!items || !cJSON_IsArray(items)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(items);
    for (int i = 0; i < n; i++) {
        int idx = page_offset + i;
        if (idx >= LAB02_INBOX_MAX_ITEMS) {
            break;
        }
        if (!parse_v2_item(cJSON_GetArrayItem(items, i), &model->items[idx])) {
            continue;
        }
    }
    if (total_count > LAB02_INBOX_MAX_ITEMS) {
        total_count = LAB02_INBOX_MAX_ITEMS;
    }
    if (total_count > 0) {
        model->count = (uint16_t)total_count;
    } else if (page_offset + n > (int)model->count) {
        model->count = (uint16_t)(page_offset + n);
    }
    if (model->page_size == 0) {
        model->page_size = LAB02_DEFAULT_PAGE_SIZE;
    }
    cJSON_Delete(root);
    return model->count > 0;
}

const lab02_inbox_item_t *lab02_inbox_model_item(const lab02_inbox_model_t *m, int index)
{
    if (!m || index < 0 || index >= (int)m->count) {
        return NULL;
    }
    return &m->items[index];
}
