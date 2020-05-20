#pragma once
#pragma warning(push, 3)
#include <ntddk.h>
#include <kbdmou.h>
#include <ntddmou.h>
#include <ntdd8042.h>
#include <wdf.h>
#pragma warning(pop)


typedef struct _DEVICE_EXTENSION
{
 
     //
    // Previous hook routine and context
    //                               
    PVOID UpperContext;
     
    PI8042_MOUSE_ISR UpperIsrHook;

    //
    // Write to the mouse in the context of MouFilter_IsrHook
    //
    IN PI8042_ISR_WRITE_PORT IsrWritePort;

    //
    // Context for IsrWritePort, QueueMousePacket
    //
    IN PVOID CallContext;

    //
    // Queue the current packet (ie the one passed into MouFilter_IsrHook)
    // to be reported to the class driver
    //
    IN PI8042_QUEUE_PACKET QueueMousePacket;

    //
    // The real connect data that this driver reports to
    //
    CONNECT_DATA UpperConnectData;

  
} DEVICE_EXTENSION, *PDEVICE_EXTENSION;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_EXTENSION, FilterGetData)
 
//
// Prototypes
//
DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD MouFilter_EvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_INTERNAL_DEVICE_CONTROL MouFilter_EvtIoInternalDeviceControl;
 


VOID MouFilter_DispatchPassThrough(_In_ WDFREQUEST Request, _In_ WDFIOTARGET Target);

BOOLEAN MouFilter_IsrHook(
    PVOID DeviceExtension,
    PMOUSE_INPUT_DATA CurrentInput, 
    POUTPUT_PACKET CurrentOutput,
    UCHAR StatusByte,
    PUCHAR DataByte,
    PBOOLEAN ContinueProcessing,
    PMOUSE_STATE MouseState,
    PMOUSE_RESET_SUBSTATE ResetSubState);

VOID MouFilter_ServiceCallback(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PMOUSE_INPUT_DATA InputDataStart,
    _In_ PMOUSE_INPUT_DATA InputDataEnd,
    _Inout_ PULONG InputDataConsumed);

