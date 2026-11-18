cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(am_prop_box)

# Dateisystem-Image (www, Szenarien, Antennen) in die Partition "storage" flashen
littlefs_create_partition_image(storage ${CMAKE_CURRENT_LIST_DIR}/fs FLASH_IN_PROJECT)
