idf_component_register(
  SRCS "probes/k380_hid_probe.cpp"
  INCLUDE_DIRS "."
  REQUIRES bt esp_hid nvs_flash
)
