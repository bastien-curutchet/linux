// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/mfd/uefc-common.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "internals.h"

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
#define OP_PREP_MASK		(OP_DMY_CNT(0x3F) |	\
				 OP_ADDR_CNT(0x07) |	\
				 OP_CMD_CNT(2) |	\
				 OP_DATA_BUSW(0x03) |	\
				 OP_DATA_DTR(0x01) |	\
				 OP_ADDR_BUSW(0x03) |	\
				 OP_ADDR_DTR(0x01) |	\
				 OP_CMD_BUSW(0x03) |	\
				 OP_CMD_DTR(0x01) |	\
				 OP_DD_RD)
#define OP_DMY_CNT(x)		(((x) & 0x3F) << 21)
#define OP_ADDR_CNT(x)		(((x) & 0x7) << 18)
#define OP_CMD_CNT(x)		(((x) >> 1) << 17)
#define OP_DATA_BUSW(x)		(((x) & 0x3) << 14)
#define OP_DATA_DTR(x)		(((x) & 0x1) << 16)
#define OP_ADDR_BUSW(x)		(((x) & 0x3) << 11)
#define OP_ADDR_DTR(x)		(((x) & 0x1) << 13)
#define OP_CMD_BUSW(x)		(((x) & 0x3) << 8)
#define OP_BUSW_1		0
#define OP_BUSW_2		1
#define OP_BUSW_4		2
#define OP_BUSW_8		3
#define OP_CMD_DTR(x)		(((x) & 1) << 10)
#define OP_DD_RD		BIT(4)

/* Transfer Control Register */
#define TFR_CTRL			0x20
#define TFR_CTRL_DEV_DIS		BIT(18)
#define TFR_CTRL_IO_END			BIT(16)
#define TFR_CTRL_CS_DIS_MASK	(TFR_CTRL_DEV_DIS | \
				 TFR_CTRL_IO_END)
#define TFR_CTRL_DEV_ACT		BIT(2)
#define TFR_CTRL_HC_ACT			BIT(1)
#define TFR_CTRL_IO_START		BIT(0)
#define TFR_CTRL_CS_EN_MASK		(TFR_CTRL_DEV_ACT | \
					 TFR_CTRL_HC_ACT | \
					 TFR_CTRL_IO_START)
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

/*
 * ADMA2 descriptor (64-bit): [ data address : 32 | length : 16 | attr : 16 ]
 * Each transfer descriptor sets VALID | TRAN; the final descriptor of a
 * list additionally sets END.
 */
#define ADMA2_DESC_ATTR_VALID		BIT(0)
#define ADMA2_DESC_ATTR_END		BIT(1)
#define ADMA2_DESC_ATTR_TRAN		BIT(5)
#define ADMA2_DESC(addr, len) \
	(((u64)lower_32_bits(addr) << 32) | (((u64)(len) & 0xffff) << 16) | \
	 ADMA2_DESC_ATTR_TRAN | ADMA2_DESC_ATTR_VALID)

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
#define TO_CTRL_DAT(x)			(((x) & 0xF) << 16)

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

/* General Purpose Inputs and Outputs Register */
#define GPIO_REG			0xD4
#define GPIO_REG_DATA_LEVEL(x)		(((x) & 0xFF) << 24)
#define GPIO_REG_RYBYB_LEVEL		BIT(23)
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

/* One ADMA2 descriptor for the page data, one for the OOB area. */
#define UEFC_NAND_ADMA_DESC_NB		2
#define UEFC_NAND_ADMA_TABLE_SIZE	(UEFC_NAND_ADMA_DESC_NB * sizeof(u64))

/**
 * struct uefc_nand - UEFC raw NAND channel
 *
 * @uefc:	UEFC core handler
 * @chan:	Channel this NAND controller drives
 * @controller:	Base NAND controller structure
 * @chip:	NAND chip attached to the channel
 * @adma_desc:	ADMA2 descriptor table
 * @adma_desc_dma: DMA address of the descriptor table
 * @data_buf:	Bounce buffer for the page data
 * @data_buf_dma: DMA address of the page data bounce buffer
 * @oob_buf:	Bounce buffer for the OOB area
 * @oob_buf_dma: DMA address of the OOB bounce buffer
 *
 * The buffers are allocated once, after nand_scan() has established the page
 * and OOB sizes, and reused by every transfer. They are shared by the whole
 * channel: page reads and writes hold the host controller's transfer lock for
 * their entire DMA sequence, and a channel drives a single chip, so no two
 * transfers can be in flight at once.
 */
