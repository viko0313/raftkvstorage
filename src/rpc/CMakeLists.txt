get_filename_component(SRC_DIR "${CMKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

aux_source_directory(${SRC_DIR} SRC_LIST)

#定义了一个库目标（通常是静态或动态库），名称为rpc_lib。它将SRC_LIST中的所有源文件以及其他由变量src_common指定的源文件作为构建这个库的源文件。
add_library(rpc_lib ${SRC_LIST} ${src_common})

#这行代码指定了rpc_lib库在链接时需要链接的库。在这个例子中，rpc_lib需要链接boost_serialization库。
target_link_libraries(rpc_lib boost_serialization)

set(src_rpc ${SRC_LIST} CACHE INTERNAL "Desciption of the variable")