/*++

Copyright (c) 1992  Microsoft Corporation

Module Name:

    xmem.c

Abstract:

    This module contains routines for allocating and freeing "extended" memory.
    The memory is allocated directly from NT.

Author:

    Dave Hastings (daveh) 12-Dec-1992

Notes:

    Moved from dpmi32\i386

Revision History:

    09-Feb-1994 (daveh)
        Modified to be the common front end for the memory allocation.  Calls
        processor specific code to do actual allocation

--*/
#include "precomp.h"
#pragma hdrstop
#include "softpc.h"
#include <malloc.h>

ULONG
DpmiCalculateAppXmem(
    VOID
    );

MEM_DPMI XmemHead = { NULL, 0, &XmemHead, &XmemHead, 0};

PMEM_DPMI
DpmiAllocateXmem(
    ULONG BlockSize
    )
/*++

Routine Description:

    This routine allocates a block of "extended" memory from NT.  The
    blocks allocated this way will be 64K aligned (for now).  The address
    of the block is returned to the segmented app in bx:cx

Arguments:

    None.

Return Value:

    None.

--*/
{
    ULONG BlockAddress;
    NTSTATUS Status;
    PMEM_DPMI XmemBlock;
    ULONG size;

    //
    // First check if this app owns more than 16 MB
    //
    size = DpmiCalculateAppXmem();
    if (size + BlockSize > MAX_APP_XMEM) {
        return NULL;
    }

    //
    // Get a block of memory from NT (any base address)
    //
    BlockAddress = 0;
    Status = DpmiAllocateVirtualMemory(
        (PVOID)&BlockAddress,
        &BlockSize
        );

    if (!NT_SUCCESS(Status)) {
#if DBG
        OutputDebugString("DPMI: DpmiAllocateXmem failed to get memory block\n");
#endif
        return NULL;
    }
    XmemBlock = malloc(sizeof(MEM_DPMI));
    if (!XmemBlock) {
        DpmiFreeVirtualMemory(
            (PVOID)&BlockAddress,
            &BlockSize
            );
        return NULL;
    }
    XmemBlock->Address = (PVOID)BlockAddress;
    XmemBlock->Length = BlockSize;
    XmemBlock->Owner = CurrentPSPSelector;
    XmemBlock->Sel = 0;
    XmemBlock->SelCount = 0;
    INSERT_BLOCK(XmemBlock, XmemHead);

    return XmemBlock;

}

BOOL
DpmiFreeXmem(
    PMEM_DPMI XmemBlock
    )
/*++

Routine Description:

    This routine frees a block of "extended" memory from NT.

Arguments:

    None.

Return Value:

    None.

--*/
{
    NTSTATUS Status;
    PVOID BlockAddress;
    ULONG BlockSize;


    BlockAddress = XmemBlock->Address;
    BlockSize = XmemBlock->Length;

    Status = DpmiFreeVirtualMemory(
        &BlockAddress,
        &BlockSize
        );

    if (!NT_SUCCESS(Status)) {
#if DBG
        OutputDebugString("DPMI: DpmiFreeXmem failed to free block\n");
#endif
        return FALSE;
    }

    DELETE_BLOCK(XmemBlock);

    free(XmemBlock);
    return TRUE;
}

BOOL
DpmiIsXmemHandle(
    PMEM_DPMI XmemBlock
    )
/*++

Routine Description:

    This routine verifies that the given handle is a valid xmem handle.

Arguments:

    Handle to be verified.

Return Value:

    TRUE if handle is valid, FALSE otherwise.

--*/
{
    PMEM_DPMI p1;

    p1 = XmemHead.Next;

    while(p1 != &XmemHead) {
        if (p1 == XmemBlock) {
            return TRUE;
        }
        p1 = p1->Next;
    }
    return FALSE;
}

PMEM_DPMI
DpmiFindXmem(
    USHORT Sel
    )
/*++

Routine Description:

    This routine finds a block of "extended" memory based on its Selector
    field.

Arguments:

    None.

Return Value:

    None.

--*/
{
    PMEM_DPMI p1;

    p1 = XmemHead.Next;

    while(p1 != &XmemHead) {
        if (p1->Sel == Sel) {
            return p1;
        }
        p1 = p1->Next;
    }
    return NULL;
}

