idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS ""
  # The whole OS. `apps` is required rather than merely linked so that the
  # component's WHOLE_ARCHIVE reaches the final image: an app's only symbol is
  # its registration constructor and nothing calls it by name.
  REQUIRES nev_port nev_kernel nev_board nev_services nev_appkit nev_persona
           nev_bridge apps lvgl
)
