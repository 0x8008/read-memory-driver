#include <ntifs.h>
#include <ntddk.h>

#define IOCTL_SIOCTL_METHOD_BUFFERED CTL_CODE(40000, 0x902, METHOD_BUFFERED, FILE_ANY_ACCESS)

static PEPROCESS process = nullptr;
int cs_pid;
UINT64 client_address;

enum {
   input_type_initialize,
   input_type_read
};

struct input_data {
   int type;
   union {
      struct {
         int process_id;
      }initialize;
      struct {
         UINT64 address;
      }read;
   };
};

extern "C" NTSTATUS NTAPI MmCopyVirtualMemory
(
   PEPROCESS SourceProcess,
   PVOID SourceAddress,
   PEPROCESS TargetProcess,
   PVOID TargetAddress,
   SIZE_T BufferSize,
   KPROCESSOR_MODE PreviousMode,
   PSIZE_T ReturnSize
);

extern "C" PPEB NTAPI PsGetProcessPeb(IN PEPROCESS Process);


void driver_unload(PDRIVER_OBJECT DriverObject) {
   UNICODE_STRING symlink_name;
   RtlInitUnicodeString(&symlink_name, L"\\DosDevices\\dupa");
   IoDeleteSymbolicLink(&symlink_name);
   IoDeleteDevice(DriverObject->DeviceObject);
   DbgPrint("unloaded\n");
   if (process) ObDereferenceObject(process);
}

NTSTATUS mf_create_close(PDEVICE_OBJECT, PIRP Irp) {
   Irp->IoStatus.Status = STATUS_SUCCESS;
   Irp->IoStatus.Information = 0;
   IoCompleteRequest(Irp, IO_NO_INCREMENT);
   return STATUS_SUCCESS;
}


NTSTATUS mf_device_control(PDEVICE_OBJECT, PIRP Irp) {
   SIZE_T bytes = 0;
   NTSTATUS status = STATUS_SUCCESS;
   PIO_STACK_LOCATION stack_location = IoGetCurrentIrpStackLocation(Irp);
   if (stack_location->Parameters.DeviceIoControl.IoControlCode == IOCTL_SIOCTL_METHOD_BUFFERED) {
      input_data* input = (input_data*)Irp->AssociatedIrp.SystemBuffer;
      if (input->type == input_type_initialize) {
         if (process) ObDereferenceObject(process); // jezeli jest to sie pozbywamy jak mnie sie powinni byli pozbyc +- 19 lat temu
         status = PsLookupProcessByProcessId((HANDLE)input->initialize.process_id, &process);
         if (NT_SUCCESS(status)) {
            *(PPEB*)Irp->AssociatedIrp.SystemBuffer = PsGetProcessPeb(process);
            bytes = sizeof(PPEB);
         }
      }
      if (input->type == input_type_read) {
         if (!process) status = STATUS_ACCESS_DENIED;
         else {
            status = MmCopyVirtualMemory(process, (PVOID)input->read.address, PsGetCurrentProcess(), Irp->AssociatedIrp.SystemBuffer,
               stack_location->Parameters.DeviceIoControl.OutputBufferLength, KernelMode, &bytes);
         }
      }
   }
   Irp->IoStatus.Status = status;
   Irp->IoStatus.Information = bytes;
   IoCompleteRequest(Irp, IO_NO_INCREMENT);
   return status;
}


extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING) {

   NTSTATUS status = STATUS_SUCCESS;    // NTSTATUS variable to record success or failure

   UNICODE_STRING device_name;
   RtlInitUnicodeString(&device_name, L"\\Device\\dupa"); // initialize device name (Shocker)

   PDEVICE_OBJECT device;
   status = IoCreateDevice(DriverObject, 0, &device_name, FILE_DEVICE_BEEP, 0, FALSE, &device);
   if (!NT_SUCCESS(status)) return status; //jak sie cos zjebie to bedziemy wiedziec co
   UNICODE_STRING symlink_name;
   RtlInitUnicodeString(&symlink_name, L"\\DosDevices\\dupa");
   status = IoCreateSymbolicLink(&symlink_name, &device_name);
   if (!NT_SUCCESS(status)) return status;

   DriverObject->MajorFunction[IRP_MJ_CREATE] = mf_create_close;
   DriverObject->MajorFunction[IRP_MJ_CLOSE] = mf_create_close;
   DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = mf_device_control;

   DriverObject->DriverUnload = driver_unload;

   DbgPrint("dupa\n");

   return status;
}