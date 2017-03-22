TFTP=/tftpboot
SDK_PATH=/home/phil/advantech/works/ti-processor-sdk-linux-am335x
KDIR=$(SDK_PATH)/board-support/linux-4.1.18+gitAUTOINC+bbe8cfc1da-gbbe8cfc
PWD=$(shell pwd)
CROSS_COMPILE=$(SDK_PATH)/linux-devkit/sysroots/x86_64-arago-linux/usr/bin/arm-linux-gnueabihf-
KERNEL_MAKE_OPTS=ARCH=arm
KERNEL_MAKE_OPTS+=CROSS_COMPILE=$(CROSS_COMPILE)
CC=$(CROSS_COMPILE)gcc
APP=test_gpmc_app.c
obj-m = test_gpmc.o
ccflags-y := -std=gnu99
TARGET=test_gpmc.ko

all: modules app

modules:
	$(MAKE) $(KERNEL_MAKE_OPTS) -C $(KDIR) M=$(PWD) modules
	@echo =========== copy module to tftp folder ==============
	cp $(TARGET) $(TFTP)

app:
	$(CC) -Wall -std=c99 -o $@ $(APP)
	@echo =========== copy applcation to tftp folder ==============
	cp app $(TFTP)

clean:
	rm -rf app *.o *.order *.ko *~ core* .dep* .*.d .*.cmd *.mod.c *.a *.s .*.flags .tmp_versions Module.symvers Modules.symvers rset

