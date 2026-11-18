idf_component_register(SRCS "udp_in.cpp" "gpio_ui.cpp" "console.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES amp_config amp_net amp_sim
                       PRIV_REQUIRES esp_driver_gpio esp_driver_uart lwip esp_system)
