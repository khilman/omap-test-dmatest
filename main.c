/*
 * Simple test of OMAP DMA: memory-to-memory only 
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/dma-mapping.h>

#include <plat/dma.h>

MODULE_AUTHOR("Kevin Hilman");
MODULE_LICENSE("GPL");

static struct timer_list restart_timer;

/* load-time options */
int debug = 0;
int linking = 0;  /* link channels together : 0-> 1->2->3-> ... ->15 */
int startup = 1;  /* whether to start DMA channels during module init */
int forever = 0;  /* use recurring timer to restart DMA chain 
		     every 'forever_period' ticks */
int forever_period = 10;
int channels = 16;
int loop = 0;     /* last two channels are in a loop */
int max_transfers = 5;

module_param(debug, int, 0);
module_param(linking, int, 0);
module_param(startup, int, 0);
module_param(forever, int, 0);
module_param(forever_period, int, 0);
module_param(channels, int, 0);
module_param(loop, int, 0);
module_param(max_transfers, int, 0);

#define BUF_SIZE PAGE_SIZE
#define MAX_CHANNELS 16

struct dma_test_s {
	int dev_id;
	int dma_ch, next_ch;
	unsigned int src_buf, dest_buf;
	unsigned int src_buf_phys, dest_buf_phys;
	unsigned int count, good;
	unsigned int max_transfers;
};
static struct dma_test_s dma_test[MAX_CHANNELS];

static int verify_buffer(unsigned int *src, unsigned int *dest) {
	unsigned int i;
	unsigned int *s = src, *d = dest;

	for (i=0; i<BUF_SIZE/4; i++) {
		if (*s != *d) {
			printk("DMA copy failed at offset 0x%x.  "
			       "Expected 0x%08x, got 0x%08x\n",
			       (unsigned int)s - (unsigned)src, *s, *d);
			return 1;
		}
		s++; d++;
	}
	return 0;
}

static void dma_callback(int lch, u16 ch_status, void *data) {
	struct dma_test_s *t = (struct dma_test_s *)data;

	if (lch == t->dma_ch) {
		if (debug)
			printk("dma_callback(): lch=0x%x, ch_status=0x%04x\n",
			       lch, ch_status);
		t->count++;

		if (t->max_transfers && (t->count >= t->max_transfers)) {
			omap_stop_dma(t->dma_ch);
		}
	} else {
		printk("dma_callback(): Unexpected event on channel %d\n", 
		       lch);
	}
}

static int dmatest_read_procmem(char *buf, char **start, off_t offset,
				int count, int *eof, void *data)
{
	int i, len=0;
	struct dma_test_s *t;

	for(i=0; i<channels; i++) {
		t = &dma_test[i];
		if (verify_buffer((unsigned int *)t->src_buf, 
				  (unsigned int *)t->dest_buf) == 0) {
			t->good = 1;
		}
	}

	len += sprintf(buf+len, "OMAP DMA test\n");

	len += sprintf(buf+len, " Ch# Nxt count good max\n");
	for(i=0; i<channels; i++) {
		len += sprintf(buf+len, " %2d%s%2d %4d %4d %4d\n",
			       dma_test[i].dma_ch, 
			       dma_test[i].next_ch != -1 ? "->" : "  ",
			       dma_test[i].next_ch,
			       dma_test[i].count, dma_test[i].good,
			       dma_test[i].max_transfers);
	}

	return len;
}

static void __exit dmatest_cleanup(void) 
{
	int i;

	if (forever)
		del_timer(&restart_timer);

	for(i=0; i<channels; i++) {
		if (dma_test[i].dma_ch >= 0) {
			omap_stop_dma(dma_test[i].dma_ch);

			if (dma_test[i].next_ch != -1)
				omap_dma_unlink_lch(dma_test[i].dma_ch,
						    dma_test[i].next_ch);

			omap_free_dma(dma_test[i].dma_ch);

			dma_test[i].dma_ch = -1;
		}

		if (dma_test[i].src_buf) {
			dma_free_coherent(NULL, PAGE_SIZE,
					  (void *)dma_test[i].src_buf,
					  dma_test[i].src_buf_phys);
		}
		if (dma_test[i].dest_buf) {
			dma_free_coherent(NULL, PAGE_SIZE,
					  (void *)dma_test[i].dest_buf,
					  dma_test[i].dest_buf_phys);
		}
		dma_test[i].src_buf_phys = 0;
		dma_test[i].dest_buf_phys = 0;
	}

	remove_proc_entry("dmatest", NULL);
}

static int dmatest_start(void) {
	int i;

	if (linking) {
		if (debug) 
			printk("   Start DMA channel %d\n", 
			       dma_test[0].dma_ch);
		omap_start_dma(dma_test[0].dma_ch);
	} else for(i=0; i<channels; i++) {
		if (debug) 
			printk("   Start DMA channel %d\n", 
			       dma_test[i].dma_ch);
		omap_start_dma(dma_test[i].dma_ch);
	}

	return 0;
}

