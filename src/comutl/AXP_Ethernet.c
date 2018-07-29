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
 *  This source file contains the code to allow the emulator to use one or more
 *  ethernet devices and send and receive packets over then for specific MAC
 *  addresses.
 *
 * Revision History:
 *
 *  V01.000	Jul 28, 2018	Jonathan D. Belanger
 *  Initially written.
 */
#include "AXP_Utility.h"
#include "AXP_Configure.h"
#include "AXP_Trace.h"
#include "AXP_Ethernet.h"
#include "AXP_Blocks.h"

/*
 * AXP_EthernetOpen
 *  This function is called to open an ethernet device for sending and
 *  receiving packets over the device.
 *
 * Input Parameters:
 *  name:
 *	A pointer to the string containing the name of the device to open.
 *
 * Output Parameters:
 *  None.
 *
 * Return Value:
 *  NULL:	Failed to open the device indicated.
 *  ~NULL:	A pointer to the Ethernet Handle through which packets are
 *		send and received.
 */
AXP_Ethernet_Handle *AXP_EthernetOpen(char *name)
{
    AXP_Ethernet_Handle *retVal = NULL;

    retVal = AXP_Allocate_Block(AXP_ETHERNET_BLK);
    if (retVal != NULL)
    {
	retVal->handle = pcap_open(
				name,
				SIXTYFOUR_K,
				(PCAP_OPENFLAG_PROMISCUOUS +
				    PCAP_OPENFLAG_NOCAPTURE_LOCAL),
				AXP_ETH_READ_TIMEOUT,
				NULL,
				retVal->errorBuf);
	if (retVal->handle == NULL)
	{
	    AXP_Deallocate_Block(retVal);
	    retVal = NULL;
	}
    }

    /*
     * Return the results of this call back to the caller.
     */
    return(retVal);
}

/*
 * AXP_EthernetClose
 *  This function is called to close an ethernet device that is no longer
 *  needed.
 *
 * Input Parameters:
 *  handle:
 *	A pointer to the handle created in the AXP_EthernetOpen call.
 *
 * Output Parameters:
 *  None.
 *
 * Return Value:
 *  None.
 */
void AXP_EthernetClose(AXP_Ethernet_Handle *handle)
{
    if (handle->handle != NULL)
    {
	pcap_close(handle->handle);
	handle->handle = NULL;
    }
    AXP_Deallocate_Block(handle);

    /*
     * Return back to the caller.
     */
    return;
}
