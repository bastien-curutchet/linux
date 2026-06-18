// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mfd/uefc-common.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/spinlock.h>

/* Host Controller Register */
#define HC_CTRL				0x00
#define HC_CTRL_RQE_EN			BIT(31)
#define HC_CTRL_SDMA_BD(x)		(((x) & 0x7) << 28)
#define HC_CTRL_PARALLEL_1		BIT(27)
#define HC_CTRL_PARALLEL_0		BIT(26)
#define HC_CTRL_DATA_ORDER		BIT(25) //OctaFlash, OctaRAM
#define HC_CTRL_SIO_SHIFTER(x)		(((x) & 0x3) << 23)
#define HC_CTRL_EX_SER_B		BIT(22)
#define HC_CTRL_EX_SER_A		BIT(21)
#define HC_CTRL_ASSIMI_BYTE_B(x)	(((x) & 0x3) << 19)
#define HC_CTRL_ASSIMI_BYTE_A(x)	(((x) & 0x3) << 17)
#define HC_CTRL_EX_PHY_ITE_B		BIT(16)
#define HC_CTRL_EX_PHY_ITE_A		BIT(15)
#define HC_CTRL_EX_PHY_DQS_B		BIT(14)
#define HC_CTRL_EX_PHY_DQS_A		BIT(13)
#define HC_CTRL_LED			BIT(12)
#define HC_CTRL_CH_SEL			BIT(11)
#define HC_CTRL_CH_SEL_B		BIT(11)
#define HC_CTRL_CH_SEL_A		0
#define HC_CTRL_LUN_SEL(x)		(((x) & 0x7) << 8) //NAND
#define HC_CTRL_PORT_SEL(x)		(((x) & 0xFF) << 0)
#define HC_CTRL_PORT_SEL_0		0
#define HC_CTRL_PORT_CH_MASK		(HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL(0xff))
#define HC_CTRL_PORT_0_CH_B		(HC_CTRL_PORT_SEL_0 | HC_CTRL_CH_SEL_B)
#define HC_CTRL_PORT_0_CH_A		(HC_CTRL_PORT_SEL_0 | HC_CTRL_CH_SEL_A)

/* Normal Interrupt Status Register */
#define INT_STS				0x04
#define INT_STS_CA_REQ			BIT(30)
#define INT_STS_CACHE_RDY		BIT(29)
#define INT_STS_AC_RDY			BIT(28)
#define INT_STS_ERR_INT			BIT(15)
#define INT_STS_CQE_INT			BIT(14)
#define INT_STS_DMA_TFR_CMPLT		BIT(7)
#define INT_STS_DMA_INT			BIT(6)
#define INT_STS_BUF_RD_RDY		BIT(5)
#define INT_STS_BUF_WR_RDY		BIT(4)
#define INT_STS_ALL_CLR		(INT_STS_AC_RDY | \
					INT_STS_ERR_INT | \
					INT_STS_DMA_TFR_CMPLT | \
					INT_STS_DMA_INT)

/* Error Interrupt Status Register */
#define ERR_INT_STS			0x08
#define ERR_INT_STS_ECC			BIT(19)
#define ERR_INT_STS_PREAM		BIT(18)
#define ERR_INT_STS_CRC			BIT(17)
#define ERR_INT_STS_AC			BIT(16)
#define ERR_INT_STS_ADMA		BIT(9)
#define ERR_INT_STS_AUTO_CMD		BIT(8)
#define ERR_INT_STS_DATA_END		BIT(6)
#define ERR_INT_STS_DATA_CRC		BIT(5)
#define ERR_INT_STS_DATA_TIMEOUT	BIT(4)
#define ERR_INT_STS_CMD_IDX		BIT(3)
#define ERR_INT_STS_CMD_END		BIT(2)
#define ERR_INT_STS_CMD_CRC		BIT(1)
#define ERR_INT_STS_CMD_TIMEOUT		BIT(0)
#define ERR_INT_STS_ALL_CLR		(ERR_INT_STS_ECC | \
					ERR_INT_STS_PREAM | \
					ERR_INT_STS_CRC | \
					ERR_INT_STS_AC | \
					ERR_INT_STS_ADMA)

/* Normal Interrupt Status Enable Register */
#define INT_STS_EN			0x0C
#define INT_STS_EN_CA_REQ		BIT(30)
#define INT_STS_EN_CACHE_RDY		BIT(29)
#define INT_STS_EN_AC_RDY		BIT(28)
#define INT_STS_EN_ERR_INT		BIT(15)
#define INT_STS_EN_DMA_TFR_CMPLT	BIT(7)
#define INT_STS_DMA					BIT(6)
#define INT_STS_EN_BUF_RD_RDY		BIT(5)
#define INT_STS_EN_BUF_WR_RDY		BIT(4)
#define INT_STS_EN_DMA_INT		BIT(3)
#define INT_STS_EN_BLK_GAP		BIT(2)
#define INT_STS_EN_DAT_CMPLT		BIT(1)
#define INT_STS_EN_CMD_CMPLT		BIT(0)
#define INT_STS_EN_ALL_EN		(INT_STS_EN_AC_RDY | \
					INT_STS_EN_ERR_INT | \
					INT_STS_EN_DMA_TFR_CMPLT | \
					INT_STS_EN_DMA_INT)

