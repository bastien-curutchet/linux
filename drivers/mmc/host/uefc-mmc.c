// SPDX-License-Identifier: GPL-2.0+
/*
 * eMMC host driver for the Macronix Unified Extensible Flash Controller (UEFC).
 *
 * The controller is exposed as a child of the UEFC MFD core
 * (drivers/mfd/uefc-common.c); this driver drives a single eMMC device attached
 * to one of the controller channels.
 *
 * Copyright (C) 2026 Macronix International Co., Ltd.
 * Author(s): Cheng Ming Lin <chengminglin@mxic.com.tw>
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mfd/uefc-common.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>

/* Host Controller Register (HC_CTRL, Cat. A) */
#define HC_CTRL				0x00
/* OctaFlash/OctaRAM byte-order swap; must be 0 for eMMC. */
#define HC_CTRL_DATA_ORDER		BIT(25)
#define HC_CTRL_EX_SER_B		BIT(22)	/* 1: channel B uses SERDES, 0: IO Buffer */
#define HC_CTRL_EX_SER_A		BIT(21)	/* 1: channel A uses SERDES, 0: IO Buffer */
#define HC_CTRL_SIO_SHIFTER(x)		(((x) & 0x3) << 23)
#define HC_CTRL_SIO_SHIFTER_MASK	HC_CTRL_SIO_SHIFTER(0x3)
#define HC_CTRL_CH_SEL			BIT(11)
#define HC_CTRL_PORT_SEL(x)		(((x) & 0xFF) << 0)
#define HC_CTRL_PORT_SEL_MASK		HC_CTRL_PORT_SEL(0xFF)

/* Normal Interrupt Status / Enable / Signal Registers */
#define INT_STS				0x04
#define INT_STS_EN			0x0C
#define INT_STS_SIG			0x14
#define INT_ERR				BIT(15)
#define INT_BUF_RD_RDY			BIT(5)
#define INT_BUF_WR_RDY			BIT(4)
#define INT_BLK_GAP			BIT(2)
#define INT_DAT_CMPLT			BIT(1)
#define INT_CMD_CMPLT			BIT(0)
#define INT_CA_REQ			BIT(30)
#define INT_AC_RDY			BIT(28)
#define INT_STS_ALL_EN			(INT_ERR | INT_BUF_RD_RDY | INT_BUF_WR_RDY | \
					 INT_BLK_GAP | INT_DAT_CMPLT | INT_CMD_CMPLT | \
					 INT_CA_REQ | INT_AC_RDY)

/* Error Interrupt Status / Enable Registers */
#define ERR_INT_STS			0x08
#define ERR_INT_STS_EN			0x10
#define ERR_INT_ECC			BIT(19)
#define ERR_INT_PREAM			BIT(18)
#define ERR_INT_CRC			BIT(17)
#define ERR_INT_AC			BIT(16)
#define ERR_INT_AUTO_CMD		BIT(8)
#define ERR_INT_DATA_END		BIT(6)
#define ERR_INT_DATA_CRC		BIT(5)
#define ERR_INT_DATA_TIMEOUT		BIT(4)
#define ERR_INT_CMD_IDX			BIT(3)
#define ERR_INT_CMD_END			BIT(2)
#define ERR_INT_CMD_CRC			BIT(1)
#define ERR_INT_CMD_TIMEOUT		BIT(0)
#define ERR_INT_STS_ALL_EN		(ERR_INT_ECC | ERR_INT_CMD_CRC | ERR_INT_AUTO_CMD | \
					 ERR_INT_DATA_CRC | ERR_INT_CRC | ERR_INT_DATA_TIMEOUT | \
					 ERR_INT_DATA_END | ERR_INT_AC | \
					 ERR_INT_CMD_IDX | ERR_INT_CMD_END | ERR_INT_CMD_TIMEOUT)

/* Transfer Mode Register (TFR_MODE, Cat. A) */
#define TFR_MODE			0x1C
#define TFR_MODE_BUS_MASK		(0x1FF << 8)
#define TFR_MODE_DAT_BUS(x)		(((x) & 0x7) << 14)
#define TFR_MODE_ADR_BUS(x)		(((x) & 0x7) << 11)
#define TFR_MODE_CMD_BUS(x)		(((x) & 0x7) << 8)
#define TFR_MODE_MULT_BLK_EN		BIT(5)
#define TFR_MODE_DD			BIT(4)
#define TFR_MODE_BLK_CNT_EN		BIT(1)
/* The data phase is PIO, but the register is shared: DMA_EN must be cleared. */
#define TFR_MODE_DMA_EN			BIT(0)
/* Per-line bus encodings: single (S) / dual data rate (D) */
#define BUS_1S				0
#define BUS_4S				2
#define BUS_8S				3
#define BUS_4D				6
#define BUS_8D				7

/* Transfer Control Register (TFR_CTRL, Cat. A) */
#define TFR_CTRL			0x20
#define TFR_CTRL_IO_END			BIT(16)
#define TFR_CTRL_HC_ACT			BIT(1)
#define TFR_CTRL_IO_START		BIT(0)

/* Present State Register (PRES_STS, Cat. A) */
#define PRES_STS			0x24
#define PRES_STS_BUF_RD_EN		BIT(11)	/* level: a block is ready to read */
#define PRES_STS_BUF_WR_EN		BIT(10)	/* level: room for a block to write */
#define PRES_STS_CMD_INH_DAT		BIT(1)
#define PRES_STS_CMD_INH_CMD		BIT(0)

/* Software Reset Register (SW_RST, Cat. A) */
#define SW_RST				0x44
#define SW_RST_DAT			BIT(2)
#define SW_RST_CMD			BIT(1)
#define SW_RST_ALL			BIT(0)

/* Timeout Control Register (TO_CTRL, Cat. A) */
#define TO_CTRL				0x48
#define TO_CTRL_CA(x)			(((x) & 0xF) << 16)
#define TO_CTRL_DAT(x)			((x) & 0xF)

