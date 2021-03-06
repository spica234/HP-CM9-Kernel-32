/* drivers/android/ram_console.c
 *
 * Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
#include <linux/rslib.h>
#endif

//Spica OTF Start
#ifdef CONFIG_SPICA_OTF

#include <linux/spica.h>
static struct proc_dir_entry *spica_dir;

#ifdef CONFIG_OTF_GPURAM
#define GPURAM_PROCFS_NAME "gpuramsize"
#define GPURAM_PROCFS_SIZE 8
static struct proc_dir_entry *GPURAM_Proc_File;
static char procfs_buffer_gpuram[GPURAM_PROCFS_SIZE];
static unsigned long procfs_buffer_size_gpuram = 0;

int gpuram_procfile_read(char *buffer, char **buffer_location, off_t offset, int buffer_length, int *eof, void *data) {
int ret;
printk(KERN_INFO "gpuram_procfile_read (/proc/spica/%s) called\n", GPURAM_PROCFS_NAME);
if (offset > 0) {
	ret = 0;
} else {
	memcpy(buffer, procfs_buffer_gpuram, procfs_buffer_size_gpuram);
	ret = procfs_buffer_size_gpuram;
}
return ret;
}

int gpuram_procfile_write(struct file *file, const char *buffer, unsigned long count, void *data) {
int temp_gpuram;
temp_gpuram = 0;
/* CAUTION: Don't change below 2 lines */
/* [Start] */
if ( sscanf(buffer,"%d",&temp_gpuram) < 1 ) return procfs_buffer_size_gpuram;
if ( temp_gpuram != 128 && temp_gpuram != 96 && temp_gpuram != 80 && temp_gpuram != 64 && temp_gpuram != 48 && temp_gpuram != 32 ) return procfs_buffer_size_gpuram;
/* [End] */
	procfs_buffer_size_gpuram = count;
if (procfs_buffer_size_gpuram > GPURAM_PROCFS_SIZE ) {
	procfs_buffer_size_gpuram = GPURAM_PROCFS_SIZE;
}
if ( copy_from_user(procfs_buffer_gpuram, buffer, procfs_buffer_size_gpuram) ) {
	printk(KERN_INFO "buffer_size error\n");
	return -EFAULT;
}
sscanf(procfs_buffer_gpuram,"%u",&GPURAMSIZE);
return procfs_buffer_size_gpuram;
}

static int __init init_gpuramfb_procsfs(void) {
GPURAM_Proc_File = spica_add(GPURAM_PROCFS_NAME);
if (GPURAM_Proc_File == NULL) {
	spica_remove(GPURAM_PROCFS_NAME);
	printk(KERN_ALERT "Error: Could not initialize /proc/spica/%s\n", GPURAM_PROCFS_NAME);
	return -ENOMEM;
} else {
	GPURAM_Proc_File->read_proc = gpuram_procfile_read;
	GPURAM_Proc_File->write_proc = gpuram_procfile_write;
	GPURAM_Proc_File->mode = S_IFREG | S_IRUGO;
	GPURAM_Proc_File->uid = 0;
	GPURAM_Proc_File->gid = 0;
	GPURAM_Proc_File->size = 37;
	sprintf(procfs_buffer_gpuram,"%d",GPURAMSIZE);
	procfs_buffer_size_gpuram = strlen(procfs_buffer_gpuram);
	printk(KERN_INFO "/proc/spica/%s created\n", GPURAM_PROCFS_NAME);
}
return 0;
}
module_init(init_gpuramfb_procsfs);
static void __exit cleanup_gpuramfb_procsfs(void) {
spica_remove(GPURAM_PROCFS_NAME);
printk(KERN_INFO "/proc/spica/%s removed\n", GPURAM_PROCFS_NAME);
}
module_exit(cleanup_gpuramfb_procsfs);
#endif // OTF_GPURAM
#endif // SPICA_OTF

