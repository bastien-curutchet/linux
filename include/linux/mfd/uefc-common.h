/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_INCLUDE_MFD_UEFC_COMMON_H_
#define _LINUX_INCLUDE_MFD_UEFC_COMMON_H_

#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * Host controller registers, in offset order. The controller presents one
 * register map to every protocol; the child drivers reach it through the
 * accessors below, so the definitions live here rather than in each driver.
 */
/* HC_CTRL (0x00) and its fields */
#define HC_CTRL				0x00
#define HC_CTRL_ASSIMI_BYTE_A(x)	(((x) & 0x3) << 17)
#define HC_CTRL_ASSIMI_BYTE_B(x)	(((x) & 0x3) << 19)
#define HC_CTRL_CH_SEL			BIT(11)
#define HC_CTRL_CH_SEL_A		0
#define HC_CTRL_CH_SEL_B		BIT(11)
#define HC_CTRL_DATA_ORDER		BIT(25)
#define HC_CTRL_EX_PHY_DQS_A		BIT(13)
#define HC_CTRL_EX_PHY_DQS_B		BIT(14)
#define HC_CTRL_EX_PHY_ITE_A		BIT(15)
#define HC_CTRL_EX_PHY_ITE_B		BIT(16)
#define HC_CTRL_EX_SER_A		BIT(21)
#define HC_CTRL_EX_SER_B		BIT(22)
#define HC_CTRL_LED			BIT(12)
#define HC_CTRL_LUN_SEL(x)		(((x) & 0x7) << 8)	/* NAND */
#define HC_CTRL_PARALLEL_0		BIT(26)
#define HC_CTRL_PARALLEL_1		BIT(27)
#define HC_CTRL_PORT_0_CH_A		(HC_CTRL_PORT_SEL_0 | HC_CTRL_CH_SEL_A)
#define HC_CTRL_PORT_0_CH_B		(HC_CTRL_PORT_SEL_0 | HC_CTRL_CH_SEL_B)
#define HC_CTRL_PORT_CH_MASK		(HC_CTRL_CH_SEL | HC_CTRL_PORT_SEL_MASK)
#define HC_CTRL_PORT_SEL(x)		(((x) & 0xFF) << 0)
#define HC_CTRL_PORT_SEL_0		0
#define HC_CTRL_PORT_SEL_MASK		HC_CTRL_PORT_SEL(0xFF)
#define HC_CTRL_RQE_EN			BIT(31)
#define HC_CTRL_SDMA_BD(x)		(((x) & 0x7) << 28)
#define HC_CTRL_SIO_SHIFTER(x)		(((x) & 0x3) << 23)
#define HC_CTRL_SIO_SHIFTER_HALF_CYCLE	HC_CTRL_SIO_SHIFTER(2)
#define HC_CTRL_SIO_SHIFTER_MASK	HC_CTRL_SIO_SHIFTER(0x3)

/* INT_STS (0x04) and its fields */
#define INT_STS				0x04
#define INT_AC_RDY			BIT(28)
#define INT_BLK_GAP			BIT(2)
#define INT_BUF_RD_RDY			BIT(5)
#define INT_BUF_WR_RDY			BIT(4)
#define INT_CA_REQ			BIT(30)
#define INT_CMD_CMPLT			BIT(0)
#define INT_DAT_CMPLT			BIT(1)
#define INT_ERR				BIT(15)
#define INT_STS_AC_RDY			BIT(28)
#define INT_STS_ALL_CLR			(INT_STS_AC_RDY | \
					INT_STS_ERR_INT | \
					INT_STS_DMA_TFR_CMPLT | \
					INT_STS_DMA_INT)
#define INT_STS_ALL_EN			(INT_ERR | INT_BUF_RD_RDY | INT_BUF_WR_RDY | \
					 INT_BLK_GAP | INT_DAT_CMPLT | INT_CMD_CMPLT | \
					 INT_CA_REQ | INT_AC_RDY)
#define INT_STS_BUF_RD_RDY		BIT(5)
#define INT_STS_BUF_WR_RDY		BIT(4)
#define INT_STS_CACHE_RDY		BIT(29)
#define INT_STS_CA_REQ			BIT(30)
#define INT_STS_CQE_INT			BIT(14)
#define INT_STS_DMA			BIT(6)
#define INT_STS_DMA_INT			BIT(6)
#define INT_STS_DMA_TFR_CMPLT		BIT(7)
#define INT_STS_ERR_INT			BIT(15)

