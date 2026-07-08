/*
 * Copyright (C) 2022 Daniel Estevez <daniel@destevez.net>
 * Copyright (C) 2026 Fix Typage vmf_insert_pfn (Séparation stricte des fautes)
 * 
 * This file forms part of maia-sdr
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/idr.h>
#include <linux/mm.h>
#include <linux/property.h>

#define DRIVER_NAME "maia-sdr"

static dev_t maia_sdr_device = 0;
static struct class *maia_sdr_class;

#define MAIA_SDR_MINOR_MAX 256
static DEFINE_IDA(maia_sdr_device_ida);

#define IOC_MAGIC 'M'
#define IOCTL_CACHEINV _IOW(IOC_MAGIC, 0, int)

struct maia_sdr_recording_drvdata {
	struct cdev cdev;
	dev_t device_number;
	struct device *device;
	phys_addr_t phys_addr;
	dma_addr_t dma_handle;
	phys_addr_t mem_size;
	int minor;
};

struct maia_sdr_rxbuffer_drvdata {
	struct cdev cdev;
	dev_t device_number;
	struct device *device;
	phys_addr_t phys_addr;
	dma_addr_t dma_handle;
	phys_addr_t buffer_size;
	unsigned int num_buffers;
	int mmap_done;
	int minor;
};

/* --- Section Fault Handler Dédiée à RECORDING --- */
static vm_fault_t maia_sdr_recording_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct maia_sdr_recording_drvdata *drvdata = vma->vm_private_data;
	unsigned long pfn;

	if (!drvdata)
		return VM_FAULT_SIGSEGV;

	pfn = (drvdata->phys_addr >> PAGE_SHIFT) + vmf->pgoff;
	return vmf_insert_pfn(vma, vmf->address, pfn);
}

static const struct vm_operations_struct maia_sdr_recording_vm_ops = {
	.fault = maia_sdr_recording_fault,
};

/* --- Section Fault Handler Dédiée à RXBUFFER --- */
static vm_fault_t maia_sdr_rxbuffer_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
	struct maia_sdr_rxbuffer_drvdata *drvdata = vma->vm_private_data;
	unsigned long pfn;

	if (!drvdata)
		return VM_FAULT_SIGSEGV;

	pfn = (drvdata->phys_addr >> PAGE_SHIFT) + vmf->pgoff;
	return vmf_insert_pfn(vma, vmf->address, pfn);
}

static void maia_sdr_rxbuffer_vm_close(struct vm_area_struct *vma)
{
	struct maia_sdr_rxbuffer_drvdata *drvdata = vma->vm_private_data;
	if (drvdata)
		drvdata->mmap_done = 0;
}

static const struct vm_operations_struct maia_sdr_rxbuffer_vm_ops = {
	.fault = maia_sdr_rxbuffer_fault,
	.close = maia_sdr_rxbuffer_vm_close,
};

/* --- Callbacks Fops --- */
static int maia_sdr_recording_open(struct inode *inode, struct file *file)
{
	struct maia_sdr_recording_drvdata *drvdata = container_of(
		inode->i_cdev, struct maia_sdr_recording_drvdata, cdev);
	file->private_data = drvdata;
	return 0;
}

static int maia_sdr_recording_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct maia_sdr_recording_drvdata *drvdata = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;

	if (size > drvdata->mem_size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vm_flags_clear(vma, VM_WRITE | VM_MAYWRITE | VM_EXEC | VM_MAYEXEC);
	vm_flags_set(vma, VM_PFNMAP | VM_IO | VM_DONTEXPAND | VM_DONTDUMP);

	vma->vm_private_data = drvdata;
	vma->vm_ops = &maia_sdr_recording_vm_ops;

	return 0;
}