static void dmatest_restart(unsigned long unused) {
	mod_timer(&restart_timer, jiffies + forever_period);

	dmatest_start();
}

static int __init dmatest_init(void) 
{
	int i, j, r;
	int elem_count, frame_count;
	unsigned int *p;

	if (channels > MAX_CHANNELS) {
		printk("%s: channels arg (%d) > MAX_CHANNELS (%d)\n",
		       __FUNCTION__, channels, MAX_CHANNELS);
		return -ENODEV;
	}

	/* Crate /proc entry */
	create_proc_read_entry("dmatest", 
			       0	/* default mode */,
			       NULL	/* parent dir */, 
			       dmatest_read_procmem,
			       NULL	/* client data */);

	/* Alloc DMA-able buffers */
	for(i=0; i<channels; i++) {
		dma_test[i].count = 0;
		dma_test[i].next_ch = -1;

		dma_test[i].src_buf = (unsigned long)
			dma_alloc_coherent(NULL, PAGE_SIZE,
					   (dma_addr_t *)&dma_test[i].src_buf_phys,
					   GFP_KERNEL | GFP_DMA);
		if (!dma_test[i].src_buf) {
			printk("dmatest_init(): get_zeroed_page() failed.\n");
			r = -ENOMEM;
			goto cleanup;
		}

		dma_test[i].dest_buf = (unsigned long)
			dma_alloc_coherent(NULL, PAGE_SIZE,
					   (dma_addr_t *)&dma_test[i].dest_buf_phys,
					   GFP_KERNEL | GFP_DMA);
		if (!dma_test[i].dest_buf) {
			printk("dmatest_init(): get_zeroed_page() failed.\n");
			r = -ENOMEM;
			goto cleanup;
		}
		if (debug)
			printk("   src=0x%08x/0x%08x, dest=0x%08x/0x%08x\n",
			       dma_test[i].src_buf,
			       dma_test[i].src_buf_phys,
			       dma_test[i].dest_buf,
			       dma_test[i].dest_buf_phys);
		       
		/* Setup DMA transfer */
		dma_test[i].dev_id = OMAP_DMA_NO_DEVICE;
		dma_test[i].dma_ch = -1;
		dma_test[i].max_transfers = max_transfers; 
		r = omap_request_dma(dma_test[i].dev_id, "DMA Test", 
				     dma_callback, 
				     (void *)&dma_test[i], 
				     &dma_test[i].dma_ch);
		if (r) {
			if (debug)
				printk("%s: request_dma(%d) failed: %d\n", 
				       __FUNCTION__, i, r);
			dma_test[i].dev_id = 0;
			printk("WARNING: Only got %d/%d channels.\n", 
			       i, channels);
			channels = i;
			break;
		}
		if (debug)  {
			printk("DMA test %d, lch=0x%x\n", i,
				dma_test[i].dma_ch);
		}

		/* src buf init */
		if (debug) printk("   pre-filling src buf %d\n", i);
		p = (unsigned int *)dma_test[i].src_buf;
		for(j=0; j<BUF_SIZE/4; j++) {
			p[j] = (~j << 24) | (dma_test[i].dma_ch << 16) |  j;
		}

		elem_count = BUF_SIZE/4;
		frame_count = 1;
		omap_set_dma_transfer_params(dma_test[i].dma_ch,
					     OMAP_DMA_DATA_TYPE_S32,
					     elem_count,
					     frame_count,
					     OMAP_DMA_SYNC_ELEMENT, 
					     0, 0);
		omap_set_dma_src_params(dma_test[i].dma_ch, 
					OMAP_DMA_PORT_EMIFF, 
					OMAP_DMA_AMODE_POST_INC, 
					dma_test[i].src_buf_phys,
					0, 0);
		omap_set_dma_dest_params(dma_test[i].dma_ch, 
					 OMAP_DMA_PORT_EMIFF, 
					 OMAP_DMA_AMODE_POST_INC, 
					 dma_test[i].dest_buf_phys,
					 0, 0);


		if (linking && (channels > 1)) {
			if (i) {
				omap_dma_link_lch(dma_test[i-1].dma_ch, 
						  dma_test[i].dma_ch);
				dma_test[i-1].next_ch = dma_test[i].dma_ch;
			} 

		}
	}

	/* Loop test: Link last channel to next-to-last channel */
	if (loop) {
		omap_dma_link_lch(dma_test[channels-1].dma_ch, 
				  dma_test[channels-2].dma_ch);
		dma_test[channels-1].next_ch = dma_test[channels-2].dma_ch;

		/* avoid looping infinitely */
		dma_test[channels-1].max_transfers = max_transfers;
	}

	if (startup) {
		dmatest_start();
	}

	if (forever) { 
		setup_timer(&restart_timer, dmatest_restart, 0);
		add_timer(&restart_timer);
		mod_timer(&restart_timer, jiffies + forever_period);
	}
	return 0; 

 cleanup:
	dmatest_cleanup();
	return r;
}

module_init(dmatest_init);
module_exit(dmatest_cleanup);