/* Error Interrupt Status Enable Register */
#define ERR_INT_STS_EN			0x10
#define ERR_INT_STS_EN_ECC		BIT(19)
#define ERR_INT_STS_EN_PREAM		BIT(18)
#define ERR_INT_STS_EN_CRC		BIT(17)
#define ERR_INT_STS_EN_AC		BIT(16)
#define ERR_INT_STS_EN_ADMA		BIT(9)
#define ERR_INT_STS_EN_AUTO_CMD		BIT(8)
#define ERR_INT_STS_EN_DATA_END		BIT(6)
#define ERR_INT_STS_EN_DATA_CRC		BIT(5)
#define ERR_INT_STS_EN_DATA_TIMEOUT	BIT(4)
#define ERR_INT_STS_EN_CMD_IDX		BIT(3)
#define ERR_INT_STS_EN_CMD_END		BIT(2)
#define ERR_INT_STS_EN_CMD_CRC		BIT(1)
#define ERR_INT_STS_EN_CMD_TIMEOUT	BIT(0)
#define ERR_INT_STS_EN_ALL_EN		(ERR_INT_STS_EN_ECC | \
					ERR_INT_STS_EN_PREAM | \
					ERR_INT_STS_EN_CRC | \
					ERR_INT_STS_EN_AC | \
					ERR_INT_STS_EN_ADMA)

/* Normal Interrupt Signal Enable Register */
#define INT_STS_SIG_EN			0x14
#define INT_STS_SIG_EN_CA_REQ		BIT(30)
#define INT_STS_SIG_EN_CACHE_RDY	BIT(29)
#define INT_STS_SIG_EN_AC_RDY		BIT(28)
#define INT_STS_SIG_EN_ERR_INT		BIT(15)
#define INT_STS_SIG_EN_DMA_TFR_CMPLT	BIT(7)
#define INT_STS_SIG_EN_BUF_RD_RDY	BIT(5)
#define INT_STS_SIG_EN_BUF_WR_RDY	BIT(4)
#define INT_STS_SIG_EN_DMA_INT		BIT(3)
#define INT_STS_SIG_EN_BLK_GAP		BIT(2)
#define INT_STS_SIG_EN_DAT_CMPLT	BIT(1)
#define INT_STS_SIG_EN_CMD_CMPLT	BIT(0)
#define INT_STS_SIG_EN_ALL_EN		(INT_STS_SIG_EN_AC_RDY | \
					INT_STS_SIG_EN_ERR_INT | \
					INT_STS_SIG_EN_DMA_TFR_CMPLT | \
					INT_STS_SIG_EN_DMA_INT)

/* Error Interrupt Signal Enable Register */
#define ERR_INT_STS_SIG_EN		0x18
#define ERR_INT_STS_SIG_EN_ECC		BIT(19)
#define ERR_INT_STS_SIG_EN_PREAM	BIT(18)
#define ERR_INT_STS_SIG_EN_CRC		BIT(17)
#define ERR_INT_STS_SIG_EN_AC		BIT(16)
#define ERR_INT_STS_SIG_EN_ADMA		BIT(9)
#define ERR_INT_STS_SIG_EN_AUTO_CMD	BIT(8)
#define ERR_INT_STS_SIG_EN_DATA_END	BIT(6)
#define ERR_INT_STS_SIG_EN_DATA_CRC	BIT(5)
#define ERR_INT_STS_SIG_EN_DATA_TIMEOUT BIT(4)
#define ERR_INT_STS_SIG_EN_CMD_IDX	BIT(3)
#define ERR_INT_STS_SIG_EN_CMD_END	BIT(2)
#define ERR_INT_STS_SIG_EN_CMD_CRC	BIT(1)
#define ERR_INT_STS_SIG_EN_CMD_TIMEOUT	BIT(0)
#define ERR_INT_STS_SIG_EN_ALL_EN	(ERR_INT_STS_SIG_EN_ECC | \
					ERR_INT_STS_SIG_EN_PREAM | \
					ERR_INT_STS_SIG_EN_CRC | \
					ERR_INT_STS_SIG_EN_AC | \
					ERR_INT_STS_SIG_EN_ADMA)

/* Transfer Mode register */
#define TFR_MODE			0x1C
#define TFR_MODE_BUSW_1			0
#define TFR_MODE_BUSW_2			1
#define TFR_MODE_BUSW_4			2
#define TFR_MODE_BUSW_8			3
#define TFR_MODE_DMA_TYPE		BIT(31)
#define TFR_MODE_DMA_KEEP_CSB		BIT(30)
#define TFR_MODE_TO_ENHC		BIT(29)
#define TFR_MODE_PREAM_WITH		BIT(28)
#define TFR_MODE_CSB_DONT_CARE		BIT(27)
#define TFR_MODE_SIO_1X_RD_BUS(x)	(((x) & 0x3) << 6)
#define TFR_MODE_MULT_BLK		BIT(5)
#define TFR_MODE_AUTO_CMD(x)		(((x) & 0x3) << 2)
#define TFR_MODE_CNT_EN			BIT(1)
#define TFR_MODE_DMA_EN			BIT(0)


