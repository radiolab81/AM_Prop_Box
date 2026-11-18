idf_component_register(SRCS "net.cpp" "udp_out.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES amp_config
                       PRIV_REQUIRES esp_wifi esp_netif esp_event lwip mdns)