/* ERR_INT_STS (0x08) and its fields */
#define ERR_INT_STS			0x08
#define ERR_INT_AC			BIT(16)
#define ERR_INT_AUTO_CMD		BIT(8)
#define ERR_INT_CMD_CRC			BIT(1)
#define ERR_INT_CMD_END			BIT(2)
#define ERR_INT_CMD_IDX			BIT(3)
#define ERR_INT_CMD_TIMEOUT		BIT(0)
#define ERR_INT_CRC			BIT(17)
#define ERR_INT_DATA_CRC		BIT(5)
#define ERR_INT_DATA_END		BIT(6)
#define ERR_INT_DATA_TIMEOUT		BIT(4)
#define ERR_INT_ECC			BIT(19)
#define ERR_INT_PREAM			BIT(18)
#define ERR_INT_STS_AC			BIT(16)
#define ERR_INT_STS_ADMA		BIT(9)
#define ERR_INT_STS_ALL_CLR		(ERR_INT_STS_ECC | \
					ERR_INT_STS_PREAM | \
					ERR_INT_STS_CRC | \
					ERR_INT_STS_AC | \
					ERR_INT_STS_ADMA)
#define ERR_INT_STS_ALL_EN		(ERR_INT_ECC | ERR_INT_CMD_CRC | ERR_INT_AUTO_CMD | \
					 ERR_INT_DATA_CRC | ERR_INT_CRC | ERR_INT_DATA_TIMEOUT | \
					 ERR_INT_DATA_END | ERR_INT_AC | \
					 ERR_INT_CMD_IDX | ERR_INT_CMD_END | ERR_INT_CMD_TIMEOUT)
#define ERR_INT_STS_AUTO_CMD		BIT(8)
#define ERR_INT_STS_CMD_CRC		BIT(1)
#define ERR_INT_STS_CMD_END		BIT(2)
#define ERR_INT_STS_CMD_IDX		BIT(3)
#define ERR_INT_STS_CMD_TIMEOUT		BIT(0)
#define ERR_INT_STS_CRC			BIT(17)
#define ERR_INT_STS_DATA_CRC		BIT(5)
#define ERR_INT_STS_DATA_END		BIT(6)
#define ERR_INT_STS_DATA_TIMEOUT	BIT(4)
#define ERR_INT_STS_ECC			BIT(19)
#define ERR_INT_STS_PREAM		BIT(18)

/* INT_STS_EN (0x0C) and its fields */
#define INT_STS_EN			0x0C
#define INT_STS_EN_AC_RDY		BIT(28)
#define INT_STS_EN_ALL_EN		(INT_STS_EN_AC_RDY | \
					INT_STS_EN_ERR_INT | \
					INT_STS_EN_DMA_TFR_CMPLT | \
					INT_STS_EN_DMA_INT)
#define INT_STS_EN_BLK_GAP		BIT(2)
#define INT_STS_EN_BUF_RD_RDY		BIT(5)
#define INT_STS_EN_BUF_WR_RDY		BIT(4)
#define INT_STS_EN_CACHE_RDY		BIT(29)
#define INT_STS_EN_CA_REQ		BIT(30)
#define INT_STS_EN_CMD_CMPLT		BIT(0)
#define INT_STS_EN_DAT_CMPLT		BIT(1)
#define INT_STS_EN_DMA_INT		BIT(3)
#define INT_STS_EN_DMA_TFR_CMPLT	BIT(7)
#define INT_STS_EN_ERR_INT		BIT(15)

/* ERR_INT_STS_EN (0x10) and its fields */
#define ERR_INT_STS_EN			0x10
#define ERR_INT_STS_EN_AC		BIT(16)
#define ERR_INT_STS_EN_ADMA		BIT(9)
#define ERR_INT_STS_EN_ALL_EN		(ERR_INT_STS_EN_ECC | \
					ERR_INT_STS_EN_PREAM | \
					ERR_INT_STS_EN_CRC | \
					ERR_INT_STS_EN_AC | \
					ERR_INT_STS_EN_ADMA)
#define ERR_INT_STS_EN_AUTO_CMD		BIT(8)
#define ERR_INT_STS_EN_CMD_CRC		BIT(1)
#define ERR_INT_STS_EN_CMD_END		BIT(2)
#define ERR_INT_STS_EN_CMD_IDX		BIT(3)
#define ERR_INT_STS_EN_CMD_TIMEOUT	BIT(0)
#define ERR_INT_STS_EN_CRC		BIT(17)
#define ERR_INT_STS_EN_DATA_CRC		BIT(5)
#define ERR_INT_STS_EN_DATA_END		BIT(6)
#define ERR_INT_STS_EN_DATA_TIMEOUT	BIT(4)
#define ERR_INT_STS_EN_ECC		BIT(19)
#define ERR_INT_STS_EN_PREAM		BIT(18)

/* INT_STS_SIG (0x14) */
#define INT_STS_SIG			0x14

