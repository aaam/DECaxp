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
 *	functionality of the Fbox Operate Instructions.
 *
 *	Revision History:
 *
 *	V01.000		24-June-2017	Jonathan D. Belanger
 *	Initially written.
 *
 *	V01.001		07-Jul-2017	Jonathan D. Belanger
 *	Renamed and left just the VAX instructions implemented within.
 */
#include "AXP_Configure.h"
#include "AXP_21264_Fbox_OperateVAX.h"

/*
 * AXP_ADDF
 *	This function implements the VAX F Format Floating-Point ADD instruction of
 *	the Alpha AXP processor.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the structure containing the information needed to emulate
 *		a single CPU.
 * 	instr:
 * 		A pointer to a structure containing the information needed to execute
 * 		this instruction.
 *
 * Output Parameters:
 * 	instr:
 * 		The contents of this structure are updated, as needed.
 *
 * Return Value:
 * 	An exception indicator.
 */
AXP_EXCEPTIONS AXP_ADDF(AXP_21264_CPU *cpu, AXP_INSTRUCTION *instr)
{
	AXP_EXCEPTIONS	retVal = NoException;
	AXP_FP_ENCODING encodingSrc1, encodingSrc2;
	AXP_FP_FUNC		*fpFunc = (AXP_FP_FUNC *) &instr->function;
	double			src1v, src2v, destv;
	int				oldRndMode =0;
	int				raised = 0;

	/*
	 * Before we go too far, let's check the contents of the source registers.
	 */
	encodingSrc1 = AXP_FP_ENCODE(&instr->src1v.fp.fpr, false);
	encodingSrc2 = AXP_FP_ENCODE(&instr->src2v.fp.fpr, false);

	/*
	 * If either encoding turned up to be a dirty-zero or a reserved operand,
	 * then we need to return an Invalid Operation.
	 */
	if (((encodingSrc1 == Reserved) || (encodingSrc1 == DirtyZero)) ||
		((encodingSrc2 == Reserved) || (encodingSrc2 == DirtyZero)))
	{
		retVal = IllegalOperand;
		raised = FE_INVALID;
	}
	else
	{

		/*
	 	 * Cast the register values into doubles (no conversion required).
	 	 */
		src1v = (double) instr->src1v.fp.uq;
		src2v = (double) instr->src2v.fp.uq;

		// TODO: We need to have a mutex starting at this point.

		/*
	 	 * Set the rounding mode, based on the function code and/or the FPCR.
	 	 */
		oldRndMode = AXP_FP_SetRoundingMode(cpu, fpFunc, oldRndMode);

		/*
	 	 * Clear the current set of exceptions.
	 	 */
		feclearexcept(FE_ALL_EXCEPT);

		/*
	 	 * Execute the instruction.
	 	 */
		destv = src1v + src2v;

		/*
	 	 * Test to see what exceptions were raised.
	 	 */
		raised = fetestexcept(FE_ALL_EXCEPT);

		/*
	 	 * Reset the rounding mode
	 	 */
		oldRndMode = AXP_FP_SetRoundingMode(NULL, NULL, oldRndMode);

		// TODO: We need to have a mutex ending at this point.

		if (raised == 0)
		{

			/*
			 * Recast the result into the destination register.  Since this is
			 * a 32-bit value, don't forget to clear the bits that are supposed
			 * to be zero.
			 */
			instr->destv.fp.uq = (u64) destv;
			instr->destv.fp.fpr32.zero = 0;

			/*
			 * Before we can simply return back to the caller, we need to
			 * determine if an overflow condition may have occurred.
			 */
			if ((instr->destv.fp.fpr.exponent - AXP_T_BIAS) > AXP_F_BIAS)
				raised = FE_OVERFLOW;
			else
				switch (AXP_FP_ENCODE(&instr->destv.fp.fpr, false))
				{

					/*
					 * These 2 cases are the same as Denormal for IEEE.
					 * Basically, these are values that cannot be represented
					 * in VAX Float.
					 */
					case DirtyZero:
					case Reserved:
						raised = FE_UNDERFLOW;
						break;

					/*
					 * These are just fine.  Nothing more to do here.
					 */
					case Finite:
					case Zero:
						break;

					/*
					 * These are not returned when IEEE is set to false above.
					 * So, there's nothing we can do here.  This is done to
					 * keep the compiler happy (it does not like switch
					 * statements with an enumeration and not all the values
					 * are present).
					 */
					case Denormal:
					case Infinity:
					case NotANumber:
						break;
				}
			if (raised != 0)
				retVal = ArithmeticTraps;
		}
	}

	/*
	 * Set the exception bits (I probably don't have to do this, but I will
	 * anyway, just so that unexpected results get returned for this
	 * instruction).
	 */
	raised &= (FE_OVERFLOW | FE_UNDERFLOW | FE_INVALID);
	AXP_FP_SetExcSum(instr, raised, false);

	/*
	 * Indicate that the instruction is ready to be retired.
	 */
	instr->state = WaitingRetirement;

	/*
	 * Return back to the caller with any exception that may have occurred.
	 */
	return(retVal);
}

/*
 * AXP_ADDG
 *	This function implements the VAX G Format Floating-Point ADD instruction of
 *	the Alpha AXP processor.
 *
 * Input Parameters:
 *	cpu:
 *		A pointer to the structure containing the information needed to emulate
 *		a single CPU.
 * 	instr:
 * 		A pointer to a structure containing the information needed to execute
 * 		this instruction.
 *
 * Output Parameters:
 * 	instr:
 * 		The contents of this structure are updated, as needed.
 *
 * Return Value:
 * 	An exception indicator.
 */
