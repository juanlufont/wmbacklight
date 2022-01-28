all:
	gcc wmbacklight.c -Wall -Wextra -lm -lXpm -lXext -lX11 -ldockapp -o wmbacklight
	strip wmbacklight
clean:
	rm wmbacklight
build-image:
	docker image build -t debian-wmbacklight:latest .
docker:
	docker run -it --rm -v $(shell pwd):/target debian-wmbacklight:latest
