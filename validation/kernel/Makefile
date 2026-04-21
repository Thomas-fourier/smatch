SRC = $(shell ls *.c)
obj-m += $(SRC:.c=.o)

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	make C=2 CHECK="../../smatch -p=kernel" -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	make -C $(KERNEL_SRC) M=$(PWD) clean


