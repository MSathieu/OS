export DESTDIR:=$(CURDIR)/sysroot
export PATH:=$(PATH):$(CURDIR)/tools/toolchain/bin
export CC:=x86_64-os-gcc
export AR:=x86_64-os-ar
export AS:=x86_64-os-as
export OBJCOPY:=x86_64-os-objcopy
export CFLAGS:=-Wall -Wextra -Werror -O2 -MMD -Iinclude
ifdef UBSAN
CFLAGS+=-fsanitize=undefined
endif

build-grub: build
	-cp -n public.key loader-mb
	$(MAKE) -Cloader-mb
	mkdir -p sysroot/boot/grub
	cp grub.cfg sysroot/boot/grub
	-test -f public.key && cp grub-signed.cfg sysroot/boot/grub/grub.cfg
	tools/bin/sign sign
	mkdir -p boot
	cp -r sysroot/boot boot
	grub-mkrescue -o os.iso boot -quiet
	mkdir -p system
	cp -r sysroot/bin system
	cp -r sysroot/lib system
	cp -r sysroot/sbin system
	tools/bin/svfs
	tools/bin/lvm
	dd if=lvm.img >> os.iso
	parted os.iso -a none mkpart primary `parted -m os.iso unit s print free | tail -n 1 | cut -f2 -d:` 100%
	sfdisk os.iso --part-type 2 0xb9 -q
build:
	mkdir -p sysroot/boot sysroot/sbin sysroot/bin sysroot/lib sysroot/usr
	-cp -n public.key kernel
	$(MAKE) -Ckernel
	CFLAGS="$(CFLAGS) -fno-sanitize=all" $(MAKE) -Ctools
	$(MAKE) install-headers -Clibc
	$(MAKE) -Clibc
	$(MAKE) -Clibraries
	$(MAKE) -Cinit
	$(MAKE) -Cdrivers
	$(MAKE) -Cservers
	-cp -n public.key filesystems
	$(MAKE) -Cfilesystems
	$(MAKE) -Cloadelf
	$(MAKE) -Csh
	$(MAKE) -Ccoreutils
run-grub: build-grub
	qemu-system-x86_64 -drive file=os.iso,format=raw -cpu max -serial stdio
run-grub-nographic: build-grub
	qemu-system-x86_64 -drive file=os.iso,format=raw -cpu max -nographic
toolchain:
	$(MAKE) install-headers -Clibc
	$(MAKE) build-toolchain -Ctools
format:
	clang-format -i $(shell find -name *.c) $(shell find -name *.h)
analyze: export CFLAGS+=-I$(DESTDIR)/usr/include
analyze:
	mkdir -p sysroot/boot sysroot/sbin sysroot/bin sysroot/lib sysroot/usr
	scan-build --status-bugs --use-cc=clang $(MAKE) -Cloader-mb
	scan-build --status-bugs --use-cc=clang $(MAKE) -Ckernel
	$(MAKE) install-headers -Clibc
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Clibc
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Clibraries
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Cinit
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Cdrivers
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Cservers
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Cfilesystems
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Cloadelf
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Csh
	scan-build --status-bugs --use-cc=x86_64-os-gcc $(MAKE) -Ccoreutils
clean:
	rm -rf sysroot boot system *.img os.iso tools/bin $(filter-out $(shell find ./tools/toolchain -name *.o), $(shell find -name *.o)) $(filter-out $(shell find ./tools/toolchain -name *.d), $(shell find -name *.d)) $(wildcard */*.key) $(wildcard */*.a)
	$(MAKE) clean -Ctools