//20101110, , Function for Warm-boot [START]
void *reserved_buffer;
//20101110, , Function for Warm-boot [END]

struct ram_console_buffer {
	uint32_t    sig;
	uint32_t    start;
	uint32_t    size;
	uint8_t     data[0];
};

#define RAM_CONSOLE_SIG (0x43474244) /* DBGC */

#ifdef CONFIG_ANDROID_RAM_CONSOLE_EARLY_INIT
static char __initdata
	ram_console_old_log_init_buffer[CONFIG_ANDROID_RAM_CONSOLE_EARLY_SIZE];
#endif
static char *ram_console_old_log;
static size_t ram_console_old_log_size;

static struct ram_console_buffer *ram_console_buffer;
static size_t ram_console_buffer_size;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
static char *ram_console_par_buffer;
static struct rs_control *ram_console_rs_decoder;
static int ram_console_corrected_bytes;
static int ram_console_bad_blocks;
#define ECC_BLOCK_SIZE CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_DATA_SIZE
#define ECC_SIZE CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_ECC_SIZE
#define ECC_SYMSIZE CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_SYMBOL_SIZE
#define ECC_POLY CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION_POLYNOMIAL
#endif

#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
static void ram_console_encode_rs8(uint8_t *data, size_t len, uint8_t *ecc)
{
	int i;
	uint16_t par[ECC_SIZE];
	/* Initialize the parity buffer */
	memset(par, 0, sizeof(par));
	encode_rs8(ram_console_rs_decoder, data, len, par, 0);
	for (i = 0; i < ECC_SIZE; i++)
		ecc[i] = par[i];
}

static int ram_console_decode_rs8(void *data, size_t len, uint8_t *ecc)
{
	int i;
	uint16_t par[ECC_SIZE];
	for (i = 0; i < ECC_SIZE; i++)
		par[i] = ecc[i];
	return decode_rs8(ram_console_rs_decoder, data, par, len,
				NULL, 0, NULL, 0, NULL);
}
#endif

static void ram_console_update(const char *s, unsigned int count)
{
	struct ram_console_buffer *buffer = ram_console_buffer;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	uint8_t *buffer_end = buffer->data + ram_console_buffer_size;
	uint8_t *block;
	uint8_t *par;
	int size = ECC_BLOCK_SIZE;
#endif
	memcpy(buffer->data + buffer->start, s, count);
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	block = buffer->data + (buffer->start & ~(ECC_BLOCK_SIZE - 1));
	par = ram_console_par_buffer +
	      (buffer->start / ECC_BLOCK_SIZE) * ECC_SIZE;
	do {
		if (block + ECC_BLOCK_SIZE > buffer_end)
			size = buffer_end - block;
		ram_console_encode_rs8(block, size, par);
		block += ECC_BLOCK_SIZE;
		par += ECC_SIZE;
	} while (block < buffer->data + buffer->start + count);
#endif
}

static void ram_console_update_header(void)
{
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	struct ram_console_buffer *buffer = ram_console_buffer;
	uint8_t *par;
	par = ram_console_par_buffer +
	      DIV_ROUND_UP(ram_console_buffer_size, ECC_BLOCK_SIZE) * ECC_SIZE;
	ram_console_encode_rs8((uint8_t *)buffer, sizeof(*buffer), par);
#endif
}

static void
ram_console_write(struct console *console, const char *s, unsigned int count)
{
	int rem;
	struct ram_console_buffer *buffer = ram_console_buffer;

	if (count > ram_console_buffer_size) {
		s += count - ram_console_buffer_size;
		count = ram_console_buffer_size;
	}
	rem = ram_console_buffer_size - buffer->start;
	if (rem < count) {
		ram_console_update(s, rem);
		s += rem;
		count -= rem;
		buffer->start = 0;
		buffer->size = ram_console_buffer_size;
	}
	ram_console_update(s, count);

	buffer->start += count;
	if (buffer->size < ram_console_buffer_size)
		buffer->size += count;
	ram_console_update_header();
}

