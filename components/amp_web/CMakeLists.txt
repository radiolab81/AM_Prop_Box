idf_component_register(SRCS "web.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES amp_sim amp_config
                       PRIV_REQUIRES amp_net esp_http_server cjson esp_system)
