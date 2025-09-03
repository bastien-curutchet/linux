/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_INCLUDE_MFD_UEFC_COMMON_H_
#define _LINUX_INCLUDE_MFD_UEFC_COMMON_H_

#include <linux/spinlock.h>
#include <linux/atomic.h>

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
 * struct uefc_common - Common UEFC resources
 *
 * @dev:	UEFC device
 * @regmap:	Regmap to handle register accesses
 * @sys_clk:	FIFO left side clock
 * @ss_clk:	FIFO right side clock
 * @chan:	UEFC channels
 * @reg_lock:	Lock to access registers.
 *		The default lock can't be used because of Cat. D registers.
 *		Indeed, to access them, we first have to select the chan/port
 *		from the Host Controller Register.
 */
struct uefc_common {
	struct device *dev;
	struct regmap *regmap;
	struct clk *sys_clk;
	struct clk *ss_clk;
	struct uefc_channel chan[UEFC_CHANNELS_NB];
	spinlock_t reg_lock;
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
#endif /* _LINUX_INCLUDE_MFD_UEFC_COMMON_H_ */
