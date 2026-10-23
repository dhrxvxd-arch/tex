
.POSIX:

include config.mk

SRC = tex.c
OBJ = $(SRC:.c=.o)

all: options tex

options:
	@echo tex build options:
	@echo "CFLAGS  = $(CFLAGS)"
	@echo "LDFLAGS = $(LDFLAGS)"
	@echo "CC      = $(CC)"

.c.o:
	$(CC) -c $(CFLAGS) $<

$(OBJ): config.mk

tex: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS)

clean:
	rm -f tex $(OBJ)

install: tex
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f tex $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/tex

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/tex

.PHONY: all options clean install uninstall

