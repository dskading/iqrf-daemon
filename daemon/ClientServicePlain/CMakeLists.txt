project(ClientServicePlain)

set(ClientServicePlain_SRC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/ClientServicePlain.cpp
)

set(ClientServicePlain_INC_FILES
	${CMAKE_CURRENT_SOURCE_DIR}/ClientServicePlain.h
)

add_library(${PROJECT_NAME} STATIC ${ClientServicePlain_SRC_FILES} ${ClientServicePlain_INC_FILES})
