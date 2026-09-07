// SPDX-License-Identifier: GPL-2.0+

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mfd/core.h>
#include <linux/mfd/uefc-common.h>
#include <linux/mutex.h>
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
#define HC_CTRL_PORT_SEL(x)		((x) & 0xFF)
#define HC_CTRL_PORT_SEL_MASK		0xFF
#define HC_CTRL_PORT_SEL_0		0
#define HC_CTRL_PORT_CH_MASK		(HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK)
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
#define INT_STS_ALL_CLR			(INT_STS_AC_RDY | \
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
#define INT_STS_DMA			BIT(6)
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
	#define OP_CMD_CNT(x)		(((x) >> 1) << 17)
	#define OP_DATA_BUSW(x)		(((x) & 0x3) << 14)
	#define OP_DATA_DTR(x)		(((x) & 0x1) << 16)
	#define OP_ADDR_BUSW(x)		(((x) & 0x3) << 11)
	#define OP_ADDR_DTR(x)		(((x) & 0x1) << 13)
	#define OP_CMD_BUSW(x)		(((x) & 0x3) << 8)
	#define OP_CMD_DTR(x)		(((x) & 0x1) << 10)
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

/* SDMA System Address Register */
#define SDMA_ADDR			0x2C

/* ADMA2 System Address Register */
#define ADMA2_ADDR			0x30

/* ADMA3 Integrated Descriptor Address Register */
#define ADMA3_ADDR			0x34

/* Mapping Base Address Register */
#define BASE_MAP_ADDR			0x38

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
#define CLK_CTRL_RX_SS_B_MASK		CLK_CTRL_RX_SS_B(0x1F)
#define CLK_CTRL_RX_SS_A(x)		(((x) & 0x1F) << 16)
#define CLK_CTRL_RX_SS_A_MASK		CLK_CTRL_RX_SS_A(0x1F)
#define CLK_CTRL_RX_SS_MAX		32
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
#define HC_VER_CHAN_PROTO_MASK		0x3
#define HC_VER_CHAN_PROTO_XSPI		0x1
#define HC_VER_CHAN_PROTO_ONFI		0x2
#define HC_VER_CHAN_PROTO_EMMC		0x3
#define HC_VER_CHAN_A_SHIFT		4
#define HC_VER_CHAN_B_SHIFT		9
#define HC_VER_CHAN_PROTO(v, shift)	(((v) >> (shift)) & HC_VER_CHAN_PROTO_MASK)

/*  RTL Version Register */
#define RTL_VER				0x60

/* Transmit Data 0~3 Register */
#define TXD_REG				0x70
#define TXD(x)				(TXD_REG + ((x) * 4))

/* Receive Data Register */
#define RXD_REG				0x80

/* Send CRC Cycle Register */
#define SEND_CRC_CYC			0x84
#define SEND_CRC_CYC_EN			BIT(0)

/* Block Count Register */
#define BLK_CNT				0x90

/* Argument Register */
#define ARG_REG				0x94

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

/* Response 2 Register */
#define RSP_2				0xA0

/* Response 3 Register */
#define RSP_3				0xA4

/* Response 4 Register */
#define RSP_4				0xA8
#define RSP_4_1_VALUE(x)		(((x) & 0xFF) << 0)

/* Buffer Data Port register */
#define DATA_REG			0xAC

/* Auto CMD Argument Register */
#define AUTO_CMD			0xB0

/* Auto CMD Error Status Register */
#define AUTO_CMD_ERR_STS		0xB4
#define AUTO_CMD_ERR_STS_IDX		BIT(4)
#define AUTO_CMD_ERR_STS_END		BIT(3)
#define AUTO_CMD_ERR_STS_CRC		BIT(2)
#define AUTO_CMD_ERR_STS_TIMEOUT	BIT(1)

/* Boot System Address Register */
#define BOOT_SYS_ADDR			0xB8

/* Block Gap Control Register */
#define BLK_GAP_CTRL			0xBC
#define BLK_GAP_CTRL_CONT_REQ		BIT(1)
#define BLK_GAP_CTRL_STOP_GAP		BIT(0)

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
/* F(CLK_SS_I)/F(SCLK) = 2 * (SCLK_SEL + 1), so 0xF is the /32 divider. */
#define DEV_CTRL_SCLK_SEL_SLOWEST	0xF
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
	.of_compatible = "mxic,uefc-mmc",
};