/* Clock Control Register (CLK_CTRL, Cat. A) */
#define CLK_CTRL			0x4C
#define CLK_CTRL_SLOW_CLOCK		BIT(31)
#define CLK_CTRL_RX_SS_B(x)		(((x) & 0x1F) << 21)
#define CLK_CTRL_RX_SS_A(x)		(((x) & 0x1F) << 16)

/* Host Controller Version Register */
#define HC_VER				0x5C

/* Block Count / Argument / Command / Response / Buffer Registers (Cat. A) */
#define BLK_CNT				0x90
#define ARG_REG				0x94
#define CMD_REG				0x98
#define CMD_REG_CMD_MASK		0xFFFF
#define CMD_REG_CMD_IDX(x)		(((x) & 0x3F) << 8)
#define CMD_REG_CRC_RESP		BIT(6)
#define CMD_REG_DAT_EN			BIT(5)
#define CMD_REG_RSP_MASK		0x3
#define CMD_REG_RSP_NA			0
#define CMD_REG_RSP_136			1
#define CMD_REG_RSP_48			2
#define CMD_REG_RSP_48_BUSY		3
#define RSP_1				0x9C
#define RSP_2				0xA0
#define RSP_3				0xA4
#define RSP_4				0xA8
#define DATA_REG			0xAC

/* Device Control Register (DEV_CTRL, Cat. D) */
#define DEV_CTRL			0xC0
#define DEV_CTRL_TYPE(x)		(((x) & 0x7) << 29)
#define DEV_CTRL_TYPE_MASK		DEV_CTRL_TYPE(0x7)
#define DEV_CTRL_TYPE_EMMC		DEV_CTRL_TYPE(6)
#define DEV_CTRL_SCLK_SEL(x)		(((x) & 0xF) << 25)
#define DEV_CTRL_SCLK_SEL_MASK		DEV_CTRL_SCLK_SEL(0xF)
#define DEV_CTRL_BLK_SZ(x)		(((x) & 0xFFF) << 7)
#define DEV_CTRL_BLK_SZ_MASK		DEV_CTRL_BLK_SZ(0xFFF)
#define DEV_CTRL_DQS_EN			BIT(5)

/* Sample Point Adjust Register (SAMPLE_ADJ, Cat. D) */
#define SAMPLE_ADJ			0xEC
#define SAMPLE_POINT_DDR(x)		(((x) & 0x7) << 3)
#define SAMPLE_POINT_DDR_MASK		SAMPLE_POINT_DDR(0x7)
#define SAMPLE_POINT_SDR(x)		((x) & 0x7)
#define SAMPLE_POINT_SDR_MASK		SAMPLE_POINT_SDR(0x7)

#define UEFC_MMC_CMD_TIMEOUT_US		1000000
#define UEFC_MMC_DATA_TIMEOUT_US	5000000
#define UEFC_MMC_MAX_BLK_SIZE		512
#define UEFC_MMC_MAX_BLK_COUNT		512

/**
 * struct uefc_mmc - eMMC host private data
 *
 * @uefc:	UEFC MFD core handler (owned by the parent)
 * @chan:	Reserved UEFC channel this host drives
 * @mmc:	MMC host
 * @vcc:	Optional card power (VCC) supply GPIO
 * @vccq:	Optional I/O (VCCQ) supply GPIO
 * @blkbuf:	Bounce buffer for a single block of PIO data
 * @irq:	HC_INTRPT_O via IRQ_F2P; 0 = not wired, poll instead
 * @irq_evt:	Completion the interrupt handler signals
 * @cal_sp:	Last known-good SDR sample point, -1 = uncalibrated
 */
struct uefc_mmc {
	struct uefc_common *uefc;
	struct uefc_channel *chan;
	struct mmc_host *mmc;
	struct gpio_desc *vcc;
	struct gpio_desc *vccq;
	void *blkbuf;
	int irq;
	struct completion irq_evt;
	int cal_sp;
};

/*
 * Software RX-sample calibration. Per the spec, SAMPLE_ADJ bits 5:3
 * (SAMP_POINT_DDR) only apply to DDR mode; the knob for our SDR reads is bits
 * 2:0 (SAMP_POINT_SDR): 0..7 CLK_RX cycles after the first sample point (the
 * next SCLK edge after the launch edge). uefc_init_channel() leaves SDR at 3,
 * a NOR/NAND-calibrated default that eMMC reads do not necessarily share, so
 * sweep it on read data until the hardware data CRC passes.
 */
#define UEFC_MMC_SP_MAX			7

/*
 * SCLK = CLK_SS_I / (2 * (SCLK_SEL + 1)), so the divider spans CLK_SS_I / 2
 * down to CLK_SS_I / 32.
 */
#define UEFC_MMC_SCLK_SEL_MAX		0xF

/*
 * Identification runs off the eMMC Slow Clock (CLK_SS_I / 1024) because the
 * divider cannot reach 400 kHz: its floor is CLK_SS_I / 32, i.e. 3.125 MHz on
 * a 100 MHz CLK_SS_I.
 */
#define UEFC_MMC_SLOW_CLK_DIV		1024

static void uefc_mmc_set_sample(struct uefc_mmc *host, unsigned int pt)
{
	uefc_update_d_register(host->uefc, host->chan->id, 0, SAMPLE_ADJ,
			       SAMPLE_POINT_SDR_MASK, SAMPLE_POINT_SDR(pt));
}

static inline u32 uefc_mmc_read(struct uefc_mmc *host, unsigned int reg)
{
	u32 val = 0;

	uefc_read_register(host->uefc, reg, &val);
	return val;
}

static inline void uefc_mmc_write(struct uefc_mmc *host, unsigned int reg, u32 val)
{
	uefc_write_register(host->uefc, reg, val);
}

static void uefc_mmc_init_regs(struct uefc_mmc *host);

