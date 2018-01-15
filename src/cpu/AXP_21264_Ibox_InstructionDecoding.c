/*
 * Copyright (C) Jonathan D. Belanger 2018.
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
 *	This source file contains the functions needed to implement the instruction
 *	decoding functionality of the Ibox.
 *
 * Revision History:
 *
 *	V01.000		15-Jan-2017	Jonathan D. Belanger
 *	Initially written from functions originally defined in AXP_21264_Ibox.c.
 */
#include "AXP_Configure.h"
#include "AXP_21264_Ibox.h"

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
 * 		A value of the instruction being parsed.
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
 * 		A value of the instruction being parsed.
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
 * 		A value of the instruction being parsed.
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
 * 		A value of the instruction being parsed.
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
 * 		A value of the instruction being parsed.
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