/* share with MAPRD, MAPWR */
	#define OP_DMY_CNT(x)		(((x) & 0x3F) << 21)
	#define OP_ADDR_CNT(x)		(((x) & 0x7) << 18)
	#define OP_CMD_CNT(x)		(((x) - 1) << 17)
	#define OP_DATA_BUSW(x)		(((x) & 0x3) << 14)
	#define OP_DATA_DTR(x)		(((x) & 0x1) << 16)
	#define OP_ADDR_BUSW(x)		(((x) & 0x3) << 11)
	#define OP_ADDR_DTR(x)		(((x) & 0x1) << 13)
	#define OP_CMD_BUSW(x)		(((x) & 0x3) << 8)
	#define OP_CMD_DTR(x)		(((x) & 1) << 10)
	#define OP_DD_RD		BIT(4)

/* Transfer Control Register */
#define TFR_CTRL			0x20
#define TFR_CTRL_DEV_DIS		BIT(18)
#define TFR_CTRL_IO_END			BIT(16)
#define TFR_CTRL_DEV_ACT		BIT(2)
#define TFR_CTRL_HC_ACT			BIT(1)
#define TFR_CTRL_IO_START		BIT(0)

/* Present State Register */
#define PRES_STS			0x24
#define PRES_STS_ADMA(x)		(((x) & 0x7) << 29)
#define PRES_STS_XSPI_TX(x)		(((x) & 0xF) << 25)
#define PRES_STS_ONFI_TX(x)		(((x) & 0x1F) << 20)
#define PRES_STS_RX_NFULL		BIT(19)
#define PRES_STS_RX_NEMPT		BIT(18)
#define PRES_STS_TX_NFULL		BIT(17)
#define PRES_STS_TX_EMPT		BIT(16)
#define PRES_STS_EMMC_TX(x)		(((x) & 0xF) << 12)
#define PRES_STS_BUF_RD_EN		BIT(11)
#define PRES_STS_BUF_WR_EN		BIT(10)
#define PRES_STS_RD_TFR			BIT(9)
#define PRES_STS_WR_TFR			BIT(8)
#define PRES_STS_DAT_ACT		BIT(2)
#define PRES_STS_CMD_INH_DAT		BIT(1)
#define PRES_STS_CMD_INH_CMD		BIT(0)

/* SDMA Transfer Count Register */
#define SDMA_CNT			0x28
#define SDMA_CNT_TFR_BYTE(x)		(((x) & 0xFFFFFFFF) << 0)

/* SDMA System Address Register */
#define SDMA_ADDR			0x2C
#define SDMA_ADDR_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* ADMA2 System Address Register */
#define ADMA2_ADDR			0x30
#define ADMA2_ADDR_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* ADMA3 Integrated Descriptor Address Register */
#define ADMA3_ADDR			0x34
#define ADMA3_ADDR_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* Mapping Base Address Register */
#define BASE_MAP_ADDR			0x38
#define BASE_MAP_ADDR_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* Software Reset Register */
#define SW_RST				0x44
#define SW_RST_DAT			BIT(2)
#define SW_RST_CMD			BIT(1)
#define SW_RST_ALL			BIT(0)

/* Timeout Control register */
#define TO_CTRL				0x48
#define TO_CTRL_CA(x)			(((x) & 0xF) << 16)
#define TO_CTRL_DAT(x)			(((x) & 0xF) << 0)

/* Clock Control Register */
#define CLK_CTRL			0x4C
#define CLK_CTRL_SLOW_CLOCK		BIT(31)
#define CLK_CTRL_RX_SS_B(x)		(((x) & 0x1F) << 21)
#define CLK_CTRL_RX_SS_A(x)		(((x) & 0x1F) << 16)
#define CLK_CTRL_PLL_SELECT(x)		(((x) & 0xFFFF) << 0)

/* Cache Control Register */
#define CACHE_CTRL			0x54
#define CACHE_CTRL_DIRTY_LEVEL(x)	(((x) & 0x3) << 30)
#define CACHE_CTRL_LEN_TH(x)		(((x) & 0xFF) << 22)
#define CACHE_CTRL_CONT_ADDR		BIT(21)
#define CACHE_CTRL_FETCH_CNT(x)		(((x) & 0x7) << 18)
#define CACHE_CTRL_MST(x)		(((x) & 0xFFFF) << 2)
#define CACHE_CTRL_CLEAN		BIT(1)
#define CACHE_CTRL_INVALID		BIT(0)