static struct console ram_console = {
	.name	= "ram",
	.write	= ram_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED,
	.index	= -1,
};

void ram_console_enable_console(int enabled)
{
	if (enabled)
		ram_console.flags |= CON_ENABLED;
	else
		ram_console.flags &= ~CON_ENABLED;
}

static void __init
ram_console_save_old(struct ram_console_buffer *buffer, char *dest)
{
	size_t old_log_size = buffer->size;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	uint8_t *block;
	uint8_t *par;
	char strbuf[80];
	int strbuf_len;

	block = buffer->data;
	par = ram_console_par_buffer;
	while (block < buffer->data + buffer->size) {
		int numerr;
		int size = ECC_BLOCK_SIZE;
		if (block + size > buffer->data + ram_console_buffer_size)
			size = buffer->data + ram_console_buffer_size - block;
		numerr = ram_console_decode_rs8(block, size, par);
		if (numerr > 0) {
#if 0
			printk(KERN_INFO "ram_console: error in block %p, %d\n",
			       block, numerr);
#endif
			ram_console_corrected_bytes += numerr;
		} else if (numerr < 0) {
#if 0
			printk(KERN_INFO "ram_console: uncorrectable error in "
			       "block %p\n", block);
#endif
			ram_console_bad_blocks++;
		}
		block += ECC_BLOCK_SIZE;
		par += ECC_SIZE;
	}
	if (ram_console_corrected_bytes || ram_console_bad_blocks)
		strbuf_len = snprintf(strbuf, sizeof(strbuf),
			"\n%d Corrected bytes, %d unrecoverable blocks\n",
			ram_console_corrected_bytes, ram_console_bad_blocks);
	else
		strbuf_len = snprintf(strbuf, sizeof(strbuf),
				      "\nNo errors detected\n");
	if (strbuf_len >= sizeof(strbuf))
		strbuf_len = sizeof(strbuf) - 1;
	old_log_size += strbuf_len;
#endif

	if (dest == NULL) {
		dest = kmalloc(old_log_size, GFP_KERNEL);
		if (dest == NULL) {
			printk(KERN_ERR
			       "ram_console: failed to allocate buffer\n");
			return;
		}
	}

	ram_console_old_log = dest;
	ram_console_old_log_size = old_log_size;
	memcpy(ram_console_old_log,
	       &buffer->data[buffer->start], buffer->size - buffer->start);
	memcpy(ram_console_old_log + buffer->size - buffer->start,
	       &buffer->data[0], buffer->start);
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	memcpy(ram_console_old_log + old_log_size - strbuf_len,
	       strbuf, strbuf_len);
#endif
}

