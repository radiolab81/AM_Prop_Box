idf_component_register(SRCS "app_main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES amp_config amp_net amp_sim amp_web amp_io nvs_flash esp_event littlefs)