static int maia_sdr_rxbuffer_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct maia_sdr_rxbuffer_drvdata *drvdata = file->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	size_t max_size = drvdata->buffer_size * drvdata->num_buffers;

	if (drvdata->mmap_done)
		return -EBUSY;

	if (size > max_size)
		return -EINVAL;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vm_flags_clear(vma, VM_WRITE | VM_MAYWRITE | VM_EXEC | VM_MAYEXEC);
	vm_flags_set(vma, VM_PFNMAP | VM_IO | VM_DONTEXPAND | VM_DONTDUMP);

	drvdata->mmap_done = 1;
	vma->vm_private_data = drvdata;
	vma->vm_ops = &maia_sdr_rxbuffer_vm_ops;

	return 0;
}

static int maia_sdr_rxbuffer_cacheinv(struct maia_sdr_rxbuffer_drvdata *drvdata,
				      unsigned int num_buffer)
{
	if (num_buffer >= drvdata->num_buffers)
		return -EINVAL;
	return 0;
}

static long maia_sdr_rxbuffer_ioctl(struct file *file, unsigned int cmd,
				    unsigned long arg)
{
	struct maia_sdr_rxbuffer_drvdata *drvdata = file->private_data;

	switch (cmd) {
	case IOCTL_CACHEINV:
		return maia_sdr_rxbuffer_cacheinv(drvdata, arg);
	default:
		return -ENOTTY;
	}
}

static int maia_sdr_rxbuffer_open(struct inode *inode, struct file *file)
{
	struct maia_sdr_rxbuffer_drvdata *drvdata = container_of(
		inode->i_cdev, struct maia_sdr_rxbuffer_drvdata, cdev);
	file->private_data = drvdata;
	return 0;
}

/* Sysfs */
static ssize_t recording_base_address_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct maia_sdr_recording_drvdata *drvdata = dev_get_drvdata(dev);
	return sysfs_emit(buf, "0x%pa\n", &drvdata->phys_addr);
}
static ssize_t recording_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct maia_sdr_recording_drvdata *drvdata = dev_get_drvdata(dev);
	return sysfs_emit(buf, "0x%pa\n", &drvdata->mem_size);
}
static ssize_t buffer_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct maia_sdr_rxbuffer_drvdata *drvdata = dev_get_drvdata(dev);
	return sysfs_emit(buf, "0x%pa\n", &drvdata->buffer_size);
}
static ssize_t num_buffers_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct maia_sdr_rxbuffer_drvdata *drvdata = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%u\n", drvdata->num_buffers);
}

static DEVICE_ATTR_RO(recording_base_address);
static DEVICE_ATTR_RO(recording_size);
static DEVICE_ATTR_RO(buffer_size);
static DEVICE_ATTR_RO(num_buffers);

static const struct file_operations recording_fops = {
	.owner = THIS_MODULE,
	.open = maia_sdr_recording_open,
	.mmap = maia_sdr_recording_mmap,
};

static const struct file_operations rxbuffer_fops = {
	.owner = THIS_MODULE,
	.open = maia_sdr_rxbuffer_open,
	.mmap = maia_sdr_rxbuffer_mmap,
	.unlocked_ioctl = maia_sdr_rxbuffer_ioctl,
};

enum maia_sdr_device_type {
	MAIA_SDR_RECORDING,
	MAIA_SDR_RXBUFFER,
};

static int maia_sdr_probe_recording(struct platform_device *pdev)
{
	int ret, minor;
	struct maia_sdr_recording_drvdata *drvdata;
	struct device_node *memory_region;
	struct reserved_mem *rmem;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	memory_region = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!memory_region)
		return -ENODEV;

	rmem = of_reserved_mem_lookup(memory_region);
	of_node_put(memory_region);
	if (!rmem)
		return -ENODEV;

	drvdata->phys_addr = rmem->base;
	drvdata->mem_size = rmem->size;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	drvdata->dma_handle = dma_map_resource(&pdev->dev, drvdata->phys_addr,
					       drvdata->mem_size, DMA_FROM_DEVICE, 0);
	if (dma_mapping_error(&pdev->dev, drvdata->dma_handle))
		return -ENOMEM;

	minor = ida_alloc_max(&maia_sdr_device_ida, MAIA_SDR_MINOR_MAX - 1, GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		goto err_unmap_resource;
	}
	drvdata->minor = minor;
	drvdata->device_number = MKDEV(MAJOR(maia_sdr_device), minor);

	cdev_init(&drvdata->cdev, &recording_fops);
	drvdata->cdev.owner = THIS_MODULE;
	ret = cdev_add(&drvdata->cdev, drvdata->device_number, 1);
	if (ret < 0)
		goto err_free_ida;

	drvdata->device = device_create(maia_sdr_class, &pdev->dev,
					drvdata->device_number, drvdata,
					"%s", pdev->dev.of_node->name);
	if (IS_ERR(drvdata->device)) {
		ret = PTR_ERR(drvdata->device);
		goto err_del_cdev;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_recording_base_address);
	if (ret) goto err_destroy_dev;
	ret = device_create_file(&pdev->dev, &dev_attr_recording_size);
	if (ret) goto err_remove_file1;

	platform_set_drvdata(pdev, drvdata);
	return 0;