/* Capabilities Register */
#define CAP_1				0x58
#define CAP_1_DUAL_CH			BIT(31)
#define CAP_1_XSPI_ITF			BIT(30)
#define CAP_1_ONFI_ITF			BIT(29)
#define CAP_1_EMMC_ITF			BIT(28)
#define CAP_1_MAPPING_MODE		BIT(27)
#define CAP_1_CACHE			BIT(26)
#define CAP_1_ATOMIC			BIT(25)
#define CAP_1_DMA_SLAVE_MODE		BIT(24)
#define CAP_1_DMA_MASTER_MODE		BIT(23)
#define CAP_1_CQE			BIT(22)
#define CAP_1_FIFO_DEPTH(x)		(((x) & 0x3) << 15)
#define CAP_1_SYS_DW(x)			(((x) & 0x3) << 13)
#define CAP_1_LUN_NUM(x)		(((x) & 0xF) << 9)
#define CAP_1_CSB_NUM(x)		(((x) & 0x1FF) << 0)

/* Host Controller Version Register */
#define HC_VER				0x5C
#define HC_VER_VALUE(x)			(((x) & 0xFFFFFFFF) << 0)

/*  RTL Version Register */
#define RTL_VER				0x60
#define RTL_VER_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* Transmit Data 0~3 Register */
#define TXD_REG				0x70
#define TXD(x)				(TXD_REG + ((x) * 4))

/* Receive Data Register */
#define RXD_REG				0x80
#define RXD_VALUE(x)			(((x) & 0xFFFFFFFF) << 0)

/* Send CRC Cycle Register */
#define SEND_CRC_CYC			0x84
#define SEND_CRC_CYC_EN			BIT(0)

/* Block Count Register */
#define BLK_CNT				0x90
#define BLK_CNT_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* Argument Register */
#define ARG_REG				0x94
#define ARG_REG_CMD(x)			(((x) & 0xFFFFFFFF) << 0)

/* Command Register */
#define CMD_REG				0x98
#define CMD_REG_BOOT_BUS(x)		(((x) & 0x7) << 19)
#define CMD_REG_BOOT_TYPE		BIT(18)
#define CMD_REG_BOOT_ACK_EN		BIT(17)
#define CMD_REG_BOOT_EN			BIT(16)
#define CMD_REG_CMD_IDX(x)		(((x) & 0x3F) << 8)
#define CMD_REG_WR_CRC_STS_EN		BIT(6)
#define CMD_REG_DAT_EN			BIT(5)
#define CMD_REG_CMD_IDX_CHK_EN		BIT(4)
#define CMD_REG_CMD_CRC_CHK_EN		BIT(3)
#define CMD_REG_RSP_SEL(x)		(((x) & 0x3) << 0)

/* Response 1 Register */
#define RSP_1				0x9C
#define RSP_1_VALUE(x)			(((x) & 0xFFFFFFFF) << 0)

/* Response 2 Register */
#define RSP_2				0xA0
#define RSP_2_VALUE(x)			(((x) & 0xFFFFFFFF) << 0)

/* Response 3 Register */
#define RSP_3				0xA4
#define RSP_3_VALUE(x)			(((x) & 0xFFFFFFFF) << 0)

/* Response 4 Register */
#define RSP_4				0xA8
#define RSP_4_1_VALUE(x)		(((x) & 0xFF) << 0)
#define RSP_4_0_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* Buffer Data Port register */
#define DATA_REG			0xAC
#define DATA_REG_BUF(x)			(((x) & 0xFFFFFFFF) << 0)

/* Auto CMD Argument Register */
#define AUTO_CMD			0xB0
#define AUTO_CMD_ARGU(x)		(((x) & 0xFFFFFFFF) << 0)

/* Auto CMD Error Status Register */
#define AUTO_CMD_ERR_STS		0xB4
#define AUTO_CMD_ERR_STS_IDX		BIT(4)
#define AUTO_CMD_ERR_STS_END		BIT(3)
#define AUTO_CMD_ERR_STS_CRC		BIT(2)
#define AUTO_CMD_ERR_STS_TIMEOUT	BIT(1)

/* Boot System Address Register */
#define BOOT_SYS_ADDR			0xB8
#define BOOT_SYS_ADDR_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* Block Gap Control Register */
#define BLK_GAP_CTRL			0xBC
#define BLK_GAP_CTRL_CONT_REQ		BIT(1)
#define BLK_GAP_CTRL_STOP_GAP		BIT(0)