struct uefc_nand {
	struct uefc_common *uefc;
	struct uefc_channel *chan;
	struct nand_controller controller;
	struct nand_chip chip;

	u64 *adma_desc;
	dma_addr_t adma_desc_dma;
	void *data_buf;
	dma_addr_t data_buf_dma;
	void *oob_buf;
	dma_addr_t oob_buf_dma;
};

struct xfer_info {
	const struct nand_op_cmd_instr *cmd;
	const struct nand_op_addr_instr *addr;
	const struct nand_op_data_instr *data;
	bool read;
	const struct nand_op_waitrdy_instr *waitrdy;
};

#define UEFC_NAND_WAITRDY_TIMEOUT_MS	1000

/*
 * Wait for the device to become ready after a read-type operation (READ PARAM,
 * GET FEATURES, page read).
 *
 * The controller samples the NAND R/B# line into the read-only RYBYB_LEVEL bit
 * of its General Purpose Inputs and Outputs Register, but the line is not fed
 * to the interrupt block: nothing is raised when the device goes ready. The
 * only way to observe the transition is to poll that bit.
 */
static int uefc_onfi_read_wait_ready(struct nand_chip *chip)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	u32 sts;
	int ret;

	ret = regmap_read_poll_timeout(mxic->uefc->regmap, GPIO_REG, sts,
				       sts & GPIO_REG_RYBYB_LEVEL, 0, USEC_PER_SEC);
	if (ret) {
		dev_err(mxic->uefc->dev, "nand device timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * Wait for the device to become ready after a program or erase operation.
 * The controller cannot poll the R/B# line for these operations, so poll the
 * NAND Read Status Register (0x70) instead. Each iteration issues a full
 * READ STATUS command via exec_op (nand_status_op()), which the hardware can
 * service, rather than relying on nand_soft_waitrdy().
 */
static int uefc_onfi_wait_ready(struct nand_chip *chip)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	unsigned long timeout;
	u8 status;
	int ret;

	timeout = jiffies + msecs_to_jiffies(UEFC_NAND_WAITRDY_TIMEOUT_MS);
	do {
		ret = nand_status_op(chip, &status);
		if (ret)
			return ret;

		if (status & NAND_STATUS_READY)
			return 0;

		usleep_range(10, 20);
	} while (time_before(jiffies, timeout));

	dev_err(mxic->uefc->dev, "nand device timeout\n");
	return -ETIMEDOUT;
}

/*
 * Move a command, address or data payload through the controller, one FIFO
 * word at a time.
 *
 * The TX and RX FIFOs are linked: reading the TX FIFO is what induces a write
 * into the RX FIFO, so every byte shifted out - command, address, dummy or
 * data alike - shifts one byte in. Both transfer directions therefore have to
 * drive both FIFOs, which is why the loop below is the same for reads and
 * writes:
 *
 *  - A read has nothing to send, but the TXD write is what generates the shift
 *    events, so it pads the TX FIFO with dummy bytes instead. Drop the padding
 *    (or the TX_NFULL poll that paces it) and nothing is ever shifted into the
 *    RX FIFO, leaving the RX_NEMPT poll below to time out.
 *  - A write has nothing to collect, but the RX FIFO fills up behind it all
 *    the same and the transfer stalls once it is full, so RXD is drained even
 *    when there is no caller buffer to copy it into.
 *
 * This mirrors the TX/RX interleaving sequence in the controller
 * documentation: poll TX not full, write TXD, poll RX not empty, read RXD.
 */
static int uefc_onfi_data_xfer(struct uefc_nand *mxic, const void *txbuf,
			      void *rxbuf, unsigned int len)
{
	unsigned int pos = 0;

	while (pos < len) {
		unsigned int nbytes = len - pos;
		/* Dummy padding, overwritten below when there is data to send. */
		u32 data = 0xffffffff;
		u32 sts;
		int ret;

		if (nbytes > 4)
			nbytes = 4;

		if (txbuf)
			memcpy(&data, txbuf + pos, nbytes);

		ret = regmap_read_poll_timeout(mxic->uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_TX_NFULL, 0, USEC_PER_SEC);
		if (ret) {
			dev_err(mxic->uefc->dev,
				"TX FIFO stall @pos %u/%u (%s), PRES_STS %#x\n",
				pos, len, txbuf ? "tx" : "rx", sts);
			return ret;
		}

		uefc_write_register(mxic->uefc, TXD(nbytes % 4), data);
		ret = regmap_read_poll_timeout(mxic->uefc->regmap, PRES_STS, sts,
					       sts & PRES_STS_RX_NEMPT, 0, USEC_PER_SEC);
		if (ret) {
			dev_err(mxic->uefc->dev,
				"RX FIFO stall @pos %u/%u (%s), PRES_STS %#x\n",
				pos, len, txbuf ? "tx" : "rx", sts);
			return ret;
		}

		uefc_read_register(mxic->uefc, RXD_REG, &data);
		if (rxbuf)
			memcpy(rxbuf + pos, &data, nbytes);

		pos += nbytes;
	}

	return 0;
}

static unsigned int uefc_onfi_prep_cad(const struct nand_operation *op,
					    struct xfer_info *cad)
{
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id, cad_count = 0;
	bool first_cad = true;

	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (first_cad)
				first_cad = false;
			else
				cad_count++;
			cad[cad_count].cmd = &instr->ctx.cmd;
			break;

		case NAND_OP_ADDR_INSTR:
			cad[cad_count].addr = &instr->ctx.addr;
			break;

		case NAND_OP_DATA_IN_INSTR:
			cad[cad_count].data = &instr->ctx.data;
			cad[cad_count].read = true;
			break;

		case NAND_OP_DATA_OUT_INSTR:
			cad[cad_count].data = &instr->ctx.data;
			cad[cad_count].read = false;
			break;

		case NAND_OP_WAITRDY_INSTR:
			cad[cad_count].waitrdy = &instr->ctx.waitrdy;
			break;
		}
	}
	cad_count++;
	return cad_count;
}

