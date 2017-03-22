/* kernel driver must init require */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <asm/mach-types.h>
/*------------------------------------------------*/
#include <linux/device.h>		// class & device 
#include <linux/cdev.h>			// cdev
#include <linux/fs.h>			// file
#include <linux/ioport.h>		// request_region
#include <asm/io.h>				// ioremap
#include <linux/omap-gpmc.h>	// gpmc
#include <asm/uaccess.h>		// copy_to_user, copy_from_user
#include <linux/slab.h> 		// kmalloc, kfree
#include <linux/semaphore.h>	// semaphore
/*-------------------------------------------------*/
#define GPMC_CS			1
/*--------------------------- copy form driver ----------------------------*/
/* GPMC register offsets */
#define GPMC_REVISION		0x00
#define GPMC_SYSCONFIG		0x10
#define GPMC_SYSSTATUS		0x14
#define GPMC_IRQSTATUS		0x18
#define GPMC_IRQENABLE		0x1c
#define GPMC_TIMEOUT_CONTROL	0x40
#define GPMC_ERR_ADDRESS	0x44
#define GPMC_ERR_TYPE		0x48
#define GPMC_CONFIG		0x50
#define GPMC_STATUS		0x54
#define GPMC_PREFETCH_CONFIG1	0x1e0
#define GPMC_PREFETCH_CONFIG2	0x1e4
#define GPMC_PREFETCH_CONTROL	0x1ec
#define GPMC_PREFETCH_STATUS	0x1f0
#define GPMC_ECC_CONFIG		0x1f4
#define GPMC_ECC_CONTROL	0x1f8
#define GPMC_ECC_SIZE_CONFIG	0x1fc
#define GPMC_ECC1_RESULT        0x200
#define GPMC_ECC_BCH_RESULT_0   0x240   /* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_1	0x244	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_2	0x248	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_3	0x24c	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_4	0x300	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_5	0x304	/* not available on OMAP2 */
#define	GPMC_ECC_BCH_RESULT_6	0x308	/* not available on OMAP2 */
/* GPMC ECC control settings */
#define GPMC_ECC_CTRL_ECCCLEAR		0x100
#define GPMC_ECC_CTRL_ECCDISABLE	0x000
#define GPMC_ECC_CTRL_ECCREG1		0x001
#define GPMC_ECC_CTRL_ECCREG2		0x002
#define GPMC_ECC_CTRL_ECCREG3		0x003
#define GPMC_ECC_CTRL_ECCREG4		0x004
#define GPMC_ECC_CTRL_ECCREG5		0x005
#define GPMC_ECC_CTRL_ECCREG6		0x006
#define GPMC_ECC_CTRL_ECCREG7		0x007
#define GPMC_ECC_CTRL_ECCREG8		0x008
#define GPMC_ECC_CTRL_ECCREG9		0x009
#define GPMC_REVISION_MAJOR(l)      ((l >> 4) & 0xf)
#define GPMC_REVISION_MINOR(l)      (l & 0xf)
/*----------------------------------------------------------------------------------*/
/*------------ Copy form u-boot, the net on GPMC configuration ---------------------*/
// config 1 = 00000000000000000001000000000000
#define NET_GPMC_CONFIG1	0x00001000
// config 2 = 00000000000111100001111000000001
#define NET_GPMC_CONFIG2	0x001e1e01
// config 3 = 00000000000010000000001100000000
#define NET_GPMC_CONFIG3	0x00080300
// config 4 = 00011100000010010001110000001001
#define NET_GPMC_CONFIG4	0x1c091c09
// config 5 = 00000100000110000001111100011111
#define NET_GPMC_CONFIG5	0x04181f1f
// config 6 = 00000000000000000000111111001111
#define NET_GPMC_CONFIG6	0x00000FCF
// config 7 = 00000000000000000000111101101100 16Mbyte, based addr = 101100
#define NET_GPMC_CONFIG7	0x00000f6c
/*-----------------------------------------------------------------------------------*/
#define BUFFSIZE 1024
#define EXCP(fmt, args...) do{pr_err(fmt, ##args);return -1;}while(0)
//#define DEBUG_MSG
#define VIRTU_READ