/* Category D Registers */
#define CAT_D_PORT_SHIFT		8
#define CAT_D_PORT(x)			(((x) & 0xFF) << CAT_D_PORT_SHIFT)
#define CAT_D_PORT_MASK			CAT_D_PORT(0xFF)
#define CAT_D_SELECT_CHAN_B		BIT(CAT_D_PORT_SHIFT + 11)
/* Device Present Status Register */
#define DEV_CTRL			0xC0
#define DEV_CTRL_TYPE(x)		(((x) & 0x7) << 29)
#define DEV_CTRL_TYPE_MASK		DEV_CTRL_TYPE(0x7)
#define DEV_CTRL_TYPE_SPI		(0 << 29)
#define DEV_CTRL_TYPE_LYBRA		(1 << 29)
#define DEV_CTRL_TYPE_OCTARAM		(2 << 29)
#define DEV_CTRL_TYPE_RAWNAND_ONFI	(4 << 29)
#define DEV_CTRL_TYPE_RAWNAND_JEDEC	(5 << 29)
#define DEV_CTRL_TYPE_RAWNAND_EMMC	(6 << 29)
#define DEV_CTRL_SCLK_SEL(x)		(((x) & 0xF) << 25)
#define DEV_CTRL_SCLK_SEL_MASK		DEV_CTRL_SCLK_SEL(0xF)
#define DEV_CTRL_SCLK_SEL_DIV(x)	((((x) >> 1) - 1) << 25)
#define DEV_CTRL_CACHEABLE		BIT(24)
#define DEV_CTRL_WR_PLCY(x)		(((x) & 0x3) << 22)
#define DEV_CTRL_PAGE_SIZE(x)		(((x) & 0x7) << 19)
#define DEV_CTRL_BLK_SIZE(x)		(((x) & 0xFFF) << 7)
#define DEV_CTRL_PRE_DQS_EN		BIT(6)
#define DEV_CTRL_DQS_EN			BIT(5)
#define DEV_CTRL_CRC_EN			BIT(4)
#define DEV_CTRL_CRCB_IN_EN		BIT(3)
#define DEV_CTRL_CRC_CHUNK_SIZE(x)	(((x) & 0x3) << 1)
#define DEV_CTRL_CRCB_OUT_EN		BIT(0)

/* Mapping Read Control Register */
#define MAP_RD_CTRL			0xC4
#define MAP_RD_CTRL_PREAM_EN		BIT(28)
#define MAP_RD_CTRL_SIO_1X_RD(x)	(((x) & 0x3) << 6)

/* Linear/Mapping Write Control Register */
#define MAP_WR_CTRL			0xC8

/* Mapping Command Register */
#define MAP_CMD_RD			0xCC
#define MAP_CMD_WR			0xCE

/* Top Mapping Address Register */
#define TOP_MAP_ADDR			0xD0
#define TOP_MAP_ADDR_VALUE(x)		(((x) & 0xFFFFFFFF) << 0)

/* General Purpose Inputs and Outputs Register */
#define GPIO_REG			0xD4
#define GPIO_REG_DATA_LEVEL(x)		(((x) & 0xFF) << 24)
#define GPIO_REG_RYBYB_LEVE		BIT(23)
#define GPIO_REG_CMD_LEVEL		BIT(22)
#define GPIO_REG_SIO3_EN		BIT(13)
#define GPIO_REG_SIO2_EN		BIT(12)
#define GPIO_REG_SIO3_DRIV_HIGH		BIT(5)
#define GPIO_REG_SIO2_DRIV_HIGH		BIT(4)
#define GPIO_REG_HP_DRIV_HIGH		BIT(3)
#define GPIO_REG_RESTB_DRIV_HIGH	BIT(2)
#define GPIO_REG_HOLDB_DRIV_HIGH	BIT(1)
#define GPIO_REG_WPB_DRIV_HIGH		BIT(0)

/* Auto Chlibration Control Register */
#define AC_CTRL				0xD8
#define AC_CTRL_CMD_2(x)		(((x) & 0xFF) << 24)
#define AC_CTRL_CMD_1(x)		(((x) & 0xFF) << 16)
#define AC_CTRL_WINDOW(x)		(((x) & 0x3) << 14)
#define AC_CTRL_LAZY_DQS_EN		BIT(9)
#define AC_CTRL_LEN_32B_SEL		BIT(8)
#define AC_CTRL_PHY_EN			BIT(6)
#define AC_CTRL_DQS_TEST_EN		BIT(5)
#define AC_CTRL_SIO_ALIG_EN		BIT(4)
#define AC_CTRL_NVDDR_EN		BIT(3)
#define AC_CTRL_SAMPLE_DQS_EN		BIT(2)
#define AC_CTRL_SAMPLE_EN		BIT(1)
#define AC_CTRL_START			BIT(0)

/* Preamble Bit 1 Register */
#define PREAM_1_REG			0xDC
#define PREAM_1_REG_SIO_1(x)		(((x) & 0xFFFF) << 16)
#define PREAM_1_REG_SIO_0(x)		(((x) & 0xFFFF) << 0)

/* Preamble Bit 2 Register */
#define PREAM_2_REG			0xE0
#define PREAM_2_REG_SIO_3(x)		(((x) & 0xFFFF) << 16)
#define PREAM_2_REG_SIO_2(x)		(((x) & 0xFFFF) << 0)

/* Preamble Bit 3 Register */
#define PREAM_3_REG 0xE4
#define PREAM_3_REG_SIO_5(x)		(((x) & 0xFFFF) << 16)
#define PREAM_3_REG_SIO_4(x)		(((x) & 0xFFFF) << 0)

/* Preamble Bit 4 Register */
#define PREAM_4_REG 0xE8
#define PREAM_4_REG_SIO_7(x)		(((x) & 0xFFFF) << 16)
#define PREAM_4_REG_SIO_6(x)		(((x) & 0xFFFF) << 0)

