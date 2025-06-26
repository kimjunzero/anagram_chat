obj-m := i2c_lcd_driver.o
KDIR := $(HOME)/project/linux
PWD  := $(shell pwd)

all:
    make -C $(KDIR) M=$(PWD) modules

clean:
    make -C $(KDIR) M=$(PWD) clean