/* INT_STS_SIG_EN (0x14) and its fields */
#define INT_STS_SIG_EN			0x14
#define INT_STS_SIG_EN_AC_RDY		BIT(28)
#define INT_STS_SIG_EN_ALL_EN		(INT_STS_SIG_EN_AC_RDY | \
					INT_STS_SIG_EN_ERR_INT | \
					INT_STS_SIG_EN_DMA_TFR_CMPLT | \
					INT_STS_SIG_EN_DMA_INT)
#define INT_STS_SIG_EN_BLK_GAP		BIT(2)
#define INT_STS_SIG_EN_BUF_RD_RDY	BIT(5)
#define INT_STS_SIG_EN_BUF_WR_RDY	BIT(4)
#define INT_STS_SIG_EN_CACHE_RDY	BIT(29)
#define INT_STS_SIG_EN_CA_REQ		BIT(30)
#define INT_STS_SIG_EN_CMD_CMPLT	BIT(0)
#define INT_STS_SIG_EN_DAT_CMPLT	BIT(1)
#define INT_STS_SIG_EN_DMA_INT		BIT(3)
#define INT_STS_SIG_EN_DMA_TFR_CMPLT	BIT(7)
#define INT_STS_SIG_EN_ERR_INT		BIT(15)

/* ERR_INT_STS_SIG_EN (0x18) and its fields */
#define ERR_INT_STS_SIG_EN		0x18
#define ERR_INT_STS_SIG_EN_AC		BIT(16)
#define ERR_INT_STS_SIG_EN_ADMA		BIT(9)
#define ERR_INT_STS_SIG_EN_ALL_EN	(ERR_INT_STS_SIG_EN_ECC | \
					ERR_INT_STS_SIG_EN_PREAM | \
					ERR_INT_STS_SIG_EN_CRC | \
					ERR_INT_STS_SIG_EN_AC | \
					ERR_INT_STS_SIG_EN_ADMA)
#define ERR_INT_STS_SIG_EN_AUTO_CMD	BIT(8)
#define ERR_INT_STS_SIG_EN_CMD_CRC	BIT(1)
#define ERR_INT_STS_SIG_EN_CMD_END	BIT(2)
#define ERR_INT_STS_SIG_EN_CMD_IDX	BIT(3)
#define ERR_INT_STS_SIG_EN_CMD_TIMEOUT	BIT(0)
#define ERR_INT_STS_SIG_EN_CRC		BIT(17)
#define ERR_INT_STS_SIG_EN_DATA_CRC	BIT(5)
#define ERR_INT_STS_SIG_EN_DATA_END	BIT(6)
#define ERR_INT_STS_SIG_EN_DATA_TIMEOUT BIT(4)
#define ERR_INT_STS_SIG_EN_ECC		BIT(19)
#define ERR_INT_STS_SIG_EN_PREAM	BIT(18)

/* TFR_MODE (0x1C) and its fields */
#define TFR_MODE			0x1C
#define OP_ADDR_BUSW(x)			(((x) & 0x3) << 11)
#define OP_ADDR_CNT(x)			(((x) & 0x7) << 18)
#define OP_ADDR_DTR(x)			(((x) & 0x1) << 13)
#define OP_BUSW_1		0
#define OP_BUSW_2		1
#define OP_BUSW_4		2
#define OP_BUSW_8		3
#define OP_CMD_BUSW(x)			(((x) & 0x3) << 8)
#define OP_CMD_CNT(x)			(((x) >> 1) << 17)
#define OP_CMD_DTR(x)			(((x) & 1) << 10)
#define OP_DATA_BUSW(x)			(((x) & 0x3) << 14)
#define OP_DATA_DTR(x)			(((x) & 0x1) << 16)
#define OP_DD_RD			BIT(4)
#define OP_DMY_CNT(x)			(((x) & 0x3F) << 21)
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
#define TFR_MODE_ADR_BUS(x)		(((x) & 0x7) << 11)
#define TFR_MODE_AUTO_CMD(x)		(((x) & 0x3) << 2)
#define TFR_MODE_BLK_CNT_EN		BIT(1)
#define TFR_MODE_BUSW_1			0
#define TFR_MODE_BUSW_2			1
#define TFR_MODE_BUSW_4			2
#define TFR_MODE_BUSW_8			3
#define TFR_MODE_BUS_MASK		(0x1FF << 8)
#define TFR_MODE_CMD_BUS(x)		(((x) & 0x7) << 8)
#define TFR_MODE_CNT_EN			BIT(1)
#define TFR_MODE_CSB_DONT_CARE		BIT(27)
#define TFR_MODE_DAT_BUS(x)		(((x) & 0x7) << 14)
#define TFR_MODE_DD			BIT(4)
#define TFR_MODE_DMA_EN			BIT(0)
#define TFR_MODE_DMA_KEEP_CSB		BIT(30)
#define TFR_MODE_DMA_TYPE		BIT(31)
#define TFR_MODE_MULT_BLK		BIT(5)
#define TFR_MODE_MULT_BLK_EN		BIT(5)
#define TFR_MODE_PREAM_WITH		BIT(28)
#define TFR_MODE_SIO_1X_RD_BUS(x)	(((x) & 0x3) << 6)
#define TFR_MODE_TO_ENHC		BIT(29)

