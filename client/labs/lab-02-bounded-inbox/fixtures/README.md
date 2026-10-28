# LAB-02 fixtures (`inbox-summary/v1`)

Synthetic inbox summaries for Phase A local paging. Schema: [`schema.v1.json`](schema.v1.json).

| File | Items | Notes |
|------|-------|--------|
| `inbox_8.json` | 8 | `S-fix-8` |
| `inbox_16.json` | 16 | `S-fix-16`, eager baseline |
| `inbox_64.json` | 64 | `S-fix-64`, metadata pages (page_size 16) |
| `inbox_256.json` | 256 | `S-fix-256`, metadata only in RAM — **3** LVGL card views |

Default **page_size** is 16 (experimental until product contract closes page size).

Future matrix rows (dup senders, removed item, corrupt stub) can extend this folder without changing the recycle controller API.
