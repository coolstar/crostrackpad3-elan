#include "internal.h"
#include "device.h"
#include "hiddevice.h"
#include "spb.h"

static ULONG ElanPrintDebugLevel = 100;
static ULONG ElanPrintDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

//#include "device.tmh"

bool deviceLoaded = false;

/////////////////////////////////////////////////
//
// WDF callbacks.
//
/////////////////////////////////////////////////

NTSTATUS
OnPrepareHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesRaw,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	FuncEntry(TRACE_FLAG_WDFLOADING);

	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
					Trace(
						TRACE_LEVEL_INFORMATION,
						TRACE_FLAG_WDFLOADING,
						"SPB resource found with ID=0x%llx",
						pDevice->I2CContext.I2cResHubId.QuadPart);
				}
				else
				{
					Trace(
						TRACE_LEVEL_WARNING,
						TRACE_FLAG_WDFLOADING,
						"Duplicate SPB resource found with ID=0x%llx",
						pDevice->I2CContext.I2cResHubId.QuadPart);
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_WDFLOADING,
			"SPB resource not found - %!STATUS!",
			status);
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);
	if (!NT_SUCCESS(status))
	{
		Trace(
			TRACE_LEVEL_ERROR,
			TRACE_FLAG_WDFLOADING,
			"Error in Spb initialization - %!STATUS!",
			status);

		return status;
	}

	FuncExit(TRACE_FLAG_WDFLOADING);

	return status;
}

NTSTATUS
OnReleaseHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	FuncEntry(TRACE_FLAG_WDFLOADING);

	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	deviceLoaded = false;

	FuncExit(TRACE_FLAG_WDFLOADING);

	return status;
}

bool IsElanLoaded(){
	return deviceLoaded;
}

void elan_i2c_read_cmd(PDEVICE_CONTEXT pDevice, UINT16 reg, uint8_t *val) {
	SpbReadDataSynchronously16(&pDevice->I2CContext, reg, val, ETP_I2C_INF_LENGTH);
}

void elan_i2c_write_cmd(PDEVICE_CONTEXT pDevice, UINT16 reg, UINT16 cmd){
	uint16_t buffer[] = { cmd };
	SpbWriteDataSynchronously16(&pDevice->I2CContext, reg, (uint8_t *)buffer, sizeof(buffer));
}

NTSTATUS BOOTTRACKPAD(
	_In_  PDEVICE_CONTEXT  pDevice
	)
{
	if (deviceLoaded)
		return 0;

	NTSTATUS status = 0;

	FuncEntry(TRACE_FLAG_WDFLOADING);

	elan_i2c_write_cmd(pDevice, ETP_I2C_STAND_CMD, ETP_I2C_RESET);
	
	uint8_t val[256];
	SpbReadDataSynchronously(&pDevice->I2CContext, 0x00, &val, ETP_I2C_INF_LENGTH);

	SpbReadDataSynchronously16(&pDevice->I2CContext, ETP_I2C_DESC_CMD, &val, ETP_I2C_DESC_LENGTH);

	SpbReadDataSynchronously16(&pDevice->I2CContext, ETP_I2C_REPORT_DESC_CMD, &val, ETP_I2C_REPORT_DESC_LENGTH);

	elan_i2c_write_cmd(pDevice, ETP_I2C_SET_CMD, ETP_ENABLE_ABS);

	elan_i2c_write_cmd(pDevice, ETP_I2C_STAND_CMD, ETP_I2C_WAKE_UP);

	uint8_t val2[3];

	elan_i2c_read_cmd(pDevice, ETP_I2C_UNIQUEID_CMD, val2);
	uint8_t prodid = val2[0];

	elan_i2c_read_cmd(pDevice, ETP_I2C_FW_VERSION_CMD, val2);
	uint8_t version = val2[0];

	elan_i2c_read_cmd(pDevice, ETP_I2C_FW_CHECKSUM_CMD, val2);
	uint16_t csum = *((uint16_t *)val2);

	elan_i2c_read_cmd(pDevice, ETP_I2C_SM_VERSION_CMD, val2);
	uint8_t smvers = val2[0];

	elan_i2c_read_cmd(pDevice, ETP_I2C_IAP_VERSION_CMD, val2);
	uint8_t iapversion = val2[0];

	elan_i2c_read_cmd(pDevice, ETP_I2C_PRESSURE_CMD, val2);

	elan_i2c_read_cmd(pDevice, ETP_I2C_MAX_X_AXIS_CMD, val2);
	uint16_t max_x = (*((uint16_t *)val2)) & 0x0fff;

	elan_i2c_read_cmd(pDevice, ETP_I2C_MAX_Y_AXIS_CMD, val2);
	uint16_t max_y = (*((uint16_t *)val2)) & 0x0fff;

	elan_i2c_read_cmd(pDevice, ETP_I2C_XY_TRACENUM_CMD, val2);

	uint8_t x_traces = val2[0];
	uint8_t y_traces = val2[1];

	pDevice->max_y = max_y;

	csgesture_softc *sc = &pDevice->sc;
	sc->resx = max_x;
	sc->resy = max_y;
	sc->phyx = max_x / x_traces;
	sc->phyy = max_y / y_traces;

	ElanPrint(DEBUG_LEVEL_INFO, DBG_PNP, "[etp] ProdID: %d Vers: %d Csum: %d SmVers: %d IAPVers: %d Max X: %d Max Y: %d\n", prodid, version, csum, smvers, iapversion, max_x, max_y);

	elan_i2c_write_cmd(pDevice, ETP_I2C_SET_CMD, ETP_ENABLE_CALIBRATE | ETP_ENABLE_ABS);

	elan_i2c_write_cmd(pDevice, ETP_I2C_STAND_CMD, ETP_I2C_WAKE_UP);

	elan_i2c_write_cmd(pDevice, ETP_I2C_CALIBRATE_CMD, 1);

	SpbReadDataSynchronously16(&pDevice->I2CContext, ETP_I2C_CALIBRATE_CMD, &val2, 1);

	elan_i2c_write_cmd(pDevice, ETP_I2C_SET_CMD, ETP_ENABLE_ABS);

	deviceLoaded = true;

	FuncExit(TRACE_FLAG_WDFLOADING);
	return status;
}