err_remove_file1:
	device_remove_file(&pdev->dev, &dev_attr_recording_base_address);
err_destroy_dev:
	device_destroy(maia_sdr_class, drvdata->device_number);
err_del_cdev:
	cdev_del(&drvdata->cdev);
err_free_ida:
	ida_free(&maia_sdr_device_ida, drvdata->minor);
err_unmap_resource:
	dma_unmap_resource(&pdev->dev, drvdata->dma_handle, drvdata->mem_size, DMA_FROM_DEVICE, 0);
	return ret;
}

static int maia_sdr_probe_rxbuffer(struct platform_device *pdev)
{
	int ret, minor;
	struct maia_sdr_rxbuffer_drvdata *drvdata;
	struct device_node *memory_region;
	struct reserved_mem *rmem;
	u32 buffer_size;
	size_t total_size;

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	ret = of_property_read_u32(pdev->dev.of_node, "buffer-size", &buffer_size);
	if (ret < 0)
		return ret;
	drvdata->buffer_size = buffer_size;

	memory_region = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!memory_region)
		return -ENODEV;

	rmem = of_reserved_mem_lookup(memory_region);
	of_node_put(memory_region);
	if (!rmem)
		return -ENODEV;

	if (rmem->size % drvdata->buffer_size != 0)
		return -EINVAL;

	drvdata->phys_addr = rmem->base;
	total_size = rmem->size;
	drvdata->num_buffers = total_size / drvdata->buffer_size;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	drvdata->dma_handle = dma_map_resource(&pdev->dev, drvdata->phys_addr,
					       total_size, DMA_FROM_DEVICE, 0);
	if (dma_mapping_error(&pdev->dev, drvdata->dma_handle))
		return -ENOMEM;

	minor = ida_alloc_max(&maia_sdr_device_ida, MAIA_SDR_MINOR_MAX - 1, GFP_KERNEL);
	if (minor < 0) {
		ret = minor;
		goto err_unmap_resource;
	}
	drvdata->minor = minor;
	drvdata->device_number = MKDEV(MAJOR(maia_sdr_device), minor);

	cdev_init(&drvdata->cdev, &rxbuffer_fops);
	drvdata->cdev.owner = THIS_MODULE;
	ret = cdev_add(&drvdata->cdev, drvdata->device_number, 1);
	if (ret < 0)
		goto err_free_ida;

	drvdata->device = device_create(maia_sdr_class, &pdev->dev,
					drvdata->device_number, drvdata,
					"%s", pdev->dev.of_node->name);
	if (IS_ERR(drvdata->device)) {
		ret = PTR_ERR(drvdata->device);
		goto err_del_cdev;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_buffer_size);
	if (ret) goto err_destroy_dev;
	ret = device_create_file(&pdev->dev, &dev_attr_num_buffers);
	if (ret) goto err_remove_file1;

	platform_set_drvdata(pdev, drvdata);
	return 0;

err_remove_file1:
	device_remove_file(&pdev->dev, &dev_attr_buffer_size);
err_destroy_dev:
	device_destroy(maia_sdr_class, drvdata->device_number);
err_del_cdev:
	cdev_del(&drvdata->cdev);
