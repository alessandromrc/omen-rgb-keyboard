install:
	dkms install .

uninstall:
	dkms remove omen-rgb-keyboard/1.0 --all

all: install