static u32 uefc_onfi_prep_op_cfg(struct xfer_info *cad)
{
	u32 cfg = OP_CMD_BUSW(OP_BUSW_8) |
		  OP_ADDR_BUSW(OP_BUSW_8) |
		  OP_DATA_BUSW(OP_BUSW_8) |
		  TFR_MODE_CSB_DONT_CARE;

	if (cad->cmd) {
		cfg |= OP_CMD_CNT(1) |
		       OP_CMD_DTR(0);
	}

	if (cad->addr) {
		cfg |= OP_ADDR_CNT(cad->addr->naddrs) |
		       OP_ADDR_DTR(0);
	}

	if (cad->data) {
		cfg |= OP_DATA_DTR(0);
		if (cad->read)
			cfg |= OP_DD_RD;
	}

	return cfg;
}

static u32 uefc_onfi_prep_rd_cfg(void)
{
	u32 cfg = OP_CMD_BUSW(OP_BUSW_8) |
		  OP_ADDR_BUSW(OP_BUSW_8) |
		  OP_DATA_BUSW(OP_BUSW_8) |
		  TFR_MODE_CSB_DONT_CARE;

	cfg |= OP_CMD_CNT(1) | OP_CMD_DTR(0);

	cfg |= OP_DATA_DTR(0) | OP_DD_RD;

	return cfg;
}

static u32 uefc_onfi_prep_wr_cfg(void)
{
	u32 cfg = OP_CMD_BUSW(OP_BUSW_8) |
		  OP_ADDR_BUSW(OP_BUSW_8) |
		  OP_DATA_BUSW(OP_BUSW_8) |
		  TFR_MODE_CSB_DONT_CARE;

	cfg |= OP_CMD_CNT(1) | OP_CMD_DTR(0);

	cfg |= OP_ADDR_CNT(5) | OP_ADDR_DTR(0);

	cfg |= OP_DATA_DTR(0);

	return cfg;
}

