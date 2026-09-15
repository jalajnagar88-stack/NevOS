# See components/nev_port/sources.cmake for why this file exists.
set(NEV_PERSONA_DIR ${CMAKE_CURRENT_LIST_DIR})

set(NEV_PERSONA_SRCS
  ${NEV_PERSONA_DIR}/src/face_presets.c
  ${NEV_PERSONA_DIR}/src/persona_core.c
  ${NEV_PERSONA_DIR}/src/face_registry.c
  ${NEV_PERSONA_DIR}/src/face_vector.c
  ${NEV_PERSONA_DIR}/src/persona.c
)

set(NEV_PERSONA_INCLUDE_DIRS ${NEV_PERSONA_DIR}/include)

set(NEV_PERSONA_TEST_SRCS
  ${NEV_PERSONA_DIR}/test/test_face_params.c
  ${NEV_PERSONA_DIR}/test/test_persona_core.c
)
