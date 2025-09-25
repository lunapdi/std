
main: main.c std.c
	clang -std=c89 -Wall -Wextra -Wpedantic main.c std.c -ggdb
