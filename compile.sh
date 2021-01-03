cd src && cmake -DCMAKE_BUILD_TYPE=Debug -B ../build && cd ..
cd build && make
cp Liburing_Server ../server
cd ..