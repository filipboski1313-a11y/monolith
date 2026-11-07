PREFIX ?= /usr/local
DESTDIR ?=

SUBDIRS = src taskgraph toolbox popup

all:
	@for dir in $(SUBDIRS); do \
		echo "==> Building $$dir..."; \
		$(MAKE) -C $$dir PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) all || exit 1; \
	done
	@echo "All Monolith native components built successfully in bin/"

clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	rm -rf bin/

install: all
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir PREFIX=$(PREFIX) DESTDIR=$(DESTDIR) install || exit 1; \
	done
	install -Dm755 scripts/monolith-ram-preloader.py $(DESTDIR)$(PREFIX)/bin/monolith-ram-preloader
	install -Dm755 scripts/monolith-updater.sh $(DESTDIR)$(PREFIX)/bin/monolith-updater
	@echo "Monolith installation complete to $(DESTDIR)$(PREFIX)/bin"

.PHONY: all clean install $(SUBDIRS)