/* TFR_CTRL (0x20) and its fields */
#define TFR_CTRL			0x20
#define TFR_CTRL_CS_DIS_MASK	(TFR_CTRL_DEV_DIS | \
				 TFR_CTRL_IO_END)
#define TFR_CTRL_CS_EN_MASK		(TFR_CTRL_DEV_ACT | \
					 TFR_CTRL_HC_ACT | \
					 TFR_CTRL_IO_START)
#define TFR_CTRL_DEV_ACT		BIT(2)
#define TFR_CTRL_DEV_DIS		BIT(18)
#define TFR_CTRL_HC_ACT			BIT(1)
#define TFR_CTRL_IO_END			BIT(16)
#define TFR_CTRL_IO_START		BIT(0)

/* PRES_STS (0x24) and its fields */
#define PRES_STS			0x24
#define PRES_STS_ADMA(x)		(((x) & 0x7) << 29)
#define PRES_STS_BUF_RD_EN		BIT(11)
#define PRES_STS_BUF_WR_EN		BIT(10)
#define PRES_STS_CMD_INH_CMD		BIT(0)
#define PRES_STS_CMD_INH_DAT		BIT(1)
#define PRES_STS_DAT_ACT		BIT(2)
#define PRES_STS_EMMC_TX(x)		(((x) & 0xF) << 12)
#define PRES_STS_ONFI_TX(x)		(((x) & 0x1F) << 20)
#define PRES_STS_RD_TFR			BIT(9)
#define PRES_STS_RX_NEMPT		BIT(18)
#define PRES_STS_RX_NFULL		BIT(19)
#define PRES_STS_TX_EMPT		BIT(16)
#define PRES_STS_TX_NFULL		BIT(17)
#define PRES_STS_WR_TFR			BIT(8)
#define PRES_STS_XSPI_TX(x)		(((x) & 0xF) << 25)

/* SDMA_CNT (0x28) */
#define SDMA_CNT			0x28

/* SDMA_ADDR (0x2C) */
#define SDMA_ADDR			0x2C

/* ADMA2_ADDR (0x30) and its fields */
#define ADMA2_ADDR			0x30
#define ADMA2_DESC(addr, len) \
	(((u64)lower_32_bits(addr) << 32) | (((u64)(len) & 0xffff) << 16) | \
	 ADMA2_DESC_ATTR_TRAN | ADMA2_DESC_ATTR_VALID)
#define ADMA2_DESC_ATTR_END		BIT(1)
#define ADMA2_DESC_ATTR_TRAN		BIT(5)
#define ADMA2_DESC_ATTR_VALID		BIT(0)

/* ADMA3_ADDR (0x34) */
#define ADMA3_ADDR			0x34

/* BASE_MAP_ADDR (0x38) */
#define BASE_MAP_ADDR			0x38

/* SW_RST (0x44) and its fields */
#define SW_RST				0x44
#define SW_RST_ALL			BIT(0)
#define SW_RST_CMD			BIT(1)
#define SW_RST_DAT			BIT(2)

/* TO_CTRL (0x48) and its fields */
#define TO_CTRL				0x48
#define TO_CTRL_CA(x)			(((x) & 0xF) << 16)
#define TO_CTRL_DAT(x)			((x) & 0xF)
#define TO_CTRL_DAT_MAX			TO_CTRL_DAT(0xA)

/* CLK_CTRL (0x4C) and its fields */
#define CLK_CTRL			0x4C
#define CLK_CTRL_PLL_SELECT(x)		(((x) & 0xFFFF) << 0)
#define CLK_CTRL_RX_SS_A(x)		(((x) & 0x1F) << 16)
#define CLK_CTRL_RX_SS_A_MASK		CLK_CTRL_RX_SS_A(0x1F)
#define CLK_CTRL_RX_SS_B(x)		(((x) & 0x1F) << 21)
#define CLK_CTRL_RX_SS_B_MASK		CLK_CTRL_RX_SS_B(0x1F)
#define CLK_CTRL_RX_SS_MAX		32
#define CLK_CTRL_SLOW_CLOCK		BIT(31)

