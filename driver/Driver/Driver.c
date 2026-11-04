#include <ndis.h>
#include <ntstrsafe.h>
#include "../SDK/NetworkProtocol.h"
#include "../Common/DriverAuthorization.h"

#define TAG 'nVtT'
#define FILTERS (NDIS_PACKET_TYPE_DIRECTED|NDIS_PACKET_TYPE_MULTICAST|NDIS_PACKET_TYPE_ALL_MULTICAST|NDIS_PACKET_TYPE_BROADCAST|NDIS_PACKET_TYPE_PROMISCUOUS)
typedef struct ADAPTER {
    NDIS_HANDLE handle,pool,control_handle;
    PDEVICE_OBJECT control_device;
    NDIS_SPIN_LOCK lock;
    EX_RUNDOWN_REF lifetime,datapath;
    KMUTEX command;
    BOOLEAN running,connected;
    ULONG filter,head,count;
    PFILE_OBJECT owner;
    UCHAR mac[6];
    UCHAR multicast[32][6];ULONG multicast_count;
    NDIS_STATISTICS_INFO statistics;
    TITAN_AUTHORIZATION_STATE* authorization;
    VN_FRAME ring[VN_RING_CAPACITY];
    ULONGLONG tx,rx,dropped;
} ADAPTER;
typedef struct FILE_CONTEXT { PEPROCESS process; TITAN_AUTHORIZATION_STATE authorization; } FILE_CONTEXT;
static NDIS_HANDLE DriverHandle;
static EX_PUSH_LOCK GlobalLock;
static KMUTEX LifecycleMutex;
static __declspec(align(8)) volatile LONG64 NextDeviceId;
static ADAPTER* Current;
static const UCHAR Mac[6]={0x02,0x54,0x49,0x54,0x41,0x01};
static VOID CountFrame(ADAPTER* a,const UCHAR* frame,ULONG length,BOOLEAN received) {
    static const UCHAR broadcast[6]={255,255,255,255,255,255};
    NDIS_STATISTICS_INFO* s=&a->statistics;
    BOOLEAN b=RtlCompareMemory(frame,broadcast,6)==6,m=(frame[0]&1)!=0;
    if(received){s->ifHCInOctets+=length;if(b){++s->ifHCInBroadcastPkts;s->ifHCInBroadcastOctets+=length;}else if(m){++s->ifHCInMulticastPkts;s->ifHCInMulticastOctets+=length;}else{++s->ifHCInUcastPkts;s->ifHCInUcastOctets+=length;}}
    else{s->ifHCOutOctets+=length;if(b){++s->ifHCOutBroadcastPkts;s->ifHCOutBroadcastOctets+=length;}else if(m){++s->ifHCOutMulticastPkts;s->ifHCOutMulticastOctets+=length;}else{++s->ifHCOutUcastPkts;s->ifHCOutUcastOctets+=length;}}
}

DRIVER_INITIALIZE DriverEntry;
MINIPORT_INITIALIZE Initialize;
MINIPORT_HALT Halt;
MINIPORT_PAUSE Pause;
MINIPORT_RESTART Restart;
MINIPORT_OID_REQUEST Oid;
MINIPORT_SEND_NET_BUFFER_LISTS Send;
MINIPORT_RETURN_NET_BUFFER_LISTS Return;
MINIPORT_UNLOAD Unload;
MINIPORT_CANCEL_SEND CancelSend;
MINIPORT_CANCEL_OID_REQUEST CancelOid;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY Pnp;
MINIPORT_SHUTDOWN Shutdown;
DRIVER_DISPATCH Dispatch;