static const struct regmap_config uefc_regmap_config = {
	.fast_io = true,
	.max_register = 0x160,
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.disable_locking = true,
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
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_write(uefc->regmap, reg, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

	return ret;
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
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_read(uefc->regmap, reg, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

	return ret;
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
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, reg, mask, val);
	spin_unlock_irqrestore(&uefc->reg_lock, flags);

	return ret;
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
	u32 hc_mask = HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? HC_CTRL_CH_SEL : 0;
	hc_val |= HC_CTRL_PORT_SEL(port);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, HC_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_write(uefc->regmap, reg, val);

out:
	spin_unlock_irqrestore(&uefc->reg_lock, flags);
	return ret;
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
	u32 hc_mask = HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? HC_CTRL_CH_SEL : 0;
	hc_val |= HC_CTRL_PORT_SEL(port);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, HC_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_read(uefc->regmap, reg, val);

out:
	spin_unlock_irqrestore(&uefc->reg_lock, flags);
	return ret;
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
	u32 hc_mask = HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK;
	unsigned long flags;
	u32 hc_val;
	int ret;

	hc_val = (chan == CHANNEL_B) ? HC_CTRL_CH_SEL : 0;
	hc_val |= HC_CTRL_PORT_SEL(port);

	spin_lock_irqsave(&uefc->reg_lock, flags);
	ret = regmap_update_bits(uefc->regmap, HC_CTRL, hc_mask, hc_val);
	if (ret < 0)
		goto out;
	ret = regmap_update_bits(uefc->regmap, reg, mask, val);

out:
	spin_unlock_irqrestore(&uefc->reg_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(uefc_update_d_register);

/**
 * uefc_init_channel - Initialize a channel
 *
 * @uefc:	UEFC core handler
 * @id:		Channel to initialize
 * @res:	Pointer to the initialized channel
 *
 * Returns 0 on success, a negative errno otherwise
 */
int uefc_init_channel(struct uefc_common *uefc, enum uefc_channel_id id,
			     struct uefc_channel **res)
{
	struct uefc_channel *chan;
	unsigned long ss_rate;
	unsigned long ratio;
	int ret;

	chan = &uefc->chan[id];
	if (atomic_cmpxchg_relaxed(&chan->used,
				   UEFC_CHAN_UNUSED, UEFC_CHAN_USED) != UEFC_CHAN_UNUSED)
		return -EBUSY;

	chan->rx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "rx_a" : "rx_b");
	if (IS_ERR(chan->rx_clk)) {
		ret = PTR_ERR(chan->rx_clk);
		goto err_unuse;
	}

	chan->tx_clk = devm_clk_get(uefc->dev, id == CHANNEL_A ? "tx_a" : "tx_b");
	if (IS_ERR(chan->tx_clk)) {
		ret = PTR_ERR(chan->tx_clk);
		goto err_unuse;
	}

	ret = clk_prepare_enable(chan->rx_clk);
	if (ret)
		goto err_unuse;

	ret = clk_prepare_enable(chan->tx_clk);
	if (ret) {
		clk_disable_unprepare(chan->rx_clk);
		goto err_unuse;
	}

	/*
	 * The controller has to be told how much faster the channel's receive
	 * clock runs than the device side clock: the spec defines the RX_SS
	 * field as F(CLK_RX_x_I)/F(CLK_SS_I), encoded as the ratio minus one.
	 * Both rates are described in the device tree, so derive it rather than
	 * assuming a particular clock tree.
	 */
	ratio = clk_get_rate(chan->rx_clk);
	ss_rate = clk_get_rate(uefc->ss_clk);
	if (!ss_rate || !ratio || ratio % ss_rate) {
		dev_err(uefc->dev,
			"rx clock (%lu Hz) must be a multiple of the ss clock (%lu Hz)\n",
			ratio, ss_rate);
		ret = -EINVAL;
		goto err_disable;
	}

	ratio /= ss_rate;
	if (ratio > CLK_CTRL_RX_SS_MAX) {
		dev_err(uefc->dev, "rx/ss clock ratio %lu is above the %u supported\n",
			ratio, CLK_CTRL_RX_SS_MAX);
		ret = -EINVAL;
		goto err_disable;
	}

	ret = uefc_update_register(uefc, CLK_CTRL,
				   id == CHANNEL_A ? CLK_CTRL_RX_SS_A_MASK :
						     CLK_CTRL_RX_SS_B_MASK,
				   id == CHANNEL_A ? CLK_CTRL_RX_SS_A(ratio - 1) :
						     CLK_CTRL_RX_SS_B(ratio - 1));
	if (ret)
		goto err_disable;

	/*
	 * Apply the default PHY sampling configuration: select the DDR/SDR
	 * sample points, run the device clock at the slowest divider and clear
	 * the per-SIO input/output delay lines so the controller starts from a
	 * known, conservative timing. The child drivers raise the clock once they
	 * know what the attached device supports.
	 */
	ret = uefc_write_d_register(uefc, id, 0, SAMPLE_ADJ,
				    SAMPLE_ADJ_POINT_SEL_DDR(5) | SAMPLE_ADJ_POINT_SEL_SDR(3));
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, DEV_CTRL,
				    DEV_CTRL_SCLK_SEL(DEV_CTRL_SCLK_SEL_SLOWEST));
	if (ret)
		goto err_disable;

	ret = uefc_update_d_register(uefc, id, 0, SAMPLE_ADJ,
				     SAMPLE_ADJ_DQS_IDLY_DOPI(0xFF),
				     SAMPLE_ADJ_DQS_IDLY_DOPI(0xFF));
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_IDLY_1, 0);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_IDLY_2, 0);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_ODLY_1, 0);
	if (ret)
		goto err_disable;

	ret = uefc_write_d_register(uefc, id, 0, SIO_ODLY_2, 0);
	if (ret)
		goto err_disable;

	*res = chan;

	return 0;