/* CACHE_CTRL (0x54) and its fields */
#define CACHE_CTRL			0x54
#define CACHE_CTRL_CLEAN		BIT(1)
#define CACHE_CTRL_CONT_ADDR		BIT(21)
#define CACHE_CTRL_DIRTY_LEVEL(x)	(((x) & 0x3) << 30)
#define CACHE_CTRL_FETCH_CNT(x)		(((x) & 0x7) << 18)
#define CACHE_CTRL_INVALID		BIT(0)
#define CACHE_CTRL_LEN_TH(x)		(((x) & 0xFF) << 22)
#define CACHE_CTRL_MST(x)		(((x) & 0xFFFF) << 2)

/* CAP_1 (0x58) and its fields */
#define CAP_1				0x58
#define CAP_1_ATOMIC			BIT(25)
#define CAP_1_CACHE			BIT(26)
#define CAP_1_CQE			BIT(22)
#define CAP_1_CSB_NUM(x)		(((x) & 0x1FF) << 0)
#define CAP_1_DMA_MASTER_MODE		BIT(23)
#define CAP_1_DMA_SLAVE_MODE		BIT(24)
#define CAP_1_DUAL_CH			BIT(31)
#define CAP_1_EMMC_ITF			BIT(28)
#define CAP_1_FIFO_DEPTH(x)		(((x) & 0x3) << 15)
#define CAP_1_LUN_NUM(x)		(((x) & 0xF) << 9)
#define CAP_1_MAPPING_MODE		BIT(27)
#define CAP_1_ONFI_ITF			BIT(29)
#define CAP_1_SYS_DW(x)			(((x) & 0x3) << 13)
#define CAP_1_XSPI_ITF			BIT(30)

/* HC_VER (0x5C) and its fields */
#define HC_VER				0x5C
#define HC_VER_CHAN_A_SHIFT		4
#define HC_VER_CHAN_B_SHIFT		9
#define HC_VER_CHAN_PROTO(v, shift)	(((v) >> (shift)) & HC_VER_CHAN_PROTO_MASK)
#define HC_VER_CHAN_PROTO_EMMC		0x3
#define HC_VER_CHAN_PROTO_MASK		0x3
#define HC_VER_CHAN_PROTO_ONFI		0x2
#define HC_VER_CHAN_PROTO_XSPI		0x1

/* RTL_VER (0x60) */
#define RTL_VER				0x60

/* TXD_REG (0x70) and its fields */
#define TXD_REG				0x70
#define TXD(x)				(TXD_REG + ((x) * 4))

/* RXD_REG (0x80) */
#define RXD_REG				0x80

/* SEND_CRC_CYC (0x84) and its fields */
#define SEND_CRC_CYC			0x84
#define SEND_CRC_CYC_EN			BIT(0)

/* BLK_CNT (0x90) */
#define BLK_CNT				0x90

/* ARG_REG (0x94) */
#define ARG_REG				0x94

/* CMD_REG (0x98) and its fields */
#define CMD_REG				0x98
#define CMD_REG_BOOT_ACK_EN		BIT(17)
#define CMD_REG_BOOT_BUS(x)		(((x) & 0x7) << 19)
#define CMD_REG_BOOT_EN			BIT(16)
#define CMD_REG_BOOT_TYPE		BIT(18)
#define CMD_REG_CMD_CRC_CHK_EN		BIT(3)
#define CMD_REG_CMD_IDX(x)		(((x) & 0x3F) << 8)
#define CMD_REG_CMD_IDX_CHK_EN		BIT(4)
#define CMD_REG_CMD_MASK		0xFFFF
#define CMD_REG_CRC_RESP		BIT(6)
#define CMD_REG_DAT_EN			BIT(5)
#define CMD_REG_RSP_136			1
#define CMD_REG_RSP_48			2
#define CMD_REG_RSP_48_BUSY		3
#define CMD_REG_RSP_MASK		0x3
#define CMD_REG_RSP_NA			0
#define CMD_REG_RSP_SEL(x)		(((x) & 0x3) << 0)
#define CMD_REG_WR_CRC_STS_EN		BIT(6)

/* RSP_1 (0x9C) */
#define RSP_1				0x9C

/* RSP_2 (0xA0) */
#define RSP_2				0xA0

/* RSP_3 (0xA4) */
#define RSP_3				0xA4

/* RSP_4 (0xA8) and its fields */
#define RSP_4				0xA8
#define RSP_4_1_VALUE(x)		(((x) & 0xFF) << 0)

/* DATA_REG (0xAC) */
#define DATA_REG			0xAC

/* AUTO_CMD (0xB0) */
#define AUTO_CMD			0xB0

/* AUTO_CMD_ERR_STS (0xB4) and its fields */
#define AUTO_CMD_ERR_STS		0xB4
#define AUTO_CMD_ERR_STS_CRC		BIT(2)
#define AUTO_CMD_ERR_STS_END		BIT(3)
#define AUTO_CMD_ERR_STS_IDX		BIT(4)
#define AUTO_CMD_ERR_STS_TIMEOUT	BIT(1)