/* Sample Point Adjust Register */
#define SAMPLE_ADJ 0xEC
#define SAMPLE_ADJ_DQS_IDLY_DOPI(x)	(((x) & 0xFF) << 24)
#define SAMPLE_ADJ_DQS_IDLY_SOPI(x)	(((x) & 0xFF) << 16)
#define SAMPLE_ADJ_DQS_ODLY(x)		(((x) & 0xFF) << 8)
#define SAMPLE_ADJ_POINT_SEL_DDR(x)	(((x) & 0x7) << 3)
#define SAMPLE_ADJ_POINT_SEL_SDR(x)	(((x) & 0x7) << 0)

/* SIO Input Delay 1 Register */
#define SIO_IDLY_1 0xF0
#define SIO_IDLY_1_SIO3(x)		(((x) & 0xFF) << 24)
#define SIO_IDLY_1_SIO2(x)		(((x) & 0xFF) << 16)
#define SIO_IDLY_1_SIO1(x)		(((x) & 0xFF) << 8)
#define SIO_IDLY_1_SIO0(x)		(((x) & 0xFF) << 0)

/* SIO Input Delay 2 Register */
#define SIO_IDLY_2 0xF4
#define SIO_IDLY_2_SIO4(x)		(((x) & 0xFF) << 24)
#define SIO_IDLY_2_SIO5(x)		(((x) & 0xFF) << 16)
#define SIO_IDLY_2_SIO6(x)		(((x) & 0xFF) << 8)
#define SIO_IDLY_2_SIO7(x)		(((x) & 0xFF) << 0)
#define IDLY_CODE_VAL(x, v)		((v) << (((x) % 4) * 8))

/* SIO Output Delay 1 Register */
#define SIO_ODLY_1			0xF8
#define SIO_ODLY_1_SIO3(x)		(((x) & 0xFF) << 24)
#define SIO_ODLY_1_SIO2(x)		(((x) & 0xFF) << 16)
#define SIO_ODLY_1_SIO1(x)		(((x) & 0xFF) << 8)
#define SIO_ODLY_1_SIO0(x)		(((x) & 0xFF) << 0)

/* SIO Output Delay 2 Register */
#define SIO_ODLY_2			0xFC
#define SIO_ODLY_2_SIO4(x)		(((x) & 0xFF) << 24)
#define SIO_ODLY_2_SIO5(x)		(((x) & 0xFF) << 16)
#define SIO_ODLY_2_SIO6(x)		(((x) & 0xFF) << 8)
#define SIO_ODLY_2_SIO7(x)		(((x) & 0xFF) << 0)

#define UEFC_REG_HOST_CTRL		0x00
#define UEFC_HC_CH_SEL			BIT(11)
#define UEFC_HC_PORT_SEL		0xFF
#define UEFC_REG_CAPABILITIES		0x58
#define UEFC_CAP_DUAL_CHAN		BIT(31)
#define UEFC_CAP_XSPI			BIT(30)
#define UEFC_CAP_ONFI			BIT(29)
#define UEFC_CAP_EMMC			BIT(28)
#define UEFC_CAP_INTERFACES		(UEFC_CAP_XSPI | UEFC_CAP_ONFI | UEFC_CAP_EMMC)
#define UEFC_CAP_MAPPING		BIT(27)
#define UEFC_CAP_CACHE			BIT(26)
#define UEFC_CAP_ATOMIC			BIT(25)
#define UEFC_CAP_DMA_SLAVE		BIT(24)
#define UEFC_CAP_DMA_MASTER		BIT(23)
#define UEFC_REG_HOST_CTRL_VERSION	0x5C
#define UEFC_HCV_CHAN_XSPI		0x1
#define UEFC_HCV_CHAN_ONFI		0x2
#define UEFC_HCV_CHAN_EMMC		0x3
#define UEFC_HCV_CHAN_PROTO_MASK	(UEFC_HCV_CHAN_XSPI \
					| UEFC_HCV_CHAN_ONFI \
					| UEFC_HCV_CHAN_EMMC)
#define UEFC_HCV_CHAN_A_SHIFT		4
#define UEFC_HCV_CHAN_B_SHIFT		9
#define UEFC_HCV_CHAN_A_PROTO_MASK	(UEFC_HCV_CHAN_PROTO_MASK << UEFC_HCV_CHAN_A_SHIFT)
#define UEFC_HCV_CHAN_B_PROTO_MASK	(UEFC_HCV_CHAN_PROTO_MASK << UEFC_HCV_CHAN_B_SHIFT)
#define UEFC_HCV_CHAN_B_MASK		0x600
#define UEFC_REG_RTL_VERSION		0x60

static const struct mfd_cell nand_cell = {
	.name = "nand",
	.of_compatible = "mxic,uefc-nand",
};

static const struct mfd_cell xspi_cell = {
	.name = "xspi",
	.of_compatible = "mxic,uefc-xspi",
};

static const struct mfd_cell memory_cell = {
	.name = "mmc",
	.of_compatible = "mxic,uefc-memory",
};

