cmake_minimum_required(VERSION 3.0...3.12)

project(clownmdemu-frontend-common LANGUAGES C)

add_library(clownmdemu-frontend-common STATIC
	"cd-reader.c"
	"cd-reader.h"
	"mixer.h"
)

add_subdirectory("clowncd" EXCLUDE_FROM_ALL)
add_subdirectory("core" EXCLUDE_FROM_ALL)

target_link_libraries(clownmdemu-frontend-common PUBLIC clowncd clownmdemu-core)