static int __init ram_console_init(struct ram_console_buffer *buffer,
				   size_t buffer_size, char *old_buf)
{
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	int numerr;
	uint8_t *par;
#endif
	ram_console_buffer = buffer;
	ram_console_buffer_size =
		buffer_size - sizeof(struct ram_console_buffer);

	if (ram_console_buffer_size > buffer_size) {
		pr_err("ram_console: buffer %p, invalid size %zu, "
		       "datasize %zu\n", buffer, buffer_size,
		       ram_console_buffer_size);
		return 0;
	}

#ifdef CONFIG_ANDROID_RAM_CONSOLE_ERROR_CORRECTION
	ram_console_buffer_size -= (DIV_ROUND_UP(ram_console_buffer_size,
						ECC_BLOCK_SIZE) + 1) * ECC_SIZE;

	if (ram_console_buffer_size > buffer_size) {
		pr_err("ram_console: buffer %p, invalid size %zu, "
		       "non-ecc datasize %zu\n",
		       buffer, buffer_size, ram_console_buffer_size);
		return 0;
	}

	ram_console_par_buffer = buffer->data + ram_console_buffer_size;


	/* first consecutive root is 0
	 * primitive element to generate roots = 1
	 */
	ram_console_rs_decoder = init_rs(ECC_SYMSIZE, ECC_POLY, 0, 1, ECC_SIZE);
	if (ram_console_rs_decoder == NULL) {
		printk(KERN_INFO "ram_console: init_rs failed\n");
		return 0;
	}

	ram_console_corrected_bytes = 0;
	ram_console_bad_blocks = 0;

	par = ram_console_par_buffer +
	      DIV_ROUND_UP(ram_console_buffer_size, ECC_BLOCK_SIZE) * ECC_SIZE;

	numerr = ram_console_decode_rs8(buffer, sizeof(*buffer), par);
	if (numerr > 0) {
		printk(KERN_INFO "ram_console: error in header, %d\n", numerr);
		ram_console_corrected_bytes += numerr;
	} else if (numerr < 0) {
		printk(KERN_INFO
		       "ram_console: uncorrectable error in header\n");
		ram_console_bad_blocks++;
	}
#endif

	if (buffer->sig == RAM_CONSOLE_SIG) {
		if (buffer->size > ram_console_buffer_size
		    || buffer->start > buffer->size)
			printk(KERN_INFO "ram_console: found existing invalid "
			       "buffer, size %d, start %d\n",
			       buffer->size, buffer->start);
		else {
			printk(KERN_INFO "ram_console: found existing buffer, "
			       "size %d, start %d\n",
			       buffer->size, buffer->start);
			ram_console_save_old(buffer, old_buf);
		}
	} else {
		printk(KERN_INFO "ram_console: no valid data in buffer "
		       "(sig = 0x%08x)\n", buffer->sig);
	}

	buffer->sig = RAM_CONSOLE_SIG;
	buffer->start = 0;
	buffer->size = 0;

	register_console(&ram_console);
#ifdef CONFIG_ANDROID_RAM_CONSOLE_ENABLE_VERBOSE
	console_verbose();
#endif
	return 0;
}

#ifdef CONFIG_ANDROID_RAM_CONSOLE_EARLY_INIT
static int __init ram_console_early_init(void)
{
	return ram_console_init((struct ram_console_buffer *)
		CONFIG_ANDROID_RAM_CONSOLE_EARLY_ADDR,
		CONFIG_ANDROID_RAM_CONSOLE_EARLY_SIZE,
		ram_console_old_log_init_buffer);
}
#else
static int ram_console_driver_probe(struct platform_device *pdev)
{
	struct resource *res = pdev->resource;
	size_t start;
	size_t buffer_size;
	void *buffer;

//20101110, , Function for Warm-boot [START]
        size_t reserved_start;
        size_t reserved_size;
//20101110, , Function for Warm-boot [END]

	if (res == NULL || pdev->num_resources != 1 ||
	    !(res->flags & IORESOURCE_MEM)) {
		printk(KERN_ERR "ram_console: invalid resource, %p %d flags "
		       "%lx\n", res, pdev->num_resources, res ? res->flags : 0);
		return -ENXIO;
	}
	buffer_size = res->end - res->start + 1;
	start = res->start;
	printk(KERN_INFO "ram_console: got buffer at %zx, size %zx\n",
	       start, buffer_size);
	buffer = ioremap(res->start, buffer_size);
	if (buffer == NULL) {
		printk(KERN_ERR "ram_console: failed to map memory\n");
		return -ENOMEM;
	}

#if 1
       //fix it
#if defined (CONFIG_STAR_HIDDEN_RESET)
       #define RAM_RESERVED_SIZE 3*512*1024
#else
       #define RAM_RESERVED_SIZE 100*1024
#endif
       //RAMHACK reboot fix ported from the CM9 ICS kernel
       //reserved_start = start+ buffer_size;
#ifdef CONFIG_OTF_GPURAM
       reserved_start = start+ buffer_size - ((128-GPURAMSIZE)*SZ_1M);
#else
       reserved_start = start+ buffer_size - ((128-CONFIG_GPU_MEM_CARVEOUT_SZ)*SZ_1M);
#endif
       reserved_buffer = ioremap(reserved_start, RAM_RESERVED_SIZE);

       //memset(reserved_buffer, 0x00, 100*1024);
       printk ("ram console : ram_console virtual addr = 0x%x \n", buffer);
       printk ("ram console : reserved_buffer virtual = 0x%x \n", reserved_buffer);
       printk ("ram console : reserved_buffer physical= 0x%x \n", reserved_start);
#endif

	return ram_console_init(buffer, buffer_size, NULL/* allocate */);
}