#define MAX_CMDS	10
static int uefc_onfi_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op, bool check_only)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	struct xfer_info cad[MAX_CMDS] = {};
	unsigned int cad_id, cad_count = 0;
	int ret = 0;

	if (check_only)
		return 0;

	mutex_lock(&mxic->uefc->tfr_lock);
	/*
	 * Select our channel and force DATA_ORDER back to 0 (normal byte order).
	 * HC_CTRL is shared with the xSPI child, which toggles DATA_ORDER per op;
	 * leaving it masked out here would inherit that bit from a concurrent SPI
	 * transfer and byte-swap the NAND data.
	 */
	if (mxic->chan->id == CHANNEL_A)
		ret = uefc_update_register(mxic->uefc, HC_CTRL,
					   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER, HC_CTRL_CH_SEL_A);
	else
		ret = uefc_update_register(mxic->uefc, HC_CTRL,
					   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER, HC_CTRL_CH_SEL_B);
	if (ret)
		return ret;

	ret = uefc_write_register(mxic->uefc, TFR_CTRL, TFR_CTRL_IO_START);
	if (ret)
		return ret;

	ret = uefc_write_register(mxic->uefc, TFR_CTRL, TFR_CTRL_DEV_ACT);
	if (ret)
		return ret;

	cad_count = uefc_onfi_prep_cad(op, cad);
	for (cad_id = 0; cad_id < cad_count; cad_id++) {
		ret = uefc_write_register(mxic->uefc, TFR_MODE, uefc_onfi_prep_op_cfg(&cad[cad_id]));
		if (ret)
			return ret;

		if (cad[cad_id].cmd) {
			uefc_write_register(mxic->uefc, TFR_CTRL, TFR_CTRL_HC_ACT);
			ret = uefc_onfi_data_xfer(mxic, &cad[cad_id].cmd->opcode, NULL, 1);
			if (ret)
				break;
		}

		if (cad[cad_id].addr) {
			ret = uefc_onfi_data_xfer(mxic, cad[cad_id].addr->addrs, NULL,
						 cad[cad_id].addr->naddrs);
			if (ret)
				break;
		}

		/* A read operation waits on the hardware R/B# line. */
		if (cad[cad_id].waitrdy && cad[cad_id].data && cad[cad_id].read) {
			ret = uefc_onfi_read_wait_ready(chip);
			if (ret)
				break;
		}

		if (cad[cad_id].data) {
			if (cad[cad_id].read)
				ret = uefc_onfi_data_xfer(mxic, NULL,
							 cad[cad_id].data->buf.in,
							 cad[cad_id].data->len);
			else
				ret = uefc_onfi_data_xfer(mxic,
							 cad[cad_id].data->buf.out, NULL,
							 cad[cad_id].data->len);
			if (ret)
				break;
		}

		/*
		 * Program and erase operations cannot use the R/B# line, so
		 * poll the status register instead. Drop the transfer lock
		 * while waiting since the poll re-enters exec_op.
		 */
		if (cad[cad_id].waitrdy &&
		    ((cad[cad_id].data && !cad[cad_id].read) || !cad[cad_id].data)) {
			ret = uefc_set_io_mode(mxic->uefc, false);
			if (ret)
				return ret;

			mutex_unlock(&mxic->uefc->tfr_lock);
			ret = uefc_onfi_wait_ready(chip);
			if (ret)
				goto out;

			mutex_lock(&mxic->uefc->tfr_lock);
			ret = uefc_update_register(mxic->uefc, HC_CTRL,
						   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER,
						   mxic->chan->id == CHANNEL_A ?
						   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
			if (ret)
				return ret;

			ret = uefc_set_io_mode(mxic->uefc, true);
			if (ret)
				return ret;
		}
	}
	ret = uefc_set_io_mode(mxic->uefc, false);
	if (ret)
		return ret;
	mutex_unlock(&mxic->uefc->tfr_lock);
out:
	return ret;
}

static int uefc_nand_page_read_begin(struct nand_chip *chip, int page)
{
	u8 addrs[5] = {0};
	struct nand_op_instr instrs[] = {
		NAND_OP_CMD(NAND_CMD_READ0, 0),
		NAND_OP_ADDR(4, addrs, 0),
	};
	struct nand_operation op = NAND_OPERATION(chip->cur_cs, instrs);

	addrs[2] = page;
	addrs[3] = page >> 8;

	if (chip->options & NAND_ROW_ADDR_3) {
		addrs[4] = page >> 16;
		instrs[1].ctx.addr.naddrs++;
	}

	return nand_exec_op(chip, &op);
}