/*
 * Issue a software reset. The SW_RST bits are RWAC: the controller clears
 * them once the reset completes, and the spec requires polling for that
 * before touching the controller again (e.g. ending I/O mode).
 */
static void uefc_mmc_sw_rst(struct uefc_mmc *host, u32 mask)
{
	u32 sts;
	int ret;

	uefc_mmc_write(host, SW_RST, mask);
	ret = regmap_read_poll_timeout(host->uefc->regmap, SW_RST, sts,
				       !(sts & mask), 0, 100000);
	if (ret) {
		/*
		 * When a read dies mid-transfer the reset request can wedge
		 * with the bit stuck at 1: the controller is then held in
		 * reset and silently ignores everything, including further
		 * resets. Writing 0 force-releases the request and, with the
		 * static configuration reprogrammed, the command engine comes
		 * back (found by poking the wedged controller over devmem).
		 */
		dev_warn(host->uefc->dev,
			 "SW_RST %#x stuck, force-releasing\n", mask);
		uefc_mmc_write(host, SW_RST, 0);
		uefc_mmc_init_regs(host);
	}
}

/*
 * HC_INTRPT_O handler. The line is level-high and gated per-bit by
 * INT_STS_SIG, which only uefc_mmc_wait_int() ever arms, so any assertion
 * belongs to the one waiter. Silence the source by disarming INT_STS_SIG
 * (never by acking INT_STS here - the waiter still needs to see the W1C
 * status bits) and wake the waiter.
 */
static irqreturn_t uefc_mmc_irq(int irq, void *data)
{
	struct uefc_mmc *host = data;

	if (!(uefc_mmc_read(host, INT_STS) & uefc_mmc_read(host, INT_STS_SIG)))
		return IRQ_NONE;

	uefc_mmc_write(host, INT_STS_SIG, 0);
	complete(&host->irq_evt);
	return IRQ_HANDLED;
}

/*
 * Wait for a normal interrupt status bit, aborting early on the error flag.
 * With the IRQ wired (host->irq) the wait sleeps on the HC interrupt; the
 * status re-check after arming INT_STS_SIG closes the race against a bit
 * that latched between the read and the arm. Without it, poll.
 */
static int uefc_mmc_wait_int(struct uefc_mmc *host, u32 mask, unsigned int timeout_us)
{
	unsigned long deadline = jiffies + usecs_to_jiffies(timeout_us) + 1;
	u32 sts;

	for (;;) {
		sts = uefc_mmc_read(host, INT_STS);
		if (sts & (mask | INT_ERR))
			break;
		if (time_after(jiffies, deadline))
			return -ETIMEDOUT;
		if (host->irq > 0) {
			reinit_completion(&host->irq_evt);
			uefc_mmc_write(host, INT_STS_SIG, mask | INT_ERR);
			sts = uefc_mmc_read(host, INT_STS);
			if (sts & (mask | INT_ERR)) {
				uefc_mmc_write(host, INT_STS_SIG, 0);
				break;
			}
			/*
			 * Cap each sleep so a wired-in-DT but dead-in-RTL
			 * interrupt line degrades to 10 ms-granular polling
			 * instead of stretching every command to timeout_us.
			 */
			wait_for_completion_timeout(&host->irq_evt,
					min(usecs_to_jiffies(timeout_us),
					    msecs_to_jiffies(10)) + 1);
			/* No-op after the ISR ran; needed on the timeout path. */
			uefc_mmc_write(host, INT_STS_SIG, 0);
		} else {
			cpu_relax();
		}
	}
	if (sts & INT_ERR) {
		/* An error interrupt really fired. */
		return -EIO;
	}

	/* Acknowledge the bit we waited for (write-1-to-clear). */
	uefc_mmc_write(host, INT_STS, sts & mask);
	return 0;
}

static int uefc_mmc_set_bus(struct uefc_mmc *host, struct mmc_ios *ios)
{
	bool ddr = ios->timing == MMC_TIMING_UHS_DDR50 ||
		   ios->timing == MMC_TIMING_MMC_DDR52 ||
		   ios->timing == MMC_TIMING_MMC_HS400;
	u32 dat;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_8:
		dat = ddr ? BUS_8D : BUS_8S;
		break;
	case MMC_BUS_WIDTH_4:
		dat = ddr ? BUS_4D : BUS_4S;
		break;
	case MMC_BUS_WIDTH_1:
		if (ddr)
			return -EINVAL;
		dat = BUS_1S;
		break;
	default:
		return -EINVAL;
	}

	/* Command and address lines always run single-data-rate on eMMC. */
	uefc_update_register(host->uefc, TFR_MODE, TFR_MODE_BUS_MASK,
			     TFR_MODE_CMD_BUS(BUS_1S) | TFR_MODE_ADR_BUS(BUS_1S) |
			     TFR_MODE_DAT_BUS(dat));

	uefc_update_d_register(host->uefc, host->chan->id, 0, SAMPLE_ADJ,
			       SAMPLE_POINT_DDR_MASK,
			       ddr ? SAMPLE_POINT_DDR(2) : 0);
	uefc_update_d_register(host->uefc, host->chan->id, 0, DEV_CTRL,
			       DEV_CTRL_DQS_EN, ddr ? DEV_CTRL_DQS_EN : 0);

	return 0;
}

/*
 * Program the device clock. SCLK is derived from the SS clock (CLK_SS_I):
 * SCLK = CLK_SS_I / (2 * (SCLK_SEL + 1)), so the controller produces 16
 * discrete rates spanning CLK_SS_I / 2 down to CLK_SS_I / 32. Identification
 * runs below that floor and takes the eMMC Slow Clock instead.
 *
 * Pick the fastest divider that does not exceed what the core asked for: both
 * roundings go towards a larger divider, so the rate programmed here is never
 * above @clock.
 */
