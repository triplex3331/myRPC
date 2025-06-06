MYRPC_DEB_DIR    := $(CURDIR)/build/deb
MYSYSLOG_DEB_DIR := $(CURDIR)/../mysyslog/build/deb
REPO_DIR ?= $(CURDIR)/../myRPC-repo

.PHONY: repository
.PHONY: all clean deb

all:
	$(MAKE) -C src/mysyslog
	$(MAKE) -C src/client
	$(MAKE) -C src/server

clean:
	$(MAKE) -C src/mysyslog clean
	$(MAKE) -C src/client clean
	$(MAKE) -C src/server clean
	rm -rf build-deb
	rm -rf build
	rm -rf bin
	rm -rf repo
	rm -rf deb
	rm -rf repository
	bash clean.sh

deb:
	$(MAKE) -C src/mysyslog deb
	$(MAKE) -C src/client deb
	$(MAKE) -C src/server deb

repo:
	mkdir -p repo
	cp deb/*.deb repo/
	dpkg-scanpackages repo /dev/null | gzip -9c > repo/Packages.gz

repository:
	@echo "Создание каталога репозитория: $(REPO_DIR)"
	mkdir -p $(REPO_DIR)

	@echo "Копирование .deb пакетов из myRPC и mysyslog..."
	cp -v $(MYRPC_DEB_DIR)/*.deb $(REPO_DIR)/
	cp -v $(MYSYSLOG_DEB_DIR)/*.deb $(REPO_DIR)/

	@echo "Генерация Packages.gz"
	cd $(REPO_DIR) && dpkg-scanpackages . /dev/null | gzip -9 > Packages.gz

	@echo "Репозиторий обновлён в $(REPO_DIR)"