err_free_ida:
	ida_free(&maia_sdr_device_ida, drvdata->minor);
err_unmap_resource:
	dma_unmap_resource(&pdev->dev, drvdata->dma_handle, total_size, DMA_FROM_DEVICE, 0);
	return ret;
}

static const struct of_device_id maia_sdr_of_match[] = {
	{ .compatible = "maia-sdr,recording", .data = (void *)MAIA_SDR_RECORDING },
	{ .compatible = "maia-sdr,rxbuffer", .data = (void *)MAIA_SDR_RXBUFFER },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, maia_sdr_of_match);

static int maia_sdr_probe(struct platform_device *pdev)
{
	uintptr_t type = (uintptr_t)device_get_match_data(&pdev->dev);

	if (type == MAIA_SDR_RECORDING)
		return maia_sdr_probe_recording(pdev);
	else if (type == MAIA_SDR_RXBUFFER)
		return maia_sdr_probe_rxbuffer(pdev);
	
	return -EINVAL;
}

static void maia_sdr_remove(struct platform_device *pdev)
{
	uintptr_t type = (uintptr_t)device_get_match_data(&pdev->dev);

	if (type == MAIA_SDR_RECORDING) {
		struct maia_sdr_recording_drvdata *drvdata = platform_get_drvdata(pdev);
		if (!drvdata) return;
		device_remove_file(&pdev->dev, &dev_attr_recording_size);
		device_remove_file(&pdev->dev, &dev_attr_recording_base_address);
		device_destroy(maia_sdr_class, drvdata->device_number);
		cdev_del(&drvdata->cdev);
		ida_free(&maia_sdr_device_ida, drvdata->minor);
		dma_unmap_resource(&pdev->dev, drvdata->dma_handle, drvdata->mem_size, DMA_FROM_DEVICE, 0);
	} else if (type == MAIA_SDR_RXBUFFER) {
		struct maia_sdr_rxbuffer_drvdata *drvdata = platform_get_drvdata(pdev);
		if (!drvdata) return;
		size_t total_size = drvdata->buffer_size * drvdata->num_buffers;
		device_remove_file(&pdev->dev, &dev_attr_num_buffers);
		device_remove_file(&pdev->dev, &dev_attr_buffer_size);
		device_destroy(maia_sdr_class, drvdata->device_number);
		cdev_del(&drvdata->cdev);
		ida_free(&maia_sdr_device_ida, drvdata->minor);
		dma_unmap_resource(&pdev->dev, drvdata->dma_handle, total_size, DMA_FROM_DEVICE, 0);
	}
}

static struct platform_driver maia_sdr_platform_driver = {
	.probe = maia_sdr_probe,
	.remove_new = maia_sdr_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = maia_sdr_of_match,
	},
};

static int __init maia_sdr_init(void)
{
	int ret = alloc_chrdev_region(&maia_sdr_device, 0, MAIA_SDR_MINOR_MAX, DRIVER_NAME);
	if (ret < 0) return ret;

	maia_sdr_class = class_create(DRIVER_NAME);
	if (IS_ERR(maia_sdr_class)) {
		ret = PTR_ERR(maia_sdr_class);
		goto err_unreg_chrdev;
	}

	ret = platform_driver_register(&maia_sdr_platform_driver);
	if (ret < 0) goto err_destroy_class;

	return 0;

err_destroy_class:
	class_destroy(maia_sdr_class);
err_unreg_chrdev:
	unregister_chrdev_region(maia_sdr_device, MAIA_SDR_MINOR_MAX);
	return ret;
}

static void __exit maia_sdr_exit(void)
{
	platform_driver_unregister(&maia_sdr_platform_driver);
	class_destroy(maia_sdr_class);
	unregister_chrdev_region(maia_sdr_device, MAIA_SDR_MINOR_MAX);
	ida_destroy(&maia_sdr_device_ida);
}

module_init(maia_sdr_init);
module_exit(maia_sdr_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Maia SDR kernel module stable for Linux 6.12 (Isolated Fault handling)");
MODULE_AUTHOR("Daniel Estevez / Adapted for 6.12");