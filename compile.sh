cd src && cmake -DCMAKE_BUILD_TYPE=DEBUG -DCMAKE_C_FLAGS_DEBUG="-g -O0" -DCMAKE_CXX_FLAGS_DEBUG="-g -O0" -B ../build && cd ..
cd build && make
rm -rf ../server
cp Liburing_Server ../server
cd ..