/* Maximum time to wait for an ADMA2 page transfer to complete. */
#define UEFC_NAND_DMA_TIMEOUT_US	100000

static int uefc_nand_wait_dma_done(struct uefc_nand *mxic)
{
	u32 int_sts, err_sts;
	int ret;

	ret = regmap_read_poll_timeout(mxic->uefc->regmap, INT_STS, int_sts,
				       int_sts & (INT_STS_DMA_TFR_CMPLT | INT_STS_ERR_INT),
				       0, UEFC_NAND_DMA_TIMEOUT_US);
	if (ret) {
		dev_err(mxic->uefc->dev, "DMA transfer timeout\n");
		return ret;
	}

	/*
	 * ERR_INT is a read-only summary of the Error Interrupt Status
	 * Register, so the details are only worth fetching - and clearing -
	 * once it reports that something went wrong.
	 */
	if (int_sts & INT_STS_ERR_INT) {
		ret = uefc_read_register(mxic->uefc, ERR_INT_STS, &err_sts);
		if (ret)
			return ret;

		dev_err(mxic->uefc->dev, "DMA transfer error, ERR_INT_STS %#x\n",
			err_sts);
		uefc_write_register(mxic->uefc, ERR_INT_STS, err_sts);

		return -EIO;
	}

	return uefc_write_register(mxic->uefc, INT_STS, INT_STS_DMA_TFR_CMPLT);
}

static int uefc_nand_page_read(struct mtd_info *mtd, struct nand_chip *chip,
			       void *buf, void *oob_buf, int oob_len, int page)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	u8 cmd = NAND_CMD_READSTART;
	int i = 0;
	int ret;

	if (!buf && !oob_buf)
		return 0;

	if (buf)
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->data_buf_dma, mtd->writesize);

	if (oob_buf)
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->oob_buf_dma, mtd->oobsize);

	mxic->adma_desc[i - 1] |= ADMA2_DESC_ATTR_END;

	ret = uefc_nand_page_read_begin(chip, page);
	if (ret)
		return ret;

	mutex_lock(&mxic->uefc->tfr_lock);

	ret = uefc_update_register(mxic->uefc, HC_CTRL,
				   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER,
				   mxic->chan->id == CHANNEL_A ?
				   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
	if (ret)
		goto out_unlock;

	ret = uefc_write_register(mxic->uefc, INT_STS,
				  INT_STS_DMA_TFR_CMPLT | INT_STS_DMA_INT);
	if (ret)
		goto out_unlock;

	ret = uefc_write_register(mxic->uefc, TFR_MODE, uefc_onfi_prep_rd_cfg());
	if (ret)
		goto out_unlock;

	ret = uefc_set_io_mode(mxic->uefc, true);
	if (ret)
		goto out_unlock;

	ret = uefc_onfi_data_xfer(mxic, &cmd, NULL, 1);
	if (ret)
		goto out;

	ret = uefc_onfi_read_wait_ready(chip);
	if (ret)
		goto out;

	ret = uefc_write_register(mxic->uefc, ADMA2_ADDR,
				  lower_32_bits(mxic->adma_desc_dma));
	if (ret)
		goto out;

	ret = uefc_nand_wait_dma_done(mxic);
	if (ret)
		goto out;

	if (buf)
		memcpy(buf, mxic->data_buf, mtd->writesize);

	if (oob_buf)
		memcpy(oob_buf, mxic->oob_buf, mtd->oobsize);

out:
	uefc_set_io_mode(mxic->uefc, false);
out_unlock:
	mutex_unlock(&mxic->uefc->tfr_lock);

	return ret;
}

