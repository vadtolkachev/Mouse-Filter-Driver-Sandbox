#include "moufiltr.h"


#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, MouFilter_EvtDeviceAdd)
#pragma alloc_text (PAGE, MouFilter_EvtIoInternalDeviceControl)
#endif


_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    NTSTATUS status;

    DbgPrint("Mouse Filter Driver Sample - Driver Framework Edition.\n");
    DbgPrint("Built %s %s\n", __DATE__, __TIME__);
    
    WDF_DRIVER_CONFIG_INIT(&config, MouFilter_EvtDeviceAdd);

    status = WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE); // hDriver optional
    if(!NT_SUCCESS(status)) 
        DbgPrint("WdfDriverCreate failed with status 0x%x\n", status);

    return status; 
}



_Use_decl_annotations_
NTSTATUS MouFilter_EvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    NTSTATUS status;
    WDFDEVICE hDevice;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    
    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    DbgPrint("Enter FilterEvtDeviceAdd \n");

    WdfFdoInitSetFilter(DeviceInit);
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_MOUSE);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_EXTENSION);

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &hDevice);
    if(!NT_SUCCESS(status)) 
    {
        DbgPrint("WdfDeviceCreate failed with status code 0x%x\n", status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);

    ioQueueConfig.EvtIoInternalDeviceControl = MouFilter_EvtIoInternalDeviceControl;

    status = WdfIoQueueCreate(hDevice, &ioQueueConfig, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) 
    {
        DbgPrint("WdfIoQueueCreate failed 0x%x\n", status);
        return status;
    }

    return status;
}


_Use_decl_annotations_
VOID MouFilter_EvtIoInternalDeviceControl(WDFQUEUE Queue, WDFREQUEST Request, size_t OutputBufferLength, size_t InputBufferLength, ULONG IoControlCode)
/*
Routine Description:

    This routine is the dispatch routine for internal device control requests.
    There are two specific control codes that are of interest:

    IOCTL_INTERNAL_MOUSE_CONNECT:
        Store the old context and function pointer and replace it with our own.
        This makes life much simpler than intercepting IRPs sent by the RIT and
        modifying them on the way back up.

    IOCTL_INTERNAL_I8042_HOOK_MOUSE:
        Add in the necessary function pointers and context values so that we can
        alter how the ps/2 mouse is initialized.

    NOTE:  Handling IOCTL_INTERNAL_I8042_HOOK_MOUSE is *NOT* necessary if
           all you want to do is filter MOUSE_INPUT_DATAs.  You can remove
           the handling code and all related device extension fields and
           functions to conserve space.
*/
{

    PDEVICE_EXTENSION devExt;
    PCONNECT_DATA connectData;
    PINTERNAL_I8042_HOOK_MOUSE hookMouse;
    NTSTATUS status = STATUS_SUCCESS;
    WDFDEVICE hDevice;
    size_t length;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    PAGED_CODE();

    hDevice = WdfIoQueueGetDevice(Queue);
    devExt = FilterGetData(hDevice);

    switch(IoControlCode)
    {
        // Connect a mouse class device driver to the port driver.
    case IOCTL_INTERNAL_MOUSE_CONNECT:
        // Only allow one connection.
        if(devExt->UpperConnectData.ClassService != NULL)
        {
            status = STATUS_SHARING_VIOLATION;
            break;
        }

        // Copy the connection parameters to the device extension.
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(CONNECT_DATA), &connectData, &length);
        if(!NT_SUCCESS(status))
        {
            DbgPrint("WdfRequestRetrieveInputBuffer failed %x\n", status);
            break;
        }


        devExt->UpperConnectData = *connectData;

        // Hook into the report chain.  Everytime a mouse packet is reported to
        // the system, MouFilter_ServiceCallback will be called
        connectData->ClassDeviceObject = WdfDeviceWdmGetDeviceObject(hDevice);
        connectData->ClassService = MouFilter_ServiceCallback;

        break;


        // Disconnect a mouse class device driver from the port driver.
    case IOCTL_INTERNAL_MOUSE_DISCONNECT:
        // Clear the connection parameters in the device extension.
        //
        // devExt->UpperConnectData.ClassDeviceObject = NULL;
        // devExt->UpperConnectData.ClassService = NULL;

        status = STATUS_NOT_IMPLEMENTED;
        break;


        // Attach this driver to the initialization and byte processing of the 
        // i8042 (ie PS/2) mouse.  This is only necessary if you want to do PS/2
        // specific functions, otherwise hooking the CONNECT_DATA is sufficient
    case IOCTL_INTERNAL_I8042_HOOK_MOUSE:

        DbgPrint("hook mouse received!\n");

        // Get the input buffer from the request
        // (Parameters.DeviceIoControl.Type3InputBuffer)
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(INTERNAL_I8042_HOOK_MOUSE), &hookMouse, &length);
        if(!NT_SUCCESS(status))
        {
            DbgPrint("WdfRequestRetrieveInputBuffer failed %x\n", status);
            break;
        }

        // Set isr routine and context and record any values from above this driver
        devExt->UpperContext = hookMouse->Context;
        hookMouse->Context = (PVOID)devExt;

        if(hookMouse->IsrRoutine)
            devExt->UpperIsrHook = hookMouse->IsrRoutine;

        hookMouse->IsrRoutine = (PI8042_MOUSE_ISR)MouFilter_IsrHook;

        // Store all of the other functions we might need in the future
        devExt->IsrWritePort = hookMouse->IsrWritePort;
        devExt->CallContext = hookMouse->CallContext;
        devExt->QueueMousePacket = hookMouse->QueueMousePacket;

        status = STATUS_SUCCESS;
        break;


        // Might want to capture this in the future.  For now, then pass it down
        // the stack.  These queries must be successful for the RIT to communicate
        // with the mouse.
    case IOCTL_MOUSE_QUERY_ATTRIBUTES:
    default:
        break;
    }

    if(!NT_SUCCESS(status))
    {
        WdfRequestComplete(Request, status);
        return;
    }

    MouFilter_DispatchPassThrough(Request, WdfDeviceGetIoTarget(hDevice));
}