static const struct regmap_range_cfg uefc_cat_d_range_cfg[] = {
	{
		.name = "Cat. D",
		.range_min = DEV_CTRL,
		.range_max = CAT_D_SELECT_CHAN_B | CAT_D_PORT(0xFF) | SIO_ODLY_2,
		.selector_reg = HC_CTRL,
		.selector_mask = GENMASK(11, 0),
		.selector_shift = 0,
		.window_start = DEV_CTRL,
		.window_len = 0x100,
	}
};

static const struct regmap_config uefc_regmap_config = {
	.fast_io = true,
	.max_register = CAT_D_SELECT_CHAN_B | CAT_D_PORT(0xFF) | SIO_ODLY_2,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.ranges = uefc_cat_d_range_cfg,
	.num_ranges = ARRAY_SIZE(uefc_cat_d_range_cfg),
};

/**
 * uefc_write_register - Write a value to a single register (Cat. A, B, C or E)
 *
 * @uefc:	UEFC core handler
 * @reg:	Register to write to
 * @val:	Value to be written
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_write_register(struct uefc_common *uefc, unsigned int reg, u32 val)
{
	return regmap_write(uefc->regmap, reg, val);;
}
EXPORT_SYMBOL_GPL(uefc_write_register);

/**
 * uefc_read_register - Read a value from a single register (Cat. A, B, C or E)
 *
 * @uefc:	UEFC core handler
 * @reg:	Register to read from
 * @val:	Pointer to store read value
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_read_register(struct uefc_common *uefc, unsigned int reg, u32 *val)
{
	return regmap_read(uefc->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(uefc_read_register);

/**
 * uefc_update_register -  Perform a read/modify/write cycle on a register (Cat. A, B, C or E)
 *
 * @uefc:	UEFC core handler
 * @reg:	Register to update
 * @mask:	Bitmask to change
 * @val:	New value for bitmask
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_update_register(struct uefc_common *uefc, unsigned int reg, u32 mask, u32 val)
{
	return regmap_update_bits(uefc->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(uefc_update_register);

/**
 * uefc_write_d_register - Write a value to a single Cat. D register
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel ID to which the register is linked
 * @port:	Port number to which the register is linked
 * @reg:	Register to write to
 * @val:	Value to be written
 *
 * Selects the relevant port / channel in the Host Controller register
 * before updating the Cat. D register.
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_write_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			  unsigned int reg, u32 val)
{
	reg |= chan == CHANNEL_B ? CAT_D_SELECT_CHAN_B : 0;
	reg |= CAT_D_PORT(port);

	return regmap_write(uefc->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(uefc_write_d_register);

/**
 * uefc_read_d_register - Read a value from a single Cat. D register
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel ID to which the register is linked
 * @port:	Port number to which the register is linked
 * @reg:	Register to write to
 * @val:	Value to be written
 *
 * Selects the relevant port / channel in the Host Controller register
 * before reading the Cat. D register.
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_read_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			 unsigned int reg, u32 *val)
{
	reg |= chan == CHANNEL_B ? CAT_D_SELECT_CHAN_B : 0;
	reg |= CAT_D_PORT(port);

	return regmap_read(uefc->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(uefc_read_d_register);

/**
 * uefc_update_d_register -  Perform a read/modify/write cycle on a Cat. D register
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel ID to which the register is linked
 * @port:	Port number to which the register is linked
 * @reg:	Register to update
 * @mask:	Bitmask to change
 * @val:	New value for bitmask
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_update_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			   unsigned int reg, u32 mask, u32 val)
{
	reg |= chan == CHANNEL_B ? CAT_D_SELECT_CHAN_B : 0;
	reg |= CAT_D_PORT(port);

	return regmap_update_bits(uefc->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(uefc_update_d_register);

/**
 * uefc_init_channel - Initialize a channel
 *
 * @uefc:	UEFC core handler
 * @id:		Channel to initialize
 * @chan_b:	Pointer to the initialized channel
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_init_channel(struct uefc_common *uefc, enum uefc_channel_id id,
			     struct uefc_channel **res)
{
	struct uefc_channel *chan;
	int ret;

	chan = &uefc->chan[id];
	if (atomic_cmpxchg_relaxed(&chan->used,
				   UEFC_CHAN_UNUSED, UEFC_CHAN_USED) != UEFC_CHAN_UNUSED)
		return -EBUSY;

	chan->rx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "rx_a" : "rx_b");
	if (IS_ERR(chan->rx_clk))
		return PTR_ERR(chan->rx_clk);

	chan->tx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "tx_a" : "tx_b");
	if (IS_ERR(chan->tx_clk))
		return PTR_ERR(chan->tx_clk);

	ret = clk_prepare_enable(chan->rx_clk);
	if (ret)
		return ret;
	ret = clk_prepare_enable(chan->tx_clk);
	if (ret)
		return ret;

	*res = chan;

	return 0;
}
EXPORT_SYMBOL_GPL(uefc_init_channel);

/**
 * uefc_release_channel - Release a channel
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel to release
 */
