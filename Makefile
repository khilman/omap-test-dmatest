KERNELDIR=${HOME}/work/kernel/omap/pm

#ARCH=arm
#CROSS_COMPILE=/opt/fthree070310_0702771/montavista/foundation/devkit/arm/v5t_le/bin/arm_v5t_le-
#BUILD=${KERNELDIR}/../BUILD/2.6.21-rc4-arm-omap2430-default
#MAKE_OPTS=ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} O=${BUILD}

obj-m = dmatest.o
dmatest-objs = main.o

all:
	make ${MAKE_OPTS} -C $(KERNELDIR) SUBDIRS=$(PWD) modules

dmatest.tgz: clean $(dmatest-objs:.o=.c) Makefile
	(cd ..; tar --exclude CVS --exclude dmatest.tgz -zcvf dmatest/dmatest.tgz ./dmatest)

clean:
	$(RM) -r *.o *.ko *.mod.c .*.cmd .tmp_versions *.symvers *~ dmatest.tgz


