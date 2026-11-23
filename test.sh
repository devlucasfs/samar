perl build.pl cpp-no-include --zed-set-config
cp bin/libsamar.so ~/.carla/bin/
gcc test/main.c -L$HOME/.carla/bin/ -lsamar -o bin/test
