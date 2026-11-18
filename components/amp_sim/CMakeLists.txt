idf_component_register(SRCS "sim.cpp" "json_util.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES propagation amp_config
                       PRIV_REQUIRES amp_net esp_timer esp_hw_support littlefs)