static void uefc_mmc_set_clk(struct uefc_mmc *host, unsigned int clock)
{
	struct uefc_common *uefc = host->uefc;
	unsigned long ss_rate;
	unsigned int div, sel;

	if (!clock)
		return;

	if (clock <= 400000) {
		uefc_update_register(uefc, CLK_CTRL, CLK_CTRL_SLOW_CLOCK,
				     CLK_CTRL_SLOW_CLOCK);
		return;
	}

	ss_rate = clk_get_rate(uefc->ss_clk);
	if (!ss_rate)
		return;

	div = DIV_ROUND_UP(ss_rate, clock);
	sel = min_t(unsigned int, DIV_ROUND_UP(div, 2) - 1, UEFC_MMC_SCLK_SEL_MAX);

	uefc_update_register(uefc, CLK_CTRL, CLK_CTRL_SLOW_CLOCK, 0);
	uefc_update_d_register(uefc, host->chan->id, 0, DEV_CTRL,
			       DEV_CTRL_SCLK_SEL_MASK, DEV_CTRL_SCLK_SEL(sel));
}

static void uefc_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct uefc_mmc *host = mmc_priv(mmc);

	switch (ios->power_mode) {
	case MMC_POWER_OFF:
		if (host->vcc)
			gpiod_set_value_cansleep(host->vcc, 0);
		if (host->vccq)
			gpiod_set_value_cansleep(host->vccq, 0);
		break;
	case MMC_POWER_UP:
		if (host->vcc)
			gpiod_set_value_cansleep(host->vcc, 1);
		if (host->vccq)
			gpiod_set_value_cansleep(host->vccq, 1);
		mdelay(6);
		break;
	case MMC_POWER_ON:
		break;
	}

	uefc_mmc_set_bus(host, ios);
	usleep_range(1000, 1500);
	uefc_mmc_set_clk(host, ios->clock);
	usleep_range(1000, 1500);
}

static void uefc_mmc_setup_tfr_mode(struct uefc_mmc *host, struct mmc_data *data)
{
	u32 tfr = 0;

	if (data) {
		uefc_update_d_register(host->uefc, host->chan->id, 0, DEV_CTRL,
				       DEV_CTRL_BLK_SZ_MASK, DEV_CTRL_BLK_SZ(data->blksz));
		tfr |= TFR_MODE_BLK_CNT_EN;
		if (data->flags & MMC_DATA_READ)
			tfr |= TFR_MODE_DD;
		if (data->blocks > 1)
			tfr |= TFR_MODE_MULT_BLK_EN;
	} else {
		tfr |= TFR_MODE_DD;
	}

	/*
	 * DMA_EN is in the mask but never in the value: TFR_MODE is shared with
	 * the other channels' children, so a DMA user leaving it set would make
	 * this host's data phase run against the DMA datapath and swallow the
	 * wire data.
	 */
	uefc_update_register(host->uefc, TFR_MODE,
			     TFR_MODE_DD | TFR_MODE_MULT_BLK_EN |
			     TFR_MODE_BLK_CNT_EN | TFR_MODE_DMA_EN, tfr);
}

static int uefc_mmc_setup_cmd(struct uefc_mmc *host, struct mmc_command *cmd,
			      struct mmc_data *data)
{
	u32 reg_cmd;

	uefc_mmc_write(host, BLK_CNT, (data && data->blocks) ? data->blocks : 0);
	uefc_mmc_write(host, ARG_REG, cmd->arg);

	reg_cmd = CMD_REG_CMD_IDX(cmd->opcode);

	if (data && cmd->opcode != 0)
		reg_cmd |= CMD_REG_DAT_EN;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136)
			reg_cmd |= CMD_REG_RSP_136;
		else if (cmd->flags & MMC_RSP_BUSY)
			reg_cmd |= CMD_REG_RSP_48_BUSY;
		else
			reg_cmd |= CMD_REG_RSP_48;
	} else {
		reg_cmd |= CMD_REG_RSP_NA;
	}

	/*
	 * Enable write CRC status reporting for data writes, except for the
	 * bus-test / lock-unlock commands which carry no CRC status token.
	 */
	if (data && (data->flags & MMC_DATA_WRITE) &&
	    cmd->opcode != MMC_BUS_TEST_W && cmd->opcode != MMC_LOCK_UNLOCK)
		reg_cmd |= CMD_REG_CRC_RESP;

	/*
	 * Writing CMD_REG is what triggers the command on the bus. A masked
	 * regmap_update_bits() skips the write when the value is unchanged
	 * (CMD0's value is 0; two back-to-back CMD1s share the same index +
	 * response type), so the command would silently never be issued. Read,
	 * replace only the command/CRC fields, and force the write.
	 */
	reg_cmd |= uefc_mmc_read(host, CMD_REG) &
		   ~(CMD_REG_CMD_MASK | CMD_REG_CRC_RESP);
	uefc_mmc_write(host, CMD_REG, reg_cmd);

	dev_dbg(host->uefc->dev, ">> CMD%-2u arg=%08x CMD_REG=%08x%s\n",
		cmd->opcode, cmd->arg, reg_cmd, data ? " +data" : "");

	return uefc_mmc_wait_int(host, INT_CMD_CMPLT, UEFC_MMC_CMD_TIMEOUT_US);
}

static void uefc_mmc_read_response(struct uefc_mmc *host, struct mmc_command *cmd)
{
	if (!(cmd->flags & MMC_RSP_PRESENT)) {
		cmd->resp[0] = 0xffffffff;
	} else if (cmd->flags & MMC_RSP_136) {
		cmd->resp[0] = uefc_mmc_read(host, RSP_1);
		cmd->resp[1] = uefc_mmc_read(host, RSP_2);
		cmd->resp[2] = uefc_mmc_read(host, RSP_3);
		/* RSP_4[23:0] maps to CID/CSD[31:8]; LSB carries the end bit. */
		cmd->resp[3] = 0x1 | (uefc_mmc_read(host, RSP_4) << 8);
	} else {
		cmd->resp[0] = uefc_mmc_read(host, RSP_1);
	}
}