static NDIS_STATUS RegisterControl(ADAPTER* a) {
    NDIS_DEVICE_OBJECT_ATTRIBUTES attributes;PDRIVER_DISPATCH dispatch[IRP_MJ_MAXIMUM_FUNCTION+1];NDIS_STATUS status;
    WCHAR name[96];UNICODE_STRING device,link=RTL_CONSTANT_STRING(L"\\DosDevices\\Global\\TitanVirtualNetwork"),sddl=RTL_CONSTANT_STRING(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    /* Old open handles can keep a deregistered CDO delete-pending. Give every
       adapter generation its own kernel name while retaining one public link. */
    status=RtlStringCchPrintfW(name,RTL_NUMBER_OF(name),L"\\Device\\TitanVirtualNetwork-%I64u",(ULONGLONG)InterlockedIncrement64(&NextDeviceId));
    if(!NT_SUCCESS(status))return status;RtlInitUnicodeString(&device,name);
    RtlZeroMemory(dispatch,sizeof(dispatch));dispatch[IRP_MJ_CREATE]=Dispatch;dispatch[IRP_MJ_CLEANUP]=Dispatch;dispatch[IRP_MJ_CLOSE]=Dispatch;dispatch[IRP_MJ_DEVICE_CONTROL]=Dispatch;
    RtlZeroMemory(&attributes,sizeof(attributes));attributes.Header.Type=NDIS_OBJECT_TYPE_DEVICE_OBJECT_ATTRIBUTES;attributes.Header.Revision=NDIS_DEVICE_OBJECT_ATTRIBUTES_REVISION_1;attributes.Header.Size=sizeof(attributes);
    attributes.DeviceName=&device;attributes.SymbolicName=&link;attributes.MajorFunctions=dispatch;attributes.DefaultSDDLString=&sddl;
    status=NdisRegisterDeviceEx(DriverHandle,&attributes,&a->control_device,&a->control_handle);
    if(status==NDIS_STATUS_SUCCESS)a->control_device->Characteristics|=FILE_DEVICE_SECURE_OPEN;
    return status;
}
static ADAPTER* ReferenceAdapter(PDEVICE_OBJECT device) {
    ADAPTER* a;
    KeEnterCriticalRegion();ExAcquirePushLockShared(&GlobalLock);
    a=Current;
    if(a&&(a->control_device!=device||!ExAcquireRundownProtection(&a->lifetime)))a=NULL;
    ExReleasePushLockShared(&GlobalLock);
    /* Keep normal APCs disabled for the full referenced access, including a
       possible command-mutex wait. A suspended caller must not strand a
       rundown reference and indefinitely block device removal. */
    if(!a)KeLeaveCriticalRegion();return a;
}
static VOID Link(ADAPTER* a,BOOLEAN connected) {
    NDIS_LINK_STATE link;NDIS_STATUS_INDICATION indication;
    NdisZeroMemory(&link,sizeof(link));link.Header.Type=NDIS_OBJECT_TYPE_DEFAULT;
    link.Header.Revision=NDIS_LINK_STATE_REVISION_1;link.Header.Size=NDIS_SIZEOF_LINK_STATE_REVISION_1;
    link.MediaConnectState=connected?MediaConnectStateConnected:MediaConnectStateDisconnected;
    link.MediaDuplexState=MediaDuplexStateFull;link.XmitLinkSpeed=link.RcvLinkSpeed=1000000000ull;
    NdisZeroMemory(&indication,sizeof(indication));indication.Header.Type=NDIS_OBJECT_TYPE_STATUS_INDICATION;
    indication.Header.Revision=NDIS_STATUS_INDICATION_REVISION_1;indication.Header.Size=NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    indication.SourceHandle=a->handle;indication.StatusCode=NDIS_STATUS_LINK_STATE;
    indication.StatusBuffer=&link;indication.StatusBufferSize=sizeof(link);NdisMIndicateStatusEx(a->handle,&indication);
}
static VOID Disconnect(ADAPTER* a,PFILE_OBJECT file) {
    BOOLEAN changed=FALSE;
    NdisAcquireSpinLock(&a->lock);
    if(a->owner==file){a->owner=NULL;a->authorization=NULL;changed=a->connected;a->connected=FALSE;a->head=a->count=0;}
    NdisReleaseSpinLock(&a->lock);if(changed)Link(a,FALSE);
}
static BOOLEAN AcceptFrame(ADAPTER* a,const UCHAR* frame) {
    ULONG filter=a->filter;static const UCHAR broadcast[6]={255,255,255,255,255,255};
    if(filter&NDIS_PACKET_TYPE_PROMISCUOUS)return TRUE;
    if(RtlCompareMemory(frame,broadcast,6)==6)return (filter&NDIS_PACKET_TYPE_BROADCAST)!=0;
    if(frame[0]&1){ULONG i;if(filter&NDIS_PACKET_TYPE_ALL_MULTICAST)return TRUE;if(!(filter&NDIS_PACKET_TYPE_MULTICAST))return FALSE;for(i=0;i<a->multicast_count;++i)if(RtlCompareMemory(frame,a->multicast[i],6)==6)return TRUE;return FALSE;}
    return (filter&NDIS_PACKET_TYPE_DIRECTED)&&RtlCompareMemory(frame,a->mac,6)==6;
}
static NTSTATUS Inject(ADAPTER* a,const VN_FRAME* frame) {
    NTSTATUS result=STATUS_DEVICE_NOT_READY;PUCHAR bytes;PMDL mdl;PNET_BUFFER_LIST nbl;
    if(!ExAcquireRundownProtection(&a->datapath))return result;
    NdisAcquireSpinLock(&a->lock);
    if(!a->running||!a->connected||!a->authorization||!TitanAuthorizationAllowed(a->authorization)){NdisReleaseSpinLock(&a->lock);goto done;}
    if(!AcceptFrame(a,frame->data)){NdisReleaseSpinLock(&a->lock);result=STATUS_SUCCESS;goto done;}
    NdisReleaseSpinLock(&a->lock);
    bytes=ExAllocatePool2(POOL_FLAG_NON_PAGED,frame->length,TAG);
    if(!bytes){result=STATUS_INSUFFICIENT_RESOURCES;goto done;}
    RtlCopyMemory(bytes,frame->data,frame->length);mdl=IoAllocateMdl(bytes,frame->length,FALSE,FALSE,NULL);
    if(!mdl){ExFreePoolWithTag(bytes,TAG);result=STATUS_INSUFFICIENT_RESOURCES;goto done;}
    MmBuildMdlForNonPagedPool(mdl);nbl=NdisAllocateNetBufferAndNetBufferList(a->pool,0,0,mdl,0,frame->length);
    if(!nbl){IoFreeMdl(mdl);ExFreePoolWithTag(bytes,TAG);result=STATUS_INSUFFICIENT_RESOURCES;goto done;}
    nbl->SourceHandle=a->handle;
    /* RESOURCES makes this indication synchronous: no buffers survive this call. */
    NdisMIndicateReceiveNetBufferLists(a->handle,nbl,NDIS_DEFAULT_PORT_NUMBER,1,NDIS_RECEIVE_FLAGS_RESOURCES);
    NdisFreeNetBufferList(nbl);IoFreeMdl(mdl);ExFreePoolWithTag(bytes,TAG);
    NdisAcquireSpinLock(&a->lock);++a->rx;CountFrame(a,frame->data,frame->length,TRUE);NdisReleaseSpinLock(&a->lock);result=STATUS_SUCCESS;
done:if(result==STATUS_INSUFFICIENT_RESOURCES){NdisAcquireSpinLock(&a->lock);++a->statistics.ifInDiscards;NdisReleaseSpinLock(&a->lock);}ExReleaseRundownProtection(&a->datapath);return result;
}
_Use_decl_annotations_ NTSTATUS Dispatch(PDEVICE_OBJECT device,PIRP irp) {
    PIO_STACK_LOCATION stack=IoGetCurrentIrpStackLocation(irp);NTSTATUS result=STATUS_INVALID_DEVICE_REQUEST;
    ADAPTER* a=NULL;ULONG_PTR bytes=0;FILE_CONTEXT* context;PVOID buffer=irp->AssociatedIrp.SystemBuffer;
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL){result=STATUS_INVALID_DEVICE_STATE;goto done;}
    if(!stack->FileObject){result=STATUS_INVALID_PARAMETER;goto done;}
    if(stack->MajorFunction==IRP_MJ_CREATE){
        if(stack->FileObject->FileName.Length){result=STATUS_OBJECT_NAME_INVALID;goto done;}
        context=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*context),TAG);
        if(!context){result=STATUS_INSUFFICIENT_RESOURCES;goto done;}
        context->process=PsGetCurrentProcess();ObReferenceObject(context->process);TitanAuthorizationInitialize(&context->authorization,TITAN_AUTHORIZATION_NETWORK);stack->FileObject->FsContext=context;result=STATUS_SUCCESS;goto done;
    }
    if(stack->MajorFunction==IRP_MJ_CLOSE){
        context=stack->FileObject->FsContext;stack->FileObject->FsContext=NULL;
        if(context){ObDereferenceObject(context->process);ExFreePoolWithTag(context,TAG);}result=STATUS_SUCCESS;goto done;
    }
    if(stack->MajorFunction!=IRP_MJ_CLEANUP&&stack->MajorFunction!=IRP_MJ_DEVICE_CONTROL)goto done;
    if(stack->MajorFunction==IRP_MJ_DEVICE_CONTROL&&!buffer&&(stack->Parameters.DeviceIoControl.InputBufferLength||stack->Parameters.DeviceIoControl.OutputBufferLength)){result=STATUS_INVALID_USER_BUFFER;goto done;}
    context=stack->FileObject->FsContext;
    if(stack->MajorFunction==IRP_MJ_DEVICE_CONTROL&&(!context||context->process!=PsGetCurrentProcess())){result=STATUS_ACCESS_DENIED;goto done;}
    a=ReferenceAdapter(device);if(!a){result=stack->MajorFunction==IRP_MJ_CLEANUP?STATUS_SUCCESS:STATUS_DEVICE_NOT_READY;goto done;}
    KeWaitForSingleObject(&a->command,Executive,KernelMode,FALSE,NULL);
    /* At the next passive operation, expire ownership and flush stale frames
       before any renewal can authorize a previous queue. The data path already
       refuses expired leases without waiting for this notification. */
    if(a->owner&&a->authorization&&!TitanAuthorizationAllowed(a->authorization))Disconnect(a,a->owner);
    if(stack->MajorFunction==IRP_MJ_CLEANUP){Disconnect(a,stack->FileObject);result=STATUS_SUCCESS;goto unlock;}
    switch(stack->Parameters.DeviceIoControl.IoControlCode){
    case VN_IOCTL_CHALLENGE:
        if(stack->Parameters.DeviceIoControl.InputBufferLength||stack->Parameters.DeviceIoControl.OutputBufferLength!=sizeof(TITAN_AUTHORIZATION_CHALLENGE)){result=STATUS_INFO_LENGTH_MISMATCH;break;}
        result=TitanAuthorizationChallenge(&context->authorization,buffer);if(NT_SUCCESS(result))bytes=sizeof(TITAN_AUTHORIZATION_CHALLENGE);break;
    case VN_IOCTL_AUTHORIZE:
        if(stack->Parameters.DeviceIoControl.InputBufferLength!=sizeof(TITAN_AUTHORIZATION_TOKEN)||stack->Parameters.DeviceIoControl.OutputBufferLength){result=STATUS_INFO_LENGTH_MISMATCH;break;}
        result=TitanAuthorizationAccept(&context->authorization,buffer);break;
    case VN_IOCTL_STATUS:
        if(stack->Parameters.DeviceIoControl.InputBufferLength||stack->Parameters.DeviceIoControl.OutputBufferLength!=sizeof(VN_STATE)){result=STATUS_INFO_LENGTH_MISMATCH;break;}
        NdisZeroMemory(buffer,sizeof(VN_STATE));
        {VN_STATE* state=buffer;state->version=VN_VERSION;state->size=sizeof(*state);state->capacity=VN_RING_CAPACITY;state->max_frame=VN_MAX_FRAME;
        NdisAcquireSpinLock(&a->lock);state->running=a->running;state->connected=a->connected;state->queued=a->count;
        state->transmitted=a->tx;state->received=a->rx;state->dropped=a->dropped;RtlCopyMemory(state->mac,a->mac,6);NdisReleaseSpinLock(&a->lock);}
        bytes=sizeof(VN_STATE);result=STATUS_SUCCESS;break;
    case VN_IOCTL_CONNECT:
    case VN_IOCTL_DISCONNECT:
        if(stack->Parameters.DeviceIoControl.InputBufferLength||stack->Parameters.DeviceIoControl.OutputBufferLength){result=STATUS_INFO_LENGTH_MISMATCH;break;}
        if(stack->Parameters.DeviceIoControl.IoControlCode==VN_IOCTL_DISCONNECT){
            if(a->owner&&a->owner!=stack->FileObject){result=STATUS_ACCESS_DENIED;break;}Disconnect(a,stack->FileObject);result=STATUS_SUCCESS;break;
        }
        NdisAcquireSpinLock(&a->lock);
        if(!a->running)result=STATUS_DEVICE_NOT_READY;
        else if(a->owner&&a->owner!=stack->FileObject)result=STATUS_SHARING_VIOLATION;
        else if(!TitanAuthorizationAllowed(&context->authorization))result=STATUS_ACCESS_DENIED;
        else{a->owner=stack->FileObject;a->authorization=&context->authorization;a->connected=TRUE;result=STATUS_SUCCESS;}
        NdisReleaseSpinLock(&a->lock);if(NT_SUCCESS(result))Link(a,TRUE);break;
    case VN_IOCTL_RECEIVE:
        if(stack->Parameters.DeviceIoControl.InputBufferLength||stack->Parameters.DeviceIoControl.OutputBufferLength!=sizeof(VN_FRAME)){result=STATUS_INFO_LENGTH_MISMATCH;break;}
        if(!TitanAuthorizationAllowed(&context->authorization)){result=STATUS_ACCESS_DENIED;break;}
        NdisAcquireSpinLock(&a->lock);
        if(a->owner!=stack->FileObject)result=STATUS_ACCESS_DENIED;
        else if(!a->count)result=STATUS_NO_MORE_ENTRIES;
        else{VN_FRAME* frame=buffer;NdisZeroMemory(frame,sizeof(*frame));frame->length=a->ring[a->head].length;RtlCopyMemory(frame->data,a->ring[a->head].data,frame->length);a->head=(a->head+1)%VN_RING_CAPACITY;--a->count;bytes=sizeof(*frame);result=STATUS_SUCCESS;}
        NdisReleaseSpinLock(&a->lock);break;
    case VN_IOCTL_SEND:
        if(stack->Parameters.DeviceIoControl.InputBufferLength!=sizeof(VN_FRAME)||stack->Parameters.DeviceIoControl.OutputBufferLength){result=STATUS_INFO_LENGTH_MISMATCH;break;}
        if(a->owner!=stack->FileObject){result=STATUS_ACCESS_DENIED;break;}
        if(!TitanAuthorizationAllowed(&context->authorization)){result=STATUS_ACCESS_DENIED;break;}
        if(((VN_FRAME*)buffer)->length<14||((VN_FRAME*)buffer)->length>VN_MAX_FRAME){result=STATUS_INVALID_BUFFER_SIZE;break;}
        result=Inject(a,buffer);break;
    default:break;
    }
