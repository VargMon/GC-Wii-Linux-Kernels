/*
 * drivers/block/rvl-mem2.c
 *
 * Nintendo Wii MEM2 block driver
 * Copyright (C) 2008 The GameCube Linux Team
 * Copyright (C) 2008 Albert Herranz
 *
 * Based on gcn-mem2.c.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/major.h>
#include <linux/of_platform.h>
#include <linux/blkdev.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/hdreg.h>	/* HDIO_GETGEO */
#include <asm/io.h>


#define DRV_MODULE_NAME "rvl-mem2"
#define DRV_DESCRIPTION "Nintendo Wii MEM2 block driver"
#define DRV_AUTHOR      "Albert Herranz"

static char mem2_driver_version[] = "0.1-isobel";

#define drv_printk(level, format, arg...) \
	printk(level DRV_MODULE_NAME ": " format , ## arg)


/*
 * Driver settings
 */
#define MEM2_NAME		DRV_MODULE_NAME
#define MEM2_MAJOR		Z2RAM_MAJOR

#define MEM2_SECTOR_SIZE	PAGE_SIZE


struct mem2_drvdata {
	spinlock_t			lock;

	void __iomem			*io_base;
	size_t				size;

	struct block_device_operations	fops;
	struct gendisk			*disk;
	struct request_queue		*queue;

	int				ref_count;

	struct device			*dev;
};

/*
 *
 */

/*
 * Performs block layer requests.
 */
static void mem2_do_request(struct request_queue *q)
{
	struct mem2_drvdata *drvdata = q->queuedata;
	struct request *req;
	unsigned long mem2_addr;
	size_t len;
	int uptodate;

	req = elv_next_request(q);
	while(req) {
		if (blk_fs_request(req)) {
			/* calculate the MEM2 address and length */
			mem2_addr = req->sector << 9;
			len = req->current_nr_sectors << 9;

			/* give up if the request goes out of bounds */
			if (mem2_addr + len > drvdata->size) {
				drv_printk(KERN_ERR, "bad access: block=%lu,"
					    " size=%u\n",
					    (unsigned long)req->sector, len);
				uptodate = 0;
			} else {
				switch(rq_data_dir(req)) {
				case READ:
					memcpy(req->buffer,
					       drvdata->io_base + mem2_addr, len);
					break;
				case WRITE:
					memcpy(drvdata->io_base + mem2_addr,
					       req->buffer, len);
					break;
				}
				uptodate = 1;
			}
		} else {
			uptodate = 0;
		}
		end_queued_request(req, uptodate);
		req = elv_next_request(q);
	}
}

/*
 * Opens the MEM2 device.
 */
static int mem2_open(struct inode *inode, struct file *filp)
{
	struct mem2_drvdata *drvdata = inode->i_bdev->bd_disk->private_data;
	unsigned long flags;
	int retval = 0;

	spin_lock_irqsave(&drvdata->lock, flags);

	/* only allow a minor of 0 to be opened */
	if (iminor(inode)) {
		retval =  -ENODEV;
		goto out;
	}

	/* honor exclusive open mode */
	if (drvdata->ref_count == -1 ||
	    (drvdata->ref_count && (filp->f_flags & O_EXCL))) {
		retval = -EBUSY;
		goto out;
	}

	if ((filp->f_flags & O_EXCL))
		drvdata->ref_count = -1;
	else
		drvdata->ref_count++;

out:
	spin_unlock_irqrestore(&drvdata->lock, flags);
	return retval;
}

/*
 * Closes the MEM2 device.
 */
static int mem2_release(struct inode *inode, struct file *filp)
{
	struct mem2_drvdata *drvdata = inode->i_bdev->bd_disk->private_data;
	unsigned long flags;

	spin_lock_irqsave(&drvdata->lock, flags);
	if (drvdata->ref_count > 0)
		drvdata->ref_count--;
	else
		drvdata->ref_count = 0;
	spin_unlock_irqrestore(&drvdata->lock, flags);
	
	return 0;
}

/*
 * Minimal ioctl for the MEM2 device.
 */
static int mem2_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct mem2_drvdata *drvdata = inode->i_bdev->bd_disk->private_data;
	struct hd_geometry geo;
	
	switch (cmd) {
	case BLKRAGET:
	case BLKFRAGET:
	case BLKROGET:
	case BLKBSZGET:
	case BLKSSZGET:
	case BLKSECTGET:
	case BLKGETSIZE:
	case BLKGETSIZE64:
	case BLKFLSBUF:
		return ioctl_by_bdev(inode->i_bdev,cmd,arg);
	case HDIO_GETGEO:
		/* fake the entries */
		geo.heads = 16;
		geo.sectors = 32;
		geo.start = 0;
		geo.cylinders = drvdata->size / (geo.heads * geo.sectors);
		if (copy_to_user((void __user*)arg,&geo,sizeof(geo)))
			return -EFAULT;
		return 0;
	default:
		return -ENOTTY;
	}
}


static struct block_device_operations mem2_fops = {
	.owner = THIS_MODULE,
	.open = mem2_open,
	.release = mem2_release,
	.ioctl = mem2_ioctl,
};


/*
 *
 */
