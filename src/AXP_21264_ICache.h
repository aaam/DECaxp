/*
 * Copyright (C) Jonathan D. Belanger 2017.
 * All Rights Reserved.
 *
 * This software is furnished under a license and may be used and copied only
 * in accordance with the terms of such license and with the inclusion of the
 * above copyright notice.  This software or any other copies thereof may not
 * be provided or otherwise made available to any other person.  No title to
 * and ownership of the software is hereby transferred.
 *
 * The information in this software is subject to change without notice and
 * should not be construed as a commitment by the author or co-authors.
 *
 * The author and any co-authors assume no responsibility for the use or
 * reliability of this software.
 *
 * Description:
 *
 *	This header file contains the structures and definitions required to
 *	implement the instruction cache for the emulation of the Alpha 21264 (EV68)
 *	processor.
 *
 *	Revision History:
 *
 *	V01.000		21-May-2017	Jonathan D. Belanger
 *	Initially written.
 */
#ifndef _AXP_21264_ICACHE_DEFS_
#define _AXP_21264_ICACHE_DEFS_
#include "AXP_Utility.h"
#include "AXP_21264_Instructions.h"

#define AXP_2_WAY_CACHE			2
#define AXP_CACHE_OFFSET_BITS	6
#define AXP_CACHE_INDEX			9
#define AXP_CACHE_LINE_INS		16
#define AXP_CACHE_SIZE			(64 * 1024)	/* 64K */

/*
 * This definition is used to quickly extract the tag and index from the
 * virtual address of the cache line for which is being looked up/stored.
 */
typedef struct
{
	u64				res_1 : 6;
	u64				index : 9;
	u64				tag : 33;
	u64				res_2 : 16;
} AXP_ICACHE_TAG_INDEX;

/*
 * One I-Cache Line
 */
typedef struct
{
	u64						_asm : 1;		/* Address Space Match */
	u64						vb 	: 1;		/* valid bit */
	u64						pal : 1;		/* PALcode */
	u64						replace : 4;
	u64						access : 4;		/* Kernel/Executive/Supervisor/User */
	u64						asn : 8;		/* Address Space Number */
	u64						res_1 : 45;		/* align to the 64-bit boundary */
	AXP_ICACHE_TAG_INDEX	address;		/* Virtual Tag Bits [47:15] */
	AXP_INS_FMT				instructions[AXP_CACHE_LINE_INS];
	u64						res_2[6];		/* align to 128 bytes */
} AXP_ICACHE_LINE;

#endif /*_AXP_21264_ICACHE_DEFS_ */