unlock:KeReleaseMutex(&a->command,FALSE);ExReleaseRundownProtection(&a->lifetime);KeLeaveCriticalRegion();
done:irp->IoStatus.Status=result;irp->IoStatus.Information=NT_SUCCESS(result)?bytes:0;IoCompleteRequest(irp,IO_NO_INCREMENT);return result;
}
_Use_decl_annotations_ VOID Send(NDIS_HANDLE context,PNET_BUFFER_LIST list,NDIS_PORT_NUMBER port,ULONG flags) {
    ADAPTER* a=context;PNET_BUFFER_LIST nbl;BOOLEAN critical=KeGetCurrentIrql()==PASSIVE_LEVEL,acquired;
    if(critical)KeEnterCriticalRegion();acquired=ExAcquireRundownProtection(&a->datapath);
    UNREFERENCED_PARAMETER(port);
    for(nbl=list;nbl;nbl=NET_BUFFER_LIST_NEXT_NBL(nbl)){
        PNET_BUFFER nb;NDIS_STATUS status=NDIS_STATUS_SUCCESS;
        NdisAcquireSpinLock(&a->lock);
        if(!acquired||!a->running)status=NDIS_STATUS_PAUSED;
        else if(!a->connected||!a->authorization||!TitanAuthorizationAllowed(a->authorization))status=NDIS_STATUS_MEDIA_DISCONNECTED;
        else for(nb=NET_BUFFER_LIST_FIRST_NB(nbl);nb;nb=NET_BUFFER_NEXT_NB(nb)){
            ULONG length=NET_BUFFER_DATA_LENGTH(nb);VN_FRAME* frame;PUCHAR data;
            if(length<14||length>VN_MAX_FRAME){++a->dropped;++a->statistics.ifOutErrors;status=NDIS_STATUS_INVALID_LENGTH;continue;}
            if(a->count==VN_RING_CAPACITY){++a->dropped;++a->statistics.ifOutDiscards;status=NDIS_STATUS_RESOURCES;continue;}
            frame=&a->ring[(a->head+a->count)%VN_RING_CAPACITY];data=NdisGetDataBuffer(nb,length,frame->data,1,0);
            if(!data){++a->dropped;++a->statistics.ifOutDiscards;status=NDIS_STATUS_RESOURCES;continue;}
            if(data!=frame->data)RtlCopyMemory(frame->data,data,length);frame->length=length;++a->count;++a->tx;CountFrame(a,frame->data,length,FALSE);
        }
        NdisReleaseSpinLock(&a->lock);NET_BUFFER_LIST_STATUS(nbl)=status;
    }
    NdisMSendNetBufferListsComplete(a->handle,list,(flags&NDIS_SEND_FLAGS_DISPATCH_LEVEL)?NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL:0);
    if(acquired)ExReleaseRundownProtection(&a->datapath);
    if(critical)KeLeaveCriticalRegion();
}
_Use_decl_annotations_ VOID Return(NDIS_HANDLE c,PNET_BUFFER_LIST n,ULONG f){UNREFERENCED_PARAMETER(c);UNREFERENCED_PARAMETER(n);UNREFERENCED_PARAMETER(f);NT_ASSERT(FALSE);}
_Use_decl_annotations_ NDIS_STATUS Pause(NDIS_HANDLE c,PNDIS_MINIPORT_PAUSE_PARAMETERS p){
    ADAPTER* a=c;UNREFERENCED_PARAMETER(p);NdisAcquireSpinLock(&a->lock);a->running=FALSE;NdisReleaseSpinLock(&a->lock);
    ExWaitForRundownProtectionRelease(&a->datapath);return NDIS_STATUS_SUCCESS;
}
_Use_decl_annotations_ NDIS_STATUS Restart(NDIS_HANDLE c,PNDIS_MINIPORT_RESTART_PARAMETERS p){
    ADAPTER* a=c;UNREFERENCED_PARAMETER(p);ExReInitializeRundownProtection(&a->datapath);NdisAcquireSpinLock(&a->lock);a->running=TRUE;NdisReleaseSpinLock(&a->lock);return NDIS_STATUS_SUCCESS;
}
static const NDIS_OID Oids[]={OID_GEN_SUPPORTED_LIST,OID_GEN_HARDWARE_STATUS,OID_GEN_MEDIA_SUPPORTED,OID_GEN_MEDIA_IN_USE,OID_GEN_MAXIMUM_LOOKAHEAD,OID_GEN_MAXIMUM_FRAME_SIZE,OID_GEN_LINK_SPEED,OID_GEN_TRANSMIT_BUFFER_SPACE,OID_GEN_RECEIVE_BUFFER_SPACE,OID_GEN_TRANSMIT_BLOCK_SIZE,OID_GEN_RECEIVE_BLOCK_SIZE,OID_GEN_VENDOR_ID,OID_GEN_VENDOR_DESCRIPTION,OID_GEN_CURRENT_PACKET_FILTER,OID_GEN_CURRENT_LOOKAHEAD,OID_GEN_DRIVER_VERSION,OID_GEN_MAXIMUM_TOTAL_SIZE,OID_GEN_MAC_OPTIONS,OID_GEN_MEDIA_CONNECT_STATUS,OID_GEN_MAXIMUM_SEND_PACKETS,OID_GEN_VENDOR_DRIVER_VERSION,OID_802_3_PERMANENT_ADDRESS,OID_802_3_CURRENT_ADDRESS,OID_802_3_MULTICAST_LIST,OID_802_3_MAXIMUM_LIST_SIZE,OID_GEN_XMIT_OK,OID_GEN_RCV_OK,OID_GEN_XMIT_ERROR,OID_GEN_RCV_ERROR,OID_GEN_RCV_NO_BUFFER,OID_GEN_STATISTICS,OID_GEN_INTERRUPT_MODERATION,OID_GEN_LINK_PARAMETERS,OID_PNP_QUERY_POWER,OID_PNP_SET_POWER};
_Use_decl_annotations_ NDIS_STATUS Oid(NDIS_HANDLE c,PNDIS_OID_REQUEST request){
    ADAPTER* a=c;ULONG value=0,length=sizeof(value);ULONGLONG count=0;USHORT version=0x0630;PVOID data=&value;
    NDIS_OID oid;PVOID buffer;ULONG capacity;NDIS_STATISTICS_INFO stats;NDIS_INTERRUPT_MODERATION_PARAMETERS moderation;
    if(request->RequestType==NdisRequestSetInformation){
        oid=request->DATA.SET_INFORMATION.Oid;buffer=request->DATA.SET_INFORMATION.InformationBuffer;capacity=request->DATA.SET_INFORMATION.InformationBufferLength;
        request->DATA.SET_INFORMATION.BytesRead=0;request->DATA.SET_INFORMATION.BytesNeeded=0;
        if(oid==OID_GEN_INTERRUPT_MODERATION)return NDIS_STATUS_INVALID_DATA;
        if(oid==OID_GEN_LINK_PARAMETERS){if(capacity<NDIS_SIZEOF_LINK_PARAMETERS_REVISION_1){request->DATA.SET_INFORMATION.BytesNeeded=NDIS_SIZEOF_LINK_PARAMETERS_REVISION_1;return NDIS_STATUS_INVALID_LENGTH;}return NDIS_STATUS_NOT_SUPPORTED;}
        if(oid==OID_802_3_MULTICAST_LIST){if(capacity%6)return NDIS_STATUS_INVALID_LENGTH;if(capacity>32*6){request->DATA.SET_INFORMATION.BytesNeeded=32*6;return NDIS_STATUS_MULTICAST_FULL;}NdisAcquireSpinLock(&a->lock);a->multicast_count=capacity/6;if(capacity)RtlCopyMemory(a->multicast,buffer,capacity);NdisReleaseSpinLock(&a->lock);request->DATA.SET_INFORMATION.BytesRead=capacity;return NDIS_STATUS_SUCCESS;}
        if(oid!=OID_GEN_CURRENT_PACKET_FILTER&&oid!=OID_GEN_CURRENT_LOOKAHEAD&&oid!=OID_PNP_SET_POWER)return NDIS_STATUS_NOT_SUPPORTED;
        if(capacity!=4){request->DATA.SET_INFORMATION.BytesNeeded=4;return NDIS_STATUS_INVALID_LENGTH;}RtlCopyMemory(&value,buffer,4);
        if(oid==OID_GEN_CURRENT_PACKET_FILTER){if(value&~FILTERS)return NDIS_STATUS_NOT_SUPPORTED;NdisAcquireSpinLock(&a->lock);a->filter=value;NdisReleaseSpinLock(&a->lock);}
        if(oid==OID_GEN_CURRENT_LOOKAHEAD&&value>1500)return NDIS_STATUS_INVALID_DATA;
        request->DATA.SET_INFORMATION.BytesRead=4;return NDIS_STATUS_SUCCESS;
    }
    if(request->RequestType!=NdisRequestQueryInformation&&request->RequestType!=NdisRequestQueryStatistics)return NDIS_STATUS_NOT_SUPPORTED;
    oid=request->DATA.QUERY_INFORMATION.Oid;buffer=request->DATA.QUERY_INFORMATION.InformationBuffer;capacity=request->DATA.QUERY_INFORMATION.InformationBufferLength;
    request->DATA.QUERY_INFORMATION.BytesWritten=0;request->DATA.QUERY_INFORMATION.BytesNeeded=0;
    switch(oid){
    case OID_GEN_SUPPORTED_LIST:data=(PVOID)Oids;length=sizeof(Oids);break;
    case OID_GEN_HARDWARE_STATUS:value=NdisHardwareStatusReady;break;
    case OID_GEN_MEDIA_SUPPORTED:case OID_GEN_MEDIA_IN_USE:value=NdisMedium802_3;break;
    case OID_GEN_MAXIMUM_LOOKAHEAD:case OID_GEN_MAXIMUM_FRAME_SIZE:case OID_GEN_CURRENT_LOOKAHEAD:value=1500;break;
    case OID_GEN_MAXIMUM_TOTAL_SIZE:case OID_GEN_TRANSMIT_BLOCK_SIZE:case OID_GEN_RECEIVE_BLOCK_SIZE:value=VN_MAX_FRAME;break;
    case OID_GEN_LINK_SPEED:value=10000000;break;
    case OID_GEN_TRANSMIT_BUFFER_SPACE:case OID_GEN_RECEIVE_BUFFER_SPACE:value=VN_RING_CAPACITY*VN_MAX_FRAME;break;
    case OID_GEN_VENDOR_ID:value=0xFFFFFF;break;
    case OID_GEN_VENDOR_DESCRIPTION:data="Titanfyre virtual Ethernet SDK";length=sizeof("Titanfyre virtual Ethernet SDK");break;
    case OID_GEN_CURRENT_PACKET_FILTER:NdisAcquireSpinLock(&a->lock);value=a->filter;NdisReleaseSpinLock(&a->lock);break;
    case OID_GEN_DRIVER_VERSION:data=&version;length=sizeof(version);break;
    case OID_GEN_VENDOR_DRIVER_VERSION:value=0x00010000;break;
    case OID_GEN_MAC_OPTIONS:value=NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA|NDIS_MAC_OPTION_TRANSFERS_NOT_PEND|NDIS_MAC_OPTION_NO_LOOPBACK;break;
    case OID_GEN_MEDIA_CONNECT_STATUS:NdisAcquireSpinLock(&a->lock);value=a->connected&&a->authorization&&TitanAuthorizationAllowed(a->authorization)?NdisMediaStateConnected:NdisMediaStateDisconnected;NdisReleaseSpinLock(&a->lock);break;
    case OID_GEN_MAXIMUM_SEND_PACKETS:value=1;break;
    case OID_802_3_PERMANENT_ADDRESS:case OID_802_3_CURRENT_ADDRESS:data=a->mac;length=6;break;
    case OID_802_3_MAXIMUM_LIST_SIZE:value=32;break;
    case OID_802_3_MULTICAST_LIST:NdisAcquireSpinLock(&a->lock);length=a->multicast_count*6;if(capacity>=length){if(length)RtlCopyMemory(buffer,a->multicast,length);request->DATA.QUERY_INFORMATION.BytesWritten=length;}NdisReleaseSpinLock(&a->lock);if(capacity<length){request->DATA.QUERY_INFORMATION.BytesNeeded=length;return NDIS_STATUS_BUFFER_TOO_SHORT;}return NDIS_STATUS_SUCCESS;
    case OID_GEN_INTERRUPT_MODERATION:RtlZeroMemory(&moderation,sizeof(moderation));moderation.Header.Type=NDIS_OBJECT_TYPE_DEFAULT;moderation.Header.Revision=NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;moderation.Header.Size=NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;moderation.InterruptModeration=NdisInterruptModerationNotSupported;data=&moderation;length=sizeof(moderation);break;
    case OID_GEN_STATISTICS:NdisAcquireSpinLock(&a->lock);stats=a->statistics;NdisReleaseSpinLock(&a->lock);data=&stats;length=sizeof(stats);break;
    case OID_GEN_XMIT_OK:case OID_GEN_RCV_OK:case OID_GEN_XMIT_ERROR:case OID_GEN_RCV_ERROR:case OID_GEN_RCV_NO_BUFFER:
        NdisAcquireSpinLock(&a->lock);count=oid==OID_GEN_XMIT_OK?a->tx:oid==OID_GEN_RCV_OK?a->rx:oid==OID_GEN_RCV_NO_BUFFER?a->statistics.ifInDiscards:oid==OID_GEN_XMIT_ERROR?a->statistics.ifOutErrors:a->statistics.ifInErrors;NdisReleaseSpinLock(&a->lock);data=&count;length=capacity>=8?8:4;break;
    case OID_PNP_QUERY_POWER:length=0;break;
    default:return NDIS_STATUS_NOT_SUPPORTED;
    }
    if(capacity<length){request->DATA.QUERY_INFORMATION.BytesNeeded=length;return NDIS_STATUS_BUFFER_TOO_SHORT;}
    if(length)RtlCopyMemory(buffer,data,length);request->DATA.QUERY_INFORMATION.BytesWritten=length;return NDIS_STATUS_SUCCESS;
}
_Use_decl_annotations_ NDIS_STATUS Initialize(NDIS_HANDLE handle,NDIS_HANDLE driver,PNDIS_MINIPORT_INIT_PARAMETERS parameters){
    ADAPTER* a;NDIS_STATUS status;NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES registration;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES general;NET_BUFFER_LIST_POOL_PARAMETERS pool;NDIS_PM_CAPABILITIES power;
    UNREFERENCED_PARAMETER(driver);UNREFERENCED_PARAMETER(parameters);
    KeWaitForSingleObject(&LifecycleMutex,Executive,KernelMode,FALSE,NULL);
    /* LifecycleMutex serializes every publisher/remover of Current. */
    if(Current){KeReleaseMutex(&LifecycleMutex,FALSE);return NDIS_STATUS_RESOURCES;}
    a=ExAllocatePool2(POOL_FLAG_NON_PAGED,sizeof(*a),TAG);if(!a){KeReleaseMutex(&LifecycleMutex,FALSE);return NDIS_STATUS_RESOURCES;}
    RtlZeroMemory(a,sizeof(*a));a->handle=handle;RtlCopyMemory(a->mac,Mac,6);
    NdisAllocateSpinLock(&a->lock);ExInitializeRundownProtection(&a->lifetime);ExInitializeRundownProtection(&a->datapath);ExWaitForRundownProtectionRelease(&a->datapath);ExRundownCompleted(&a->datapath);KeInitializeMutex(&a->command,0);
    RtlZeroMemory(&registration,sizeof(registration));registration.Header.Type=NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    registration.Header.Revision=NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;registration.Header.Size=NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    registration.MiniportAdapterContext=a;registration.AttributeFlags=NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM;registration.InterfaceType=NdisInterfaceInternal;
    status=NdisMSetMiniportAttributes(handle,(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&registration);if(status!=NDIS_STATUS_SUCCESS)goto fail;
    RtlZeroMemory(&pool,sizeof(pool));pool.Header.Type=NDIS_OBJECT_TYPE_DEFAULT;pool.Header.Revision=NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;pool.Header.Size=sizeof(pool);pool.fAllocateNetBuffer=TRUE;pool.PoolTag=TAG;
    a->pool=NdisAllocateNetBufferListPool(handle,&pool);if(!a->pool){status=NDIS_STATUS_RESOURCES;goto fail;}
    RtlZeroMemory(&general,sizeof(general));general.Header.Type=NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    general.Header.Revision=NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;general.Header.Size=NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    RtlZeroMemory(&power,sizeof(power));power.Header.Type=NDIS_OBJECT_TYPE_DEFAULT;power.Header.Revision=NDIS_PM_CAPABILITIES_REVISION_1;power.Header.Size=NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1;general.PowerManagementCapabilitiesEx=&power;
    general.SupportedStatistics=NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED|NDIS_STATISTICS_XMIT_OK_SUPPORTED|NDIS_STATISTICS_RCV_OK_SUPPORTED|NDIS_STATISTICS_XMIT_ERROR_SUPPORTED|NDIS_STATISTICS_RCV_ERROR_SUPPORTED|NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;
    a->statistics.Header.Type=NDIS_OBJECT_TYPE_DEFAULT;a->statistics.Header.Revision=NDIS_STATISTICS_INFO_REVISION_1;a->statistics.Header.Size=NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;a->statistics.SupportedStatistics=0x003F87FFu;
    general.MediaType=NdisMedium802_3;general.PhysicalMediumType=NdisPhysicalMediumUnspecified;general.MtuSize=1500;
    general.MaxXmitLinkSpeed=general.MaxRcvLinkSpeed=general.XmitLinkSpeed=general.RcvLinkSpeed=1000000000ull;
    general.MediaConnectState=MediaConnectStateDisconnected;general.MediaDuplexState=MediaDuplexStateFull;general.LookaheadSize=1500;
    general.MacOptions=NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA|NDIS_MAC_OPTION_TRANSFERS_NOT_PEND|NDIS_MAC_OPTION_NO_LOOPBACK;
    general.SupportedPacketFilters=FILTERS;general.MaxMulticastListSize=32;general.MacAddressLength=6;
    RtlCopyMemory(general.PermanentMacAddress,a->mac,6);RtlCopyMemory(general.CurrentMacAddress,a->mac,6);general.AccessType=NET_IF_ACCESS_BROADCAST;
    general.DirectionType=NET_IF_DIRECTION_SENDRECEIVE;general.ConnectionType=NET_IF_CONNECTION_DEDICATED;
    general.IfType=IF_TYPE_ETHERNET_CSMACD;general.IfConnectorPresent=FALSE;
    general.SupportedOidList=(PNDIS_OID)Oids;general.SupportedOidListLength=sizeof(Oids);
    general.AutoNegotiationFlags=NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED|NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED|NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
    status=NdisMSetMiniportAttributes(handle,(PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&general);if(status!=NDIS_STATUS_SUCCESS)goto fail;
    status=RegisterControl(a);if(status!=NDIS_STATUS_SUCCESS)goto fail;
    KeEnterCriticalRegion();ExAcquirePushLockExclusive(&GlobalLock);
    NT_ASSERT(Current==NULL);Current=a;
    ExReleasePushLockExclusive(&GlobalLock);KeLeaveCriticalRegion();KeReleaseMutex(&LifecycleMutex,FALSE);return NDIS_STATUS_SUCCESS;
fail:if(a->control_handle)NdisDeregisterDeviceEx(a->control_handle);if(a->pool)NdisFreeNetBufferListPool(a->pool);NdisFreeSpinLock(&a->lock);ExFreePoolWithTag(a,TAG);KeReleaseMutex(&LifecycleMutex,FALSE);return status;
}
_Use_decl_annotations_ VOID Halt(NDIS_HANDLE c,NDIS_HALT_ACTION action){
    ADAPTER* a=c;UNREFERENCED_PARAMETER(action);
    KeWaitForSingleObject(&LifecycleMutex,Executive,KernelMode,FALSE,NULL);
    KeWaitForSingleObject(&a->command,Executive,KernelMode,FALSE,NULL);
    /* Clear file-owned pointers before unpublishing. A concurrent CLEANUP can
       then safely miss Current without freeing a context still used by a
       previously referenced control operation queued on command. */
    NdisAcquireSpinLock(&a->lock);a->running=FALSE;a->connected=FALSE;a->owner=NULL;a->authorization=NULL;a->head=a->count=0;NdisReleaseSpinLock(&a->lock);
    KeEnterCriticalRegion();ExAcquirePushLockExclusive(&GlobalLock);
    if(Current==a)Current=NULL;ExReleasePushLockExclusive(&GlobalLock);KeLeaveCriticalRegion();
    KeReleaseMutex(&a->command,FALSE);ExWaitForRundownProtectionRelease(&a->lifetime);ExWaitForRundownProtectionRelease(&a->datapath);
    /* Deregistration removes the link. Outstanding file references keep the
       old device object delete-pending; never wait for user handles here. */
    NdisDeregisterDeviceEx(a->control_handle);NdisFreeNetBufferListPool(a->pool);NdisFreeSpinLock(&a->lock);ExFreePoolWithTag(a,TAG);KeReleaseMutex(&LifecycleMutex,FALSE);
}
_Use_decl_annotations_ VOID CancelSend(NDIS_HANDLE c,PVOID id){UNREFERENCED_PARAMETER(c);UNREFERENCED_PARAMETER(id);}
_Use_decl_annotations_ VOID CancelOid(NDIS_HANDLE c,PVOID id){UNREFERENCED_PARAMETER(c);UNREFERENCED_PARAMETER(id);}
_Use_decl_annotations_ VOID Pnp(NDIS_HANDLE c,PNET_DEVICE_PNP_EVENT e){UNREFERENCED_PARAMETER(c);UNREFERENCED_PARAMETER(e);}
_Use_decl_annotations_ VOID Shutdown(NDIS_HANDLE c,NDIS_SHUTDOWN_ACTION action){UNREFERENCED_PARAMETER(c);UNREFERENCED_PARAMETER(action);}
_Use_decl_annotations_ VOID Unload(PDRIVER_OBJECT object){UNREFERENCED_PARAMETER(object);NdisMDeregisterMiniportDriver(DriverHandle);}
_Use_decl_annotations_ NTSTATUS DriverEntry(PDRIVER_OBJECT object,PUNICODE_STRING path){
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS characteristics;
    ExInitializePushLock(&GlobalLock);KeInitializeMutex(&LifecycleMutex,0);RtlZeroMemory(&characteristics,sizeof(characteristics));characteristics.Header.Type=NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    characteristics.Header.Revision=NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;characteristics.Header.Size=NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    characteristics.MajorNdisVersion=6;characteristics.MinorNdisVersion=30;characteristics.MajorDriverVersion=1;
    characteristics.InitializeHandlerEx=Initialize;characteristics.HaltHandlerEx=Halt;characteristics.PauseHandler=Pause;characteristics.RestartHandler=Restart;
    characteristics.OidRequestHandler=Oid;characteristics.SendNetBufferListsHandler=Send;characteristics.ReturnNetBufferListsHandler=Return;
    characteristics.CancelSendHandler=CancelSend;characteristics.CancelOidRequestHandler=CancelOid;characteristics.DevicePnPEventNotifyHandler=Pnp;characteristics.ShutdownHandlerEx=Shutdown;characteristics.UnloadHandler=Unload;
    return NdisMRegisterMiniportDriver(object,path,NULL,&characteristics,&DriverHandle);
}
