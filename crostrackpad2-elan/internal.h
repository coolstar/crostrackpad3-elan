#ifndef _INTERNAL_H_
#define _INTERNAL_H_

#pragma warning(push)
#pragma warning(disable:4512)
#pragma warning(disable:4480)

#define SPBT_POOL_TAG ((ULONG) 'TBPS')

/////////////////////////////////////////////////
//
// Common includes.
//
/////////////////////////////////////////////////

#include <ntddk.h>
#include <wdm.h>
#include <wdf.h>
#include <ntstrsafe.h>

#include "spb.h"

#define RESHUB_USE_HELPER_ROUTINES
#include "reshub.h"

#include "trace.h"

#include "elantp.h"
#include "gesturerec.h"

//
// Forward Declarations
//

typedef struct _DEVICE_CONTEXT  DEVICE_CONTEXT,  *PDEVICE_CONTEXT;
typedef struct _REQUEST_CONTEXT  REQUEST_CONTEXT,  *PREQUEST_CONTEXT;

struct _DEVICE_CONTEXT 
{
    //
    // Handle back to the WDFDEVICE
    //

    WDFDEVICE FxDevice;

    //
    // Handle to the sequential SPB queue
    //

    WDFQUEUE SpbQueue;

    //
    // Connection ID for SPB peripheral
    //

	SPB_CONTEXT I2CContext;
    
    //
    // Interrupt object and wait event
    //

    WDFINTERRUPT Interrupt;

    KEVENT IsrWaitEvent;

    //
    // Setting indicating whether the interrupt should be connected
    //

    BOOLEAN ConnectInterrupt;

	BOOLEAN IsHandlingInterrupts;

	BOOLEAN ProcessedRegs;

	BOOLEAN RegsSet;

    //
    // Client request object
    //

    WDFREQUEST ClientRequest;

    //
    // WaitOnInterrupt request object
    //

    WDFREQUEST WaitOnInterruptRequest;

	WDFTIMER Timer;

	WDFQUEUE ReportQueue;

	BYTE DeviceMode;

	ULONGLONG LastInterruptTime;

	csgesture_softc sc;

	uint16_t max_y;

	uint8_t hw_res_x, hw_res_y;

	uint8_t lastreport[ETP_MAX_REPORT_LEN];
};

struct _REQUEST_CONTEXT
{    
    //
    // Associated framework device object
    //

    WDFDEVICE FxDevice;

    //
    // Variables to track write length for a sequence request.
    // There are needed to complete the client request with
    // correct bytesReturned value.
    //

    BOOLEAN IsSpbSequenceRequest;
    ULONG_PTR SequenceWriteLength;
};

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext);
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, GetRequestContext);

#pragma warning(pop)

#endif // _INTERNAL_H_