AXP_EXCEPTIONS AXP_ADDG(AXP_21264_CPU *cpu, AXP_INSTRUCTION *instr)
{
	AXP_EXCEPTIONS	retVal = NoException;
	AXP_FP_ENCODING encodingSrc1, encodingSrc2;
	AXP_FP_FUNC		*fpFunc = (AXP_FP_FUNC *) &instr->function;
	long double		src1v, src2v, destv;
	AXP_X_MEMORY	*xSrc1 = (AXP_X_MEMORY *) &src1v;
	AXP_X_MEMORY	*xSrc2 = (AXP_X_MEMORY *) &src2v;
	AXP_X_MEMORY	*xDest = (AXP_X_MEMORY *) &destv;
	int				oldRndMode =0;
	int				raised = 0;

	/*
	 * Before we go too far, let's check the contents of the source registers.
	 */
	encodingSrc1 = AXP_FP_ENCODE(&instr->src1v.fp.fpr, false);
	encodingSrc2 = AXP_FP_ENCODE(&instr->src2v.fp.fpr, false);

	/*
	 * If either encoding turned up to be a dirty-zero or a reserved operand,
	 * then we need to return an Invalid Operation.
	 */
	if (((encodingSrc1 == Reserved) || (encodingSrc1 == DirtyZero)) ||
		((encodingSrc2 == Reserved) || (encodingSrc2 == DirtyZero)))
	{
		retVal = IllegalOperand;
		raised = FE_INVALID;
	}
	else
	{

		/*
	 	 * Need to convert from 64-bit to 128-bit.  We need to do this because,
	 	 * the VAX G exponent can go up to 1024, but the IEEE T exponent can
	 	 * only go up to 1023.  We need to expand the exponent to a 15-bit
	 	 * representation from an 11-bit one, to accommodate this difference.
	 	 */
		xSrc1->sign = instr->src1v.fp.fpr.sign;
		xSrc1->exponent = AXP_FP_CVT_EXP_G2X(instr->src1v.fp.fpr);
		xSrc1->fraction = instr->src1v.fp.fpr.fraction;
		xSrc1->zero = 0;
		xSrc2->sign = instr->src2v.fp.fpr.sign;
		xSrc2->exponent = AXP_FP_CVT_EXP_G2X(instr->src2v.fp.fpr);
		xSrc2->fraction = instr->src2v.fp.fpr.fraction;
		xSrc2->zero = 0;

		// TODO: We need to have a mutex starting at this point.

		/*
	 	 * Set the rounding mode, based on the function code and/or the FPCR.
	 	 */
		oldRndMode = AXP_FP_SetRoundingMode(cpu, fpFunc, oldRndMode);

		/*
	 	 * Clear the current set of exceptions.
	 	 */
		feclearexcept(FE_ALL_EXCEPT);

		/*
	 	 * Execute the instruction.
	 	 */
		destv = src1v + src2v;

		/*
	 	 * Test to see what exceptions were raised.
	 	 */
		raised = fetestexcept(FE_ALL_EXCEPT);

		/*
	 	 * Reset the rounding mode
	 	 */
		oldRndMode = AXP_FP_SetRoundingMode(NULL, NULL, oldRndMode);

		// TODO: We need to have a mutex ending at this point.

		if (raised == 0)
		{

			/*
			 * Recast the result into the destination register.  Since this is
			 * a 32-bit value, don't forget to clear the bits that are supposed
			 * to be zero.
			 */
			instr->destv.fp.fpr.sign = xDest->sign;
			instr->destv.fp.fpr.exponent = AXP_FP_CVT_EXP_X2G(*xDest);
			instr->destv.fp.fpr.fraction = xDest->fraction;

			/*
			 * Before we can simply return back to the caller, we need to
			 * determine if an overflow condition may have occurred.
			 */
			if ((xDest->exponent - AXP_X_BIAS) > AXP_S_BIAS)
				raised = FE_OVERFLOW;
			else
				switch (AXP_FP_ENCODE(&instr->destv.fp.fpr, false))
				{

					/*
					 * These 2 cases are the same as Denormal for IEEE.
					 * Basically, these are values that cannot be represented
					 * in VAX Float.
					 */
					case DirtyZero:
					case Reserved:
						raised = FE_UNDERFLOW;
						break;

					/*
					 * These are just fine.  Nothing more to do here.
					 */
					case Finite:
					case Zero:
						break;

					/*
					 * These are not returned when IEEE is set to false above.
					 * So, there's nothing we can do here.  This is done to
					 * keep the compiler happy (it does not like switch
					 * statements with an enumeration and not all the values
					 * are present).
					 */
					case Denormal:
					case Infinity:
					case NotANumber:
						break;
				}
			if (raised != 0)
				retVal = ArithmeticTraps;
		}
	}

	/*
	 * Set the exception bits (I probably don't have to do this, but I will
	 * anyway, just so that unexpected results get returned for this
	 * instruction).
	 */
	raised &= (FE_OVERFLOW | FE_UNDERFLOW | FE_INVALID);
	AXP_FP_SetExcSum(instr, raised, false);

	/*
	 * Indicate that the instruction is ready to be retired.
	 */
	instr->state = WaitingRetirement;

	/*
	 * Return back to the caller with any exception that may have occurred.
	 */
	return(retVal);
}