BOOLEAN MouFilter_IsrHook(
    PVOID DeviceExtension,
    PMOUSE_INPUT_DATA CurrentInput,
    POUTPUT_PACKET CurrentOutput,
    UCHAR StatusByte,
    PUCHAR DataByte,
    PBOOLEAN ContinueProcessing,
    PMOUSE_STATE MouseState,
    PMOUSE_RESET_SUBSTATE ResetSubState)
    /*
    Remarks:
        i8042prt specific code, if you are writing a packet only filter driver, you
        can remove this function

    Arguments:

        DeviceExtension - Our context passed during IOCTL_INTERNAL_I8042_HOOK_MOUSE

        CurrentInput - Current input packet being formulated by processing all the
                        interrupts

        CurrentOutput - Current list of bytes being written to the mouse or the
                        i8042 port.

        StatusByte    - Byte read from I/O port 60 when the interrupt occurred

        DataByte      - Byte read from I/O port 64 when the interrupt occurred.
                        This value can be modified and i8042prt will use this value
                        if ContinueProcessing is TRUE

        ContinueProcessing - If TRUE, i8042prt will proceed with normal processing of
                             the interrupt.  If FALSE, i8042prt will return from the
                             interrupt after this function returns.  Also, if FALSE,
                             it is this functions responsibilityt to report the input
                             packet via the function provided in the hook IOCTL or via
                             queueing a DPC within this driver and calling the
                             service callback function acquired from the connect IOCTL
    */
{
    PDEVICE_EXTENSION devExt;
    BOOLEAN retVal = TRUE;

    devExt = DeviceExtension;

    if(devExt->UpperIsrHook)
    {
        retVal = (*devExt->UpperIsrHook) (devExt->UpperContext, CurrentInput, CurrentOutput, StatusByte, DataByte,
            ContinueProcessing, MouseState, ResetSubState);

        if(!retVal || !(*ContinueProcessing))
            return retVal;
    }

    *ContinueProcessing = TRUE;
    return retVal;
}


VOID MouFilter_DispatchPassThrough(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target)
/*
    Passes a request on to the lower driver.
*/
{
    // Pass the IRP to the target
    WDF_REQUEST_SEND_OPTIONS options;
    BOOLEAN ret;
    NTSTATUS status = STATUS_SUCCESS;

    // We are not interested in post processing the IRP so 
    // fire and forget.
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    ret = WdfRequestSend(Request, Target, &options);

    if(ret == FALSE) 
    {
        status = WdfRequestGetStatus (Request);
        DbgPrint("WdfRequestSend failed: 0x%x\n", status);
        WdfRequestComplete(Request, status);
    }
}           


VOID MouFilter_ServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed)
/*
Routine Description:

    Called when there are mouse packets to report to the RIT.  You can do 
    anything you like to the packets.  For instance:
    
    o Drop a packet altogether
    o Mutate the contents of a packet 
    o Insert packets into the stream 
                    
Arguments:

    DeviceObject - Context passed during the connect IOCTL
    
    InputDataStart - First packet to be reported
    
    InputDataEnd - One past the last packet to be reported.  Total number of
                   packets is equal to InputDataEnd - InputDataStart
    
    InputDataConsumed - Set to the total number of packets consumed by the RIT
                        (via the function pointer we replaced in the connect
                        IOCTL)
*/
{
    PDEVICE_EXTENSION devExt;
    WDFDEVICE hDevice;

    hDevice = WdfWdmDeviceGetWdfDeviceHandle(DeviceObject);

    devExt = FilterGetData(hDevice);

    //UpperConnectData must be called at DISPATCH
    (*(PSERVICE_CALLBACK_ROUTINE) devExt->UpperConnectData.ClassService)(devExt->UpperConnectData.ClassDeviceObject,
        InputDataStart, InputDataEnd, InputDataConsumed);
} 