/*
 * Wait until the buffer FIFO can transfer one word. The PRES_STS
 * BUF_RD_EN/BUF_WR_EN bits are FIFO fill levels, not SDHCI-style block-ready
 * flags: BUF_RD_EN rises as soon as the first words of a block arrive on the
 * wire (verified at ~25% of a block into a slowed-down transfer) and clears
 * the moment the last word is popped, so it must gate every DATA_REG access
 * individually - a single block-level wait on it lets the PIO loop outrun
 * the wire and read an empty FIFO. Pacing on the level also avoids the
 * BUF_RD_RDY/BUF_WR_RDY edge events entirely: those are W1C latches that
 * coalesce, so acknowledging block N can swallow block N+1's notification
 * and stall the transfer (the stochastic multi-block CMD18 hang).
 */
static int uefc_mmc_wait_word(struct uefc_mmc *host, u32 lvl,
			      unsigned int timeout_us)
{
	unsigned long deadline = jiffies + usecs_to_jiffies(timeout_us) + 1;

	for (;;) {
		if (uefc_mmc_read(host, PRES_STS) & lvl)
			return 0;
		if (uefc_mmc_read(host, INT_STS) & INT_ERR)
			return -EIO;
		if (time_after(jiffies, deadline))
			return -ETIMEDOUT;
		cpu_relax();
	}
}

/* PIO transfer of a single block to/from the buffer data port. */
static int uefc_mmc_xfer_block(struct uefc_mmc *host, bool read,
			       struct mmc_data *data, unsigned int blkidx)
{
	unsigned int blksz = data->blksz;
	unsigned int off;
	int i, ret;
	u32 word;

	off = blkidx * blksz;

	if (read) {
		for (i = 0; i < blksz; i += 4) {
			ret = uefc_mmc_wait_word(host, PRES_STS_BUF_RD_EN,
						 UEFC_MMC_DATA_TIMEOUT_US);
			if (ret)
				return ret;
			word = uefc_mmc_read(host, DATA_REG);
			/* Right-align the final partial word. */
			word >>= ((4 - (blksz - i) % 4) % 4) * 8;
			memcpy(host->blkbuf + i, &word, min(4, blksz - i));
		}
		sg_pcopy_from_buffer(data->sg, data->sg_len, host->blkbuf,
				     blksz, off);
		/* Consume the block's edge event as housekeeping. */
		uefc_mmc_write(host, INT_STS, INT_BUF_RD_RDY);
	} else {
		sg_pcopy_to_buffer(data->sg, data->sg_len, host->blkbuf,
				   blksz, off);

		for (i = 0; i < blksz; i += 4) {
			ret = uefc_mmc_wait_word(host, PRES_STS_BUF_WR_EN,
						 UEFC_MMC_DATA_TIMEOUT_US);
			if (ret)
				return ret;
			word = 0;
			memcpy(&word, host->blkbuf + i, min(4, blksz - i));
			uefc_mmc_write(host, DATA_REG, word);
		}
		uefc_mmc_write(host, INT_STS, INT_BUF_WR_RDY);
	}

	return 0;
}

/*
 * The data phase is PIO by design. The controller also has SDMA and ADMA2
 * engines and both were implemented and tested against this IP, but on every
 * bitstream seen so far the DMA engine never writes the block buffer contents
 * to system memory: the transfer reports completion, the descriptors are
 * fetched, the wire data is good (the very same block reads back correctly
 * through PIO), and memory is left untouched or holds the previous transfer's
 * data. An independent implementation of the same sequence behaves identically
 * on the same image, so this is an RTL defect and not a programming order
 * problem. The read of
 * EXT_CSD is what makes it visible: SEC_COUNT comes back zero and the card
 * enumerates as "0 B", with no error reported anywhere.
 *
 * Restore a DMA path only against a bitstream verified to deliver data, and
 * check the enumerated capacity before trusting anything it reads back.
 */
static int uefc_mmc_xfer_data(struct uefc_mmc *host, struct mmc_data *data)
{
	bool read = data->flags & MMC_DATA_READ;
	unsigned int b;
	int ret;

	for (b = 0; b < data->blocks; b++) {
		ret = uefc_mmc_xfer_block(host, read, data, b);
		if (ret)
			return ret;
	}

	/*
	 * Both directions end with Data Transfer Complete: on a write it is
	 * raised when the card releases the busy phase after programming.
	 */
	ret = uefc_mmc_wait_int(host, INT_DAT_CMPLT, UEFC_MMC_DATA_TIMEOUT_US);
	if (ret)
		return ret;

	data->bytes_xfered = data->blocks * data->blksz;
	return 0;
}