static int uefc_nand_page_write(struct mtd_info *mtd, struct nand_chip *chip,
				const u8 *buf, void *oob_buf, int oob_len, int page)
{
	struct uefc_nand *mxic = nand_get_controller_data(chip);
	u8 cmd = NAND_CMD_SEQIN;
	u8 addrs[5] = {0};
	int i = 0;
	int ret;

	if (!buf && !oob_buf)
		return 0;

	if (buf) {
		memcpy(mxic->data_buf, buf, mtd->writesize);
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->data_buf_dma, mtd->writesize);
	}

	if (oob_buf) {
		memcpy(mxic->oob_buf, oob_buf, mtd->oobsize);
		mxic->adma_desc[i++] = ADMA2_DESC(mxic->oob_buf_dma, mtd->oobsize);
	}

	mxic->adma_desc[i - 1] |= ADMA2_DESC_ATTR_END;

	addrs[2] = page;
	addrs[3] = page >> 8;

	if (chip->options & NAND_ROW_ADDR_3)
		addrs[4] = page >> 16;

	mutex_lock(&mxic->uefc->tfr_lock);

	ret = uefc_update_register(mxic->uefc, HC_CTRL,
				   HC_CTRL_CH_SEL | HC_CTRL_DATA_ORDER,
				   mxic->chan->id == CHANNEL_A ?
				   HC_CTRL_CH_SEL_A : HC_CTRL_CH_SEL_B);
	if (ret)
		goto out_unlock;

	ret = uefc_write_register(mxic->uefc, TFR_MODE, uefc_onfi_prep_wr_cfg());
	if (ret)
		goto out_unlock;

	ret = uefc_set_io_mode(mxic->uefc, true);
	if (ret)
		goto out_unlock;

	ret = uefc_onfi_data_xfer(mxic, &cmd, NULL, 1);
	if (ret)
		goto out;

	ret = uefc_onfi_data_xfer(mxic, addrs, NULL, sizeof(addrs));
	if (ret)
		goto out;

	ret = uefc_write_register(mxic->uefc, ADMA2_ADDR,
				  lower_32_bits(mxic->adma_desc_dma));
	if (ret)
		goto out;

	ret = uefc_nand_wait_dma_done(mxic);
	if (ret)
		goto out;

	/*
	 * The page program is committed outside the transfer lock:
	 * nand_prog_page_end_op() goes back through exec_op(), which takes the
	 * lock itself.
	 */
	uefc_set_io_mode(mxic->uefc, false);
	mutex_unlock(&mxic->uefc->tfr_lock);

	return nand_prog_page_end_op(chip);

out:
	uefc_set_io_mode(mxic->uefc, false);
out_unlock:
	mutex_unlock(&mxic->uefc->tfr_lock);

	return ret;
}

static int uefc_nand_read_page_raw(struct nand_chip *chip, u8 *buf,
				   int oob_required, int page)
{
	void *oob_buf = oob_required ? chip->oob_poi : NULL;
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_read(mtd, chip, buf, oob_buf, mtd->oobsize, page);
}

static int uefc_nand_read_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_read(mtd, chip, NULL, chip->oob_poi,
				   mtd->oobsize, page);
}

static int uefc_nand_write_page_raw(struct nand_chip *chip, const uint8_t *buf,
				   int oob_required, int page)
{
	void *oob_buf = oob_required ? chip->oob_poi : NULL;
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_write(mtd, chip, buf, oob_buf, mtd->oobsize, page);
}

static int uefc_nand_write_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);

	return uefc_nand_page_write(mtd, chip, NULL, chip->oob_poi,
				   mtd->oobsize, page);
}

static const struct nand_controller_ops uefc_nand_controller_ops = {
	.exec_op = uefc_onfi_exec_op,
};

/*
 * Allocate the DMA buffers reused by every page transfer. Must run after
 * nand_scan(), which is what establishes the page and OOB sizes.
 */
static int uefc_nand_alloc_dma_buffers(struct uefc_nand *nand)
{
	struct mtd_info *mtd = nand_to_mtd(&nand->chip);
	struct device *dev = nand->uefc->dev;

	nand->adma_desc = dma_alloc_coherent(dev, UEFC_NAND_ADMA_TABLE_SIZE,
					     &nand->adma_desc_dma, GFP_KERNEL);
	if (!nand->adma_desc)
		return -ENOMEM;

	nand->data_buf = dma_alloc_coherent(dev, mtd->writesize,
					    &nand->data_buf_dma, GFP_KERNEL);
	if (!nand->data_buf)
		goto err_free_desc;

	nand->oob_buf = dma_alloc_coherent(dev, mtd->oobsize,
					   &nand->oob_buf_dma, GFP_KERNEL);
	if (!nand->oob_buf)
		goto err_free_data;

	return 0;

err_free_data:
	dma_free_coherent(dev, mtd->writesize, nand->data_buf, nand->data_buf_dma);
err_free_desc:
	dma_free_coherent(dev, UEFC_NAND_ADMA_TABLE_SIZE, nand->adma_desc,
			  nand->adma_desc_dma);

	return -ENOMEM;
}

