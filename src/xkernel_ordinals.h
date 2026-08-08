// xkernel_ordinals.h — authoritative Xbox kernel export table.
//
// Generated from nxdk lib/xboxkrnl/xboxkrnl.exe.def (CC0).  One row per
// export: ordinal, name, calling convention, stdcall arg-bytes, kind.
// This is the single source of truth for BOTH the 64-bit recompile path
// (larush_kernel_shim.c) and the 32-bit native harness (nat_stubs.c) —
// do NOT hand-edit ordinal->name mappings elsewhere.
//
// cc classes:
//   XK_STDCALL  callee pops `argbytes` (the common case)
//   XK_FASTCALL ecx/edx first two args, callee pops the rest (@-prefixed)
//   XK_CDECL    caller cleans up (DbgPrint varargs)
//   XK_NONE     data export (kind XK_DATA)

#ifndef XKERNEL_ORDINALS_H
#define XKERNEL_ORDINALS_H

enum xk_cc  { XK_NONE = 0, XK_STDCALL, XK_FASTCALL, XK_CDECL };
enum xk_kind { XK_DATA = 0, XK_FUNC };

/* X(ordinal, Name, cc, argbytes, kind) */
#define XK_ORDINALS(X) \
    X(  1, AvGetSavedDataAddress             , XK_CDECL   ,  0, XK_FUNC) \
    X(  2, AvSendTVEncoderOption             , XK_STDCALL , 16, XK_FUNC) \
    X(  3, AvSetDisplayMode                  , XK_STDCALL , 24, XK_FUNC) \
    X(  4, AvSetSavedDataAddress             , XK_STDCALL ,  4, XK_FUNC) \
    X(  5, DbgBreakPoint                     , XK_CDECL   ,  0, XK_FUNC) \
    X(  6, DbgBreakPointWithStatus           , XK_STDCALL ,  4, XK_FUNC) \
    X(  7, DbgLoadImageSymbols               , XK_STDCALL , 12, XK_FUNC) \
    X(  8, DbgPrint                          , XK_CDECL   ,  0, XK_FUNC) \
    X(  9, HalReadSMCTrayState               , XK_STDCALL ,  8, XK_FUNC) \
    X( 10, DbgPrompt                         , XK_STDCALL , 12, XK_FUNC) \
    X( 11, DbgUnLoadImageSymbols             , XK_STDCALL , 12, XK_FUNC) \
    X( 12, ExAcquireReadWriteLockExclusive   , XK_STDCALL ,  4, XK_FUNC) \
    X( 13, ExAcquireReadWriteLockShared      , XK_STDCALL ,  4, XK_FUNC) \
    X( 14, ExAllocatePool                    , XK_STDCALL ,  4, XK_FUNC) \
    X( 15, ExAllocatePoolWithTag             , XK_STDCALL ,  8, XK_FUNC) \
    X( 16, ExEventObjectType                 , XK_NONE    ,  0, XK_DATA) \
    X( 17, ExFreePool                        , XK_STDCALL ,  4, XK_FUNC) \
    X( 18, ExInitializeReadWriteLock         , XK_STDCALL ,  4, XK_FUNC) \
    X( 19, ExInterlockedAddLargeInteger      , XK_STDCALL , 16, XK_FUNC) \
    X( 20, ExInterlockedAddLargeStatistic    , XK_FASTCALL,  8, XK_FUNC) \
    X( 21, ExInterlockedCompareExchange64    , XK_FASTCALL, 12, XK_FUNC) \
    X( 22, ExMutantObjectType                , XK_NONE    ,  0, XK_DATA) \
    X( 23, ExQueryPoolBlockSize              , XK_STDCALL ,  4, XK_FUNC) \
    X( 24, ExQueryNonVolatileSetting         , XK_STDCALL , 20, XK_FUNC) \
    X( 25, ExReadWriteRefurbInfo             , XK_STDCALL , 12, XK_FUNC) \
    X( 26, ExRaiseException                  , XK_STDCALL ,  4, XK_FUNC) \
    X( 27, ExRaiseStatus                     , XK_STDCALL ,  4, XK_FUNC) \
    X( 28, ExReleaseReadWriteLock            , XK_STDCALL ,  4, XK_FUNC) \
    X( 29, ExSaveNonVolatileSetting          , XK_STDCALL , 16, XK_FUNC) \
    X( 30, ExSemaphoreObjectType             , XK_NONE    ,  0, XK_DATA) \
    X( 31, ExTimerObjectType                 , XK_NONE    ,  0, XK_DATA) \
    X( 32, ExfInterlockedInsertHeadList      , XK_FASTCALL,  8, XK_FUNC) \
    X( 33, ExfInterlockedInsertTailList      , XK_FASTCALL,  8, XK_FUNC) \
    X( 34, ExfInterlockedRemoveHeadList      , XK_FASTCALL,  4, XK_FUNC) \
    X( 35, FscGetCacheSize                   , XK_CDECL   ,  0, XK_FUNC) \
    X( 36, FscInvalidateIdleBlocks           , XK_CDECL   ,  0, XK_FUNC) \
    X( 37, FscSetCacheSize                   , XK_STDCALL ,  4, XK_FUNC) \
    X( 38, HalClearSoftwareInterrupt         , XK_FASTCALL,  4, XK_FUNC) \
    X( 39, HalDisableSystemInterrupt         , XK_STDCALL ,  4, XK_FUNC) \
    X( 40, HalDiskCachePartitionCount        , XK_NONE    ,  0, XK_DATA) \
    X( 41, HalDiskModelNumber                , XK_NONE    ,  0, XK_DATA) \
    X( 42, HalDiskSerialNumber               , XK_NONE    ,  0, XK_DATA) \
    X( 43, HalEnableSystemInterrupt          , XK_STDCALL ,  8, XK_FUNC) \
    X( 44, HalGetInterruptVector             , XK_STDCALL ,  8, XK_FUNC) \
    X( 45, HalReadSMBusValue                 , XK_STDCALL , 16, XK_FUNC) \
    X( 46, HalReadWritePCISpace              , XK_STDCALL , 24, XK_FUNC) \
    X( 47, HalRegisterShutdownNotification   , XK_STDCALL ,  8, XK_FUNC) \
    X( 48, HalRequestSoftwareInterrupt       , XK_FASTCALL,  4, XK_FUNC) \
    X( 49, HalReturnToFirmware               , XK_STDCALL ,  4, XK_FUNC) \
    X( 50, HalWriteSMBusValue                , XK_STDCALL , 16, XK_FUNC) \
    X( 51, InterlockedCompareExchange        , XK_FASTCALL, 12, XK_FUNC) \
    X( 52, InterlockedDecrement              , XK_FASTCALL,  4, XK_FUNC) \
    X( 53, InterlockedIncrement              , XK_FASTCALL,  4, XK_FUNC) \
    X( 54, InterlockedExchange               , XK_FASTCALL,  8, XK_FUNC) \
    X( 55, InterlockedExchangeAdd            , XK_FASTCALL,  8, XK_FUNC) \
    X( 56, InterlockedFlushSList             , XK_FASTCALL,  4, XK_FUNC) \
    X( 57, InterlockedPopEntrySList          , XK_FASTCALL,  4, XK_FUNC) \
    X( 58, InterlockedPushEntrySList         , XK_FASTCALL,  8, XK_FUNC) \
    X( 59, IoAllocateIrp                     , XK_STDCALL ,  4, XK_FUNC) \
    X( 60, IoBuildAsynchronousFsdRequest     , XK_STDCALL , 24, XK_FUNC) \
    X( 61, IoBuildDeviceIoControlRequest     , XK_STDCALL , 36, XK_FUNC) \
    X( 62, IoBuildSynchronousFsdRequest      , XK_STDCALL , 28, XK_FUNC) \
    X( 63, IoCheckShareAccess                , XK_STDCALL , 20, XK_FUNC) \
    X( 64, IoCompletionObjectType            , XK_NONE    ,  0, XK_DATA) \
    X( 65, IoCreateDevice                    , XK_STDCALL , 24, XK_FUNC) \
    X( 66, IoCreateFile                      , XK_STDCALL , 40, XK_FUNC) \
    X( 67, IoCreateSymbolicLink              , XK_STDCALL ,  8, XK_FUNC) \
    X( 68, IoDeleteDevice                    , XK_STDCALL ,  4, XK_FUNC) \
    X( 69, IoDeleteSymbolicLink              , XK_STDCALL ,  4, XK_FUNC) \
    X( 70, IoDeviceObjectType                , XK_NONE    ,  0, XK_DATA) \
    X( 71, IoFileObjectType                  , XK_NONE    ,  0, XK_DATA) \
    X( 72, IoFreeIrp                         , XK_STDCALL ,  4, XK_FUNC) \
    X( 73, IoInitializeIrp                   , XK_STDCALL , 12, XK_FUNC) \
    X( 74, IoInvalidDeviceRequest            , XK_STDCALL ,  8, XK_FUNC) \
    X( 75, IoQueryFileInformation            , XK_STDCALL , 20, XK_FUNC) \
    X( 76, IoQueryVolumeInformation          , XK_STDCALL , 20, XK_FUNC) \
    X( 77, IoQueueThreadIrp                  , XK_STDCALL ,  4, XK_FUNC) \
    X( 78, IoRemoveShareAccess               , XK_STDCALL ,  8, XK_FUNC) \
    X( 79, IoSetIoCompletion                 , XK_STDCALL , 20, XK_FUNC) \
    X( 80, IoSetShareAccess                  , XK_STDCALL , 16, XK_FUNC) \
    X( 81, IoStartNextPacket                 , XK_STDCALL ,  4, XK_FUNC) \
    X( 82, IoStartNextPacketByKey            , XK_STDCALL ,  8, XK_FUNC) \
    X( 83, IoStartPacket                     , XK_STDCALL , 12, XK_FUNC) \
    X( 84, IoSynchronousDeviceIoControlRequest, XK_STDCALL , 32, XK_FUNC) \
    X( 85, IoSynchronousFsdRequest           , XK_STDCALL , 20, XK_FUNC) \
    X( 86, IofCallDriver                     , XK_FASTCALL,  8, XK_FUNC) \
    X( 87, IofCompleteRequest                , XK_FASTCALL,  8, XK_FUNC) \
    X( 88, KdDebuggerEnabled                 , XK_NONE    ,  0, XK_DATA) \
    X( 89, KdDebuggerNotPresent              , XK_NONE    ,  0, XK_DATA) \
    X( 90, IoDismountVolume                  , XK_STDCALL ,  4, XK_FUNC) \
    X( 91, IoDismountVolumeByName            , XK_STDCALL ,  4, XK_FUNC) \
    X( 92, KeAlertResumeThread               , XK_STDCALL ,  4, XK_FUNC) \
    X( 93, KeAlertThread                     , XK_STDCALL ,  8, XK_FUNC) \
    X( 94, KeBoostPriorityThread             , XK_STDCALL ,  8, XK_FUNC) \
    X( 95, KeBugCheck                        , XK_STDCALL ,  4, XK_FUNC) \
    X( 96, KeBugCheckEx                      , XK_STDCALL , 20, XK_FUNC) \
    X( 97, KeCancelTimer                     , XK_STDCALL ,  4, XK_FUNC) \
    X( 98, KeConnectInterrupt                , XK_STDCALL ,  4, XK_FUNC) \
    X( 99, KeDelayExecutionThread            , XK_STDCALL , 12, XK_FUNC) \
    X(100, KeDisconnectInterrupt             , XK_STDCALL ,  4, XK_FUNC) \
    X(101, KeEnterCriticalRegion             , XK_CDECL   ,  0, XK_FUNC) \
    X(102, MmGlobalData                      , XK_NONE    ,  0, XK_DATA) \
    X(103, KeGetCurrentIrql                  , XK_CDECL   ,  0, XK_FUNC) \
    X(104, KeGetCurrentThread                , XK_CDECL   ,  0, XK_FUNC) \
    X(105, KeInitializeApc                   , XK_STDCALL , 28, XK_FUNC) \
    X(106, KeInitializeDeviceQueue           , XK_STDCALL ,  4, XK_FUNC) \
    X(107, KeInitializeDpc                   , XK_STDCALL , 12, XK_FUNC) \
    X(108, KeInitializeEvent                 , XK_STDCALL , 12, XK_FUNC) \
    X(109, KeInitializeInterrupt             , XK_STDCALL , 28, XK_FUNC) \
    X(110, KeInitializeMutant                , XK_STDCALL ,  8, XK_FUNC) \
    X(111, KeInitializeQueue                 , XK_STDCALL ,  8, XK_FUNC) \
    X(112, KeInitializeSemaphore             , XK_STDCALL , 12, XK_FUNC) \
    X(113, KeInitializeTimerEx               , XK_STDCALL ,  8, XK_FUNC) \
    X(114, KeInsertByKeyDeviceQueue          , XK_STDCALL , 12, XK_FUNC) \
    X(115, KeInsertDeviceQueue               , XK_STDCALL ,  8, XK_FUNC) \
    X(116, KeInsertHeadQueue                 , XK_STDCALL ,  8, XK_FUNC) \
    X(117, KeInsertQueue                     , XK_STDCALL ,  8, XK_FUNC) \
    X(118, KeInsertQueueApc                  , XK_STDCALL , 16, XK_FUNC) \
    X(119, KeInsertQueueDpc                  , XK_STDCALL , 12, XK_FUNC) \
    X(120, KeInterruptTime                   , XK_NONE    ,  0, XK_DATA) \
    X(121, KeIsExecutingDpc                  , XK_CDECL   ,  0, XK_FUNC) \
    X(122, KeLeaveCriticalRegion             , XK_CDECL   ,  0, XK_FUNC) \
    X(123, KePulseEvent                      , XK_STDCALL , 12, XK_FUNC) \
    X(124, KeQueryBasePriorityThread         , XK_STDCALL ,  4, XK_FUNC) \
    X(125, KeQueryInterruptTime              , XK_CDECL   ,  0, XK_FUNC) \
    X(126, KeQueryPerformanceCounter         , XK_CDECL   ,  0, XK_FUNC) \
    X(127, KeQueryPerformanceFrequency       , XK_CDECL   ,  0, XK_FUNC) \
    X(128, KeQuerySystemTime                 , XK_STDCALL ,  4, XK_FUNC) \
    X(129, KeRaiseIrqlToDpcLevel             , XK_CDECL   ,  0, XK_FUNC) \
    X(130, KeRaiseIrqlToSynchLevel           , XK_CDECL   ,  0, XK_FUNC) \
    X(131, KeReleaseMutant                   , XK_STDCALL , 16, XK_FUNC) \
    X(132, KeReleaseSemaphore                , XK_STDCALL , 16, XK_FUNC) \
    X(133, KeRemoveByKeyDeviceQueue          , XK_STDCALL ,  8, XK_FUNC) \
    X(134, KeRemoveDeviceQueue               , XK_STDCALL ,  4, XK_FUNC) \
    X(135, KeRemoveEntryDeviceQueue          , XK_STDCALL ,  8, XK_FUNC) \
    X(136, KeRemoveQueue                     , XK_STDCALL , 12, XK_FUNC) \
    X(137, KeRemoveQueueDpc                  , XK_STDCALL ,  4, XK_FUNC) \
    X(138, KeResetEvent                      , XK_STDCALL ,  4, XK_FUNC) \
    X(139, KeRestoreFloatingPointState       , XK_STDCALL ,  4, XK_FUNC) \
    X(140, KeResumeThread                    , XK_STDCALL ,  4, XK_FUNC) \
    X(141, KeRundownQueue                    , XK_STDCALL ,  4, XK_FUNC) \
    X(142, KeSaveFloatingPointState          , XK_STDCALL ,  4, XK_FUNC) \
    X(143, KeSetBasePriorityThread           , XK_STDCALL ,  8, XK_FUNC) \
    X(144, KeSetDisableBoostThread           , XK_STDCALL ,  8, XK_FUNC) \
    X(145, KeSetEvent                        , XK_STDCALL , 12, XK_FUNC) \
    X(146, KeSetEventBoostPriority           , XK_STDCALL ,  8, XK_FUNC) \
    X(147, KeSetPriorityProcess              , XK_STDCALL ,  8, XK_FUNC) \
    X(148, KeSetPriorityThread               , XK_STDCALL ,  8, XK_FUNC) \
    X(149, KeSetTimer                        , XK_STDCALL , 16, XK_FUNC) \
    X(150, KeSetTimerEx                      , XK_STDCALL , 20, XK_FUNC) \
    X(151, KeStallExecutionProcessor         , XK_STDCALL ,  4, XK_FUNC) \
    X(152, KeSuspendThread                   , XK_STDCALL ,  4, XK_FUNC) \
    X(153, KeSynchronizeExecution            , XK_STDCALL , 12, XK_FUNC) \
    X(154, KeSystemTime                      , XK_NONE    ,  0, XK_DATA) \
    X(155, KeTestAlertThread                 , XK_STDCALL ,  4, XK_FUNC) \
    X(156, KeTickCount                       , XK_NONE    ,  0, XK_DATA) \
    X(157, KeTimeIncrement                   , XK_NONE    ,  0, XK_DATA) \
    X(158, KeWaitForMultipleObjects          , XK_STDCALL , 32, XK_FUNC) \
    X(159, KeWaitForSingleObject             , XK_STDCALL , 20, XK_FUNC) \
    X(160, KfRaiseIrql                       , XK_FASTCALL,  4, XK_FUNC) \
    X(161, KfLowerIrql                       , XK_FASTCALL,  4, XK_FUNC) \
    X(162, KiBugCheckData                    , XK_NONE    ,  0, XK_DATA) \
    X(163, KiUnlockDispatcherDatabase        , XK_FASTCALL,  4, XK_FUNC) \
    X(164, LaunchDataPage                    , XK_NONE    ,  0, XK_DATA) \
    X(165, MmAllocateContiguousMemory        , XK_STDCALL ,  4, XK_FUNC) \
    X(166, MmAllocateContiguousMemoryEx      , XK_STDCALL , 20, XK_FUNC) \
    X(167, MmAllocateSystemMemory            , XK_STDCALL ,  8, XK_FUNC) \
    X(168, MmClaimGpuInstanceMemory          , XK_STDCALL ,  8, XK_FUNC) \
    X(169, MmCreateKernelStack               , XK_STDCALL ,  8, XK_FUNC) \
    X(170, MmDeleteKernelStack               , XK_STDCALL ,  8, XK_FUNC) \
    X(171, MmFreeContiguousMemory            , XK_STDCALL ,  4, XK_FUNC) \
    X(172, MmFreeSystemMemory                , XK_STDCALL ,  8, XK_FUNC) \
    X(173, MmGetPhysicalAddress              , XK_STDCALL ,  4, XK_FUNC) \
    X(174, MmIsAddressValid                  , XK_STDCALL ,  4, XK_FUNC) \
    X(175, MmLockUnlockBufferPages           , XK_STDCALL , 12, XK_FUNC) \
    X(176, MmLockUnlockPhysicalPage          , XK_STDCALL ,  8, XK_FUNC) \
    X(177, MmMapIoSpace                      , XK_STDCALL , 12, XK_FUNC) \
    X(178, MmPersistContiguousMemory         , XK_STDCALL , 12, XK_FUNC) \
    X(179, MmQueryAddressProtect             , XK_STDCALL ,  4, XK_FUNC) \
    X(180, MmQueryAllocationSize             , XK_STDCALL ,  4, XK_FUNC) \
    X(181, MmQueryStatistics                 , XK_STDCALL ,  4, XK_FUNC) \
    X(182, MmSetAddressProtect               , XK_STDCALL , 12, XK_FUNC) \
    X(183, MmUnmapIoSpace                    , XK_STDCALL ,  8, XK_FUNC) \
    X(184, NtAllocateVirtualMemory           , XK_STDCALL , 20, XK_FUNC) \
    X(185, NtCancelTimer                     , XK_STDCALL ,  8, XK_FUNC) \
    X(186, NtClearEvent                      , XK_STDCALL ,  4, XK_FUNC) \
    X(187, NtClose                           , XK_STDCALL ,  4, XK_FUNC) \
    X(188, NtCreateDirectoryObject           , XK_STDCALL ,  8, XK_FUNC) \
    X(189, NtCreateEvent                     , XK_STDCALL , 16, XK_FUNC) \
    X(190, NtCreateFile                      , XK_STDCALL , 36, XK_FUNC) \
    X(191, NtCreateIoCompletion              , XK_STDCALL , 16, XK_FUNC) \
    X(192, NtCreateMutant                    , XK_STDCALL , 12, XK_FUNC) \
    X(193, NtCreateSemaphore                 , XK_STDCALL , 16, XK_FUNC) \
    X(194, NtCreateTimer                     , XK_STDCALL , 12, XK_FUNC) \
    X(195, NtDeleteFile                      , XK_STDCALL ,  4, XK_FUNC) \
    X(196, NtDeviceIoControlFile             , XK_STDCALL , 40, XK_FUNC) \
    X(197, NtDuplicateObject                 , XK_STDCALL , 12, XK_FUNC) \
    X(198, NtFlushBuffersFile                , XK_STDCALL ,  8, XK_FUNC) \
    X(199, NtFreeVirtualMemory               , XK_STDCALL , 12, XK_FUNC) \
    X(200, NtFsControlFile                   , XK_STDCALL , 40, XK_FUNC) \
    X(201, NtOpenDirectoryObject             , XK_STDCALL ,  8, XK_FUNC) \
    X(202, NtOpenFile                        , XK_STDCALL , 24, XK_FUNC) \
    X(203, NtOpenSymbolicLinkObject          , XK_STDCALL ,  8, XK_FUNC) \
    X(204, NtProtectVirtualMemory            , XK_STDCALL , 16, XK_FUNC) \
    X(205, NtPulseEvent                      , XK_STDCALL ,  8, XK_FUNC) \
    X(206, NtQueueApcThread                  , XK_STDCALL , 20, XK_FUNC) \
    X(207, NtQueryDirectoryFile              , XK_STDCALL , 40, XK_FUNC) \
    X(208, NtQueryDirectoryObject            , XK_STDCALL , 24, XK_FUNC) \
    X(209, NtQueryEvent                      , XK_STDCALL ,  8, XK_FUNC) \
    X(210, NtQueryFullAttributesFile         , XK_STDCALL ,  8, XK_FUNC) \
    X(211, NtQueryInformationFile            , XK_STDCALL , 20, XK_FUNC) \
    X(212, NtQueryIoCompletion               , XK_STDCALL ,  8, XK_FUNC) \
    X(213, NtQueryMutant                     , XK_STDCALL ,  8, XK_FUNC) \
    X(214, NtQuerySemaphore                  , XK_STDCALL ,  8, XK_FUNC) \
    X(215, NtQuerySymbolicLinkObject         , XK_STDCALL , 12, XK_FUNC) \
    X(216, NtQueryTimer                      , XK_STDCALL ,  8, XK_FUNC) \
    X(217, NtQueryVirtualMemory              , XK_STDCALL ,  8, XK_FUNC) \
    X(218, NtQueryVolumeInformationFile      , XK_STDCALL , 20, XK_FUNC) \
    X(219, NtReadFile                        , XK_STDCALL , 32, XK_FUNC) \
    X(220, NtReadFileScatter                 , XK_STDCALL , 32, XK_FUNC) \
    X(221, NtReleaseMutant                   , XK_STDCALL ,  8, XK_FUNC) \
    X(222, NtReleaseSemaphore                , XK_STDCALL , 12, XK_FUNC) \
    X(223, NtRemoveIoCompletion              , XK_STDCALL , 20, XK_FUNC) \
    X(224, NtResumeThread                    , XK_STDCALL ,  8, XK_FUNC) \
    X(225, NtSetEvent                        , XK_STDCALL ,  8, XK_FUNC) \
    X(226, NtSetInformationFile              , XK_STDCALL , 20, XK_FUNC) \
    X(227, NtSetIoCompletion                 , XK_STDCALL , 20, XK_FUNC) \
    X(228, NtSetSystemTime                   , XK_STDCALL ,  8, XK_FUNC) \
    X(229, NtSetTimerEx                      , XK_STDCALL , 32, XK_FUNC) \
    X(230, NtSignalAndWaitForSingleObjectEx  , XK_STDCALL , 20, XK_FUNC) \
    X(231, NtSuspendThread                   , XK_STDCALL ,  8, XK_FUNC) \
    X(232, NtUserIoApcDispatcher             , XK_STDCALL , 12, XK_FUNC) \
    X(233, NtWaitForSingleObject             , XK_STDCALL , 12, XK_FUNC) \
    X(234, NtWaitForSingleObjectEx           , XK_STDCALL , 16, XK_FUNC) \
    X(235, NtWaitForMultipleObjectsEx        , XK_STDCALL , 24, XK_FUNC) \
    X(236, NtWriteFile                       , XK_STDCALL , 32, XK_FUNC) \
    X(237, NtWriteFileGather                 , XK_STDCALL , 32, XK_FUNC) \
    X(238, NtYieldExecution                  , XK_CDECL   ,  0, XK_FUNC) \
    X(239, ObCreateObject                    , XK_STDCALL , 16, XK_FUNC) \
    X(240, ObDirectoryObjectType             , XK_NONE    ,  0, XK_DATA) \
    X(241, ObInsertObject                    , XK_STDCALL , 16, XK_FUNC) \
    X(242, ObMakeTemporaryObject             , XK_STDCALL ,  4, XK_FUNC) \
    X(243, ObOpenObjectByName                , XK_STDCALL , 16, XK_FUNC) \
    X(244, ObOpenObjectByPointer             , XK_STDCALL , 12, XK_FUNC) \
    X(245, ObpObjectHandleTable              , XK_NONE    ,  0, XK_DATA) \
    X(246, ObReferenceObjectByHandle         , XK_STDCALL , 12, XK_FUNC) \
    X(247, ObReferenceObjectByName           , XK_STDCALL , 20, XK_FUNC) \
    X(248, ObReferenceObjectByPointer        , XK_STDCALL ,  8, XK_FUNC) \
    X(249, ObSymbolicLinkObjectType          , XK_NONE    ,  0, XK_DATA) \
    X(250, ObfDereferenceObject              , XK_FASTCALL,  4, XK_FUNC) \
    X(251, ObfReferenceObject                , XK_FASTCALL,  4, XK_FUNC) \
    X(252, PhyGetLinkState                   , XK_STDCALL ,  4, XK_FUNC) \
    X(253, PhyInitialize                     , XK_STDCALL ,  8, XK_FUNC) \
    X(254, PsCreateSystemThread              , XK_STDCALL , 20, XK_FUNC) \
    X(255, PsCreateSystemThreadEx            , XK_STDCALL , 40, XK_FUNC) \
    X(256, PsQueryStatistics                 , XK_STDCALL ,  4, XK_FUNC) \
    X(257, PsSetCreateThreadNotifyRoutine    , XK_STDCALL ,  4, XK_FUNC) \
    X(258, PsTerminateSystemThread           , XK_STDCALL ,  4, XK_FUNC) \
    X(259, PsThreadObjectType                , XK_NONE    ,  0, XK_DATA) \
    X(260, RtlAnsiStringToUnicodeString      , XK_STDCALL , 12, XK_FUNC) \
    X(261, RtlAppendStringToString           , XK_STDCALL ,  8, XK_FUNC) \
    X(262, RtlAppendUnicodeStringToString    , XK_STDCALL ,  8, XK_FUNC) \
    X(263, RtlAppendUnicodeToString          , XK_STDCALL ,  8, XK_FUNC) \
    X(264, RtlAssert                         , XK_STDCALL , 16, XK_FUNC) \
    X(265, RtlCaptureContext                 , XK_STDCALL ,  4, XK_FUNC) \
    X(266, RtlCaptureStackBackTrace          , XK_STDCALL , 16, XK_FUNC) \
    X(267, RtlCharToInteger                  , XK_STDCALL , 12, XK_FUNC) \
    X(268, RtlCompareMemory                  , XK_STDCALL , 12, XK_FUNC) \
    X(269, RtlCompareMemoryUlong             , XK_STDCALL , 12, XK_FUNC) \
    X(270, RtlCompareString                  , XK_STDCALL , 12, XK_FUNC) \
    X(271, RtlCompareUnicodeString           , XK_STDCALL , 12, XK_FUNC) \
    X(272, RtlCopyString                     , XK_STDCALL ,  8, XK_FUNC) \
    X(273, RtlCopyUnicodeString              , XK_STDCALL ,  8, XK_FUNC) \
    X(274, RtlCreateUnicodeString            , XK_STDCALL ,  8, XK_FUNC) \
    X(275, RtlDowncaseUnicodeChar            , XK_STDCALL ,  4, XK_FUNC) \
    X(276, RtlDowncaseUnicodeString          , XK_STDCALL , 12, XK_FUNC) \
    X(277, RtlEnterCriticalSection           , XK_STDCALL ,  4, XK_FUNC) \
    X(278, RtlEnterCriticalSectionAndRegion  , XK_STDCALL ,  4, XK_FUNC) \
    X(279, RtlEqualString                    , XK_STDCALL , 12, XK_FUNC) \
    X(280, RtlEqualUnicodeString             , XK_STDCALL , 12, XK_FUNC) \
    X(281, RtlExtendedIntegerMultiply        , XK_STDCALL , 12, XK_FUNC) \
    X(282, RtlExtendedLargeIntegerDivide     , XK_STDCALL , 16, XK_FUNC) \
    X(283, RtlExtendedMagicDivide            , XK_STDCALL , 20, XK_FUNC) \
    X(284, RtlFillMemory                     , XK_STDCALL , 12, XK_FUNC) \
    X(285, RtlFillMemoryUlong                , XK_STDCALL , 12, XK_FUNC) \
    X(286, RtlFreeAnsiString                 , XK_STDCALL ,  4, XK_FUNC) \
    X(287, RtlFreeUnicodeString              , XK_STDCALL ,  4, XK_FUNC) \
    X(288, RtlGetCallersAddress              , XK_STDCALL ,  8, XK_FUNC) \
    X(289, RtlInitAnsiString                 , XK_STDCALL ,  8, XK_FUNC) \
    X(290, RtlInitUnicodeString              , XK_STDCALL ,  8, XK_FUNC) \
    X(291, RtlInitializeCriticalSection      , XK_STDCALL ,  4, XK_FUNC) \
    X(292, RtlIntegerToChar                  , XK_STDCALL , 16, XK_FUNC) \
    X(293, RtlIntegerToUnicodeString         , XK_STDCALL , 12, XK_FUNC) \
    X(294, RtlLeaveCriticalSection           , XK_STDCALL ,  4, XK_FUNC) \
    X(295, RtlLeaveCriticalSectionAndRegion  , XK_STDCALL ,  4, XK_FUNC) \
    X(296, RtlLowerChar                      , XK_STDCALL ,  4, XK_FUNC) \
    X(297, RtlMapGenericMask                 , XK_STDCALL ,  8, XK_FUNC) \
    X(298, RtlMoveMemory                     , XK_STDCALL , 12, XK_FUNC) \
    X(299, RtlMultiByteToUnicodeN            , XK_STDCALL , 20, XK_FUNC) \
    X(300, RtlMultiByteToUnicodeSize         , XK_STDCALL , 12, XK_FUNC) \
    X(301, RtlNtStatusToDosError             , XK_STDCALL ,  4, XK_FUNC) \
    X(302, RtlRaiseException                 , XK_STDCALL ,  4, XK_FUNC) \
    X(303, RtlRaiseStatus                    , XK_STDCALL ,  4, XK_FUNC) \
    X(304, RtlTimeFieldsToTime               , XK_STDCALL ,  8, XK_FUNC) \
    X(305, RtlTimeToTimeFields               , XK_STDCALL ,  8, XK_FUNC) \
    X(306, RtlTryEnterCriticalSection        , XK_STDCALL ,  4, XK_FUNC) \
    X(307, RtlUlongByteSwap                  , XK_FASTCALL,  4, XK_FUNC) \
    X(308, RtlUnicodeStringToAnsiString      , XK_STDCALL , 12, XK_FUNC) \
    X(309, RtlUnicodeStringToInteger         , XK_STDCALL , 12, XK_FUNC) \
    X(310, RtlUnicodeToMultiByteN            , XK_STDCALL , 20, XK_FUNC) \
    X(311, RtlUnicodeToMultiByteSize         , XK_STDCALL , 12, XK_FUNC) \
    X(312, RtlUnwind                         , XK_STDCALL , 16, XK_FUNC) \
    X(313, RtlUpcaseUnicodeChar              , XK_STDCALL ,  4, XK_FUNC) \
    X(314, RtlUpcaseUnicodeString            , XK_STDCALL , 12, XK_FUNC) \
    X(315, RtlUpcaseUnicodeToMultiByteN      , XK_STDCALL , 20, XK_FUNC) \
    X(316, RtlUpperChar                      , XK_STDCALL ,  4, XK_FUNC) \
    X(317, RtlUpperString                    , XK_STDCALL ,  8, XK_FUNC) \
    X(318, RtlUshortByteSwap                 , XK_FASTCALL,  4, XK_FUNC) \
    X(319, RtlWalkFrameChain                 , XK_STDCALL , 12, XK_FUNC) \
    X(320, RtlZeroMemory                     , XK_STDCALL ,  8, XK_FUNC) \
    X(321, XboxEEPROMKey                     , XK_NONE    ,  0, XK_DATA) \
    X(322, XboxHardwareInfo                  , XK_NONE    ,  0, XK_DATA) \
    X(323, XboxHDKey                         , XK_NONE    ,  0, XK_DATA) \
    X(324, XboxKrnlVersion                   , XK_NONE    ,  0, XK_DATA) \
    X(325, XboxSignatureKey                  , XK_NONE    ,  0, XK_DATA) \
    X(326, XeImageFileName                   , XK_NONE    ,  0, XK_DATA) \
    X(327, XeLoadSection                     , XK_STDCALL ,  4, XK_FUNC) \
    X(328, XeUnloadSection                   , XK_STDCALL ,  4, XK_FUNC) \
    X(329, READ_PORT_BUFFER_UCHAR            , XK_STDCALL , 12, XK_FUNC) \
    X(330, READ_PORT_BUFFER_USHORT           , XK_STDCALL , 12, XK_FUNC) \
    X(331, READ_PORT_BUFFER_ULONG            , XK_STDCALL , 12, XK_FUNC) \
    X(332, WRITE_PORT_BUFFER_UCHAR           , XK_STDCALL , 12, XK_FUNC) \
    X(333, WRITE_PORT_BUFFER_USHORT          , XK_STDCALL , 12, XK_FUNC) \
    X(334, WRITE_PORT_BUFFER_ULONG           , XK_STDCALL , 12, XK_FUNC) \
    X(335, XcSHAInit                         , XK_STDCALL ,  4, XK_FUNC) \
    X(336, XcSHAUpdate                       , XK_STDCALL , 12, XK_FUNC) \
    X(337, XcSHAFinal                        , XK_STDCALL ,  8, XK_FUNC) \
    X(338, XcRC4Key                          , XK_STDCALL , 12, XK_FUNC) \
    X(339, XcRC4Crypt                        , XK_STDCALL , 12, XK_FUNC) \
    X(340, XcHMAC                            , XK_STDCALL , 28, XK_FUNC) \
    X(341, XcPKEncPublic                     , XK_STDCALL , 12, XK_FUNC) \
    X(342, XcPKDecPrivate                    , XK_STDCALL , 12, XK_FUNC) \
    X(343, XcPKGetKeyLen                     , XK_STDCALL ,  4, XK_FUNC) \
    X(344, XcVerifyPKCS1Signature            , XK_STDCALL , 12, XK_FUNC) \
    X(345, XcModExp                          , XK_STDCALL , 20, XK_FUNC) \
    X(346, XcDESKeyParity                    , XK_STDCALL ,  8, XK_FUNC) \
    X(347, XcKeyTable                        , XK_STDCALL , 12, XK_FUNC) \
    X(348, XcBlockCrypt                      , XK_STDCALL , 20, XK_FUNC) \
    X(349, XcBlockCryptCBC                   , XK_STDCALL , 28, XK_FUNC) \
    X(350, XcCryptService                    , XK_STDCALL ,  8, XK_FUNC) \
    X(351, XcUpdateCrypto                    , XK_STDCALL ,  8, XK_FUNC) \
    X(352, RtlRip                            , XK_STDCALL , 12, XK_FUNC) \
    X(353, XboxLANKey                        , XK_NONE    ,  0, XK_DATA) \
    X(354, XboxAlternateSignatureKeys        , XK_NONE    ,  0, XK_DATA) \
    X(355, XePublicKeyData                   , XK_NONE    ,  0, XK_DATA) \
    X(356, HalBootSMCVideoMode               , XK_NONE    ,  0, XK_DATA) \
    X(357, IdexChannelObject                 , XK_NONE    ,  0, XK_DATA) \
    X(358, HalIsResetOrShutdownPending       , XK_CDECL   ,  0, XK_FUNC) \
    X(359, IoMarkIrpMustComplete             , XK_STDCALL ,  4, XK_FUNC) \
    X(360, HalInitiateShutdown               , XK_CDECL   ,  0, XK_FUNC) \
    X(361, RtlSnprintf                       , XK_CDECL   ,  0, XK_FUNC) \
    X(362, RtlSprintf                        , XK_CDECL   ,  0, XK_FUNC) \
    X(363, RtlVsnprintf                      , XK_CDECL   ,  0, XK_FUNC) \
    X(364, RtlVsprintf                       , XK_CDECL   ,  0, XK_FUNC) \
    X(365, HalEnableSecureTrayEject          , XK_CDECL   ,  0, XK_FUNC) \
    X(366, HalWriteSMCScratchRegister        , XK_STDCALL ,  4, XK_FUNC) \
    X(374, MmDbgAllocateMemory               , XK_STDCALL ,  8, XK_FUNC) \
    X(375, MmDbgFreeMemory                   , XK_STDCALL ,  8, XK_FUNC) \
    X(376, MmDbgQueryAvailablePages          , XK_CDECL   ,  0, XK_FUNC) \
    X(377, MmDbgReleaseAddress               , XK_STDCALL ,  8, XK_FUNC) \
    X(378, MmDbgWriteCheck                   , XK_STDCALL ,  8, XK_FUNC) \

#define XK_ORDINAL_MAX 378

#endif /* XKERNEL_ORDINALS_H */
