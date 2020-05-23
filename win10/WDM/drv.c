#pragma warning(push, 3)
#include <ntddk.h>
#pragma warning(pop)


typedef struct _DEVICE_EXTENTION
{
    PDEVICE_OBJECT LowerMouDevice;
}
DEVICE_EXTENTION, *PDEVICE_EXTENTION;


typedef struct _MOUSE_INPUT_DATA
{
    USHORT UnitId;
    USHORT Flags;
    union
    {
        ULONG Buttons;
        struct
        {
            USHORT ButtonFlags;
            USHORT ButtonData;
        };
    };
    ULONG  RawButtons;
    LONG   LastX;
    LONG   LastY;
    ULONG  ExtraInformation;
}
MOUSE_INPUT_DATA, *PMOUSE_INPUT_DATA;


#define MOUSE_MOVE_RELATIVE 0x00
#define MOUSE_MOVE_ABSOLUTE 0x01
#define MOUSE_VIRTUAL_DESKTOP 0x02
#define MOUSE_ATTRIBUTES_CHANGED 0x04
#define MOUSE_MOVE_NOCOALESCE 0x08

#define MOUSE_LEFT_BUTTON_DOWN 0x01
#define MOUSE_LEFT_BUTTON_UP 0x02
#define MOUSE_RIGHT_BUTTON_DOWN 0x04
#define MOUSE_RIGHT_BUTTON_UP 0x08
#define MOUSE_MIDDLE_BUTTON_DOWN 0x10
#define MOUSE_MIDDLE_BUTTON_UP 0x20

extern POBJECT_TYPE *IoDriverObjectType;

ULONG pendingKey = 0;


NTSTATUS ObReferenceObjectByName(
    _In_ PUNICODE_STRING ObjectName,
    _In_ ULONG Attributes,
    _In_ PACCESS_STATE AccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ PVOID ParseContext,
    _Out_ PVOID *Object);


DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD Unload;
DRIVER_DISPATCH DispatchPass;
DRIVER_DISPATCH DispatchRead;
IO_COMPLETION_ROUTINE ReadComplete;


NTSTATUS MyAttachDevice(PDRIVER_OBJECT DriverObject);


#define COMBINATION_SIZE 4
USHORT combination_buffer[COMBINATION_SIZE] = { 0 };
const USHORT right_combination[COMBINATION_SIZE] = { MOUSE_LEFT_BUTTON_DOWN, MOUSE_RIGHT_BUTTON_DOWN, MOUSE_RIGHT_BUTTON_UP, MOUSE_LEFT_BUTTON_UP };
unsigned position = 0;
BOOLEAN shouldInvert = FALSE;


_Use_decl_annotations_
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DbgPrint("DriverEntry\n");

    DriverObject->DriverUnload = Unload;

    for(int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        DriverObject->MajorFunction[i] = DispatchPass;
    }

    DriverObject->MajorFunction[IRP_MJ_READ] = DispatchRead;

    NTSTATUS status = MyAttachDevice(DriverObject);
    if(!NT_SUCCESS(status))
    {
        DbgPrint("MyAttachDevice failure\n");
    }
    else
    {
        DbgPrint("MyAttachDevice success\n");
    }

    return status;
}


_Use_decl_annotations_
VOID Unload(PDRIVER_OBJECT DriverObject)
{
    DbgPrint("Unload\n");

    LARGE_INTEGER interval = { 0 };

    PDEVICE_OBJECT DeviceObject = DriverObject->DeviceObject;
    interval.QuadPart = -10 * 1000 * 1000;

    while(DeviceObject)
    {
        IoDetachDevice(((PDEVICE_EXTENTION)DeviceObject->DeviceExtension)->LowerMouDevice);
        DeviceObject = DeviceObject->NextDevice;
    }

    while(pendingKey)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
    }

    DeviceObject = DriverObject->DeviceObject;
    while(DeviceObject)
    {
        IoDeleteDevice(DeviceObject);
        DeviceObject = DeviceObject->NextDevice;
    }
}


