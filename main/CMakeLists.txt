idf_component_register(SRCS "main.c" "dns_server.c" "sensors.c" "web.c" "ota/ota.c" "sim.c"
                    INCLUDE_DIRS "include"
                    EMBED_FILES root.html setup.html ota/ota.html)