err_disable:
	clk_disable_unprepare(chan->tx_clk);
	clk_disable_unprepare(chan->rx_clk);
err_unuse:
	/*
	 * Hand the channel back so a later probe attempt (e.g. after a clock
	 * provider shows up and -EPROBE_DEFER is retried) is not rejected with
	 * -EBUSY by the reservation above.
	 */
	atomic_set(&chan->used, UEFC_CHAN_UNUSED);

	return ret;
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
			dev_warn(uefc->dev, "Release already unused channel %s",
				 chan->id == CHANNEL_A ? "A" : "B");

		/* Channel clocks are owned by the channel reservation. */
		if (!IS_ERR_OR_NULL(chan->tx_clk))
			clk_disable_unprepare(chan->tx_clk);
		if (!IS_ERR_OR_NULL(chan->rx_clk))
			clk_disable_unprepare(chan->rx_clk);
	}
}
EXPORT_SYMBOL_GPL(uefc_release_channel);

int uefc_set_io_mode(struct uefc_common *uefc, bool enable)
{
	int ret;
	u32 sts;

	if (enable) {
		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_IO_START);
		if (ret)
			return ret;

		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_HC_ACT);
		if (ret)
			return ret;

		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_DEV_ACT);
	} else {
		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_DEV_DIS);
		if (ret)
			return ret;

		/*
		 * Per the UEFC I/O Mode programming flow, the TX FIFO must be
		 * empty before disabling I/O mode: Disable Device Transfer,
		 * poll TX FIFO until empty, then End I/O Mode. DMA completion
		 * only signals that the data reached the FIFO, so ending I/O
		 * mode right away can cut off bytes still being clocked out to
		 * the device and wedge the protocol FSM (next command stalls).
		 */
		ret = regmap_read_poll_timeout(uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_TX_EMPT,
					       10, 10 * USEC_PER_MSEC);
		if (ret) {
			/*
			 * Report the drain failure, but still end I/O mode:
			 * leaving the host controller in I/O mode guarantees
			 * that every subsequent operation fails, which is worse
			 * than the truncated transfer we are already stuck with.
			 */
			dev_warn(uefc->dev,
				 "TX FIFO not empty on I/O mode exit, PRES_STS %#x\n",
				 sts);
			uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_IO_END);
			return ret;
		}

		ret = uefc_write_register(uefc, TFR_CTRL, TFR_CTRL_IO_END);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(uefc_set_io_mode);

/**
 * uefc_hw_init - Apply the controller-wide configuration
 *
 * @uefc:	UEFC core handler
 *
 * Implements the controller-scoped part of the Set Controller Configuration
 * Sequence from the UEFC specification (v2.12, Programming Guide,
 * Initialization). Only registers that are global to the controller are
 * touched here: the per-channel PHY timing (Sample Point Adjust and the SIO
 * delay lines) is applied by uefc_init_channel(), and the Set Device
 * Configuration Sequence is left to the child drivers.
 *
 * Host Controller Register
 *	SIO_SHIFTER is set to 10b, which shifts SIO half a CLK_TX_I cycle
 *	after SCLK. Devices require SIO to meet a setup/hold window around
 *	the SCLK edge, and this field is the adjustment the controller
 *	provides to satisfy it.
 *
 * Clock Control Register
 *	RX_SS_A and RX_SS_B give the ratio between each channel's receive
 *	clock and CLK_SS_I. The field encodes the ratio minus one, so the 1
 *	programmed here means CLK_RX_x_I runs at twice CLK_SS_I. Both must
 *	match the board clock tree before any device operation.
 *
 * Normal and Error Interrupt Status Registers
 *	The event bits are RW1C, so writing the mask back clears events left
 *	over from a previous boot or reset.
 *
 * Normal and Error Interrupt Status Enable Registers
 *	Gate whether an event is recorded in the status register at all; a
 *	masked event never becomes visible.
 *
 * Normal and Error Interrupt Signal Enable Registers
 *	Gate whether a recorded event is allowed to assert the interrupt line.
 *
 * Note that the closing write to INT_STS_EN replaces the mask programmed
 * earlier in the sequence rather than adding to it, so it alone determines
 * which normal interrupt statuses end up enabled.
 *
 * Returns 0 on success, a negative errno otherwise
 */