/* BOOT_SYS_ADDR (0xB8) */
#define BOOT_SYS_ADDR			0xB8

/* BLK_GAP_CTRL (0xBC) and its fields */
#define BLK_GAP_CTRL			0xBC
#define BLK_GAP_CTRL_CONT_REQ		BIT(1)
#define BLK_GAP_CTRL_STOP_GAP		BIT(0)

/* DEV_CTRL (0xC0) and its fields */
#define DEV_CTRL			0xC0
#define DEV_CTRL_BLK_SIZE(x)		(((x) & 0xFFF) << 7)
#define DEV_CTRL_BLK_SZ(x)		(((x) & 0xFFF) << 7)
#define DEV_CTRL_BLK_SZ_MASK		DEV_CTRL_BLK_SZ(0xFFF)
#define DEV_CTRL_CACHEABLE		BIT(24)
#define DEV_CTRL_CRCB_IN_EN		BIT(3)
#define DEV_CTRL_CRCB_OUT_EN		BIT(0)
#define DEV_CTRL_CRC_CHUNK_SIZE(x)	(((x) & 0x3) << 1)
#define DEV_CTRL_CRC_EN			BIT(4)
#define DEV_CTRL_DQS_EN			BIT(5)
#define DEV_CTRL_PAGE_SIZE(x)		(((x) & 0x7) << 19)
#define DEV_CTRL_PRE_DQS_EN		BIT(6)
#define DEV_CTRL_SCLK_SEL(x)		(((x) & 0xF) << 25)
#define DEV_CTRL_SCLK_SEL_DEFAULT	DEV_CTRL_SCLK_SEL(3)
#define DEV_CTRL_SCLK_SEL_DIV(x)	((((x) >> 1) - 1) << 25)
#define DEV_CTRL_SCLK_SEL_MASK		DEV_CTRL_SCLK_SEL(0xF)
#define DEV_CTRL_SCLK_SEL_MAX		0xF
#define DEV_CTRL_SCLK_SEL_SLOWEST	DEV_CTRL_SCLK_SEL(0xF)
#define DEV_CTRL_TYPE(x)		(((x) & 0x7) << 29)
#define DEV_CTRL_TYPE_EMMC		DEV_CTRL_TYPE(6)
#define DEV_CTRL_TYPE_LYBRA		DEV_CTRL_TYPE(1)
#define DEV_CTRL_TYPE_MASK		DEV_CTRL_TYPE(0x7)
#define DEV_CTRL_TYPE_OCTARAM		DEV_CTRL_TYPE(2)
#define DEV_CTRL_TYPE_RAWNAND_EMMC	DEV_CTRL_TYPE(6)
#define DEV_CTRL_TYPE_RAWNAND_JEDEC	DEV_CTRL_TYPE(5)
#define DEV_CTRL_TYPE_RAWNAND_ONFI	DEV_CTRL_TYPE(4)
#define DEV_CTRL_TYPE_SPI		DEV_CTRL_TYPE(0)
#define DEV_CTRL_WR_PLCY(x)		(((x) & 0x3) << 22)

/* MAP_RD_CTRL (0xC4) and its fields */
#define MAP_RD_CTRL			0xC4
#define MAP_RD_CTRL_PREAM_EN		BIT(28)
#define MAP_RD_CTRL_SIO_1X_RD(x)	(((x) & 0x3) << 6)

/* MAP_WR_CTRL (0xC8) */
#define MAP_WR_CTRL			0xC8

/* MAP_CMD_RD (0xCC) */
#define MAP_CMD_RD			0xCC

/* MAP_CMD_WR (0xCE) */
#define MAP_CMD_WR			0xCE

/* TOP_MAP_ADDR (0xD0) */
#define TOP_MAP_ADDR			0xD0

/* GPIO_REG (0xD4) and its fields */
#define GPIO_REG			0xD4
#define GPIO_REG_CMD_LEVEL		BIT(22)
#define GPIO_REG_DATA_LEVEL(x)		(((x) & 0xFF) << 24)
#define GPIO_REG_HOLDB_DRIV_HIGH	BIT(1)
#define GPIO_REG_HP_DRIV_HIGH		BIT(3)
#define GPIO_REG_RESTB_DRIV_HIGH	BIT(2)
#define GPIO_REG_RYBYB_LEVE		BIT(23)
#define GPIO_REG_RYBYB_LEVEL		BIT(23)
#define GPIO_REG_SIO2_DRIV_HIGH		BIT(4)
#define GPIO_REG_SIO2_EN		BIT(12)
#define GPIO_REG_SIO3_DRIV_HIGH		BIT(5)
#define GPIO_REG_SIO3_EN		BIT(13)
#define GPIO_REG_WPB_DRIV_HIGH		BIT(0)