/* Decode the error interrupt status into an errno and clear the latched bits. */
static int uefc_mmc_error(struct uefc_mmc *host, struct mmc_command *cmd)
{
	u32 err = uefc_mmc_read(host, ERR_INT_STS);
	unsigned int blk, i;
	int ret = -EIO;

	if (err & (ERR_INT_CMD_TIMEOUT | ERR_INT_DATA_TIMEOUT))
		ret = -ETIMEDOUT;
	else if (err & ERR_INT_CMD_IDX)
		ret = -EINVAL;

	dev_warn_ratelimited(host->uefc->dev,
		"CMD%-2u failed: ERR=%#x [%s%s%s%s%s%s] PRES=%#x RSP %08x %08x %08x %08x\n",
		cmd->opcode, err,
		err & ERR_INT_CMD_TIMEOUT ? "cmd_timeout " : "",
		err & ERR_INT_CMD_CRC     ? "cmd_crc "     : "",
		err & ERR_INT_CMD_END     ? "cmd_endbit "  : "",
		err & ERR_INT_CMD_IDX     ? "cmd_index "   : "",
		err & ERR_INT_DATA_TIMEOUT ? "data_timeout " : "",
		err & ERR_INT_DATA_CRC     ? "data_crc "     : "",
		uefc_mmc_read(host, PRES_STS),
		uefc_mmc_read(host, RSP_1), uefc_mmc_read(host, RSP_2),
		uefc_mmc_read(host, RSP_3), uefc_mmc_read(host, RSP_4));

	uefc_mmc_write(host, ERR_INT_STS, err);

	/*
	 * Unwedge the controller before resetting it. When a read dies mid-
	 * transfer the buffer may still hold unread blocks (BUF_RD_EN stuck in
	 * PRES_STS) and the DAT FSM stays mid-stream; in that state SW_RST_ALL
	 * never self-clears and the controller is dead for good. End I/O mode
	 * first, drain whatever is left in the read buffer, clear the stale
	 * normal status bits, then reset.
	 */
	uefc_mmc_write(host, TFR_CTRL, TFR_CTRL_IO_END);
	/*
	 * Drain on the BUF_RD_EN level, not the BUF_RD_RDY edge event: a block
	 * abandoned by a failed transfer sets the level without a latched edge,
	 * and one undrained block leaves every later read off by one block
	 * (the next transfer consumes the stale block and strands its own).
	 */
	for (blk = 0; blk < UEFC_MMC_MAX_BLK_COUNT; blk++) {
		if (!(uefc_mmc_read(host, PRES_STS) & PRES_STS_BUF_RD_EN))
			break;
		uefc_mmc_write(host, INT_STS, INT_BUF_RD_RDY);
		for (i = 0; i < UEFC_MMC_MAX_BLK_SIZE / 4; i++)
			uefc_mmc_read(host, DATA_REG);
	}
	uefc_mmc_write(host, INT_STS, ~0);

	uefc_mmc_sw_rst(host, SW_RST_ALL);
	return ret;
}

/* Issue a single command (with optional data phase) and collect the response. */
static int uefc_mmc_do_command(struct uefc_mmc *host, struct mmc_command *cmd,
			       struct mmc_data *data)
{
	u32 sts;
	int ret;

	/*
	 * A data phase must never run on the Slow Clock, which corrupts the
	 * data CRC. The mmc core still runs at the <= 400 kHz identification
	 * clock when it reads EXT_CSD (CMD8) right after selecting the card,
	 * so move to the divider for any command carrying data.
	 */
	if (data) {
		if (host->mmc->ios.clock <= 400000)
			uefc_mmc_set_clk(host, host->mmc->f_max);

		/*
		 * The sample-point sweep re-issues the whole command, so a
		 * failed attempt must not leave its error behind for a later
		 * one that succeeds.
		 */
		data->error = 0;
	}

	/* Wait until the controller can accept a new command. */
	ret = regmap_read_poll_timeout(host->uefc->regmap, PRES_STS, sts,
				       !(sts & PRES_STS_CMD_INH_CMD), 0,
				       UEFC_MMC_CMD_TIMEOUT_US);
	if (ret)
		return uefc_mmc_error(host, cmd);
	if ((data || (cmd->flags & MMC_RSP_BUSY)) &&
	    cmd->opcode != MMC_STOP_TRANSMISSION) {
		ret = regmap_read_poll_timeout(host->uefc->regmap, PRES_STS, sts,
					       !(sts & PRES_STS_CMD_INH_DAT), 0,
					       UEFC_MMC_CMD_TIMEOUT_US);
		if (ret)
			return uefc_mmc_error(host, cmd);
	}
	uefc_mmc_write(host, INT_STS, INT_DAT_CMPLT);
	uefc_mmc_setup_tfr_mode(host, data);

	/* Enter I/O mode for this command. */
	uefc_mmc_write(host, TFR_CTRL, TFR_CTRL_IO_START);
	uefc_mmc_write(host, TFR_CTRL, TFR_CTRL_HC_ACT);
	ret = uefc_mmc_setup_cmd(host, cmd, data);
	if (ret)
		return uefc_mmc_error(host, cmd);
	uefc_mmc_read_response(host, cmd);
	if (data) {
		ret = uefc_mmc_xfer_data(host, data);
		if (ret) {
			/*
			 * The command itself was accepted and its response is
			 * already collected; only the data phase failed. Record
			 * it against the data so the core sees the two apart -
			 * it retries and throttles a data error, but treats a
			 * command error as a reason to re-initialise the card.
			 */
			data->error = uefc_mmc_error(host, cmd);
			return data->error;
		}
	} else if (cmd->flags & MMC_RSP_BUSY) {
		/*
		 * R1b: the card holds DAT0 busy and the controller tracks it
		 * (Command Inhibit (DAT) stays set; its release raises Data
		 * Transfer Complete - the "Wait Transfer Complete" step of the
		 * Buffer Read Write sequence for busy-only commands). Ending
		 * I/O mode while the busy phase is still running abandons the
		 * DAT FSM mid-transfer; the next data command then starts
		 * against a busy DAT line and deadlocks the controller beyond
		 * what SW_RST can recover (seen with CMD18 right after the
		 * CMD6 boot-partition switch).
		 */
		ret = uefc_mmc_wait_int(host, INT_DAT_CMPLT, UEFC_MMC_DATA_TIMEOUT_US);
		if (ret)
			return uefc_mmc_error(host, cmd);
	}
	uefc_mmc_write(host, TFR_CTRL, TFR_CTRL_IO_END);

	if (cmd->opcode == MMC_STOP_TRANSMISSION)
		uefc_mmc_sw_rst(host, SW_RST_DAT);

	return 0;
}

/*
 * Issue a command. Read data is software-calibrated: sweep the SDR sample
 * point (SAMPLE_ADJ bits 2:0) until the hardware data CRC passes, and remember
 * the winning point for subsequent transfers (re-sweep if it drifts). Only
 * reads are swept - re-issuing the command re-reads harmlessly.
 */
static int uefc_mmc_command(struct uefc_mmc *host, struct mmc_command *cmd,
			    struct mmc_data *data)
{
	bool cal = data && (data->flags & MMC_DATA_READ);
	unsigned int try, pt;
	int ret = -EIO;