/* 
void *get_reserved_buffer()
{
	return reserved_buffer;
}
*/

void write_cmd_reserved_buffer(unsigned char *buf, size_t len)
{
	memcpy(reserved_buffer, buf, len);
}

void read_cmd_reserved_buffer(unsigned char *buf, size_t len)
{
	memcpy(buf, reserved_buffer, len);
}

EXPORT_SYMBOL_GPL(write_cmd_reserved_buffer);
EXPORT_SYMBOL_GPL(read_cmd_reserved_buffer);
#if defined (CONFIG_STAR_HIDDEN_RESET)
void write_screen_shot_reserved_buffer(unsigned char *buf, size_t len)
{
	printk("write_screen_shot_reserved_buffer address =%x + 32\n",reserved_buffer);


	copy_from_user(reserved_buffer+32, buf, 800*480*3);
	//memcpy(reserved_buffer+32, buf, 800*480*3);
}

void read_screen_shot_reserved_buffer(unsigned char *buf, size_t len)
{

	copy_to_user(buf ,reserved_buffer+32,800*480*3);
	//memcpy(buf, reserved_buffer+32, 800*480*3);

}
EXPORT_SYMBOL_GPL(write_screen_shot_reserved_buffer);
EXPORT_SYMBOL_GPL(read_screen_shot_reserved_buffer);

#endif

static struct platform_driver ram_console_driver = {
	.probe = ram_console_driver_probe,
	.driver		= {
		.name	= "ram_console",
	},
};

static int __init ram_console_module_init(void)
{
	int err;
	err = platform_driver_register(&ram_console_driver);
	return err;
}
#endif

static ssize_t ram_console_read_old(struct file *file, char __user *buf,
				    size_t len, loff_t *offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= ram_console_old_log_size)
		return 0;

	count = min(len, (size_t)(ram_console_old_log_size - pos));
	if (copy_to_user(buf, ram_console_old_log + pos, count))
		return -EFAULT;

	*offset += count;
	return count;
}

static const struct file_operations ram_console_file_ops = {
	.owner = THIS_MODULE,
	.read = ram_console_read_old,
};

static int __init ram_console_late_init(void)
{
	struct proc_dir_entry *entry;

	if (ram_console_old_log == NULL)
		return 0;
#ifdef CONFIG_ANDROID_RAM_CONSOLE_EARLY_INIT
	ram_console_old_log = kmalloc(ram_console_old_log_size, GFP_KERNEL);
	if (ram_console_old_log == NULL) {
		printk(KERN_ERR
		       "ram_console: failed to allocate buffer for old log\n");
		ram_console_old_log_size = 0;
		return 0;
	}
	memcpy(ram_console_old_log,
	       ram_console_old_log_init_buffer, ram_console_old_log_size);
#endif
	entry = create_proc_entry("last_kmsg", S_IFREG | S_IRUGO, NULL);
	if (!entry) {
		printk(KERN_ERR "ram_console: failed to create proc entry\n");
		kfree(ram_console_old_log);
		ram_console_old_log = NULL;
		return 0;
	}

	entry->proc_fops = &ram_console_file_ops;
	entry->size = ram_console_old_log_size;
	return 0;
}

#ifdef CONFIG_ANDROID_RAM_CONSOLE_EARLY_INIT
console_initcall(ram_console_early_init);
#else
postcore_initcall(ram_console_module_init);
#endif
late_initcall(ram_console_late_init);


