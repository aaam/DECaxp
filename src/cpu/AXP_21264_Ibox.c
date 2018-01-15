
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
 *	This source file contains the functions needed to implement the
 *	functionality of the Ibox.
 *
 * Revision History:
 *
 *	V01.000		10-May-2017	Jonathan D. Belanger
 *	Initially written.
 *
 *	V01.001		14-May-2017	Jonathan D. Belanger
 *	Cleaned up some compiler errors.  Ran some tests, and corrected some coding
 *	mistakes.  The prediction code correctly predicts a branch instruction
 *	between 95.0% and 99.1% of the time.
 *
 *	V01.002		20-May-2017	Jonathan D. Belanger
 *	Did a bit of reorganization.  This module will contain all the
 *	functionality that is described as being part of the Ibox.  I move the
 *	prediction code here.
 *
 *	V01.003		22-May-2017	Jonathan D. Belanger
 *	Moved the main() function out of this module and into its own.
 *
 *	V01.004		24-May-2017	Jonathan D. Belanger
 *	Added some instruction cache (icache) code and definitions into this code.
 *	The iCache structure should be part of the CPU structure and the iCache
 *	code should not be adding a cache entry on a miss.  There should be a
 *	separate fill cache function.  Also, the iCache look-up code should return
 *	the requested instructions on a hit.
 *
 *	V01.005		01-Jun-2017	Jonathan D. Belanger
 *	Added a function to add an Icache line/block.  There are a couple to do
 *	items for ASM and ASN in this function.
 *
 *	V01.006		11-Jun-2017	Jonathan D. Belanger
 *	Started working on the instruction decode and register renaming code.
 *
 *	V01.007		12-Jun-2017	Jonathan D. Belanger
 *	Forgot to include the generation of a unique ID (8-bits long) to associate
 *	with each decoded instruction.
 *	BTW: We need a better way to decode an instruction.  The decoded
 *	instruction does not require the architectural register number, so we
 *	should not save them.  Also, renaming can be done while we are decoding the
 *	instruction.  We are also going to have to special case some instructions,
 *	such as PAL instructions, CMOVE, and there are exceptions for instruction
 *	types (Ra is a destination register for a Load, but a source register for a
 *	store.
 *
 *	V01.008		15-Jun-2017	Jonathan D. Belanger
 *	Finished defining structure array to be able to normalize how a register is
 *	used in an instruction.  Generally speaking, register 'c' (Rc or Fc) is a
 *	destination register.  In load operations, register 'a' (Ra or Fa) is the
 *	destination register.  Register 'b' (Rb or Fb) is never used as a
 *	designation (through I have implemented provisions for this).
 *	Unfortunately, sometimes a register is not used, or 'b' may be source 1 and
 *	other times it is source 2.  Sometimes not register is utilized (e.g. MB).
 *	The array structures assist in these differences so that in the end we end
 *	up with a destination (not not), a source 1 (or not), and/or a source 2
 *	(or not) set of registers.  If we have a destination register, then
 *	register renaming needs to allocate a new register.  IF we have source
 *	register, then renaming needs to associate the current register mapping to
 *	this register.
 *
 *	V01.009		16-Jun-2017	Jonathan D. Belanger
 *	Finished the implementation of normalizing registers, as indicated above,
 *	and then mapping them from architectural to physical registers, generating
 *	a new mapping for the destination register.
 *
 *	V01.010		23-Jun-2017	Jonathan D. Belanger
 *	The way I'm handling the Program Counter (PC/VPC) is not correct.  When
 *	instructions are decoded, queued, and executed, their results are not
 *	realized until the instruction is retired.  The Integer Control
 *	instructions currently determine the next PC and set it immediately. not
 *	are retirement.  This is because I tried to do to much in the code that
 *	handles the PC/VPC.  The next PC should only be updated as a part of normal
 *	instruction queuing or instruction retirement.  We should only have one
 *	way to "set" the PC, but other functions to calculate one, based on various
 *	criteria.
 *
 *	V01.011		18-Jul-2017
 *	Updated the instruction decoding code to replace instruction indicated
 *	registers for PALshadow registers when we are in, or going to be in,
 *	PALmode.  Also, do not include floating point registers, as there are no
 *	PALshadow registers for these.
 *
 *	TODO:	We need a retirement function to put the destination value into the
 *			physical register and indicate that the register value is Valid.
 */
#include "AXP_Configure.h"
#include "AXP_21264_Ibox.h"

/*
 * A local structure used to calculate the PC for a CALL_PAL function.
 */
struct palBaseBits21264
{
	u64	res : 15;
	u64	highPC : 49;
};
struct palBaseBits21164
{
	u64	res : 14;
	u64	highPC : 50;
};

typedef union
{
	 struct palBaseBits21164	bits21164;
	 struct palBaseBits21264	bits21264;
	 u64						palBaseAddr;

}AXP_IBOX_PALBASE_BITS;

struct palPCBits21264
{
	u64	palMode : 1;
	u64 mbz_1 : 5;
	u64 func_5_0 : 6;
	u64 func_7 : 1;
	u64 mbo : 1;
	u64 mbz_2 : 1;
	u64 highPC : 49;
};
struct palPCBits21164
{
	u64	palMode : 1;
	u64 mbz : 5;
	u64 func_5_0 : 6;
	u64 func_7 : 1;
	u64 mbo : 1;
	u64 highPC : 50;
};

typedef union
{
	struct palPCBits21164	bits21164;
	struct palPCBits21264	bits21264;
	AXP_PC					vpc;
} AXP_IBOX_PAL_PC;

struct palFuncBits
{
	u32 func_5_0 : 6;
	u32 res_1 : 1;
	u32 func_7 : 1;
	u32 res_2 : 24;
};

typedef union
{
	struct palFuncBits	bits;
	u32					func;
} AXP_IBOX_PAL_FUNC_BITS;

/*
 * Prototypes for local functions
 */
static AXP_OPER_TYPE AXP_DecodeOperType(u8, u32);

/* Functions to decode which instruction registers are used for which purpose
 * for the complex instructions (opcodes that have different ways to use
 * registers).
 */
static u16 AXP_RegisterDecodingOpcode11(AXP_INS_FMT);
static u16 AXP_RegisterDecodingOpcode14(AXP_INS_FMT);
static u16 AXP_RegisterDecodingOpcode15_16(AXP_INS_FMT);
static u16 AXP_RegisterDecodingOpcode17(AXP_INS_FMT);
static u16 AXP_RegisterDecodingOpcode18(AXP_INS_FMT);
static u16 AXP_RegisterDecodingOpcode1c(AXP_INS_FMT);
static void AXP_RenameRegisters(AXP_21264_CPU *, AXP_INSTRUCTION *, u16);

/*
 * Functions that manage the instruction queue free entries.
 */
static AXP_QUEUE_ENTRY *AXP_GetNextIQEntry(AXP_21264_CPU *);
static AXP_QUEUE_ENTRY *AXP_GetNextFQEntry(AXP_21264_CPU *);

/*
 * The following module specific structure and variable are used to be able to
 * decode opcodes that have differing ways register are utilized.  The index
 * into this array is the opcodeRegDecode fields in the bits field of the
 * register mapping in the above table.  There is no entry [0], so that is
 * just NULL and should never get referenced.
 */
 typedef u16 (*regDecodeFunc)(AXP_INS_FMT);
regDecodeFunc decodeFuncs[] =
{
	NULL,
	AXP_RegisterDecodingOpcode11,
	AXP_RegisterDecodingOpcode14,
	AXP_RegisterDecodingOpcode15_16,
	AXP_RegisterDecodingOpcode15_16,
	AXP_RegisterDecodingOpcode17,
	AXP_RegisterDecodingOpcode18,
	AXP_RegisterDecodingOpcode1c
};

/*
 * AXP_Decode_Rename
 * 	This function is called to take a set of 4 instructions and decode them
 * 	and then rename the architectural registers to physical ones.  The results
 * 	are put onto either the Integer Queue or Floating-point Queue (FQ) for
 * 	execution.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to
 *		emulate an Alpha AXP 21264 CPU.
 *	next:
 *		A pointer to a location to receive the next 4 instructions to be
 *		processed.
 *
 * Output Parameters:
 * 	decodedInsr:
 * 		A pointer to the decoded version of the instruction.
 *
 * Return value:
 * 	None.
 */
void AXP_Decode_Rename(AXP_21264_CPU *cpu,
					   AXP_INS_LINE *next,
					   int nextInstr,
					   AXP_INSTRUCTION *decodedInstr)
{
	bool			callingPAL = false;
	bool			src1Float = false;
	bool			src2Float = false;
	bool			destFloat = false;
	u32				function;

	/*
	 * Decode the next instruction.
	 *
	 * First, Assign a unique ID to this instruction (the counter should
	 * auto-wrap) and initialize some of the other fields within the decoded
	 * instruction.
	 */
	decodedInstr->uniqueID = cpu->instrCounter++;
	decodedInstr->fault = AXP_NO_FAULTS;
	decodedInstr->excRegMask = NoException;

	/*
	 * Let's, decode the instruction.
	 */
	decodedInstr->format = next->instrType[nextInstr];
	decodedInstr->opcode = next->instructions[nextInstr].pal.opcode;
	switch (decodedInstr->format)
	{
		case Bra:
		case FPBra:
			decodedInstr->displacement = next->instructions[nextInstr].br.branch_disp;
			break;

		case FP:
			decodedInstr->function = next->instructions[nextInstr].fp.func;
			break;

		case Mem:
		case Mbr:
			decodedInstr->displacement = next->instructions[nextInstr].mem.mem.disp;
			break;

		case Mfc:
			decodedInstr->function = next->instructions[nextInstr].mem.mem.func;
			break;

		case Opr:
			decodedInstr->function = next->instructions[nextInstr].oper1.func;
			break;

		case Pcd:
			decodedInstr->function = next->instructions[nextInstr].pal.palcode_func;
			callingPAL = true;
			break;

		case PAL:
			switch (decodedInstr->opcode)
			{
				case HW_LD:
				case HW_ST:
					decodedInstr->displacement = next->instructions[nextInstr].hw_ld.disp;
					decodedInstr->type_hint_index = next->instructions[nextInstr].hw_ld.type;
					decodedInstr->len_stall = next->instructions[nextInstr].hw_ld.len;
					break;

				case HW_RET:
					decodedInstr->displacement = next->instructions[nextInstr].hw_ret.disp;
					decodedInstr->type_hint_index = next->instructions[nextInstr].hw_ret.hint;
					decodedInstr->len_stall = next->instructions[nextInstr].hw_ret.stall;
					break;

				case HW_MFPR:
				case HW_MTPR:
					decodedInstr->type_hint_index = next->instructions[nextInstr].hw_mxpr.index;
					decodedInstr->scbdMask = next->instructions[nextInstr].hw_mxpr.scbd_mask;
					break;

				default:
					break;
			}
			break;

		default:
			break;
	}
	decodedInstr->type = AXP_OperationType(decodedInstr->opcode);
	if ((decodedInstr->type == Other) && (decodedInstr->format != Res))
		decodedInstr->type = AXP_DecodeOperType(
				decodedInstr->opcode,
				decodedInstr->function);
	decodedInstr->decodedReg = AXP_RegisterDecoding(decodedInstr->opcode);
	if (decodedInstr->decodedReg.bits.opcodeRegDecode != 0)
		decodedInstr->decodedReg.raw =
			decodeFuncs[decodedInstr->decodedReg.bits.opcodeRegDecode]
				(next->instructions[nextInstr]);
	if ((decodedInstr->opcode == HW_MFPR) || (decodedInstr->opcode == HW_MFPR))
		function = decodedInstr->type_hint_index;
	else
		function = decodedInstr->function;
	decodedInstr->pipeline = AXP_InstructionPipeline(
										decodedInstr->opcode,
										function);

	/*
	 * Decode destination register
	 */
	switch (decodedInstr->decodedReg.bits.dest)
	{
		case AXP_REG_RA:
			decodedInstr->aDest = next->instructions[nextInstr].oper1.ra;
			break;

		case AXP_REG_RB:
			decodedInstr->aDest = next->instructions[nextInstr].oper1.rb;
			break;

		case AXP_REG_RC:
			decodedInstr->aDest = next->instructions[nextInstr].oper1.rc;
			break;

		case AXP_REG_FA:
			decodedInstr->aDest = next->instructions[nextInstr].fp.fa;
			destFloat = true;
			break;

		case AXP_REG_FB:
			decodedInstr->aDest = next->instructions[nextInstr].fp.fb;
			destFloat = true;
			break;

		case AXP_REG_FC:
			decodedInstr->aDest = next->instructions[nextInstr].fp.fc;
			destFloat = true;
			break;

		default:

			/*
			 *  If the instruction being decoded is a CALL_PAL, then there is a
			 *  linkage register (basically a return address after the CALL_PAL
			 *  has completed).  For Jumps, the is usually specified in the
			 *  register fields of the instruction.  For CALL_PAL, this is
			 *  either R23 or R27, depending upon the setting of the
			 *  call_pal_r23 in the I_CTL IPR.
			 */
			if (decodedInstr->opcode == PAL00)
			{
				if (cpu->iCtl.call_pal_r23 == 1)
					decodedInstr->aDest = 23;
				else
					decodedInstr->aDest = 27;
			}
			else
				decodedInstr->aDest = AXP_UNMAPPED_REG;
			break;
	}

	/*
	 * Decode source1 register
	 */
	switch (decodedInstr->decodedReg.bits.src1)
	{
		case AXP_REG_RA:
			decodedInstr->aSrc1 = next->instructions[nextInstr].oper1.ra;
			break;

		case AXP_REG_RB:
			decodedInstr->aSrc1 = next->instructions[nextInstr].oper1.rb;
			break;

		case AXP_REG_RC:
			decodedInstr->aSrc1 = next->instructions[nextInstr].oper1.rc;
			break;

		case AXP_REG_FA:
			decodedInstr->aSrc1 = next->instructions[nextInstr].fp.fa;
			src1Float = true;
			break;

		case AXP_REG_FB:
			decodedInstr->aSrc1 = next->instructions[nextInstr].fp.fb;
			src1Float = true;
			break;

		case AXP_REG_FC:
			decodedInstr->aSrc1 = next->instructions[nextInstr].fp.fc;
			src1Float = true;
			break;

		default:
			decodedInstr->aSrc1 = AXP_UNMAPPED_REG;
			break;
	}

	/*
	 * Decode source2 register
	 */
	switch (decodedInstr->decodedReg.bits.src2)
	{
		case AXP_REG_RA:
			decodedInstr->aSrc2 = next->instructions[nextInstr].oper1.ra;
			break;

		case AXP_REG_RB:
			decodedInstr->aSrc2 = next->instructions[nextInstr].oper1.rb;
			break;

		case AXP_REG_RC:
			decodedInstr->aSrc2 = next->instructions[nextInstr].oper1.rc;
			break;

		case AXP_REG_FA:
			decodedInstr->aSrc2 = next->instructions[nextInstr].fp.fa;
			src2Float = true;
			break;

		case AXP_REG_FB:
			decodedInstr->aSrc2 = next->instructions[nextInstr].fp.fb;
			src2Float = true;
			break;

		case AXP_REG_FC:
			decodedInstr->aSrc2 = next->instructions[nextInstr].fp.fc;
			src2Float = true;
			break;

		default:
			decodedInstr->aSrc2 = AXP_UNMAPPED_REG;
			break;
	}

	/*
	 * When running in PALmode, the shadow registers may come into play.  If we
	 * are in PALmode, then the PALshadow registers may come into play.  If so,
	 * we need to replace the specified register with the PALshadow one.
	 *
	 * There is no such thing as Floating Point PALshadow registers, so the
	 * register specified on the instruction is the one to use.  Now need to
	 * check.
	 */
	decodedInstr->pc = next->instrPC[nextInstr];
	callingPAL |= (decodedInstr->pc.pal == AXP_PAL_MODE);
	if (src1Float == false)
		decodedInstr->aSrc1 = AXP_REG(decodedInstr->aSrc1, callingPAL);
	if (src2Float == false)
		decodedInstr->aSrc2 = AXP_REG(decodedInstr->aSrc2, callingPAL);
	if (destFloat == false)
		decodedInstr->aDest = AXP_REG(decodedInstr->aDest, callingPAL);

	/*
	 * We need to rename the architectural registers to physical
	 * registers, now that we know which one, if any, is the
	 * destination register and which one(s) is(are) the source
	 * register(s).
	 */
	AXP_RenameRegisters(cpu, decodedInstr);

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_DecodeOperType
 * 	This function is called to convert an operation type of 'Other' to a more
 * 	usable value.  The opcode and funcCode are used in combination to determine
 * 	the operation type.
 *
 * Input Parameters:
 * 	opCode:
 * 		A byte value specifying the opcode to be used to determine the
 * 		operation type.
 * 	funcCode:
 * 		A 32-bit value specifying the function code used to determine the
 * 		operation type.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	The operation type value for the opCode/FuncCode pair.
 */
static AXP_OPER_TYPE AXP_DecodeOperType(u8 opCode, u32 funcCode)
{
	AXP_OPER_TYPE retVal = Other;

	switch (opCode)
	{
		case INTA:		/* OpCode == 0x10 */
			if (funcCode == AXP_FUNC_CMPBGE)
				retVal = Logic;
			else
				retVal = Arith;
			break;

		case INTL:		/* OpCode == 0x11 */
			if ((funcCode == AXP_FUNC_AMASK) ||
				(funcCode == AXP_FUNC_IMPLVER))
				retVal = Oper;
			else
				retVal = Logic;
			break;

		case FLTV:		/* OpCode == 0x15 */
			if ((funcCode == AXP_FUNC_CMPGEQ) ||
				(funcCode == AXP_FUNC_CMPGLT) ||
				(funcCode == AXP_FUNC_CMPGLE) ||
				(funcCode == AXP_FUNC_CMPGEQ_S) ||
				(funcCode == AXP_FUNC_CMPGLT_S) ||
				(funcCode == AXP_FUNC_CMPGLE_S))
				retVal = Logic;
			else
				retVal = Arith;
			break;

		case FLTI:		/* OpCode == 0x16 */
			if ((funcCode == AXP_FUNC_CMPTUN) ||
				(funcCode == AXP_FUNC_CMPTEQ) ||
				(funcCode == AXP_FUNC_CMPTLT) ||
				(funcCode == AXP_FUNC_CMPTLE) ||
				(funcCode == AXP_FUNC_CMPTUN_SU) ||
				(funcCode == AXP_FUNC_CMPTEQ_SU) ||
				(funcCode == AXP_FUNC_CMPTLT_SU) ||
				(funcCode == AXP_FUNC_CMPTLE_SU))
				retVal = Logic;
			else
				retVal = Arith;
			break;

		case FLTL:		/* OpCode == 0x17 */
			if (funcCode == AXP_FUNC_MT_FPCR)
				retVal = Load;
			else if (funcCode == AXP_FUNC_MF_FPCR)
				retVal = Store;
			else
				retVal = Arith;
			break;

		case MISC:		/* OpCode == 0x18 */
			if ((funcCode == AXP_FUNC_RPCC) ||
				(funcCode == AXP_FUNC_RC) ||
				(funcCode == AXP_FUNC_RS))
				retVal = Load;
			else
				retVal = Store;
			break;
	}

	/*
	 * Return back to the caller with what we determined.
	 */
	return(retVal);
}

/*
 * AXP_RegisterDecodingOpcode11
 * 	This function is called to determine which registers in the instruction are
 * 	the destination and source.  It returns a proper mask to be used by the
 * 	register renaming process.  The Opcode associated with this instruction is
 * 	0x11.
 *
 * Input Parameters:
 * 	instr:
 * 		A value of the instruction being parsed.
 *
 * Output Parameters:
 * 	None.
 *
 * Return value:
 * 	The register mask to be used to rename the registers from architectural to
 * 	physical.
 */
static u16 AXP_RegisterDecodingOpcode11(AXP_INS_FMT instr)
{
	u16 retVal = 0;

	switch (instr.oper1.func)
	{
		case 0x61:		/* AMASK */
			retVal = AXP_DEST_RC|AXP_SRC1_RB;
			break;

		case 0x6c:		/* IMPLVER */
			retVal = AXP_DEST_RC;
			break;

		default:		/* All others */
			retVal = AXP_DEST_RC|AXP_SRC1_RA|AXP_SRC2_RB;
			break;
	}
	return(retVal);
}

/*
 * AXP_RegisterDecodingOpcode14
 * 	This function is called to determine which registers in the instruction are
 * 	the destination and source.  It returns a proper mask to be used by the
 * 	register renaming process.  The Opcode associated with this instruction is
 * 	0x14.
 *
 * Input Parameters:
 * 	instr:
 * 		A valus of the instruction being parsed.
 *
 * Output Parameters:
 * 	None.
 *
 * Return value:
 * 	The register mask to be used to rename the registers from architectural to
 * 	physical.
 */
static u16 AXP_RegisterDecodingOpcode14(AXP_INS_FMT instr)
{
	u16 retVal = AXP_DEST_FC;

	if ((instr.oper1.func & 0x00f) != 0x004)
		retVal |= AXP_SRC1_FB;
	else
		retVal |= AXP_SRC1_RB;
	return(retVal);
}

/*
 * AXP_RegisterDecodingOpcode15_16
 * 	This function is called to determine which registers in the instruction are
 * 	the destination and source.  It returns a proper mask to be used by the
 * 	register renaming process.  The Opcodes associated with this instruction
 * 	are 0x15 and 0x16.
 *
 * Input Parameters:
 * 	instr:
 * 		A valus of the instruction being parsed.
 *
 * Output Parameters:
 * 	None.
 *
 * Return value:
 * 	The register mask to be used to rename the registers from architectural to
 * 	physical.
 */
static u16 AXP_RegisterDecodingOpcode15_16(AXP_INS_FMT instr)
{
	u16 retVal = AXP_DEST_FC;

	if ((instr.fp.func & 0x008) == 0)
		retVal |= (AXP_SRC1_FA|AXP_SRC2_FB);
	else
		retVal |= AXP_SRC1_FB;
	return(retVal);
}

/*
 * AXP_RegisterDecodingOpcode17
 * 	This function is called to determine which registers in the instruction are
 * 	the destination and source.  It returns a proper mask to be used by the
 * 	register renaming process.  The Opcode associated with this instruction is
 * 	0x17.
 *
 * Input Parameters:
 * 	instr:
 * 		A valus of the instruction being parsed.
 *
 * Output Parameters:
 * 	None.
 *
 * Return value:
 * 	The register mask to be used to rename the registers from architectural to
 * 	physical.
 */
static u16 AXP_RegisterDecodingOpcode17(AXP_INS_FMT instr)
{
	u16 retVal = 0;

	switch (instr.fp.func)
	{
		case 0x010:
		case 0x030:
		case 0x130:
		case 0x530:
			retVal = AXP_DEST_FC|AXP_SRC1_FB;
			break;

		case 0x024:
			retVal = AXP_DEST_FA;
			break;

		case 0x025:
			retVal = AXP_SRC1_FA;
			break;

		default:		/* All others */
			retVal = AXP_DEST_FC|AXP_SRC1_FA|AXP_SRC2_FB;
			break;
	}
	return(retVal);
}

/*
 * AXP_RegisterDecodingOpcode18
 * 	This function is called to determine which registers in the instruction are
 * 	the destination and source.  It returns a proper mask to be used by the
 * 	register renaming process.  The Opcode associated with this instruction is
 * 	0x18.
 *
 * Input Parameters:
 * 	instr:
 * 		A valus of the instruction being parsed.
 *
 * Output Parameters:
 * 	None.
 *
 * Return value:
 * 	The register mask to be used to rename the registers from architectural to
 * 	physical.
 */
static u16 AXP_RegisterDecodingOpcode18(AXP_INS_FMT instr)
{
	u16 retVal = 0;

	if ((instr.mem.mem.func & 0x8000) != 0)
	{
		if ((instr.mem.mem.func == 0xc000) ||
			(instr.mem.mem.func == 0xe000) ||
			(instr.mem.mem.func == 0xf000))
			retVal = AXP_DEST_RA;
		else
			retVal = AXP_SRC1_RB;
	}
	return(retVal);
}

/*
 * AXP_RegisterDecodingOpcode1c
 * 	This function is called to determine which registers in the instruction are
 * 	the destination and source.  It returns a proper mask to be used by the
 * 	register renaming process.  The Opcode associated with this instruction is
 * 	0x1c.
 *
 * Input Parameters:
 * 	instr:
 * 		A valus of the instruction being parsed.
 *
 * Output Parameters:
 * 	None.
 *
 * Return value:
 * 	The register mask to be used to rename the registers from architectural to
 * 	physical.
 */
static u16 AXP_RegisterDecodingOpcode1c(AXP_INS_FMT instr)
{
	u16 retVal = AXP_DEST_RC;

	switch (instr.oper1.func)
	{
		case 0x31:
		case 0x37:
		case 0x38:
		case 0x39:
		case 0x3a:
		case 0x3b:
		case 0x3c:
		case 0x3d:
		case 0x3e:
		case 0x3f:
			retVal |= (AXP_SRC1_RA|AXP_SRC2_RB);
			break;

		case 0x70:
		case 0x78:
			retVal |= AXP_SRC1_FA;
			break;

		default:		/* All others */
			retVal |= AXP_SRC1_RB;
			break;
	}
	return(retVal);
}

/*
 * AXP_RenameRegisters
 *	This function is called to map the instruction registers from architectural
 *	to physical ones.  For the destination register, we get the next one off
 *	the free list.  We also differentiate between integer and floating point
 *	registers at this point (previously, we just noted it).
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	decodedInstr:
 *		A pointer to the structure containing a decoded representation of the
 *		Alpha AXP instruction.
 *
 * Output Parameters:
 *	decodedInstr:
 *		A pointer to structure that will be updated to indicate which physical
 *		registers are being used for this particular instruction.
 *
 * Return Value:
 *	None.
 */
static void AXP_RenameRegisters(
		AXP_21264_CPU *cpu,
		AXP_INSTRUCTION *decodedInstr)
{
	bool src1Float = ((decodedInstr->decodedReg.raw & 0x0008) == 0x0008);
	bool src2Float = ((decodedInstr->decodedReg.raw & 0x0080) == 0x0080);
	bool destFloat = ((decodedInstr->decodedReg.raw & 0x0800) == 0x0800);

	/*
	 * The source registers just use the current register mapping (integer or
	 * floating-point).  If the register number is 31, it is not mapped.
	 */
	decodedInstr->src1 = src1Float ? cpu->pfMap[decodedInstr->aSrc1].pr :
			cpu->prMap[decodedInstr->aSrc1].pr;
	decodedInstr->src2 = src2Float ? cpu->pfMap[decodedInstr->aSrc2].pr :
			cpu->prMap[decodedInstr->aSrc2].pr;

	/*
	 * The destination register needs a little more work.  If the register
	 * number is 31, it is not mapped.
	 */
	if (decodedInstr->aDest != AXP_UNMAPPED_REG)
	{

		/*
		 * Is this a floating-point register or an integer register?
		 */
		if (destFloat)
		{

			/*
			 * Get the next register off of the free-list.
			 */
			decodedInstr->dest = cpu->pfFreeList[cpu->pfFlStart];

			/*
			 * If the register for the previous mapping was not R31 or F31
			 * then, put this previous register back on the free-list, then
			 * make the previous mapping the current one and the current
			 * mapping the register we just took off the free-list.
			 */
			if (cpu->pfMap[decodedInstr->aDest].prevPr != AXP_UNMAPPED_REG)
			{
				cpu->pfFreeList[cpu->pfFlEnd] = cpu->pfMap[decodedInstr->aDest].prevPr;
				cpu->pfFlEnd = (cpu->pfFlEnd + 1) % AXP_F_FREELIST_SIZE;
			}
			cpu->pfMap[decodedInstr->aDest].prevPr = cpu->pfMap[decodedInstr->aDest].pr;
			cpu->pfMap[decodedInstr->aDest].pr = decodedInstr->dest;

			/*
			 * Until the instruction executes, the newly mapped register is
			 * pending a value.  After execution, the state will be waiting to
			 * retire.  After retirement, the value will be written to the
			 * physical register.
			 */
			cpu->pfState[decodedInstr->aDest] = Pending;

			/*
			 * Compute the next free physical register on the free-list.  Wrap
			 * the counter to the beginning of the list, if we are at the end.
			 */
			cpu->pfFlStart = (cpu->pfFlStart + 1) % AXP_F_FREELIST_SIZE;
		}
		else
		{

			/*
			 * Get the next register off of the free-list.
			 */
			decodedInstr->dest = cpu->prFreeList[cpu->prFlStart++];

			/*
			 * If the register for the previous mapping was not R31 or F31
			 * then, put this previous register back on the free-list, then
			 * make the previous mapping the current one and the current
			 * mapping the register we just took off the free-list.
			 */
			if (cpu->prMap[decodedInstr->aDest].prevPr != AXP_UNMAPPED_REG)
			{
				cpu->prFreeList[cpu->prFlEnd] = cpu->prMap[decodedInstr->aDest].prevPr;
				cpu->prFlEnd = (cpu->prFlEnd + 1) % AXP_I_FREELIST_SIZE;
			}
			cpu->prMap[decodedInstr->aDest].prevPr = cpu->pfMap[decodedInstr->aDest].pr;
			cpu->prMap[decodedInstr->aDest].pr = decodedInstr->dest;

			/*
			 * Until the instruction executes, the newly mapped register is
			 * pending a value.  After execution, the state will be waiting to
			 * retire.  After retirement, the value will be written to the
			 * physical register.
			 */
			cpu->prState[decodedInstr->aDest] = Pending;

			/*
			 * Compute the next free physical register on the free-list.  Wrap
			 * the counter to the beginning of the list, if we are at the end.
			 */
			cpu->prFlStart = (cpu->prFlStart + 1) % AXP_I_FREELIST_SIZE;
		}
	}
	else
	{

		/*
		 * No need to map R31 or F31 to a physical register on the free-list.
		 * R31 and F31 always equal zero and writes to them do not occur.
		 */
		decodedInstr->dest = src2Float ? cpu->pfMap[decodedInstr->aDest].pr :
				cpu->prMap[decodedInstr->aDest].pr;
	}
	return;
}

/*
 * AXP_GetNextIQEntry
 * 	This function is called to get the next available entry for the IQ queue.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	A pointer to the next available pre-allocated queue entry for the IQ.
 *
 * NOTE:	This function assumes that there is always at least one free entry.
 * 			Since the number of entries pre-allocated is equal to the maximum
 * 			number of entries that can be in the IQ, this is not necessarily a
 * 			bad assumption.
 */
static AXP_QUEUE_ENTRY *AXP_GetNextIQEntry(AXP_21264_CPU *cpu)
{
	AXP_QUEUE_ENTRY *retVal;

	retVal = &cpu->iqEntries[cpu->iqEFlStart];
	cpu->iqEFlStart = (cpu->iqEFlStart + 1) % AXP_IQ_LEN;

	/*
	 * Return back to the caller.
	 */
	return(retVal);
}

/*
 * AXP_ReturnIQEntry
 * 	This function is called to return an entry back to the IQ queue for a
 * 	future instruction.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	entry:
 *		A pointer to the entry being returned to the free-list.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	None.
 */
void AXP_ReturnIQEntry(AXP_21264_CPU *cpu, AXP_QUEUE_ENTRY *entry)
{

	/*
	 * Enter the index of the IQ entry onto the end of the free-list.
	 */
	cpu->iqEFreelist[cpu->iqEFlEnd] = entry->index;

	/*
	 * Increment the counter, in a round-robin fashion, for the entry just
	 * after end of the free-list.
	 */
	cpu->iqEFlEnd = (cpu->iqEFlEnd + 1) % AXP_IQ_LEN;

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_GetNextFQEntry
 * 	This function is called to get the next available entry for the FQ queue.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	A pointer to the next available pre-allocated queue entry for the FQ.
 *
 * NOTE:	This function assumes that there is always at least one free entry.
 * 			Since the number of entries pre-allocated is equal to the maximum
 * 			number of entries that can be in the FQ, this is not necessarily a
 * 			bad assumption.
 */
static AXP_QUEUE_ENTRY *AXP_GetNextFQEntry(AXP_21264_CPU *cpu)
{
	AXP_QUEUE_ENTRY *retVal;

	retVal = &cpu->fqEntries[cpu->fqEFlStart];
	cpu->fqEFlStart = (cpu->fqEFlStart + 1) % AXP_FQ_LEN;

	/*
	 * Return back to the caller.
	 */
	return(retVal);
}

/*
 * AXP_ReturnFQEntry
 * 	This function is called to return an entry back to the FQ queue for a
 * 	future instruction.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	entry:
 *		A pointer to the entry being returned to the free-list.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	None.
 */
void AXP_ReturnFQEntry(AXP_21264_CPU *cpu, AXP_QUEUE_ENTRY *entry)
{

	/*
	 * Enter the index of the IQ entry onto the end of the free-list.
	 */
	cpu->fqEFreelist[cpu->fqEFlEnd] = entry->index;

	/*
	 * Increment the counter, in a round-robin fashion, for the entry just
	 * after end of the free-list.
	 */
	cpu->fqEFlEnd = (cpu->fqEFlEnd + 1) % AXP_FQ_LEN;

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_21264_AddVPC
 * 	This function is called to add a Virtual Program Counter (VPC) to the list
 * 	of VPCs.  This is a round-robin list.  The End points to the next entry to
 * 	be written to.  The Start points to the least recent VPC, which is the one
 * 	immediately after the End.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	vpc:
 *		A value of the next VPC to be entered into the VPC list.
 *
 * Output Parameters:
 * 	cpu:
 * 		The vpc list will be updated with the newly added VPC and the Start and
 * 		End indexes will be updated appropriately.
 *
 * Return Value:
 * 	None.
 */
void AXP_21264_AddVPC(AXP_21264_CPU *cpu, AXP_PC vpc)
{
	cpu->vpc[cpu->vpcEnd] = vpc;
	cpu->vpcEnd = (cpu->vpcEnd + 1) % AXP_INFLIGHT_MAX;
	if (cpu->vpcEnd == cpu->vpcStart)
		cpu->vpcStart = (cpu->vpcStart + 1) % AXP_INFLIGHT_MAX;

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_21264_GetPALFuncVPC
 * 	This function is called to get the Virtual Program Counter (VPC) to a
 * 	specific PAL function which is an offset from the address specified in the
 *	PAL_BASE register.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	func:
 *		The value of the function field in the PALcode Instruction Format.
 *
 * Output Parameters:
 *	None.
 *
 * Return Value:
 * 	The value that the PC should be set to to call the requested function.
 */
AXP_PC AXP_21264_GetPALFuncVPC(AXP_21264_CPU *cpu, u32 func)
{
	AXP_IBOX_PAL_PC pc;
	AXP_IBOX_PALBASE_BITS palBase;
	AXP_IBOX_PAL_FUNC_BITS palFunc;

	palBase.palBaseAddr = cpu->palBase.pal_base_pc;
	palFunc.func = func;

	/*
	 * We assume that the function supplied follows any of the following
	 * criteria:
	 *
	 *		Is in the range of 0x40 and 0x7f, inclusive
	 *		Is greater than 0xbf
	 *		Is between 0x00 and 0x3f, inclusive, and IER_CM[CM] is not equal to
	 *			the kernel mode value (0).
	 *
	 * Now, let's compose the PC for the PALcode function we are being
	 * requested to call.
	 */
	if (cpu->majorType >= EV6)
	{
		pc.bits21264.highPC = palBase.bits21264.highPC;
		pc.bits21264.mbz_2 = 0;
		pc.bits21264.mbo = 1;
		pc.bits21264.func_7 = palFunc.bits.func_7;
		pc.bits21264.func_5_0 = palFunc.bits.func_5_0;
		pc.bits21264.mbz_1 = 0;
		pc.bits21264.palMode = AXP_PAL_MODE;
	}
	else
	{
		pc.bits21164.highPC = palBase.bits21164.highPC;
		pc.bits21164.mbo = 1;
		pc.bits21164.func_7 = palFunc.bits.func_7;
		pc.bits21164.func_5_0 = palFunc.bits.func_5_0;
		pc.bits21164.mbz = 0;
		pc.bits21164.palMode = AXP_PAL_MODE;
	}

	/*
	 * Return the composed VPC it back to the caller.
	 */
	return(pc.vpc);
}

/*
 * AXP_21264_GetPALBaseVPC
 * 	This function is called to get the Virtual Program Counter (VPC) to a
 * 	specific offset from the address specified in the PAL_BASE register.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	offset:
 *		An offset value, from PAL_BASE, of the next VPC to be entered into the
 *		VPC list.
 *
 * Output Parameters:
 *	None.
 *
 * Return Value:
 * 	The value that the PC should be set to to call the requested offset.
 */
AXP_PC AXP_21264_GetPALBaseVPC(AXP_21264_CPU *cpu, u64 offset)
{
	u64 pc;

	pc = cpu->palBase.pal_base_pc + offset;

	/*
	 * Get the VPC set with the correct PALmode bit and return it back to the
	 * caller.
	 */
	return(AXP_21264_GetVPC(cpu, pc, AXP_PAL_MODE));
}

/*
 * AXP_21264_GetVPC
 * 	This function is called to get the Virtual Program Counter (VPC) to a
 * 	specific value.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	addr:
 *		A value of the next VPC to be entered into the VPC list.
 *	palMode:
 *		A value to indicate if we will be running in PAL mode.
 *
 * Output Parameters:
 *	None.
 *
 * Return Value:
 * 	The calculated VPC, based on a target virtual address.
 */
AXP_PC AXP_21264_GetVPC(AXP_21264_CPU *cpu, u64 pc, u8 pal)
{
	union
	{
		u64		pc;
		AXP_PC	vpc;
	} vpc;

	vpc.pc = pc;
	vpc.vpc.res = 0;
	vpc.vpc.pal = pal & AXP_PAL_MODE;

	/*
	 * Return back to the caller.
	 */
	return(vpc.vpc);
}

/*
 * AXP_21264_GetNextVPC
 * 	This function is called to retrieve the VPC for the next set of
 * 	instructions to be fetched.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	The VPC of the next set of instructions to fetch from the cache..
 */
AXP_PC AXP_21264_GetNextVPC(AXP_21264_CPU *cpu)
{
	AXP_PC retVal;
	u32	prevVPC;

	/*
	 * The End, points to the next location to be filled.  Therefore, the
	 * previous location is the next VPC to be executed.
	 */
	prevVPC = ((cpu->vpcEnd != 0) ? cpu->vpcEnd : AXP_INFLIGHT_MAX) - 1;
	retVal = cpu->vpc[prevVPC];

	/*
	 * Return what we found back to the caller.
	 */
	return(retVal);
}

/*
 * AXP_21264_IncrementVPC
 * 	This function is called to increment Virtual Program Counter (VPC).
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *
 * Output Parameters:
 *	None.
 *
 * Return Value:
 * 	The value of the incremented PC.
 */
AXP_PC AXP_21264_IncrementVPC(AXP_21264_CPU *cpu)
{
	AXP_PC vpc;

	/*
	 * Get the PC for the instruction just executed.
	 */
	vpc = AXP_21264_GetNextVPC(cpu);

	/*
	 * Increment it.
	 */
	vpc.pc++;

	/*
	 * Store it on the VPC List and return to the caller.
	 */
	return(vpc);
}

/*
 * AXP_21264_DisplaceVPC
 * 	This function is called to add a displacement value to the VPC.
 *
 * Input Parameters:
 * 	cpu:
 *		A pointer to the structure containing all the fields needed to emulate
 *		an Alpha AXP 21264 CPU.
 *	displacement:
 *		A signed 64-bit value to be added to the VPC.
 *
 * Output Parameters:
 *	None.
 *
 * Return Value:
 * 	The value of the PC with the displacement.
 */
AXP_PC AXP_21264_DisplaceVPC(AXP_21264_CPU *cpu, i64 displacement)
{
	AXP_PC vpc;

	/*
	 * Get the PC for the instruction just executed.
	 */
	vpc = AXP_21264_GetNextVPC(cpu);

	/*
	 * Increment and then add the displacement.
	 */
	vpc.pc = vpc.pc + 1 + displacement;

	/*
	 * Return back to the caller.
	 */
	return(vpc);
}

/*
 * AXP_21264_Ibox_Event
 *	This function is called from a number of places.  It receives information
 *	about an event (interrupt) that just occurred.  We need to queue this event
 *	up for the Ibox to process.  The callers to this function include not only
 *	the Ibox, itself, but also the Mbox.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the CPU structure for the emulated Alpha AXP 21264
 *		processor.
 *	fault:
 *		A value indicating the fault that occurred.
 *	pc:
 *		A value indicating the PC for the instruction being executed.
 *	va:
 *		A value indicating the Virtual Address where the fault occurred.
 *	opcode:
 *		A value for the opcode for with the instruction associated with the
 *		fault
 *	reg:
 *		A value indicating the architectural register associated with the
 *		fault.
 *	write:
 *		A boolean to indicate if it was a write operation associated with the
 *		fault.
 *	self:
 *		A boolean too  indicate that the Ibox is actually calling this
 *		function.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Values:
 * 	None.
 */
void AXP_21264_Ibox_Event(
			AXP_21264_CPU *cpu,
			u32 fault,
			AXP_PC pc,
			u64 va,
			u8 opcode,
			u8 reg,
			bool write,
			bool self)
{
	u8	mmStatOpcode = opcode;

	/*
	 * We, the Ibox, did not call this function, then we need to lock down the
	 * the Ibox mutex.
	 */
	if (self == false)
		pthread_mutex_lock(&cpu->iBoxMutex);

	/*
	 * If there is already an exception pending, swallow this current one.
	 */
	if (cpu->excPend == false)
	{

		/*
		 * We always need to lock down the IPR mutex.
		 */
		pthread_mutex_lock(&cpu->iBoxIPRMutex);

		/*
		 * HW_LD (0x1b = 27 -> 3) and HW_ST (0x1f = 31 -> 7), subtract 0x18(24)
		 * from both.
		 */
		if ((opcode == HW_LD) || (opcode == HW_ST))
			mmStatOpcode -= 0x18;
		cpu->excAddr.exc_pc = pc;

		/*
		 * Clear out the fault IPRs.
		 */
		cpu->va = 0;
		*((u64 *) &cpu->excSum) = 0;
		*((u64 *) &cpu->mmStat) = 0;

		/*
		 * Based on the fault, set the appropriate IPRs.
		 */
		switch (fault)
		{
			case AXP_DTBM_DOUBLE_3:
			case AXP_DTBM_DOUBLE_4:
			case AXP_ITB_MISS:
			case AXP_DTBM_SINGLE:
				cpu->mmStat.opcodes = mmStatOpcode;
				cpu->mmStat.wr = (write ? 1 : 0);
				cpu->va = va;
				cpu->excSum.reg = reg;
				break;

			case AXP_DFAULT:
			case AXP_UNALIGNED:
				cpu->excSum.reg = reg;
				cpu->mmStat.opcodes = mmStatOpcode;
				cpu->mmStat.wr = (write ? 1 : 0);
				cpu->mmStat.fow = (write ? 1 : 0);
				cpu->mmStat._for = (write ? 0 : 1);
				cpu->mmStat.acv = 1;
				cpu->va = va;
				break;

			case AXP_IACV:
				cpu->excSum.bad_iva = 0;	/* VA contains the address */
				cpu->va = va;
				break;

			case AXP_ARITH:
			case AXP_FEN:
			case AXP_MT_FPCR_TRAP:
				cpu->excSum.reg = reg;
				break;

			case AXP_OPCDEC:
				cpu->mmStat.opcodes = mmStatOpcode;
				break;

			case AXP_INTERRUPT:
				cpu->iSum.ei = cpu->irqH;
				cpu->irqH = 0;
				break;

			case AXP_MCHK:
			case AXP_RESET_WAKEUP:
				break;
		}

		/*
		 * Sign-extend the set_iov bit.
		 */
		if (cpu->excSum.set_iov == 1)
			cpu->excSum.sext_set_iov = 0xffff;

		/*
		 * Set the exception PC, which the main line will pick up when
		 * processing the exception.
		 */
		cpu->excPC = AXP_21264_GetPALFuncVPC(cpu, fault);

		/*
		 * Make sure to unlock the IPR mutex.
		 */
		pthread_mutex_unlock(&cpu->iBoxIPRMutex);

		/*
		 * Let the main loop know that there is an exception pending.
		 */
		cpu->excPend = true;

		/*
		 * We, the Ibox, did not call this function, then we need signal the
		 * Ibox to process this fault.
		 */
		if (self == false)
			pthread_cond_signal(&cpu->iBoxCondition);
	}

	/*
	 * Now unlock the Ibox mutex.
	 */
	if (self == false)
		pthread_mutex_unlock(&cpu->iBoxMutex);

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_21264_Ibox_Retire_HW_MFPR
 *	This function is called to move a value from a processor register to an
 *	architectural register.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the CPU structure for the emulated Alpha AXP 21264
 *		processor.
 *	instr:
 *		A pointer to the Instruction being retired.  We already know it is a
 *		HW_MFPR instruction.
 *
 * Output Parameters:
 *	None.
 *
 * Return Values:
 *	None.
 */
void AXP_21264_Ibox_Retire_HW_MFPR(AXP_21264_CPU *cpu, AXP_INSTRUCTION *instr)
{

	/*
	 * Before we do anything, we need to lock the appropriate IPR mutex.
	 */
	if (((instr->type_hint_index >= AXP_IPR_ITB_TAG) &&
		 (instr->type_hint_index <= AXP_IPR_SLEEP)) ||
		((instr->type_hint_index >= AXP_IPR_PCXT0) &&
		 (instr->type_hint_index <= AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER_ASN)))
		 pthread_mutex_lock(&cpu->iBoxIPRMutex);
	else if (((instr->type_hint_index >= AXP_IPR_DTB_TAG0) &&
			  (instr->type_hint_index <= AXP_IPR_DC_STAT)) ||
			 ((instr->type_hint_index >= AXP_IPR_DTB_TAG1) &&
			  (instr->type_hint_index <= AXP_IPR_DTB_ASN1)))
		pthread_mutex_lock(&cpu->mBoxIPRMutex);
	else if ((instr->type_hint_index >= AXP_IPR_CC) &&
			 (instr->type_hint_index <= AXP_IPR_VA_CTL))
		pthread_mutex_lock(&cpu->eBoxIPRMutex);
	else
		pthread_mutex_lock(&cpu->cBoxIPRMutex);

	switch (instr->type_hint_index)
	{

		/*
		 * Ibox IPRs (RO and RW)
		 */
		case AXP_IPR_EXC_ADDR:
			AXP_IBOX_READ_EXC_ADDR(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_IVA_FORM:
			AXP_IBOX_READ_IVA_FORM(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_CM:
			AXP_IBOX_READ_CM(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_IER:
			AXP_IBOX_READ_IER(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_IER_CM:
			AXP_IBOX_READ_IER_CM(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_SIRR:
			AXP_IBOX_READ_SIRR(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_ISUM:
			AXP_IBOX_READ_ISUM(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_EXC_SUM:
			AXP_IBOX_READ_EXC_SUM(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_PAL_BASE:
			AXP_IBOX_READ_PAL_BASE(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_I_CTL:
			AXP_IBOX_READ_I_CTL(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_PCTR_CTL:
			AXP_IBOX_READ_PCTR_CTL(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_I_STAT:
			AXP_IBOX_READ_I_STAT(instr->destv.r.uq, cpu);
			break;

		/*
		 * Mbox IPRs (RO and RW)
		 */
		case AXP_IPR_MM_STAT:
			AXP_MBOX_READ_MM_STAT(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_DC_STAT:
			AXP_MBOX_READ_DC_STAT(instr->destv.r.uq, cpu);
			break;

		/*
		 * Cbox IPR (RW)
		 */
		case AXP_IPR_C_DATA:
			AXP_CBOX_READ_C_DATA(instr->destv.r.uq, cpu);
			break;

		/*
		 * Ibox Process Context IPR (R)
		 * NOTE: When reading, all the bits are returned always.
		 */
		case AXP_IPR_PCXT0:
		case AXP_IPR_PCXT0_ASN:
		case AXP_IPR_PCXT0_ASTER:
		case AXP_IPR_PCXT0_ASTER_ASN:
		case AXP_IPR_PCXT0_ASTRR:
		case AXP_IPR_PCXT0_ASTRR_ASN:
		case AXP_IPR_PCXT0_ASTRR_ASTER:
		case AXP_IPR_PCXT0_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT0_PPCE:
		case AXP_IPR_PCXT0_PPCE_ASN:
		case AXP_IPR_PCXT0_PPCE_ASTER:
		case AXP_IPR_PCXT0_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT0_PPCE_ASTRR:
		case AXP_IPR_PCXT0_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT0_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT0_PPCE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE:
		case AXP_IPR_PCXT0_FPE_ASN:
		case AXP_IPR_PCXT0_FPE_ASTER:
		case AXP_IPR_PCXT0_FPE_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE_ASTRR:
		case AXP_IPR_PCXT0_FPE_ASTRR_ASN:
		case AXP_IPR_PCXT0_FPE_ASTRR_ASTER:
		case AXP_IPR_PCXT0_FPE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE:
		case AXP_IPR_PCXT0_FPE_PPCE_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTER:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1:
		case AXP_IPR_PCXT1_ASN:
		case AXP_IPR_PCXT1_ASTER:
		case AXP_IPR_PCXT1_ASTER_ASN:
		case AXP_IPR_PCXT1_ASTRR:
		case AXP_IPR_PCXT1_ASTRR_ASN:
		case AXP_IPR_PCXT1_ASTRR_ASTER:
		case AXP_IPR_PCXT1_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1_PPCE:
		case AXP_IPR_PCXT1_PPCE_ASN:
		case AXP_IPR_PCXT1_PPCE_ASTER:
		case AXP_IPR_PCXT1_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT1_PPCE_ASTRR:
		case AXP_IPR_PCXT1_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT1_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT1_PPCE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE:
		case AXP_IPR_PCXT1_FPE_ASN:
		case AXP_IPR_PCXT1_FPE_ASTER:
		case AXP_IPR_PCXT1_FPE_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE_ASTRR:
		case AXP_IPR_PCXT1_FPE_ASTRR_ASN:
		case AXP_IPR_PCXT1_FPE_ASTRR_ASTER:
		case AXP_IPR_PCXT1_FPE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE:
		case AXP_IPR_PCXT1_FPE_PPCE_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTER:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER_ASN:
			AXP_IBOX_READ_PCTX(instr->destv.r.uq, cpu);
			break;

		/*
		 * Ebox IPRS (RO and RW)
		 */
		case AXP_IPR_CC:
			AXP_EBOX_READ_CC(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_VA:
			AXP_EBOX_READ_VA(instr->destv.r.uq, cpu);
			break;

		case AXP_IPR_VA_FORM:
			AXP_EBOX_READ_VA_FORM(instr->destv.r.uq, cpu);
			break;

		default:
			break;
	}

	/*
	 * Make sure to unlock the appropriate IPR mutex.
	 */
	if (((instr->type_hint_index >= AXP_IPR_ITB_TAG) &&
		 (instr->type_hint_index <= AXP_IPR_SLEEP)) ||
		((instr->type_hint_index >= AXP_IPR_PCXT0) &&
		 (instr->type_hint_index <= AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER_ASN)))
		 pthread_mutex_unlock(&cpu->iBoxIPRMutex);
	else if (((instr->type_hint_index >= AXP_IPR_DTB_TAG0) &&
			  (instr->type_hint_index <= AXP_IPR_DC_STAT)) ||
			 ((instr->type_hint_index >= AXP_IPR_DTB_TAG1) &&
			  (instr->type_hint_index <= AXP_IPR_DTB_ASN1)))
		pthread_mutex_unlock(&cpu->mBoxIPRMutex);
	else if ((instr->type_hint_index >= AXP_IPR_CC) &&
			 (instr->type_hint_index <= AXP_IPR_VA_CTL))
		pthread_mutex_unlock(&cpu->eBoxIPRMutex);
	else
		pthread_mutex_unlock(&cpu->cBoxIPRMutex);

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_21264_Ibox_Retire_HW_MTPR
 *	This function is called to move a value from an architectural register to a
 *	processor register.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the CPU structure for the emulated Alpha AXP 21264
 *		processor.
 *	instr:
 *		A pointer to the Instruction being retired.  We already know it is a
 *		HW_MTPR instruction.
 *
 * Output Parameters:
 *	None.
 *
 * Return Values:
 *	None.
 */
void AXP_21264_Ibox_Retire_HW_MFPR(AXP_21264_CPU *cpu, AXP_INSTRUCTION *instr)
{

	/*
	 * Before we do anything, we need to lock the appropriate IPR mutex.
	 */
	if (((instr->type_hint_index >= AXP_IPR_ITB_TAG) &&
		 (instr->type_hint_index <= AXP_IPR_SLEEP)) ||
		((instr->type_hint_index >= AXP_IPR_PCXT0) &&
		 (instr->type_hint_index <= AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER_ASN)))
		 pthread_mutex_lock(&cpu->iBoxIPRMutex);
	else if (((instr->type_hint_index >= AXP_IPR_DTB_TAG0) &&
			  (instr->type_hint_index <= AXP_IPR_DC_STAT)) ||
			 ((instr->type_hint_index >= AXP_IPR_DTB_TAG1) &&
			  (instr->type_hint_index <= AXP_IPR_DTB_ASN1)))
		pthread_mutex_lock(&cpu->mBoxIPRMutex);
	else if ((instr->type_hint_index >= AXP_IPR_CC) &&
			 (instr->type_hint_index <= AXP_IPR_VA_CTL))
		pthread_mutex_lock(&cpu->eBoxIPRMutex);
	else
		pthread_mutex_lock(&cpu->cBoxIPRMutex);

	switch (instr->type_hint_index)
	{

		/*
		 * Ibox IPRs (RW, WO, and W)
		 */
		case AXP_IPR_ITB_TAG:
			AXP_IBOX_WRITE_ITB_TAG(instr->src1v.r.uq, cpu);
			break;

		case AXP_IPR_ITB_PTE:
			AXP_IBOX_WRITE_ITB_PTE(instr->src1v.r.uq, cpu);

			/*
			 * Retiring this instruction causes the TAG and PTE to be written
			 * into the ITB entry.
			 */
			AXP_addTLBEntry(
					cpu,
					*((u64 *) &cpu->itbTag),
					*((u64 *) &cpu->itbPte),
					false);
			break;

		case AXP_IPR_ITB_IAP:

			/*
			 * This is a Pseudo register.  Writing to it clears all the ITB PTE
			 * entries with an ASM bit clear.
			 */
			AXP_tbiap(cpu, false);
			break;

		case AXP_IPR_ITB_IA:

			/*
			 * This is a Pseudo register.  Writing to it clears all the ITB PTE
			 * entries.
			 */
			AXP_tbia(cpu, false);
			break;

		case AXP_IPR_ITB_IS:
			AXP_IBOX_WRITE_ITB_IS(instr->src1v.r.uq, cpu);

			/*
			 * Writing to it clears the ITB PTE entries that matches the ITB_IS
			 * IPR.
			 */
			AXP_tbis(cpu, *((u64 *) &cpu->itbIs), false);
			break;


		case AXP_IPR_CM:
			AXP_IBOX_WRITE_CM(instr->src1v.r.uq, cpu);
			break;

		case AXP_IPR_IER:
			AXP_IBOX_WRITE_IER(instr->src1v.r.uq, cpu);
			break;

		case AXP_IPR_IER_CM:
			AXP_IBOX_WRITE_IER_CM(instr->src1v.r.uq, cpu);
			break;

		case AXP_IPR_SIRR:
			AXP_IBOX_WRITE_SIRR(instr->src1v.r.uq, cpu);
			break;

		case AXP_IPR_HW_INT_CLR:
			AXP_IBOX_WRITE_HW_INT_CLR(instr->src1v.r.uq, cpu);
			/* TODO: Must we do more to actually clear the hardware interrupts */
			break;

		case AXP_IPR_PAL_BASE:
			AXP_IBOX_WRITE_PAL_BASE(instr->src1v.r.uq, cpu);
			break;

		case AXP_IPR_I_CTL:
			AXP_IBOX_WRITE_I_CTL(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_IC_FLUSH_ASM:
			/* TODO: Pseudo register */
			break;

		case AXP_IPR_IC_FLUSH:
			/* TODO: Pseudo register */
			break;

		case AXP_IPR_PCTR_CTL:
			AXP_IBOX_WRITE_PCTR_CTL(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_CLR_MAP:
			/* TODO: Pseudo register */
			break;

		case AXP_IPR_I_STAT:
			AXP_IBOX_WRITE_I_STAT(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_SLEEP:
			/* TODO: Pseudo register */
			break;

		case AXP_IPR_DTB_TAG0:
			AXP_MBOX_WRITE_DTB_TAG0(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_DTB_PTE0:
			AXP_MBOX_WRITE_DTB_PTE0(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_DTB_IS0:
			AXP_MBOX_WRITE_DTB_IS0(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_DTB_ASN0:
			AXP_MBOX_WRITE_DTB_ASN0(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_DTB_ALTMODE:
			AXP_MBOX_WRITE_DTB_ALTMODE(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_M_CTL:
			AXP_MBOX_WRITE_M_CTL(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_DC_CTL:
			AXP_MBOX_WRITE_DC_CTL(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_DC_STAT:
			AXP_MBOX_WRITE_DC_STAT(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_C_DATA:
			AXP_CBOX_WRITE_C_DATA(instr->src1v.r.uq,cpu);
			break;

		case AXP_IPR_C_SHFT:
			AXP_CBOX_WRITE_C_SHFT(instr->src1v.r.uq,cpu);
			break;

		/*
		 * Ibox Process Context IPR (W)
		 */
		case AXP_IPR_PCXT0:
		case AXP_IPR_PCXT0_ASN:
		case AXP_IPR_PCXT0_ASTER:
		case AXP_IPR_PCXT0_ASTER_ASN:
		case AXP_IPR_PCXT0_ASTRR:
		case AXP_IPR_PCXT0_ASTRR_ASN:
		case AXP_IPR_PCXT0_ASTRR_ASTER:
		case AXP_IPR_PCXT0_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT0_PPCE:
		case AXP_IPR_PCXT0_PPCE_ASN:
		case AXP_IPR_PCXT0_PPCE_ASTER:
		case AXP_IPR_PCXT0_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT0_PPCE_ASTRR:
		case AXP_IPR_PCXT0_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT0_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT0_PPCE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE:
		case AXP_IPR_PCXT0_FPE_ASN:
		case AXP_IPR_PCXT0_FPE_ASTER:
		case AXP_IPR_PCXT0_FPE_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE_ASTRR:
		case AXP_IPR_PCXT0_FPE_ASTRR_ASN:
		case AXP_IPR_PCXT0_FPE_ASTRR_ASTER:
		case AXP_IPR_PCXT0_FPE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE:
		case AXP_IPR_PCXT0_FPE_PPCE_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTER:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT0_FPE_PPCE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1:
		case AXP_IPR_PCXT1_ASN:
		case AXP_IPR_PCXT1_ASTER:
		case AXP_IPR_PCXT1_ASTER_ASN:
		case AXP_IPR_PCXT1_ASTRR:
		case AXP_IPR_PCXT1_ASTRR_ASN:
		case AXP_IPR_PCXT1_ASTRR_ASTER:
		case AXP_IPR_PCXT1_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1_PPCE:
		case AXP_IPR_PCXT1_PPCE_ASN:
		case AXP_IPR_PCXT1_PPCE_ASTER:
		case AXP_IPR_PCXT1_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT1_PPCE_ASTRR:
		case AXP_IPR_PCXT1_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT1_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT1_PPCE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE:
		case AXP_IPR_PCXT1_FPE_ASN:
		case AXP_IPR_PCXT1_FPE_ASTER:
		case AXP_IPR_PCXT1_FPE_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE_ASTRR:
		case AXP_IPR_PCXT1_FPE_ASTRR_ASN:
		case AXP_IPR_PCXT1_FPE_ASTRR_ASTER:
		case AXP_IPR_PCXT1_FPE_ASTRR_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE:
		case AXP_IPR_PCXT1_FPE_PPCE_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTER:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTER_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASN:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER:
		case AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER_ASN:
			break;

		/*
		 * Mbox IPRs (RW and WO)
		 */
		case AXP_IPR_DTB_TAG1:
		case AXP_IPR_DTB_PTE1:
		case AXP_IPR_DTB_IAP:
		case AXP_IPR_DTB_IA:
		case AXP_IPR_DTB_IS1:
		case AXP_IPR_DTB_ASN1:
			break;

		/*
		 * Ebox IPRs (RW and W0)
		 */
		case AXP_IPR_CC:
		case AXP_IPR_CC_CTL:
		case AXP_IPR_VA_CTL:
			break;

		default:
			break;
	}

	/*
	 * Make sure we unlock the appropriate IPR mutex.
	 */
	if (((instr->type_hint_index >= AXP_IPR_ITB_TAG) &&
		 (instr->type_hint_index <= AXP_IPR_SLEEP)) ||
		((instr->type_hint_index >= AXP_IPR_PCXT0) &&
		 (instr->type_hint_index <= AXP_IPR_PCXT1_FPE_PPCE_ASTRR_ASTER_ASN)))
		 pthread_mutex_unlock(&cpu->iBoxIPRMutex);
	else if (((instr->type_hint_index >= AXP_IPR_DTB_TAG0) &&
			  (instr->type_hint_index <= AXP_IPR_DC_STAT)) ||
			 ((instr->type_hint_index >= AXP_IPR_DTB_TAG1) &&
			  (instr->type_hint_index <= AXP_IPR_DTB_ASN1)))
		pthread_mutex_unlock(&cpu->mBoxIPRMutex);
	else if ((instr->type_hint_index >= AXP_IPR_CC) &&
			 (instr->type_hint_index <= AXP_IPR_VA_CTL))
		pthread_mutex_unlock(&cpu->eBoxIPRMutex);
	else
		pthread_mutex_unlock(&cpu->cBoxIPRMutex);

	/*
	 * Return back to the caller.
	 */
	return;
}
/*
 * AXP_21264_Ibox_Retire
 *	This function is called whenever an instruction is transitioned to
 *	WaitingRetirement state.  This function will search through the ReOrder
 *	Buffer (ROB) from the oldest to the newest and retire all the instructions
 *	it can, in order.  If there was an exception, this should cause the
 *	remaining instructions to be flushed and not retired.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the CPU structure for the emulated Alpha AXP 21264
 *		processor.
 *
 * Output Parameters:
 *	None.
 *
 * Return Values:
 *	None.
 */
void AXP_21264_Ibox_Retire(AXP_21264_CPU *cpu)
{
	AXP_INSTRUCTION	*rob;
	u32				ii, start, end;
	bool			split;
	bool			done = false;

	/*
	 * First lock the ROB mutex so that it is not updated by anyone but this
	 * function.
	 */
	pthread_mutex_lock(&cpu->robMutex);

	/*
	 * The split flag is used to determine when the end index has wrap to the
	 * the start of the list, making it less than the beginning index (at least
	 * until the beginning index wraps to the start as well).
	 */
	split = cpu->robEnd < cpu->robStart;

	/*
	 * Determine out initial start and end entries.  If the end has wrapped
	 * around, then we search in 2 passes (start to list end; list beginning to
	 * end).
	 */
	start = cpu->robStart;
	end = (split ? end = cpu->robEnd : AXP_INFLIGHT_MAX);

	/*
	 * Set our starting value.  Loop until we reach the end or we find an entry
	 * that is not ready for retirement (maybe its 401K is not where it should
	 * be or his employer bankrupt the pension fund).
	 */
	ii = start;
	while ((ii < end) && (done == false))
	{
		rob = &cpu->rob[ii];

		/*
		 * If the next entry is ready for retirement, then complete the work
		 * necessary for this instruction.  If it is not, then because
		 * instructions need to be completed in order, then we are done trying
		 * to retire instructions.
		 */
		if (rob->state == WaitingRetirement)
		{

			/*
			 * If an exception occurred, we need to process it.  Otherwise, the
			 * destination value should be written to the destination
			 * (physical) register.  If it is a store operation, then we need
			 * to update the Dcache.
			 */
			if (rob->excRegMask != NoException)
			{
			}
			else
			{

				/*
				 * We do this here so that the subsequent code can move the IPR
				 * value into the correct register.  The HW_MTPR is handled
				 * below (in the switch statement).
				 */
				if (rob->opcode == HW_MFPR)
					AXP_21264_Ibox_Retire_HW_MFPR(cpu, rob);

				/*
				 * If the destination register is a floating-point register,
				 * then move the instruction result into the correct physical
				 * floating-point register.
				 */
				if ((rob->decodedReg.bits.dest & AXP_DEST_FLOAT) == AXP_DEST_FLOAT)
					cpu->pf[rob->dest] = rob->destv;

				/*
				 * If the destination register is not a floating-point
				 * register, we either have an instruction that stores the
				 * result into a physical integer register, or does not store
				 * a result at all.  For the latter, there is nothing more to
				 * do.
				 */
				else if (rob->decodedReg.bits.dest != 0)
					cpu->pr[rob->dest] = rob->destv;

				/*
				 * If a store, write it to the Dcache.
				 */
				switch (rob->opcode)
				{
					case STW:
					case STB:
					case STQ_U:
					case HW_ST:
					case STF:
					case STG:
					case STS:
					case STT:
					case STL:
					case STQ:
					case STL_C:
					case STQ_C:
						AXP_21264_Mbox_RetireWrite(cpu, rob->slot);
						break;

					case HW_MTPR:
						AXP_21264_Ibox_Retire_HW_MFPR(cpu, rob);
						break;

					default:
						break;
				}
			}

			/*
			 * Mark the instruction retired and move the top of the stack to
			 * the next instruction location.
			 */
			cpu->rob[ii].state = Retired;
			cpu->robStart = (cpu->robStart + 1) % AXP_INFLIGHT_MAX;
		}
		else
			done = true;

		/*
		 * We processed the current ROB.  Time to move onto the next.
		 */
		ii++;

		/*
		 * If we reached the end, but the search is split, then change the
		 * index to the start of the list and the end to the end of the list.
		 * Clear the split flag, so that we don't get ourselves into an
		 * infinite loop.
		 */
		if ((ii == end) && (split == true))
		{
			ii = 0;
			end = cpu->robEnd;
			split = false;
		}
	}

	/*
	 * Finally, unlock the ROB mutex so that it can be updated by another
	 * thread.
	 */
	pthread_mutex_unlock(&cpu->robMutex);

	/*
	 * Return back to the caller.
	 */
	return;
}

/*
 * AXP_21264_Ibox_Init
 *	This function is called to initialize the Ibox.  It will set the IPRs
 *	associated with the Ibox to their initial/reset values.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the CPU structure for the emulated Alpha AXP 21264
 *		processor.
 *
 * Output Parameters:
 *	None.
 *
 * Return Value:
 *	true:	Failed to perform all initialization processing.
 *	false:	Normal Successful Completion.
 */
bool AXP_21264_Ibox_Init(AXP_21264_CPU *cpu)
{
	bool	retVal = false;
	int		ii, jj, kk;

	/*
	 * We start out with no exceptions pending.
	 */
	cpu->excPend = false;

	/*
	 * Initialize the branch prediction information.
	 */
	for (ii = 0; ii < ONE_K; ii++)
	{
		cpu->localHistoryTable.lcl_history[ii] = 0;
		cpu->localPredictor.lcl_pred[ii] = 0;
		cpu->choicePredictor.choice_pred[ii] = 0;
	}
	for (ii = 0; ii < FOUR_K; ii++)
		cpu->globalPredictor.gbl_pred[ii] = 0;
	cpu->globalPathHistory = 0;

	/*
	 * TODO: Initialize the Integer and Floating Point register arrays.
	 */

	/*
	 * Initialize the Ibox IPRs.
	 */
	cpu->itbTag.res_1 = 0;		/* ITB_TAG */
	cpu->itbTag.tag = 0;
	cpu->itbTag.res_2 = 0;
	cpu->itbPte.res_1 = 0;		/* ITB_PTE */
	cpu->itbPte._asm = 0;
	cpu->itbPte.gh = 0;
	cpu->itbPte.res_2 = 0;
	cpu->itbPte.kre = 0;
	cpu->itbPte.ere = 0;
	cpu->itbPte.sre = 0;
	cpu->itbPte.ure = 0;
	cpu->itbPte.res_3 = 0;
	cpu->itbPte.pfn = 0;
	cpu->itbPte.res_4 = 0;
	cpu->itbIs.res_1 = 0;			/* ITB_IS */
	cpu->itbIs.inval_itb = 0;
	cpu->itbIs.res_2 = 0;
	cpu->excAddr.exc_addr = 0;		/* EXC_ADDR */
	cpu->ivaForm.form10.res = 0;	/* IVA_FORM */
	cpu->ivaForm.form10.va_sext_vptb = 0;
	cpu->ierCm.res_1 = 0;			/* IER_CM */
	cpu->ierCm.cm = 0;
	cpu->ierCm.res_2 = 0;
	cpu->ierCm.asten = 0;
	cpu->ierCm.sien = 0;
	cpu->ierCm.pcen = 0;
	cpu->ierCm.cren = 0;
	cpu->ierCm.slen = 0;
	cpu->ierCm.eien = 0;
	cpu->ierCm.res_3 = 0;
	cpu->sirr.res_1 = 0;			/* SIRR */
	cpu->sirr.sir = 0;
	cpu->sirr.res_2 = 0;
	cpu->iSum.res_1 = 0;			/* ISUM */
	cpu->iSum.astk = 0;
	cpu->iSum.aste = 0;
	cpu->iSum.res_2 = 0;
	cpu->iSum.asts = 0;
	cpu->iSum.astu = 0;
	cpu->iSum.res_3 = 0;
	cpu->iSum.si = 0;
	cpu->iSum.pc = 0;
	cpu->iSum.cr = 0;
	cpu->iSum.sl = 0;
	cpu->iSum.ei = 0;
	cpu->iSum.res_4 = 0;
	cpu->hwIntClr.res_1 = 0;		/* HW_INT_CLR */
	cpu->hwIntClr.fbtp = 0;
	cpu->hwIntClr.mchk_d = 0;
	cpu->hwIntClr.res_2 = 0;
	cpu->hwIntClr.pc = 0;
	cpu->hwIntClr.cr = 0;
	cpu->hwIntClr.sl = 0;
	cpu->hwIntClr.res_3 = 0;
	cpu->excSum.swc = 0;			/* EXC_SUM */
	cpu->excSum.inv = 0;
	cpu->excSum.dze = 0;
	cpu->excSum.ovf = 0;
	cpu->excSum.unf = 0;
	cpu->excSum.ine = 0;
	cpu->excSum.iov = 0;
	cpu->excSum._int = 0;
	cpu->excSum.reg = 0;
	cpu->excSum.bad_iva = 0;
	cpu->excSum.res = 0;
	cpu->excSum.pc_ovfl = 0;
	cpu->excSum.set_inv = 0;
	cpu->excSum.set_dze = 0;
	cpu->excSum.set_ovf = 0;
	cpu->excSum.set_unf = 0;
	cpu->excSum.set_ine = 0;
	cpu->excSum.set_iov = 0;
	cpu->excSum.sext_set_iov = 0;
	cpu->palBase.pal_base_pc = 0;	/* PAL_BASE */
	cpu->iCtl.spce = 0;				/* I_CTL */
	cpu->iCtl.ic_en = 3;
	cpu->iCtl.spe = 0;
	cpu->iCtl.sde = 0;
	cpu->iCtl.sbe = 0;
	cpu->iCtl.bp_mode = 0;
	cpu->iCtl.hwe = 0;
	cpu->iCtl.sl_xmit = 0;
	cpu->iCtl.sl_rcv = 0;
	cpu->iCtl.va_48 = 0;
	cpu->iCtl.va_form_32 = 0;
	cpu->iCtl.single_issue_h = 0;
	cpu->iCtl.pct0_en = 0;
	cpu->iCtl.pct1_en = 0;
	cpu->iCtl.call_pal_r23 = 0;
	cpu->iCtl.mchk_en = 0;
	cpu->iCtl.tb_mb_en = 0;
	cpu->iCtl.bist_fail = 0;
	cpu->iCtl.chip_id = 0;
	cpu->iCtl.vptb = 0;
	cpu->iCtl.sext_vptb = 0;
	cpu->iStat.res_1 = 0;			/* I_STAT */
	cpu->iStat.tpe = 0;
	cpu->iStat.dpe = 0;
	cpu->iStat.res_2 = 0;
	cpu->pCtx.res_1 = 0;			/* PCTX */
	cpu->pCtx.ppce = 0;
	cpu->pCtx.fpe = 1;
	cpu->pCtx.res_2 = 0;
	cpu->pCtx.aster = 0;
	cpu->pCtx.astrr = 0;
	cpu->pCtx.res_3 = 0;
	cpu->pCtx.asn = 0;
	cpu->pCtx.res_4 = 0;
	cpu->pCtrCtl.sl1 = 0;			/* PCTR_CTL */
	cpu->pCtrCtl.sl0 = 0;
	cpu->pCtrCtl.res_1 = 0;
	cpu->pCtrCtl.pctr1 = 0;
	cpu->pCtrCtl.res_2 = 0;
	cpu->pCtrCtl.pctr0 = 0;
	cpu->pCtrCtl.sext_pctr0 = 0;

	/*
	 * Initialize the Unique instruction ID and the VPC array.
	 */
	cpu->instrCounter = 0;
	cpu->vpcStart = 0;
	cpu->vpcEnd = 0;
	for (ii = 0; ii < AXP_INFLIGHT_MAX; ii++)
	{
		cpu->vpc[ii].pal = 0;
		cpu->vpc[ii].res = 0;
		cpu->vpc[ii].pc = 0;
	}

	/*
	 * Initialize the instruction cache.
	 */
	for (ii = 0; ii < AXP_CACHE_ENTRIES; ii++)
	{
		for (jj = 0; jj < AXP_2_WAY_CACHE; jj++)
		{
			cpu->iCache[ii][jj].kre = 0;
			cpu->iCache[ii][jj].ere = 0;
			cpu->iCache[ii][jj].sre = 0;
			cpu->iCache[ii][jj].ure = 0;
			cpu->iCache[ii][jj]._asm = 0;
			cpu->iCache[ii][jj].asn = 0;
			cpu->iCache[ii][jj].pal = 0;
			cpu->iCache[ii][jj].vb = 0;
			cpu->iCache[ii][jj].tag = 0;
			cpu->iCache[ii][jj].set_0_1 = 0;
			cpu->iCache[ii][jj].res_1 = 0;
			for (kk = 0; kk < AXP_ICACHE_LINE_INS; kk++)
				cpu->iCache[ii][jj].instructions[kk].instr = 0;
		}
	}

	/*
	 * Initialize the Instruction Translation Look-aside Buffer.
	 */
	cpu->nextITB = 0;
	for (ii = 0; ii < AXP_TB_LEN; ii++)
	{
		cpu->itb[ii].virtAddr = 0;
		cpu->itb[ii].physAddr = 0;
		cpu->itb[ii].matchMask = 0;
		cpu->itb[ii].keepMask = 0;
		cpu->itb[ii].kre = 0;
		cpu->itb[ii].ere = 0;
		cpu->itb[ii].sre = 0;
		cpu->itb[ii].ure = 0;
		cpu->itb[ii].kwe = 0;
		cpu->itb[ii].ewe = 0;
		cpu->itb[ii].swe = 0;
		cpu->itb[ii].uwe = 0;
		cpu->itb[ii].faultOnRead = 0;
		cpu->itb[ii].faultOnWrite = 0;
		cpu->itb[ii].faultOnExecute = 0;
		cpu->itb[ii].res_1 = 0;
		cpu->itb[ii].asn = 0;
		cpu->itb[ii]._asm = false;
		cpu->itb[ii].valid = false;
	}

	/*
	 * Initialize the ReOrder Buffer (ROB).
	 */
	cpu->robStart = 0;
	cpu->robEnd = 0;
	for (ii = 0; ii < AXP_INFLIGHT_MAX; ii++)
	{
		cpu->rob[ii].state = Retired;
	}

	/*
	 * Return the result of this initialization back to the caller.
	 */
	return(retVal);
}

/*
 * AXP_21264_IboxMain
 * 	This function is called to perform the emulation for the Ibox within the
 * 	Alpha AXP 21264 CPU.
 *
 * Input Parameters:
 * 	cpu:
 * 		A pointer to the structure holding the  fields required to emulate an
 * 		Alpha AXP 21264 CPU.
 *
 * Output Parameters:
 * 	None.
 *
 * Return Value:
 * 	None.
 */
void *AXP_21264_IboxMain(void *voidPtr)
{
	AXP_21264_CPU	*cpu = (AXP_21264_CPU *) voidPtr;
	AXP_PC			nextPC, branchPC;
	AXP_INS_LINE	nextCacheLine;
	AXP_EXCEPTIONS	exception;
	AXP_INSTRUCTION	*decodedInstr;
	AXP_QUEUE_ENTRY	*xqEntry;
	u32				ii, fault;
	bool			local, global, choice, wasRunning = false;
	bool			_asm;
	u16				whichQueue;
	int				qFull;
	bool			noop;

	/*
	 * OK, we are just starting out and there is probably nothing available to
	 * process, yet.  Lock the CPU mutex, which the state of the CPU and if not
	 * in a Run or ShuttingDown state, then wait on the CPU condition variable.
	 */
	pthread_mutex_lock(&cpu->cpuMutex);
	while ((cpu->cpuState != Run) && (cpu->cpuState != ShuttingDown))
	{
		pthread_cond_wait(&cpu->cpuCond, &cpu->cpuMutex);
	}
	pthread_mutex_unlock(&cpu->cpuMutex);

	/*
	 * OK, we've either been successfully initialized or we are shutting-down
	 * before we even started.  If it is the former, then we need to lock the
	 * iBox mutex.
	 */
	if (cpu->cpuState == Run)
	{
		pthread_mutex_lock(&cpu->iBoxMutex);
		wasRunning = true;
	}

	/*
	 * Here we'll loop starting at the current PC and working our way through
	 * all the instructions.  We will do the following steps.
	 *
	 *	1) Fetch the next set of instructions.
	 *	2) If step 1 returns a Miss, then get the Cbox to fill the Icache with
	 *		the next set of instructions.
	 *	3) If step 1 returns a WayMiss, then we need to generate an ITB Miss
	 *		exception, with the PC address we were trying to step to as the
	 *		return address.
	 *	4) If step 1 returns a Hit, then process the next set of instructions.
	 *		a) Decode and rename the registers in each instruction into the ROB.
	 *		b) If the decoded instruction is a branch, then predict if this
	 *			branch will be taken.
	 *		c) If step 4b is true, then adjust the line and set predictors
	 *			appropriately.
	 *		d) Fetch and insert an instruction entry into the appropriate
	 *			instruction queue (IQ or FQ).
	 *	5) If the branch predictor indicated a branch, then determine if
	 *		we have to load an ITB entry and ultimately load the iCache
	 *	6) Loop back to step 1.
	 */

	/*
	 * We keep looping while the CPU is in a running state.
	 */
	while (cpu->cpuState == Run)
	{

		/*
		 * Exceptions take precedence over normal CPU processing.  IF an
		 * exception occurred, then make this the next PC and clear the
		 * exception pending flag.
		 */
		if (cpu->excPend == true)
		{

			/*
			 * TODO: Push the excAddr onto the prediction stack.
			 */
			nextPC = cpu->excPC;
			cpu->excPend = false;

		}
		else

			/*
			 * Get the PC for the next set of instructions to be fetched from
			 * the Icache and Fetch those instructions.
			 */
			nextPC = AXP_21264_GetNextVPC(cpu);

		/*
		 * The cache fetch will return true or false.  If true, we received the
		 * next four instructions.  If false, we need to to determine if we
		 * need to call the PALcode to add a TLB entry to the ITB and/or then
		 * get the Cbox to fill the iCache.  If the former, store the faulting
		 * PC and generate an exception.
		 */
		if (AXP_IcacheFetch(cpu, nextPC, &nextCacheLine) == true)
		{
			for (ii = 0; ii < AXP_NUM_FETCH_INS; ii++)
			{

				/*
				 * TODO:	We need an ROB mutex, because the Ibox, Ebox, and
				 *			Fbox all access this queue on the fly.  Also,
				 *			consider doing this as a counter queue.
				 */
				decodedInstr = &cpu->rob[cpu->robEnd];
				cpu->robEnd = (cpu->robEnd + 1) % AXP_INFLIGHT_MAX;
				if (cpu->robEnd == cpu->robStart)
					cpu->robStart = (cpu->robStart + 1) % AXP_INFLIGHT_MAX;
				AXP_Decode_Rename(cpu, &nextCacheLine, ii, decodedInstr);
				if (decodedInstr->type == Branch)
				{
					decodedInstr->branchPredict = AXP_Branch_Prediction(
								cpu,
								nextPC,
								&local,
								&global,
								&choice);

					/*
					 * TODO:	First, we can use the PC handling functions
					 *			to calculate the branch PC.
					 * TODO:	Second, we need to make sure that we are
					 *			calculating the branch address correctly.
					 * TODO:	Third, We need to be able to handle
					 *			returns, and utilization of the, yet to be
					 *			implemented, prediction stack.
					 * TODO:	Finally, we need to flush the remaining
					 *			instructions to be decoded and go get the
					 *			predicted instructions.
					 */
					if (decodedInstr->branchPredict == true)
					{
						branchPC.pc = nextPC.pc + 1 + decodedInstr->displacement;
						if (AXP_IcacheValid(cpu, branchPC) == false)
						{
							u64		pa;
							bool	_asm;
							u32		fault;

							/*
							 * We are branching to a location that is not
							 * currently in the Icache.  We have to do the
							 * following:
							 *	1) Convert the virtual address to a physical
							 *	   address.
							 * 	2) Request the Cbox fetch the next set of
							 * 	   instructions.
							 */
							pa = AXP_va2pa(
									cpu,
									*((u64 *) &branchPC),
									nextPC,
									false,
									Execute,
									&_asm,
									&fault,
									&exception);
							AXP_21264_Add_MAF(
									cpu,
									Istream,
									pa,
									0,
									AXP_ICACHE_BUF_LEN,
									false);
						}

						/*
						 * TODO:	If we get a Hit, there is nothing else
						 * 			to do.  If we get a Miss, we probably
						 * 			should have someone fill the Icache
						 * 			with the next set of instructions.  If
						 * 			a WayMiss, we don't do anything either.
						 * 			In this last case, we will end up
						 * 			generating an ITB_MISS event to be
						 * 			handled by the PALcode.
						 */
					}
				}

				/*
				 * If this is one of the potential NOOP instructions, then the
				 * instruction is already completed and does not need to be
				 * queued up.
				 */
				noop = (decodedInstr->pipeline == PipelineNone ? true : false);
				if (decodedInstr->aDest == AXP_UNMAPPED_REG)
				{
					switch (decodedInstr->opcode)
					{
						case INTA:
						case INTL:
						case INTM:
						case INTS:
						case LDQ_U:
						case ITFP:
							if (decodedInstr->aDest == AXP_UNMAPPED_REG)
								noop = true;
							break;

						case FLTI:
						case FLTL:
						case FLTV:
							if ((decodedInstr->aDest == AXP_UNMAPPED_REG) &&
								(decodedInstr->function != AXP_FUNC_MT_FPCR))
								noop = true;
							break;
					}
				}
				if (noop == false)
				{

					/*
					 * Before we do much more, if we have a load/store, we need
					 * to request an entry in either the LQ or SQ in the Mbox.
					 */
					switch (decodedInstr->opcode)
					{
						case LDBU:
						case LDQ_U:
						case LDW_U:
						case HW_LD:
						case LDF:
						case LDG:
						case LDS:
						case LDT:
						case LDL:
						case LDQ:
						case LDL_L:
						case LDQ_L:
							decodedInstr->slot = AXP_21264_Mbox_GetLQSlot(cpu);
							break;

						case STW:
						case STB:
						case STQ_U:
						case HW_ST:
						case STF:
						case STG:
						case STS:
						case STT:
						case STL:
						case STQ:
						case STL_C:
						case STQ_C:
							decodedInstr->slot = AXP_21264_Mbox_GetSQSlot(cpu);
							break;

						default:
							break;
					}
					whichQueue = AXP_InstructionQueue(decodedInstr->opcode);
					if(whichQueue == AXP_COND)
					{
						if (decodedInstr->opcode == ITFP)
						{
							if ((decodedInstr->function == AXP_FUNC_ITOFS) ||
								(decodedInstr->function == AXP_FUNC_ITOFF) ||
								(decodedInstr->function == AXP_FUNC_ITOFT))
								whichQueue = AXP_IQ;
							else
								whichQueue = AXP_FQ;
						}
						else	/* FPTI */
						{
							if ((decodedInstr->function == AXP_FUNC_FTOIT) ||
								(decodedInstr->function == AXP_FUNC_FTOIS))
								whichQueue = AXP_FQ;
							else
								whichQueue = AXP_IQ;
							}
					}
					if (whichQueue == AXP_IQ)
					{
						xqEntry = AXP_GetNextIQEntry(cpu);
						xqEntry->ins = decodedInstr;
						pthread_mutex_lock(&cpu->eBoxMutex);
						qFull = AXP_InsertCountedQueue(
								(AXP_QUEUE_HDR *) &cpu->iq,
								(AXP_CQUE_ENTRY *) xqEntry);
						pthread_cond_broadcast(&cpu->eBoxCondition);
						pthread_mutex_unlock(&cpu->eBoxMutex);
					}
					else	/* FQ */
					{
						xqEntry = AXP_GetNextFQEntry(cpu);
						xqEntry->ins = decodedInstr;
						pthread_mutex_lock(&cpu->fBoxMutex);
						qFull = AXP_InsertCountedQueue(
								(AXP_QUEUE_HDR *) &cpu->fq,
								(AXP_CQUE_ENTRY *) xqEntry);
						pthread_cond_broadcast(&cpu->fBoxCondition);
						pthread_mutex_unlock(&cpu->fBoxMutex);
					}

					/*
					 * TODO:	We need to make sure that there is at least four
					 *			entries available in the IQ/FQ, not just 1.
					 */
					if (qFull < 0)
						printf("\n>>>>> We need to determine if there are any instructions to parse <<<<<\n");
					decodedInstr->state = Queued;
				}
				else
					decodedInstr->state = WaitingRetirement;
				nextPC = AXP_21264_IncrementVPC(cpu);
			}
		}

		/*
		 * We failed to get the next instruction.  We need to request an Icache
		 * Fill, or we have an ITB_MISS
		 */
		else
		{
			AXP_21264_TLB *itb;

			itb = AXP_findTLBEntry(cpu, *((u64 *) &nextPC), false);

			/*
			 * If we didn't get an ITB, then we got to a virtual address that
			 * has not yet to be mapped.  We need to call the PALcode to get
			 * this mapping for us, at which time we'll attempt to fetch the
			 * instructions again, which will cause us to get here again, but
			 * this time the ITB will be found.
			 */
			if (itb == NULL)
			{
				AXP_21264_Ibox_Event(
							cpu,
							AXP_ITB_MISS,
							nextPC,
							*((u64 *) &nextPC),
							PAL00,
							AXP_UNMAPPED_REG,
							false,
							true);
			}

			/*
			 * We failed to get the next set of instructions from the Icache.
			 * We need to request the Cbox to get them and put them into the
			 * cache.  We are going to have some kind of pending Cbox indicator
			 * to know when the Cbox has actually filled in the cache block.
			 *
			 * TODO:	We may want to try and utilize the branch predictor to
			 * 			"look ahead" and request the Cbox fill in the Icache
			 * 			before we have a cache miss, in oder to avoid this
			 * 			waiting on the Cbox (at least for too long a period of
			 * 			time).
			 */
			else
			{
				u64	pa;
				AXP_EXCEPTIONS exception;

				/*
				 * First, try and convert the virtual address of the PC into
				 * its physical address equivalent.
				 */
				pa = AXP_va2pa(
						cpu,
						*((u64 *) &nextPC),
						nextPC,
						false,
						Execute,
						&_asm,
						&fault,
						&exception);

				/*
				 * If converting the VA to a PA generated an exception, then we
				 * need to handle this now.  Otherwise, put in a request to the
				 * Cbox to perform a Icache Fill.
				 */
				if (exception != NoException)
					AXP_21264_Ibox_Event(
								cpu,
								fault,
								nextPC,
								*((u64 *) &nextPC),
								PAL00,
								AXP_UNMAPPED_REG,
								false,
								true);
				else
					AXP_21264_Add_MAF(
							cpu,
							Istream,
							pa,
							0,
							AXP_ICACHE_BUF_LEN,
							false);
			}
		}

		/*
		 * Before we loop back to the top, we need to see if there is something
		 * to process or places to put what needs to be processed (IQ and/or FQ
		 * cannot handle another entry).
		 */
		if (((cpu->excPend == false) &&
			 (AXP_IcacheValid(cpu, nextPC) == false)) ||
			((AXP_CountedQueueFull(&cpu->iq) != 0) ||
			 (AXP_CountedQueueFull(&cpu->fq) != 0)))
			pthread_cond_wait(&cpu->iBoxCondition, &cpu->iBoxMutex);
	}

	/*
	 *
	 */
	if (wasRunning == true)
	{
		pthread_mutex_unlock(&cpu->iBoxMutex);
	}
	return(NULL);
}