	if (!cal)
		return uefc_mmc_do_command(host, cmd, data);

	/* Start from the last known-good sample point, then sweep the rest. */
	pt = host->cal_sp >= 0 ? host->cal_sp : 0;
	for (try = 0; try <= UEFC_MMC_SP_MAX; try++) {
		uefc_mmc_set_sample(host, pt);
		host->cal_sp = pt;
		ret = uefc_mmc_do_command(host, cmd, data);
		if (!ret) {
			if (try)
				dev_info(host->uefc->dev,
					 "cal: CMD%u clean at SDR sample point %u\n",
					 cmd->opcode, pt);
			return 0;
		}
		pt = (pt + 1) % (UEFC_MMC_SP_MAX + 1);
	}

	host->cal_sp = -1;
	return ret;
}

static void uefc_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct uefc_mmc *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;
	int ret;

	mutex_lock(&host->uefc->tfr_lock);

	/*
	 * Closed-ended transfers issue CMD23 (SET_BLOCK_COUNT) first; open-ended
	 * ones are terminated with CMD12 (mrq->stop) afterwards. See the eMMC
	 * "Block Count Related Register Setting" table in the UEFC spec.
	 */
	if (mrq->sbc)
		mrq->sbc->error = uefc_mmc_command(host, mrq->sbc, NULL);

	ret = uefc_mmc_command(host, mrq->cmd, data);
	mrq->cmd->error = (data && data->error) ? 0 : ret;

	if (mrq->stop) {
		/*
		 * A CMD23-bounded (closed-ended) transfer auto-stops after the
		 * programmed block count, so CMD12 is neither needed nor valid
		 * - the card is already back in tran and times the command out.
		 * Only open-ended transfers (no sbc) need the real CMD12.
		 */
		if (mrq->sbc)
			mrq->stop->error = 0;
		else
			mrq->stop->error = uefc_mmc_command(host, mrq->stop, NULL);
	}

	mutex_unlock(&host->uefc->tfr_lock);

	mmc_request_done(mmc, mrq);
}

static int uefc_mmc_get_cd(struct mmc_host *mmc)
{
	/* eMMC is non-removable; the card is always present. */
	return 1;
}

static const struct mmc_host_ops uefc_mmc_ops = {
	.request	= uefc_mmc_request,
	.set_ios	= uefc_mmc_set_ios,
	.get_cd		= uefc_mmc_get_cd,
};

/*
 * (Re)program the static controller configuration. Called at probe and from
 * the SW_RST force-release recovery, so it must not clobber runtime clock
 * state: keep the Slow Clock bit as-is.
 */
static void uefc_mmc_init_regs(struct uefc_mmc *host)
{
	uefc_update_register(host->uefc, CLK_CTRL, ~CLK_CTRL_SLOW_CLOCK,
			     CLK_CTRL_RX_SS_A(1) | CLK_CTRL_RX_SS_B(1));

	uefc_mmc_write(host, INT_STS_EN, INT_STS_ALL_EN);
	uefc_mmc_write(host, ERR_INT_STS_EN, ERR_INT_STS_ALL_EN);
	/* Nothing drives HC_INTRPT_O until uefc_mmc_wait_int() arms it. */
	uefc_mmc_write(host, INT_STS_SIG, 0);

	/*
	 * Select this host's channel / port and the eMMC SIO shifter mode, and
	 * force two shared HC_CTRL bits the xSPI child leaves set back to 0:
	 *  - DATA_ORDER: OctaFlash/OctaRAM byte-order swap; if set it swaps the
	 *    DAT byte order and every data transfer fails its CRC.
	 *  - EX_SER (this channel): use SERDES. This FPGA has no PHY, so SERDES
	 *    read sampling can't be calibrated and corrupts data; clear it to use
	 *    the IO Buffer (direct SCLK-edge sampling), which is right for SDR eMMC.
	 * Both are shared (Cat. A), so include them in the mask to clear them here.
	 */
	uefc_update_register(host->uefc, HC_CTRL,
			     HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK |
			     HC_CTRL_SIO_SHIFTER_MASK | HC_CTRL_DATA_ORDER |
			     (host->chan->id == CHANNEL_B ? HC_CTRL_EX_SER_B : HC_CTRL_EX_SER_A),
			     (host->chan->id == CHANNEL_B ? HC_CTRL_CH_SEL : 0) |
			     HC_CTRL_PORT_SEL(0) | HC_CTRL_SIO_SHIFTER(2));

	/* Mark the device as eMMC and start from the slowest clock divider. */
	uefc_update_d_register(host->uefc, host->chan->id, 0, DEV_CTRL,
			       DEV_CTRL_TYPE_MASK | DEV_CTRL_SCLK_SEL_MASK,
			       DEV_CTRL_TYPE_EMMC | DEV_CTRL_SCLK_SEL(15));

	/*
	 * DAT timeout = CLK_SYS x 2^23 (~168 ms at 50 MHz). The CA field is the
	 * periodic "calibration requirement" alarm timer - we do not service
	 * tuning, so disable it instead of letting CA_REQ latch forever.
	 */
	uefc_mmc_write(host, TO_CTRL, TO_CTRL_CA(0) | TO_CTRL_DAT(0xA));

	uefc_mmc_write(host, TFR_CTRL, TFR_CTRL_HC_ACT);
}

static void uefc_mmc_hw_init(struct uefc_mmc *host)
{
	uefc_mmc_sw_rst(host, SW_RST_ALL);
	uefc_mmc_init_regs(host);
}