NTSTATUS
OnD0Entry(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	FuncEntry(TRACE_FLAG_WDFLOADING);

	UNREFERENCED_PARAMETER(FxPreviousState);

	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	WdfTimerStart(pDevice->Timer, WDF_REL_TIMEOUT_IN_MS(10));

	pDevice->RegsSet = false;
	pDevice->ConnectInterrupt = true;

	FuncExit(TRACE_FLAG_WDFLOADING);

	return status;
}

NTSTATUS
OnD0Exit(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	FuncEntry(TRACE_FLAG_WDFLOADING);

	UNREFERENCED_PARAMETER(FxPreviousState);

	PDEVICE_CONTEXT pDevice = GetDeviceContext(FxDevice);

	WdfTimerStop(pDevice->Timer, TRUE);

	pDevice->ConnectInterrupt = false;

	FuncExit(TRACE_FLAG_WDFLOADING);

	return STATUS_SUCCESS;
}

VOID
OnTopLevelIoDefault(
_In_  WDFQUEUE    FxQueue,
_In_  WDFREQUEST  FxRequest
)
/*++

Routine Description:

Accepts all incoming requests and pends or forwards appropriately.

Arguments:

FxQueue -  Handle to the framework queue object that is associated with the
I/O request.
FxRequest - Handle to a framework request object.

Return Value:

None.

--*/
{
	FuncEntry(TRACE_FLAG_SPBAPI);

	UNREFERENCED_PARAMETER(FxQueue);

	WDFDEVICE device;
	PDEVICE_CONTEXT pDevice;
	WDF_REQUEST_PARAMETERS params;
	NTSTATUS status;

	device = WdfIoQueueGetDevice(FxQueue);
	pDevice = GetDeviceContext(device);

	WDF_REQUEST_PARAMETERS_INIT(&params);

	WdfRequestGetParameters(FxRequest, &params);

	status = WdfRequestForwardToIoQueue(FxRequest, pDevice->SpbQueue);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(
			DEBUG_LEVEL_ERROR,
			DBG_IOCTL,
			"Failed to forward WDFREQUEST %p to SPB queue %p - %!STATUS!",
			FxRequest,
			pDevice->SpbQueue,
			status);

		WdfRequestComplete(FxRequest, status);
	}

	FuncExit(TRACE_FLAG_SPBAPI);
}

