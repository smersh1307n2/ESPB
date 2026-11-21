set(TEST_ESPB "test.espb")

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    # Добавляем зависимость от нового компонента espb
    REQUIRES espb
    EMBED_TXTFILES ${TEST_ESPB}
)