/* AC_CTRL (0xD8) and its fields */
#define AC_CTRL				0xD8
#define AC_CTRL_CMD_1(x)		(((x) & 0xFF) << 16)
#define AC_CTRL_CMD_2(x)		(((x) & 0xFF) << 24)
#define AC_CTRL_DQS_TEST_EN		BIT(5)
#define AC_CTRL_LAZY_DQS_EN		BIT(9)
#define AC_CTRL_LEN_32B_SEL		BIT(8)
#define AC_CTRL_NVDDR_EN		BIT(3)
#define AC_CTRL_PHY_EN			BIT(6)
#define AC_CTRL_SAMPLE_DQS_EN		BIT(2)
#define AC_CTRL_SAMPLE_EN		BIT(1)
#define AC_CTRL_SIO_ALIG_EN		BIT(4)
#define AC_CTRL_START			BIT(0)
#define AC_CTRL_WINDOW(x)		(((x) & 0x3) << 14)

/* PREAM_1_REG (0xDC) and its fields */
#define PREAM_1_REG			0xDC
#define PREAM_1_REG_SIO_0(x)		(((x) & 0xFFFF) << 0)
#define PREAM_1_REG_SIO_1(x)		(((x) & 0xFFFF) << 16)

/* PREAM_2_REG (0xE0) and its fields */
#define PREAM_2_REG			0xE0
#define PREAM_2_REG_SIO_2(x)		(((x) & 0xFFFF) << 0)
#define PREAM_2_REG_SIO_3(x)		(((x) & 0xFFFF) << 16)

/* PREAM_3_REG (0xE4) and its fields */
#define PREAM_3_REG 0xE4
#define PREAM_3_REG_SIO_4(x)		(((x) & 0xFFFF) << 0)
#define PREAM_3_REG_SIO_5(x)		(((x) & 0xFFFF) << 16)

/* PREAM_4_REG (0xE8) and its fields */
#define PREAM_4_REG 0xE8
#define PREAM_4_REG_SIO_6(x)		(((x) & 0xFFFF) << 0)
#define PREAM_4_REG_SIO_7(x)		(((x) & 0xFFFF) << 16)

/* SAMPLE_ADJ (0xEC) and its fields */
#define SAMPLE_ADJ			0xEC
#define SAMPLE_ADJ_DQS_IDLY_DOPI(x)	(((x) & 0xFF) << 24)
#define SAMPLE_ADJ_DQS_IDLY_DOPI_DEFAULT SAMPLE_ADJ_DQS_IDLY_DOPI(0xFF)
#define SAMPLE_ADJ_DQS_IDLY_DOPI_MASK	SAMPLE_ADJ_DQS_IDLY_DOPI(0xFF)
#define SAMPLE_ADJ_DQS_IDLY_SOPI(x)	(((x) & 0xFF) << 16)
#define SAMPLE_ADJ_DQS_ODLY(x)		(((x) & 0xFF) << 8)
#define SAMPLE_ADJ_POINT_SEL_DDR(x)	(((x) & 0x7) << 3)
#define SAMPLE_ADJ_POINT_SEL_DDR_DEFAULT SAMPLE_ADJ_POINT_SEL_DDR(5)
#define SAMPLE_ADJ_POINT_SEL_SDR(x)	(((x) & 0x7) << 0)
#define SAMPLE_ADJ_POINT_SEL_SDR_DEFAULT SAMPLE_ADJ_POINT_SEL_SDR(3)
#define SAMPLE_POINT_DDR(x)		(((x) & 0x7) << 3)
#define SAMPLE_POINT_DDR_EMMC		SAMPLE_POINT_DDR(2)
#define SAMPLE_POINT_DDR_MASK		SAMPLE_POINT_DDR(0x7)
#define SAMPLE_POINT_SDR(x)		((x) & 0x7)
#define SAMPLE_POINT_SDR_MASK		SAMPLE_POINT_SDR(0x7)

/* SIO_IDLY_1 (0xF0) and its fields */
#define SIO_IDLY_1 0xF0
#define SIO_IDLY_1_SIO0(x)		(((x) & 0xFF) << 0)
#define SIO_IDLY_1_SIO1(x)		(((x) & 0xFF) << 8)
#define SIO_IDLY_1_SIO2(x)		(((x) & 0xFF) << 16)
#define SIO_IDLY_1_SIO3(x)		(((x) & 0xFF) << 24)

/* SIO_IDLY_2 (0xF4) and its fields */
#define SIO_IDLY_2 0xF4
#define SIO_IDLY_2_SIO4(x)		(((x) & 0xFF) << 24)
#define SIO_IDLY_2_SIO5(x)		(((x) & 0xFF) << 16)
#define SIO_IDLY_2_SIO6(x)		(((x) & 0xFF) << 8)
#define SIO_IDLY_2_SIO7(x)		(((x) & 0xFF) << 0)

