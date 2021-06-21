SOURCE_FILES=$(find . -type d \( -path ./build -o -path ./src/vendor \) -prune -false -o \( -name *.cpp -o -name *.tcc -o -name *.h \) | sed -E 's:\.\/src\/(.*):\1:g' | tr '\r\n' ' ')
# above will go through all of the directories, except those specified, and find all .cpp, .h and .tcc files,
# and make the output into a space separated string of paths
cd src
cmake \
  -DCMAKE_BUILD_TYPE=DEBUG \
  -DCMAKE_C_FLAGS_DEBUG="-g -O0" \
  -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \
  -B ../build \
  -DSOURCE_FILES="$SOURCE_FILES" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=1
cd ..
cd build && make
cp compile_commands.json .. # for clangd
cp webserver ../server
cd ..