static int uefc_mmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_common *uefc;
	struct uefc_mmc *host;
	struct mmc_host *mmc;
	unsigned long ss_rate;
	u32 version;
	u32 reg;
	int ret;

	ret = of_property_read_u32(dev->of_node, "reg", &reg);
	if (ret)
		return ret;
	if (reg >= UEFC_CHANNELS_NB)
		return -EINVAL;

	uefc = dev_get_drvdata(dev->parent);
	if (!uefc)
		return -ENODEV;

	mmc = mmc_alloc_host(sizeof(*host), dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->uefc = uefc;
	host->cal_sp = -1;

	host->blkbuf = devm_kzalloc(dev, UEFC_MMC_MAX_BLK_SIZE, GFP_KERNEL);
	if (!host->blkbuf) {
		ret = -ENOMEM;
		goto err_free_host;
	}

	/*
	 * HC_INTRPT_O -> Zynq IRQ_F2P[0], declared on the MFD parent node
	 * ("status"). Bitstreams without the wiring have no interrupts
	 * property there; fall back to polling so both keep working.
	 */
	init_completion(&host->irq_evt);
	host->irq = irq_of_parse_and_map(dev->parent->of_node, 0);
	if (host->irq > 0) {
		ret = devm_request_irq(dev, host->irq, uefc_mmc_irq, 0,
				       dev_name(dev), host);
		if (ret) {
			dev_warn(dev, "IRQ %d request failed (%d), polling\n",
				 host->irq, ret);
			host->irq = 0;
		} else {
			dev_info(dev, "using IRQ %d\n", host->irq);
		}
	} else {
		host->irq = 0;
		dev_info(dev, "no IRQ wired, polling\n");
	}

	host->vcc = devm_gpiod_get_optional(dev, "vcc", GPIOD_OUT_LOW);
	if (IS_ERR(host->vcc)) {
		ret = dev_err_probe(dev, PTR_ERR(host->vcc), "Failed to get vcc GPIO\n");
		goto err_free_host;
	}

	host->vccq = devm_gpiod_get_optional(dev, "vccq", GPIOD_OUT_LOW);
	if (IS_ERR(host->vccq)) {
		ret = dev_err_probe(dev, PTR_ERR(host->vccq), "Failed to get vccq GPIO\n");
		goto err_free_host;
	}

	ret = uefc_read_register(uefc, HC_VER, &version);
	if (ret)
		goto err_free_host;
	dev_info(dev, "UEFC host controller version %#x\n", version);

	ret = uefc_init_channel(uefc, reg, &host->chan);
	if (ret)
		goto err_free_host;

	uefc_mmc_hw_init(host);

	ss_rate = clk_get_rate(uefc->ss_clk);
	if (!ss_rate) {
		ret = dev_err_probe(dev, -EINVAL, "ss clock has no rate\n");
		goto err_release_chan;
	}

	mmc->ops = &uefc_mmc_ops;
	/*
	 * Identification is served by the Slow Clock (CLK_SS_I / 1024), which is
	 * below the 400 kHz the core asks for; everything faster comes from the
	 * SCLK_SEL divider, whose fastest step is CLK_SS_I / 2.
	 */
	mmc->f_min = 400000;
	mmc->f_max = ss_rate / 2;
	mmc->ocr_avail = MMC_VDD_165_195 | MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps |= MMC_CAP_NONREMOVABLE | MMC_CAP_CMD23;
	/*
	 * R1b busy phases are waited out in uefc_mmc_do_command() with the data
	 * timeout; tell the core so longer-busy commands (e.g. cache flush) are
	 * converted to R1 + CMD13 polling instead.
	 */
	mmc->max_busy_timeout = UEFC_MMC_DATA_TIMEOUT_US / USEC_PER_MSEC;
	mmc->max_blk_size = UEFC_MMC_MAX_BLK_SIZE;
	mmc->max_blk_count = UEFC_MMC_MAX_BLK_COUNT;
	mmc->max_segs = 128;
	mmc->max_req_size = UEFC_MMC_MAX_BLK_SIZE * UEFC_MMC_MAX_BLK_COUNT;
	mmc->max_seg_size = mmc->max_req_size;

	/* Pulls in bus-width, max-frequency, speed-mode and removable caps. */
	ret = mmc_of_parse(mmc);
	if (ret)
		goto err_release_chan;

	/* "max-frequency" is taken verbatim, so re-apply the divider ceiling. */
	mmc->f_max = min_t(unsigned int, mmc->f_max, ss_rate / 2);

	/*
	 * The FPGA's UEFC DMA master only decodes AXI addresses below
	 * 0x20000000 (per the RTL block design), while dma_alloc_coherent()
	 * on this 1 GiB board hands out top-of-RAM pages (~0x3c000000) the
	 * master cannot reach. Bind the DT "memory-region" carveout
	 * (uefc_dma@10000000) to the parent so that the coherent buffers its
	 * DMA-capable children allocate from it land inside that window. This
	 * host runs its own data phase in PIO and needs none of it - see
	 * uefc_mmc_xfer_data() for why.
	 */
	ret = of_reserved_mem_device_init(uefc->dev);
	if (ret && ret != -EEXIST)
		dev_warn(dev,
			 "no DMA carveout (%d), DMA buffers may sit above the AXI master window\n",
			 ret);

	ret = mmc_add_host(mmc);
	if (ret)
		goto err_release_chan;

	platform_set_drvdata(pdev, host);
	return 0;

err_release_chan:
	uefc_release_channel(uefc, host->chan);
err_free_host:
	mmc_free_host(mmc);
	return ret;
}

static void uefc_mmc_remove(struct platform_device *pdev)
{
	struct uefc_mmc *host = platform_get_drvdata(pdev);

	mmc_remove_host(host->mmc);
	uefc_release_channel(host->uefc, host->chan);
	mmc_free_host(host->mmc);
}

static const struct of_device_id uefc_mmc_of_match[] = {
	{ .compatible = "mxic,uefc-mmc", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, uefc_mmc_of_match);

static struct platform_driver uefc_mmc_driver = {
	.probe	= uefc_mmc_probe,
	.remove	= uefc_mmc_remove,
	.driver	= {
		.name		= "uefc-mmc",
		.of_match_table	= uefc_mmc_of_match,
	},
};
module_platform_driver(uefc_mmc_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("eMMC driver for the Macronix UEFC");