static int uefc_hw_init(struct uefc_common *uefc)
{
	int ret;

	ret = uefc_update_register(uefc, HC_CTRL, HC_CTRL_SIO_SHIFTER(3), HC_CTRL_SIO_SHIFTER(2));
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS, INT_STS_ALL_CLR);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS_EN, INT_STS_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS_SIG_EN, INT_STS_SIG_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, ERR_INT_STS, ERR_INT_STS_ALL_CLR);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, ERR_INT_STS_EN, ERR_INT_STS_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, ERR_INT_STS_SIG_EN, ERR_INT_STS_SIG_EN_ALL_EN);
	if (ret)
		return ret;

	ret = uefc_write_register(uefc, INT_STS_EN, INT_STS_DMA | INT_STS_EN_DMA_TFR_CMPLT);

	return ret;
}

static int uefc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_common *uefc;
	struct gpio_desc *reset;
	struct resource *res;
	bool added = false;
	void __iomem *regs;
	u32 host_version;
	u32 proto[2];
	int ret;
	int i;

	uefc = devm_kzalloc(dev, sizeof(*uefc), GFP_KERNEL);
	if (!uefc)
		return -ENOMEM;

	uefc->dev = dev;
	spin_lock_init(&uefc->reg_lock);
	mutex_init(&uefc->tfr_lock);

	regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(regs))
		return dev_err_probe(dev, PTR_ERR(regs), "Failed to ioremap resource");

	uefc->regmap = devm_regmap_init_mmio(dev, regs, &uefc_regmap_config);
	if (IS_ERR(uefc->regmap))
		return dev_err_probe(dev, PTR_ERR(uefc->regmap), "Failed to init regmap");

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

	/* The direct-mapped flash window is optional. */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dirmap");
	if (res) {
		uefc->dirmap.addr = devm_ioremap_resource(dev, res);
		if (IS_ERR(uefc->dirmap.addr))
			return dev_err_probe(dev, PTR_ERR(uefc->dirmap.addr),
					     "Failed to map dirmap resource");
		uefc->dirmap.raw_addr = res->start;
		uefc->dirmap.size = resource_size(res);
	}

	ret = regmap_read(uefc->regmap, HC_VER, &host_version);
	if (ret)
		return ret;

	ret = uefc_hw_init(uefc);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize the controller\n");

	dev_set_drvdata(dev, uefc);

	proto[0] = HC_VER_CHAN_PROTO(host_version, HC_VER_CHAN_A_SHIFT);
	proto[1] = HC_VER_CHAN_PROTO(host_version, HC_VER_CHAN_B_SHIFT);

	dev_dbg(dev, "HC_VER %#x, protocol A=%u B=%u\n", host_version, proto[0], proto[1]);

	for (i = 0; i < UEFC_CHANNELS_NB; i++) {
		const struct mfd_cell *cell;

		uefc->chan[i].id = i;

		switch (proto[i]) {
		case HC_VER_CHAN_PROTO_XSPI:
			cell = &xspi_cell;
			break;
		case HC_VER_CHAN_PROTO_ONFI:
			cell = &nand_cell;
			break;
		case HC_VER_CHAN_PROTO_EMMC:
			cell = &memory_cell;
			break;
		default:
			dev_dbg(dev, "Channel %s is disabled\n", i ? "B" : "A");
			continue;
		}

		ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cell, 1, NULL, 0, NULL);
		if (ret)
			return dev_err_probe(dev, ret, "Failed to add %s device\n",
					     cell->name);
		added = true;
	}

	if (!added)
		return dev_err_probe(dev, -ENODEV, "No usable channel found\n");

	return 0;
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
