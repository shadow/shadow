LDPATH=/home/${USER}/.shadow/lib
#LDPATH=/home/rjansen/elf-loader
echo "using loader at ${LDPATH}"

gcc -shared -Wl,-soname,libplugin.so -fPIC -o libplugin.so plugin.c 
gcc -std=c99 -Wl,-rpath=${LDPATH},--no-as-needed,-dynamic-linker=${LDPATH}/ldso -fPIC -o test test.c -ldl

./test

rm libplugin.so test
