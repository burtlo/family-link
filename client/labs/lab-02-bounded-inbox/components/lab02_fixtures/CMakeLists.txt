idf_component_register(
    SRCS "fixtures_embed.c"
    INCLUDE_DIRS "include"
    EMBED_FILES
        "../../fixtures/inbox_8.json"
        "../../fixtures/inbox_16.json"
        "../../fixtures/inbox_64.json"
        "../../fixtures/inbox_256.json"
)