/* SIO_ODLY_1 (0xF8) and its fields */
#define SIO_ODLY_1			0xF8
#define SIO_ODLY_1_SIO0(x)		(((x) & 0xFF) << 0)
#define SIO_ODLY_1_SIO1(x)		(((x) & 0xFF) << 8)
#define SIO_ODLY_1_SIO2(x)		(((x) & 0xFF) << 16)
#define SIO_ODLY_1_SIO3(x)		(((x) & 0xFF) << 24)

/* SIO_ODLY_2 (0xFC) and its fields */
#define SIO_ODLY_2			0xFC
#define SIO_ODLY_2_SIO4(x)		(((x) & 0xFF) << 24)
#define SIO_ODLY_2_SIO5(x)		(((x) & 0xFF) << 16)
#define SIO_ODLY_2_SIO6(x)		(((x) & 0xFF) << 8)
#define SIO_ODLY_2_SIO7(x)		(((x) & 0xFF) << 0)

/* Bus width encodings and helpers used with the fields above */
#define BUS_1S				0
#define BUS_4D				6
#define BUS_4S				2
#define BUS_8D				7
#define BUS_8S				3
#define IDLY_CODE_VAL(x, v)		((v) << (((x) % 4) * 8))
#define MAX_CMDS	10

#define UEFC_CHANNELS_NB	2

enum uefc_channel_id {
	CHANNEL_A = 0,
	CHANNEL_B = 1,
};

/**
 * struct uefc_channel - Channel resources
 *
 * @id:		Channel's ID
 * @rx_clk:	Input logic clock
 * @tx_clk:	Output logic clock
 * @used:	Used flag
 *		Allows to ensure that a channel is used by one child at a time.
 */
struct uefc_channel {
	enum uefc_channel_id id;
	struct clk *rx_clk;
	struct clk *tx_clk;
#define UEFC_CHAN_USED		1
#define UEFC_CHAN_UNUSED	0
	atomic_t used;
};

/**
 * struct uefc_dirmap - Direct-mapped flash window
 *
 * @addr:	ioremap cookie for the window
 * @raw_addr:	Physical base address of the window
 * @size:	Size of the window, in bytes
 *
 * The window lets flash contents be accessed without going through the
 * register interface. It is optional: a controller may be synthesised without
 * it, and it is only mapped when the device tree describes a "dirmap" region.
 */
struct uefc_dirmap {
	void __iomem *addr;
	dma_addr_t raw_addr;
	size_t size;
};

/**
 * struct uefc_common - Common UEFC resources
 *
 * @dev:	UEFC device
 * @regmap:	Regmap to handle register accesses
 * @sys_clk:	FIFO left side clock
 * @ss_clk:	FIFO right side clock
 * @chan:	UEFC channels
 * @tfr_lock:	Serializes transfers issued by the child drivers, which share
 *		the single host controller.
 * @reg_lock:	Lock to access registers.
 *		The default lock can't be used because of Cat. D registers.
 *		Indeed, to access them, we first have to select the chan/port
 *		from the Host Controller Register.
 * @dirmap:	Optional direct-mapped flash window
 */
struct uefc_common {
	struct device *dev;
	struct regmap *regmap;
	struct clk *sys_clk;
	struct clk *ss_clk;
	struct uefc_channel chan[UEFC_CHANNELS_NB];
	/* Serializes the transfers issued by the child drivers */
	struct mutex tfr_lock;
	/* Serializes the channel/port selection of a Cat. D access */
	spinlock_t reg_lock;

	struct uefc_dirmap dirmap;
};

/* Helpers to read/write Cat A, B, C and E registers */
int uefc_read_register(struct uefc_common *uefc, unsigned int reg, u32 *val);
int uefc_write_register(struct uefc_common *uefc, unsigned int reg, u32 val);
int uefc_update_register(struct uefc_common *uefc, unsigned int reg, u32 mask, u32 val);

/* Helpers to read/write Cat D registers */
int uefc_read_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			 unsigned int reg, u32 *val);
int uefc_write_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			  unsigned int reg, u32 val);
int uefc_update_d_register(struct uefc_common *uefc, enum uefc_channel_id chan, u8 port,
			   unsigned int reg, u32 mask, u32 val);

int uefc_init_channel(struct uefc_common *uefc, enum uefc_channel_id id,
		      struct uefc_channel **chan);
void uefc_release_channel(struct uefc_common *uefc, struct uefc_channel *chan);
int uefc_set_io_mode(struct uefc_common *uefc, bool enable);
#endif /* _LINUX_INCLUDE_MFD_UEFC_COMMON_H_ */
