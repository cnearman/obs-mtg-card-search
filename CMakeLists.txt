cmake_minimum_required(VERSION 3.19)

project(mtg-card-search)

 if (NOT DEFINED OBS_SRC_DIR)
        set(OBS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/obs-src")
endif ()
if (NOT DEFINED OBS_BIN_DIR)
	set(OBS_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/obs-bin")
endif ()

set(mtg-card-search_SOURCES
	mtg-card-search.cpp)

add_library(mtg-card-search MODULE
	${mtg-card-search_SOURCES})

include_directories(
	${CMAKE_CURRENT_BINARY_DIR}
	"${CMAKE_SOURCE_DIR}/UI/obs-frontend-api"
)

find_package(cpr)


target_link_libraries(mtg-card-search 
	libobs
	Qt::Widgets
	obs-frontend-api
	cpr)

set_target_properties(mtg-card-search PROPERTIES FOLDER "plugins")

#This command adds in the locale information if used. Requires a folder 'data', potentially a subfolder
#'locale' with 1 or more .ini files describing the locale info, but haven't tested.
install_obs_plugin_with_data(mtg-card-search data)