BOOL
DpmiReallocateXmem(
    PMEM_DPMI OldBlock,
    ULONG NewSize
    )
/*++

Routine Description:

    This routine resizes a block of "extended memory".  If the change in size
    is less than 4K, no change is made.

Arguments:

    None.

Return Value:

    None.

--*/
{
    ULONG BlockAddress;
    NTSTATUS Status;

    if (DpmiCalculateAppXmem() + NewSize - OldBlock->Length > MAX_APP_XMEM) {
            return FALSE;
    }

    BlockAddress = 0;
    Status = DpmiReallocateVirtualMemory(
        OldBlock->Address,
        OldBlock->Length,
        (PVOID)&BlockAddress,
        &NewSize
        );

    if (!NT_SUCCESS(Status)) {
#if DBG
        OutputDebugString("DPMI: DpmiAllocateXmem failed to get memory block\n");
#endif
        return FALSE;
    }

    RESIZE_BLOCK(OldBlock, BlockAddress, NewSize);

    return TRUE;
}

VOID
DpmiFreeAppXmem(
    USHORT Owner
    )
/*++

Routine Description:

    This routine frees Xmem allocated for the application

Arguments:

    Client DX = client PSP selector

Return Value:

    TRUE  if everything goes fine.
    FALSE if unable to release the memory
--*/
{
    PMEM_DPMI p1, p2;
    NTSTATUS Status;
    PVOID BlockAddress;
    ULONG BlockSize;

    p1 = XmemHead.Next;

    while(p1 != &XmemHead) {
        if (p1->Owner == Owner) {
            BlockAddress = p1->Address;
            BlockSize = p1->Length;

            Status = DpmiFreeVirtualMemory(
                &BlockAddress,
                &BlockSize
                );

            if (!NT_SUCCESS(Status)) {
#if DBG
                OutputDebugString("DPMI: DpmiFreeXmem failed to free block\n");
#endif
                return;
            }
            p2 = p1->Next;
            DELETE_BLOCK(p1);
            free(p1);
            p1 = p2;
            continue;
        }
        p1 = p1->Next;
    }
    return;
}

ULONG
DpmiCalculateAppXmem(
    VOID
    )
/*++

Routine Description:

    This routine calculates the Xmem allocated to the CURRENT application

Arguments:

    None.

Return Value:

    The size of xmem allocated to the app.
    Note the size will never exceed 2 GB.  We put a cap at about 16MB when
    allocating memory for the app.

--*/
{
    PMEM_DPMI p;
    NTSTATUS Status;
    ULONG Size = 0;

    //
    // If not current APP, we don't keep track of the xmem size
    //
    if (CurrentPSPSelector == 0) {
        return 0;
    }

    //
    // If CurrentPSPXmem is not zero, it is under control.
    // No need to initialize it. Make sure it is not negative value.
    //
    if (CurrentPSPXmem != 0 && CurrentPSPXmem <= MAX_APP_XMEM) {
        return CurrentPSPXmem;
    }
    p = XmemHead.Next;

    while(p != &XmemHead) {
        if (p->Owner == CurrentPSPSelector) {
            Size += p->Length;
        }
        p = p->Next;
    }
    CurrentPSPXmem = Size;
    return Size;
}

VOID
DpmiFreeAllXmem(
    VOID
    )
/*++

Routine Description:

    This function frees all allocated xmem.

Arguments:

    none

Return Value:

    None.

--*/
{
    PMEM_DPMI p1, p2;
    NTSTATUS Status;
    PVOID BlockAddress;
    ULONG BlockSize;

    p1 = XmemHead.Next;
    while(p1 != &XmemHead) {
        BlockAddress = p1->Address;
        BlockSize = p1->Length;

        Status = DpmiFreeVirtualMemory(
            &BlockAddress,
            &BlockSize
            );

        if (!NT_SUCCESS(Status)) {
#if DBG
            OutputDebugString("DPMI: DpmiFreeXmem failed to free block\n");
#endif
            return;
        }
        p2 = p1->Next;
        DELETE_BLOCK(p1);
        free(p1);
        p1 = p2;
    }
    CurrentPSPXmem = 0;
}