static void uefc_nand_free_dma_buffers(struct uefc_nand *nand)
{
	struct mtd_info *mtd = nand_to_mtd(&nand->chip);
	struct device *dev = nand->uefc->dev;

	dma_free_coherent(dev, mtd->oobsize, nand->oob_buf, nand->oob_buf_dma);
	dma_free_coherent(dev, mtd->writesize, nand->data_buf, nand->data_buf_dma);
	dma_free_coherent(dev, UEFC_NAND_ADMA_TABLE_SIZE, nand->adma_desc,
			  nand->adma_desc_dma);
}

static int uefc_nand_probe(struct platform_device *pdev)
{
	struct device_node *nand_np, *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct nand_chip *nand_chip;
	struct uefc_nand *nand;
	struct mtd_info *mtd;
	u32 version;
	u32 reg;
	int ret;

	ret = of_property_read_u32(dev->of_node, "reg", &reg);
	if (ret)
		return ret;

	if (reg > UEFC_CHANNELS_NB)
		return -EINVAL;

	nand = devm_kzalloc(dev, sizeof(*nand), GFP_KERNEL);
	if (!nand)
		return -ENOMEM;

	nand->uefc = dev_get_drvdata(dev->parent);
	ret = uefc_read_register(nand->uefc, HC_VER, &version);
	if (ret)
		return ret;

	dev_dbg(dev, "UEFC host controller version %x\n", version);

	ret = uefc_init_channel(nand->uefc, reg, &nand->chan);
	if (ret)
		return ret;

	ret = uefc_update_d_register(nand->uefc, nand->chan->id, 0, DEV_CTRL,
				     DEV_CTRL_TYPE_MASK | DEV_CTRL_SCLK_SEL_MASK,
				     DEV_CTRL_TYPE_RAWNAND_ONFI | DEV_CTRL_SCLK_SEL(3));
	if (ret)
		return ret;

	nand_chip = &nand->chip;
	mtd = nand_to_mtd(nand_chip);
	mtd->dev.parent = dev;

	for_each_child_of_node(np, nand_np)
		nand_set_flash_node(nand_chip, nand_np);

	nand_set_controller_data(nand_chip, nand);
	nand->controller.ops = &uefc_nand_controller_ops;
	nand_controller_init(&nand->controller);
	nand_chip->controller = &nand->controller;

	uefc_write_register(nand->uefc, TFR_CTRL, TFR_CTRL_HC_ACT);

	nand_chip->ecc.read_page_raw = uefc_nand_read_page_raw;
	nand_chip->ecc.read_oob = uefc_nand_read_oob;
	nand_chip->ecc.write_page_raw = uefc_nand_write_page_raw;
	nand_chip->ecc.write_oob = uefc_nand_write_oob;

	ret = nand_scan(nand_chip, 1);
	if (ret)
		goto fail;

	ret = uefc_nand_alloc_dma_buffers(nand);
	if (ret)
		goto fail_cleanup;

	ret = mtd_device_register(mtd, NULL, 0);
	if (ret)
		goto fail_free_dma;

	dev_set_drvdata(dev, nand);

	return 0;

fail_free_dma:
	uefc_nand_free_dma_buffers(nand);
fail_cleanup:
	nand_cleanup(nand_chip);
fail:
	uefc_release_channel(nand->uefc, nand->chan);
	return ret;
}

static void uefc_nand_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct uefc_nand *nand;

	nand = dev_get_drvdata(dev);

	uefc_nand_free_dma_buffers(nand);
	uefc_release_channel(nand->uefc, nand->chan);
}

static const struct of_device_id uefc_nand_of_match[] = {
	{ .compatible = "mxic,uefc-nand", },
	{ /* end node */ },
};

MODULE_DEVICE_TABLE(of, uefc_nand_of_match);
static struct platform_driver uefc_nand_driver = {
	.probe		= uefc_nand_probe,
	.remove		= uefc_nand_remove,
	.driver		= {
		.name	= "uefc-nand",
		.of_match_table = of_match_ptr(uefc_nand_of_match),
	},
};

module_platform_driver(uefc_nand_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("NAND driver for the UEFC");
