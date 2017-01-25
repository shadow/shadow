## build the core simulator
add_subdirectory(main)

## build the library that will intercept system calls using LD_PRELOAD
add_subdirectory(preload)

## build plug-in libraries and other components
add_subdirectory(plugin)

## build rpth and elf-loader
add_subdirectory(external)
