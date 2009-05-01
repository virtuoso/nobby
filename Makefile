CFLAGS := -O0 -g3 -Wall
LDFLAGS := $(shell pkg-config --libs gnutls)

ifneq ($(USE_SLANG),)
CFLAGS += -DUSE_SLANG=1
LDFLAGS += -lslang
else
LDFLAGS += -lncurses
endif

SRCS := \
	cobby.c \
	lineedit.c \
	commands.c \
	main.c

OBJS := $(SRCS:.c=.o)

all: nobby

%.o: $(@:.o=.c)

clean:
	rm -f nobby $(OBJS)

nobby: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

