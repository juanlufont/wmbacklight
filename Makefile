all:
	gcc wmbacklight.c -Wall -Wextra -lm -lXpm -lXext -lX11 -ldockapp -o wmbacklight
	strip wmbacklight
clean:
	rm wmbacklight
