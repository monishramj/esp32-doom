file(GLOB SOURCES "*.c")

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS "."
                    LDFRAGMENTS "../../linker.lf") 

idf_component_get_property(doom_lib doomgeneric COMPONENT_LIB)
target_compile_options(${doom_lib} PRIVATE "-w")