VOID
OnIoDeviceControl(
_In_  WDFQUEUE    FxQueue,
_In_  WDFREQUEST  FxRequest,
_In_  size_t      OutputBufferLength,
_In_  size_t      InputBufferLength,
_In_  ULONG       IoControlCode
)
/*++
Routine Description:

This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
requests from the system.

Arguments:

FxQueue - Handle to the framework queue object that is associated
with the I/O request.
FxRequest - Handle to a framework request object.
OutputBufferLength - length of the request's output buffer,
if an output buffer is available.
InputBufferLength - length of the request's input buffer,
if an input buffer is available.
IoControlCode - the driver-defined or system-defined I/O control code
(IOCTL) that is associated with the request.

Return Value:

VOID

--*/
{
	FuncEntry(TRACE_FLAG_SPBAPI);

	WDFDEVICE device;
	PDEVICE_CONTEXT pDevice;
	BOOLEAN fSync = FALSE;
	NTSTATUS status = STATUS_SUCCESS;
	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(FxQueue);
	pDevice = GetDeviceContext(device);

	ElanPrint(
		DEBUG_LEVEL_INFO, DBG_IOCTL,
		"DeviceIoControl request %p received with IOCTL=%lu",
		FxRequest,
		IoControlCode);
	ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
		"%s, Queue:0x%p, Request:0x%p\n",
		DbgHidInternalIoctlString(IoControlCode),
		FxQueue,
		FxRequest
		);

	//
	// Translate the test IOCTL into the appropriate 
	// SPB API method.  Open and close are completed 
	// synchronously.
	//

	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		//
		// Retrieves the device's HID descriptor.
		//
		status = ElanGetHidDescriptor(device, FxRequest);
		fSync = TRUE;
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = ElanGetDeviceAttributes(FxRequest);
		fSync = TRUE;
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		//
		//Obtains the report descriptor for the HID device.
		//
		status = ElanGetReportDescriptor(device, FxRequest);
		fSync = TRUE;
		break;

	case IOCTL_HID_GET_STRING:
		//
		// Requests that the HID minidriver retrieve a human-readable string
		// for either the manufacturer ID, the product ID, or the serial number
		// from the string descriptor of the device. The minidriver must send
		// a Get String Descriptor request to the device, in order to retrieve
		// the string descriptor, then it must extract the string at the
		// appropriate index from the string descriptor and return it in the
		// output buffer indicated by the IRP. Before sending the Get String
		// Descriptor request, the minidriver must retrieve the appropriate
		// index for the manufacturer ID, the product ID or the serial number
		// from the device extension of a top level collection associated with
		// the device.
		//
		status = ElanGetString(FxRequest);
		fSync = TRUE;
		break;

	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_HID_SET_OUTPUT_REPORT:
		//
		//Transmits a class driver-supplied report to the device.
		//
		status = BOOTTRACKPAD(pDevice);
		if (!NT_SUCCESS(status)){
			ElanPrint(DBG_IOCTL, DEBUG_LEVEL_ERROR, "Error booting Elan device!\n");
		}
		fSync = TRUE;
		break;

	case IOCTL_HID_READ_REPORT:
	case IOCTL_HID_GET_INPUT_REPORT:
		//
		// Returns a report from the device into a class driver-supplied buffer.
		// 
		status = ElanReadReport(pDevice, FxRequest, &fSync);
		break;

	case IOCTL_HID_GET_FEATURE:
		//
		// returns a feature report associated with a top-level collection
		//
		status = ElanGetFeature(pDevice, FxRequest, &fSync);
		break;
	case IOCTL_HID_ACTIVATE_DEVICE:
		//
		// Makes the device ready for I/O operations.
		//
	case IOCTL_HID_DEACTIVATE_DEVICE:
		//
		// Causes the device to cease operations and terminate all outstanding
		// I/O requests.
		//
	default:
		fSync = TRUE;
		status = STATUS_NOT_SUPPORTED;
		ElanPrint(
			DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Request %p received with unexpected IOCTL=%lu",
			FxRequest,
			IoControlCode);
	}

	//
	// Complete the request if necessary.
	//

	if (fSync)
	{
		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s completed, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			FxQueue,
			FxRequest
			);

		WdfRequestComplete(FxRequest, status);
	}
	else {
		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s deferred, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			FxQueue,
			FxRequest
			);
	}

	FuncExit(TRACE_FLAG_SPBAPI);
}