_Use_decl_annotations_
NTSTATUS DispatchPass(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    return IoCallDriver(((PDEVICE_EXTENTION)DeviceObject->DeviceExtension)->LowerMouDevice, Irp);
}


_Use_decl_annotations_
NTSTATUS DispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);

    IoSetCompletionRoutine(Irp, ReadComplete, NULL, TRUE, TRUE, TRUE);

    pendingKey++;

    return IoCallDriver(((PDEVICE_EXTENTION)DeviceObject->DeviceExtension)->LowerMouDevice, Irp);
}


_Use_decl_annotations_
NTSTATUS ReadComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    PMOUSE_INPUT_DATA Keys = (PMOUSE_INPUT_DATA)Irp->AssociatedIrp.SystemBuffer;
    USHORT flags = Keys->ButtonFlags;


    if(Irp->IoStatus.Status == STATUS_SUCCESS)
    {
        NT_ASSERT(position <= 3);

        if(flags == MOUSE_LEFT_BUTTON_DOWN || flags == MOUSE_LEFT_BUTTON_UP || flags == MOUSE_RIGHT_BUTTON_DOWN || flags == MOUSE_RIGHT_BUTTON_UP)
        {
            combination_buffer[position] = Keys->ButtonFlags;
            if(combination_buffer[position] == right_combination[position])
            {
                position++;
                if(position == COMBINATION_SIZE)
                {
                    DbgPrint("invertion\n");
                    shouldInvert = !shouldInvert;
                    position = 0;
                }
            }
            else
                position = 0;
        }


        if(shouldInvert && (Keys->Flags & MOUSE_MOVE_ABSOLUTE))
        {
            Keys->LastY = 65535 - Keys->LastY;
        }

        /*for(int i = 0; i < COMBINATION_SIZE; i++)
            DbgPrint("combination_buffer[%d]; ", combination_buffer[i]);
        DbgPrint("\n");*/
    }

    if(Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }

    pendingKey--;

    return Irp->IoStatus.Status;
}


NTSTATUS MyAttachDevice(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING MCName = RTL_CONSTANT_STRING(L"\\Driver\\mouclass");

    PDRIVER_OBJECT targetDriverObject = NULL;
    PDEVICE_OBJECT currentDeviceObject = NULL;
    PDEVICE_OBJECT myDeviceObject = NULL;
    NTSTATUS status = ObReferenceObjectByName(&MCName, OBJ_CASE_INSENSITIVE, NULL, 0, *IoDriverObjectType, KernelMode, NULL, (PVOID*)&targetDriverObject);
    if(!NT_SUCCESS(status))
    {
        DbgPrint("ObReferenceObjectByName failure\n");
        return status;
    }


    ObDereferenceObject(targetDriverObject);

    currentDeviceObject = targetDriverObject->DeviceObject;

    while(currentDeviceObject)
    {
        status = IoCreateDevice(DriverObject, sizeof(DEVICE_EXTENTION), NULL, FILE_DEVICE_MOUSE, 0, FALSE, &myDeviceObject);
        if(!NT_SUCCESS(status))
        {
            //TODO: proper error handling, deleteing multiple devices
            return status;
        }

        RtlZeroMemory(myDeviceObject->DeviceExtension, sizeof(DEVICE_EXTENTION));

        status = IoAttachDeviceToDeviceStackSafe(myDeviceObject, currentDeviceObject, &((PDEVICE_EXTENTION)myDeviceObject->DeviceExtension)->LowerMouDevice);
        if(!NT_SUCCESS(status))
        {
            //TODO: proper error handling, deleteing multiple devices
            return status;
        }

        myDeviceObject->Flags |= DO_BUFFERED_IO;
        myDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

        currentDeviceObject = currentDeviceObject->NextDevice;
    }


    return STATUS_SUCCESS;
}