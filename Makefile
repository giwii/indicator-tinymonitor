CFLAGS		:= $(shell pkg-config --libs --cflags gtk+-3.0)
CFLAGS		+= $(shell pkg-config --libs --cflags appindicator3-0.1)
CFLAGS		+= -Wall

bin_PROG	:= indicator-tinymonitor
objs		+= indicator-tinymonitor.o

$(bin_PROG):$(objs)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm *.o

