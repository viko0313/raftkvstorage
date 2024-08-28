#获取当前源文件所在目录的绝对路径
#SRC_DIR：这是变量的名称，这里用来存储获取的源文件目录的绝对路径。
#"${CMAKE_CURRENT_SOURCE_DIR}"：这是CMake预定义变量，表示当前正在处理的CMakeLists.txt文件所在的目录。
#ABSOLUTE：这是get_filename_component命令的一个参数，指定要获取路径的绝对形式。

get_filename_component(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)

aux_source_directory(${SRC_DIR} SRC_LIST)
#将这个源文件列表赋值给另一个变量，并将其缓存为内部变量，同时提供一个描述。
set(src_raftRpcPro ${SRC_LIST} CACHE INTERNAL "Desciption of the variable")

# $的作用我感觉就是取值，捕获输出或者文件