static void __iomem *pseudo_base;
static unsigned long gpmc_base;

struct pseudo_dev{
	dev_t devt;
	struct cdev cdev;
	struct semaphore sem;
	struct class *class;
	struct device *device;
	unsigned char *buff;
};
static struct pseudo_dev pseudo;	// golable

static void show_gpmc_rev(void)
{
	u32 val = gpmc_read_reg(GPMC_REVISION);
	pr_info("GPMC revision %d.%d\n", 
					GPMC_REVISION_MAJOR(val), 
					GPMC_REVISION_MINOR(val));
}

#ifdef OWN_SHOW_CONF
static void gpmc_show_conf(void)
{
	pr_info("GPMC CONF1: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG1));
	pr_info("GPMC CONF2: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG2));
	pr_info("GPMC CONF3: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG3));
	pr_info("GPMC CONF4: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG4));
	pr_info("GPMC CONF5: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG5));
	pr_info("GPMC CONF6: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG6));
	pr_info("GPMC CONF7: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG7));
}
#endif

static int 
pseudo_open(struct inode *inode, struct file *filp)
{
	int res = 0;

	pr_info("<OPEN> pseudo open\n");
	try_module_get(THIS_MODULE);	// the kernel module counter++

	if(down_interruptible(&pseudo.sem))	//semaphore down
		return -ERESTARTSYS;

	pseudo.buff = kmalloc(BUFFSIZE, GFP_KERNEL);
	if(!pseudo.buff){
		pr_err("kmalloc failed!!!!\n");
		res = -ENOMEM;
		goto open_failed;
	}
	pr_info("kmalloc %d byte success!!\n", BUFFSIZE);

open_failed:
	up(&pseudo.sem);	//semaphore up
	
	return res;
}

static ssize_t 
pseudo_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	ssize_t res = 0;
	size_t len;

	if(*offp > 0) return 0;
	
	if(down_interruptible(&pseudo.sem)) return -ERESTARTSYS;
#ifdef VIRTU_READ
	strcpy(pseudo.buff, "Hello_wrold");
#else
	memset(pseudo.buff, 0, BUFFSIZE);
	/* We use the I/O memory allocate(ioremap), so that needs to use this funtion */
	ioread8_rep(pseudo_base, pseudo.buff, count);
	*(pseudo.buff+count) = '\0';
#ifdef DEBUG_MSG	
	pr_info("<READ> pseudo read data:\n");
	for(int i = 0; i < count; ++i)
		pr_info("%x ", *(pseudo.buff+i));
	pr_info("\n total %d byte\n", count);
#endif
#endif
	len = strlen((char *)pseudo.buff);	
	if(len > count) 
		len = count;
	
	/* Copy from buffer to user */ 
	if(copy_to_user(buff, pseudo.buff, len)){
		pr_err("copy_to_user ERROR!!!!\n");
		res = -EFAULT;
	}
	
	up(&pseudo.sem);
	
	return 0;
}

static ssize_t 
pseudo_write(struct file *filp, const char __user *buff, size_t count, loff_t *f_pos)
{
	ssize_t res = 0;
	size_t len = BUFFSIZE-1;
	
	if(count == 0) return 0;
	
	if(down_interruptible(&pseudo.sem)) return -ERESTARTSYS;

	if(len > count)
		len = count;
	/*
	 * Copy data from user to buffer, 
	 * so clean up the buffer
	*/
	memset(pseudo.buff, 0, BUFFSIZE);
	if(copy_from_user(pseudo.buff, buff, len)){
		pr_err("copy_from_user ERROR!!!\n");
		res = -EFAULT;
		goto write_done;
	}
	/* We use the I/O memory allocate(ioremap), so that needs to use this funtion */
	iowrite8_rep(pseudo_base, pseudo.buff, count);
#ifdef DEBUG_MSG
	pr_info("<WRITE> pseudo write data:\n");
	for(int i = 0; i < count; ++i)
		pr_info("%x ", *(pseudo.buff+i));
	pr_info("\n total %d byte\n", count);
#endif
write_done:
	up(&pseudo.sem);
	return res;
}

static int 
pseudo_release(struct inode *inode, struct file *filp)
{
	module_put(THIS_MODULE);	//kernel module counter--
	return 0;
}
/* this marco is useless, just for fun! */
#define COMMAND(NAME) .NAME=pseudo_##NAME
static const struct file_operations pseudo_fops = {
	.owner = THIS_MODULE,
	COMMAND(open),
	COMMAND(read),
	COMMAND(write),
	COMMAND(release),
};

static int __cdev_setup(void)
{
	cdev_init(&pseudo.cdev, &pseudo_fops);
	pseudo.cdev.owner = THIS_MODULE;
	pseudo.cdev.ops = &pseudo_fops;

	return cdev_add(&pseudo.cdev, pseudo.devt, 1);
}
/*
 *@brief	set the register bit to 1
 *@param	the bit wanna to setting begian...
 *
 * For example:
 * 	SET_BIT(1, 2, 3);
 *		means set bit(1), bit(2), bit(3) to 1,
 * Warning!! you cannot invoke the macro SET_BIT() without params
*/
#define NUMARGS(...)	(sizeof((int[]){__VA_ARGS__})/sizeof(int))
#define SET_BIT(...)	(adv_set_bit(NUMARGS(__VA_ARGS__), __VA_ARGS__))
static u32 adv_set_bit(int num, ...)
{
	u32 res = 0;
	va_list ap;

	va_start(ap, num);
	while(num--)
		res |= BIT(va_arg(ap, int));
	va_end(ap);

	return res;
}
/*
 * The GPMC configur register setting 
*/
static void _set_gpmc_conf(u32 *conf)
{
	/*
	 * Config 1, register sets signal control parameters per chip select.
	 *	BIT0~1	: GPMC_CLK = 11 = GPMC_FCLK(internal max 100MHz) / 4 
	 *	BIT4	: Signals timing latencies scalar factor **unknow**
	 *	BIT8~9	: muxadd data  **unkonw**
	 *	BIT10~11: Device Type = 00 =  NOR Flash like, asynchronous and synchronous devices
	 *	BIT12~13: Device size = 01 = 16bit **unknow**
	 *	BIT16~17: Wait pin0 or wait pin 1	**unkonw**
	 *	BIT21	: Write wait pin monitor	
	 *	BIT22	: read wait pin monitor
	 *	BIT23~24: device page burst length
	*/
	*conf = SET_BIT(12);
	/*
	 * Config 2, chip-select signal timing parameter configuration
	 *	BIT0~3	: assertion time form start cycle time = 001 = 1 GPMC_FCLK cycle
	 *	BIT7	: chip-select timing control signal delay
	 *	BIT8~12 : de-assertion time from start cycle time for read accesses
	 *	BIT16~20: de-assertion time from start cycle time for write accesses
	*/
	*(conf+1) = SET_BIT(0, 8, 16);
	/* 
	 * Config 3, ADV#(ADVn pin) signal timing parameter configuration
	 *	BIT0~3	: assertion time form start cycle time in ADV#
	 *	BIT4~6	: assertion for first address phase when using the AAD-Multiplexed in ADV#
	 *	BIT7	: add extra half GPMC_FCLK cycle in ADV#
	 *	BIT8~12 : de-assertion time from start cycle time for READ accesses in ADV#
	 *	BIT16~20: de-assertion time from start cycle time for WRITE accesses in ADV#
	 *	BIT24~26: assertion time for first address phase when using the ADD-Multiplexed in ADV#
	 *	BIT28~30: de-assertion time for first address phase when using the ADD-Multiplexed in ADV#
	*/
	*(conf+2) = SET_BIT(0, 10, 18);
	/*
	 * Config 4, WE# and OE# signal timing parameter configuration
	 *	BIT0~3	: assertion time for start cycle time in OE#
	 *	BIT4~6	: assertion for first address phase when using the AAD-Multiplexed access in OE#
	 *	BIT7	: add extra half GPMC_FCLK cycle in OE#
	 *	BIT8~12	: de-assertion time form start cycle time in OE#
	 *	BIT13~15: de-assertion timr for the first address phase in an AAD-Multiplexed access in OE#
	 *	BIT16~19: assertion time for start cycle time in WE#
	 *	BIT23	: add extra half GPMC_FCLK cycle in WE#
	 *	BIT24~28: de-assertion time form start cycle time in WE#
	*/
	*(conf+3) = SET_BIT(0, 2, 12, 16, 18, 28);	
	/*
	 * Config 5, RdAccessTime and CycleTime timing parameters configuration.
	 *	BIT0~4	: total READ cycle time
	 *	BIT8~12	: total write cycle time
	 *	BIT16~20: Delay between start cycle time and first data vaild
	 *	BIT24~27: Delay between successive words in multiple access
	*/
	*(conf+4) = SET_BIT(0, 1, 2, 3, 4, 8, 9, 10, 11, 12, 17, 19, 20, 25);
	/*
	 * Config 6, WrAccessTime, WrDataOnADmuxBus, Cycle2Cycle, and BusTurnAround parameters configuration
	 *	BIT0~3	: Bus turn around latency between two successive accesses to the
	 *				same chip-select (read to write) or to a different chip-select (read to
	 *				read and read to write)
	 *	BIT4	: Add Cycle2CycleDelay between two successive accesses to a
	 *				different chip-select (any access type)
	 *	BIT7	: Add Cycle2CycleDelay between two successive accesses to the
	 *				same chip-select (any access type)
	 *	BIT8~11 : Chip-select high pulse delay between two successive accesses
	 *	BIT16~19: Specifies on which GPMC.FCLK rising edge the first data of the
	 *				synchronous burst write is driven in the add/data multiplexed bus.
	 *				Reset value is 0x7.
	 *	BIT24~28: Delay from StartAccessTime to the GPMC.FCLK rising edge
	 *				corresponding the the GPMC.CLK rising edge used by the attached
  	 *				memory for the first data capture.
	 *				Reset value is 0xF.
	*/
	*(conf+5) = SET_BIT(16, 18, 24, 25, 26, 27, 31);
	/*
	 * << The GPMC requare memory block >>
	 * Config 7, chip-select address mapping configuration, 
	 *	BIT0~5  : Baseaddr = A24 = 000001 = 32 Mbyte 
	 *	BIT6    : chip select enable/disable **must be enable**
	 *	BIT8~11 : Mask Address = 1111 = 16 Mbyte
	*/
	*(conf+6) = SET_BIT(2, 3, 5, 8, 9, 10, 11);	
}

static int gpmc_setting(void)
{
	int err;
	u32 conf[7];
	
	_set_gpmc_conf(conf);

	pr_info("Ready to software resset!\n");
	gpmc_write_reg(GPMC_SYSCONFIG, BIT(1));	//software gpmc init reset
	if(gpmc_read_reg(GPMC_SYSSTATUS))
		pr_info("software resset success!\n");
	/*
	 * The IRQ ENABLE config, form Ti's TRM
	 *		bit0: FIFO event enable
	 *		bit1: termial count event
	 *		bit8: wait0 edge detection
	 *		bit9: wait1 edge detection
	*/
	gpmc_write_reg(GPMC_IRQENABLE, BIT(0));

	/* Write the GPMC config in register */	
	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG1, conf[0]);
//	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG2, conf[1]);
//	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG3, conf[2]);
//	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG4, conf[3]);
//	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG5, conf[4]);
//	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG6, conf[5]);

//	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG1, NET_GPMC_CONFIG1);
	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG2, NET_GPMC_CONFIG2);
	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG3, NET_GPMC_CONFIG3);
	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG4, NET_GPMC_CONFIG4);
	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG5, NET_GPMC_CONFIG5);
	gpmc_cs_write_reg(GPMC_CS, GPMC_CS_CONFIG6, NET_GPMC_CONFIG6);
	/* 
	 * The `request_mem_region` function use `adjust_resource` to find resource 
	 * memory base address will auto-setting memory and enable CS.
	 * So we don't need to configure the GPMC_CS_CONFIG7
	*/
	err = gpmc_cs_request(GPMC_CS, SZ_16M, (unsigned long *)&gpmc_base);
	if(err < 0) EXCP("gpmc_cs_request failed, err %d\n", err);
	pr_info("GPMC CONF7: 0x%x\n", 
			gpmc_cs_read_reg(GPMC_CS, GPMC_CS_CONFIG7));

	pr_info("Get Chip-Select %d, mem_addr %lx\n", GPMC_CS, gpmc_base);

	pr_info("==========================================================================\n");
	gpmc_cs_show_timings(GPMC_CS, "<< After GPMC configration setting >>");
	pr_info("==========================================================================\n");

	if(request_mem_region(gpmc_base, SZ_16M, "psedo_mem") == 0)
		EXCP("request_mem_region failed...\n");

	pseudo_base = ioremap(gpmc_base, SZ_1M);	//ioremap from GPMC memory base
	pr_info("ioremap to pseudo driver 0x%lx\n", (unsigned long)pseudo_base);
	
	return 0;
}

static void __cdev_exit(void)
{
	cdev_del(&pseudo.cdev);
	unregister_chrdev_region(pseudo.devt, 1);
}

static void __class_exit(void)
{
	class_unregister(pseudo.class);
	class_destroy(pseudo.class);
}

static int __init _pseudo_init(void)
{
	int res;
	int err;
	int major = 0;
	int minor = 0;
	int count = 1; // the number of device number required

	pseudo.devt = MKDEV(major, minor);
	if(major){
		res = register_chrdev_region(pseudo.devt, count, "pseudo");
	}else{
		res = alloc_chrdev_region(&pseudo.devt, minor, count, "pseudo");
		major = MAJOR(pseudo.devt);
	}
	if(res < 0) EXCP("pseudo device cannot get MAJOR number !\n");
	pr_info("pseudo device get MAJOR number %d !\n", major);
	
	pr_info("try to cdev add!\n");
	err = __cdev_setup();
	if(err < 0) EXCP("cdev setup failed, error(%d)\n", err);
	pr_info("cdev setup success!\n");
	sema_init(&pseudo.sem, 1);	

	return 0;
}

static int __init _pseudo_class_init(void)
{
	int retval;
	
	pseudo.class = class_create(THIS_MODULE, "pseudo");
	if(IS_ERR(pseudo.class)){
		pr_err("Failed to register `pseudo` class !\n");
		retval = PTR_ERR(pseudo.class);
		goto class_reg_failed;
	}
	
	pseudo.device = device_create(pseudo.class, NULL, 
								pseudo.devt, NULL, "pseudo");
	if(IS_ERR(pseudo.device)){
		pr_err("Failed to create `pseudo` deivce !\n");
		retval = PTR_ERR(pseudo.device);
		goto device_reg_failed;
	}

	return 0;
	
device_reg_failed:
	__class_exit();
class_reg_failed:
	__cdev_exit();

	return -1;
}		

static void _pseudo_exit(void)
{
	device_destroy(pseudo.class, pseudo.devt);
	__class_exit();
	__cdev_exit();
}

static void _gpmc_exit(void)
{
	release_mem_region(gpmc_base, SZ_16M);
	gpmc_cs_free(GPMC_CS);
}

static int __init test_init(void)
{
	pr_info("pseudo driver initial...\n");
	if(_pseudo_init() == -1) EXCP("Pseudo init failed\n");
	pr_info("pseudo driver initial done!\n");
	show_gpmc_rev();
	pr_info("GPMC initial...\n");
	if(gpmc_setting() == -1) return -1;
	pr_info("gpmc setting done!\n");
	pr_info("class initial...\n");
	if(_pseudo_class_init() == -1) return -1;
	pr_info("class initial done!\n");

	return 0;
}
module_init(test_init);

static void __exit test_exit(void)
{
	pr_info("pseudo driver exit!\n");
	_pseudo_exit();
	pr_info("GPMC exit!\n");
	_gpmc_exit();
	kfree(pseudo.buff);
	iounmap(pseudo_base);
}
module_exit(test_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Phil");
MODULE_DESCRIPTION("GPMC testing driver");