void uefc_release_channel(struct uefc_common *uefc, struct uefc_channel *chan)
{
	if (chan) {
		if (atomic_xchg_relaxed(&chan->used, UEFC_CHAN_UNUSED) != UEFC_CHAN_USED)
			dev_warn(uefc->dev, "Release already unused channel A");
	}
}
EXPORT_SYMBOL_GPL(uefc_release_channel);

static irqreturn_t uefc_status_handler(int irq, void *dev_id)
{
	pr_info("%s - %d \n", __func__, irq);

	return IRQ_NONE;
}

static irqreturn_t uefc_error_handler(int irq, void *dev_id)
{

	pr_info("%s - %d \n", __func__, irq);

	return IRQ_NONE;
}

static int uefc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_common *uefc;
	struct gpio_desc *reset;
	void __iomem *regs;
	u32 host_version;
	u32 cap_register;
	int irq_status;
	u32 proto[2];
	int irq_err;
	int ret;
	int i;

	uefc = devm_kzalloc(dev, sizeof(*uefc), GFP_KERNEL);
	if (!uefc)
		return -ENOMEM;

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs), "Failed to ioremap resource");

	uefc->regmap = devm_regmap_init_mmio(dev, regs, &uefc_regmap_config);
	if (IS_ERR(uefc->regmap))
		return dev_err_probe(dev, PTR_ERR(uefc->regmap), "Failed to init regmap");

	irq_status = platform_get_irq_byname_optional(pdev, "status");
	if (irq_status < 0 && irq_status != -ENXIO)
		return dev_err_probe(dev, irq_status, "Failed to get status IRQ");
	if (irq_status > 0) {
		ret = devm_request_threaded_irq(dev, irq_status,  NULL, uefc_status_handler,
						IRQF_ONESHOT | IRQF_SHARED, dev_name(dev), uefc);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to setup IRQ status handler");
	}

	irq_err = platform_get_irq_byname_optional(pdev, "error");
	if (irq_err < 0 && irq_err != -ENXIO)
		return dev_err_probe(dev, irq_err, "Failed to get error IRQ");
	if (irq_err > 0) {
		ret = devm_request_threaded_irq(dev, irq_err,  NULL, uefc_error_handler,
						IRQF_ONESHOT | IRQF_SHARED, dev_name(dev), uefc);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to setup IRQ error handler");
	}

	uefc->sys_clk = devm_clk_get(dev, "sys");
	if (IS_ERR(uefc->sys_clk))
		return dev_err_probe(dev, PTR_ERR(uefc->sys_clk), "Failed to get sys clk");

	uefc->ss_clk = devm_clk_get(dev, "ss");
	if (IS_ERR(uefc->ss_clk))
		return dev_err_probe(dev, PTR_ERR(uefc->ss_clk), "Failed to get ss clk");

	reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(reset))
		return dev_err_probe(dev, PTR_ERR(reset), "Failed to get reset");
	if (reset) {
		gpiod_set_value(reset, 1);
		msleep(20);
		gpiod_set_value(reset, 0);
	}

	ret = regmap_read(uefc->regmap, UEFC_REG_HOST_CTRL_VERSION, &host_version);
	if (ret)
		return ret;

	ret = regmap_read(uefc->regmap, UEFC_REG_CAPABILITIES, &cap_register);
	if (ret)
		return ret;

	spin_lock_init(&uefc->reg_lock);
	dev_set_drvdata(dev, uefc);

	ret = -ENODEV;
	proto[0] = (host_version & UEFC_HCV_CHAN_A_PROTO_MASK) >> UEFC_HCV_CHAN_A_SHIFT;
	proto[1] = (host_version & UEFC_HCV_CHAN_B_PROTO_MASK) >> UEFC_HCV_CHAN_B_SHIFT;
	for (i = 0; i < UEFC_CHANNELS_NB; i++) {
		uefc->chan[i].id = i;

		switch (proto[i]) {
		case UEFC_HCV_CHAN_XSPI:
			dev_info(dev, "Channel %s is XSPI\n", i ? "B" : "A");
			ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &xspi_cell,
						   1, NULL, 0, NULL);
			break;
		case UEFC_HCV_CHAN_ONFI:
			dev_info(dev, "Channel %s is ONFI\n", i ? "B" : "A");
			ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &nand_cell,
						   1, NULL, 0, NULL);
			break;
		case UEFC_HCV_CHAN_EMMC:
			dev_info(dev, "Channel %s is EMMC\n", i ? "B" : "A");
			ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, &memory_cell,
						   1, NULL, 0, NULL);
			break;
		default:
			dev_info(dev, "Channel %s is disabled\n", i ? "B" : "A");
		}
	}

	uefc->dev = dev;

	pr_info("%s - host_version %x - caps %x\n", __func__, host_version, cap_register);

	return ret;
}

static const struct of_device_id uefc_of_match[] = {
	{ .compatible = "mxic,uefc", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, uefc_of_match);

static struct platform_driver uefc_driver = {
	.probe = uefc_probe,
	.driver = {
		.name = "uefc",
		.of_match_table = uefc_of_match,
	},
};
module_platform_driver(uefc_driver);

MODULE_DESCRIPTION("Core driver for the Macronix UEFC");
MODULE_LICENSE("GPL");