static int mem2_init_blk_dev(struct mem2_drvdata *drvdata)
{
	struct gendisk *disk;
	struct request_queue *queue;
	int retval;

	drvdata->ref_count = 0;

	retval = register_blkdev(MEM2_MAJOR, MEM2_NAME);
	if (retval)
		goto err_register_blkdev;
	
	retval = -ENOMEM;
	spin_lock_init(&drvdata->lock);
	queue = blk_init_queue(mem2_do_request, &drvdata->lock);
	if (!queue)
		goto err_blk_init_queue;

	blk_queue_hardsect_size(queue, MEM2_SECTOR_SIZE);
	blk_queue_max_phys_segments(queue, 1);
	blk_queue_max_hw_segments(queue, 1);
	queue->queuedata = drvdata;
	drvdata->queue = queue;

	disk = alloc_disk(1);
	if (!disk)
		goto err_alloc_disk;

	disk->major = MEM2_MAJOR;
	disk->first_minor = 0;
	disk->fops = &mem2_fops;
	strcpy(disk->disk_name, MEM2_NAME);
	disk->queue = drvdata->queue;
	set_capacity(disk, drvdata->size >> 9);
	disk->private_data = drvdata;
	drvdata->disk = disk;

	add_disk(drvdata->disk);

	retval = 0;
	goto out;

err_alloc_disk:
	blk_cleanup_queue(drvdata->queue);
err_blk_init_queue:
	unregister_blkdev(MEM2_MAJOR, MEM2_NAME);
err_register_blkdev:
out:
	return retval;
}

/*
 *
 */
static void mem2_exit_blk_dev(struct mem2_drvdata *drvdata)
{
	if (drvdata->disk) {
		del_gendisk(drvdata->disk);
		put_disk(drvdata->disk);
	}
	if (drvdata->queue)
		blk_cleanup_queue(drvdata->queue);
	unregister_blkdev(MEM2_MAJOR, MEM2_NAME);
}

/*
 *
 */
static int mem2_init(struct mem2_drvdata *drvdata, struct resource *mem)
{
	int retval;
	size_t size;

	size = mem->end - mem->start + 1;
	drvdata->size = size;
	drvdata->io_base = ioremap(mem->start, size);
	if (!drvdata->io_base) {
		drv_printk(KERN_ERR, "failed to ioremap MEM2\n");
		return -EIO;
	}

	retval = mem2_init_blk_dev(drvdata);
	if (retval) {
		iounmap(drvdata->io_base);
	}
	return retval;
}

/*
 *
 */
static void mem2_exit(struct mem2_drvdata *drvdata)
{
	if (drvdata->io_base)
		iounmap(drvdata->io_base);
	mem2_exit_blk_dev(drvdata);
}

/*
 *
 */
static int mem2_do_probe(struct device *dev, struct resource *mem)
{
	struct mem2_drvdata *drvdata;
	int retval;

	drvdata = kzalloc(sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		drv_printk(KERN_ERR, "failed to allocate mem2_drvdata\n");
		return -ENOMEM;
	}
	dev_set_drvdata(dev, drvdata);
	drvdata->dev = dev;

	retval = mem2_init(drvdata, mem);
	if (retval) {
		dev_set_drvdata(dev, NULL);
		kfree(drvdata);
	}
	return retval;
}

/*
 *
 */
static int mem2_do_remove(struct device *dev)
{
	struct mem2_drvdata *drvdata = dev_get_drvdata(dev);

	if (drvdata) {
		mem2_exit(drvdata);
		dev_set_drvdata(dev, NULL);
		return 0;
	}
	return -ENODEV;
}

/*
 * Driver model probe function.
 */
static int __init mem2_of_probe(struct of_device *odev,
				const struct of_device_id *match)
{
	struct resource res;
	int retval;

	retval = of_address_to_resource(odev->node, 0, &res);
	if (retval) {
		drv_printk(KERN_ERR, "no memory range found\n");
		return -ENODEV;
	}

	return mem2_do_probe(&odev->dev, &res);
}

/*
 * Driver model remove function.
 */
static int __exit mem2_of_remove(struct of_device *odev)
{
	return mem2_do_remove(&odev->dev);
}

static struct of_device_id mem2_of_match[] = {
	{ .compatible = "nintendo,hollywood-mem2" },
	{ },
};

MODULE_DEVICE_TABLE(of, mem2_of_match);

static struct of_platform_driver mem2_of_driver = {
	.owner = THIS_MODULE,
	.name = DRV_MODULE_NAME,
	.match_table = mem2_of_match,
	.probe = mem2_of_probe,
	.remove = mem2_of_remove,
};

/*
 * Module initialization function.
 */
static int __init mem2_init_module(void)
{
	drv_printk(KERN_INFO, "%s - version %s\n", DRV_DESCRIPTION,
		   mem2_driver_version);

	return of_register_platform_driver(&mem2_of_driver);
}

/*
 * Module deinitialization funtion.
 */
static void __exit mem2_exit_module(void)
{
	of_unregister_platform_driver(&mem2_of_driver);
}

module_init(mem2_init_module);
module_exit(mem2_exit_module);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_LICENSE("GPL");

