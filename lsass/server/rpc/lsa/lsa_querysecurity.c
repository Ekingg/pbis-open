/* Editor Settings: expandtabs and use 4 spaces for indentation
 * ex: set softtabstop=4 tabstop=8 expandtab shiftwidth=4: *
 */

/*
 * Copyright © BeyondTrust Software 2004 - 2019
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * BEYONDTRUST MAKES THIS SOFTWARE AVAILABLE UNDER OTHER LICENSING TERMS AS
 * WELL. IF YOU HAVE ENTERED INTO A SEPARATE LICENSE AGREEMENT WITH
 * BEYONDTRUST, THEN YOU MAY ELECT TO USE THE SOFTWARE UNDER THE TERMS OF THAT
 * SOFTWARE LICENSE AGREEMENT INSTEAD OF THE TERMS OF THE APACHE LICENSE,
 * NOTWITHSTANDING THE ABOVE NOTICE.  IF YOU HAVE QUESTIONS, OR WISH TO REQUEST
 * A COPY OF THE ALTERNATE LICENSING TERMS OFFERED BY BEYONDTRUST, PLEASE CONTACT
 * BEYONDTRUST AT beyondtrust.com/contact
 */

/*
 * Copyright (C) BeyondTrust Software. All rights reserved.
 *
 * Module Name:
 *
 *        lsa_querysecurity.c
 *
 * Abstract:
 *
 *        Remote Procedure Call (RPC) Server Interface
 *
 *        LsaQuerySecurity function
 *
 * Authors: Rafal Szczesniak (rafal@likewise.com)
 */

#include "includes.h"

static
NTSTATUS
LsaSrvQueryAccountSecurity(
    PLSAR_ACCOUNT_CONTEXT pAccountContext,
    SECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR_RELATIVE *ppSecurityDescRelative,
    PDWORD pSecurityDescRelativeSize
    );

static
NTSTATUS
LsaSrvQueryPolicySecurity(
    PPOLICY_CONTEXT pAccountContext,
    SECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR_RELATIVE *ppSecurityDescRelative,
    PDWORD pSecurityDescRelativeSize
    );


NTSTATUS
LsaSrvQuerySecurity(
    /* [in] */ handle_t IDL_handle,
    /* [in] */ PVOID hObject,
    /* [in] */ SECURITY_INFORMATION SecurityInformation,
    /* [out] */ PLSA_SECURITY_DESCRIPTOR_BUFFER *ppSecurityDescBuffer
    )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD err = ERROR_SUCCESS;
    PLSA_GENERIC_CONTEXT pContext = NULL;
    PSECURITY_DESCRIPTOR_RELATIVE pSecurityDescRelative = NULL;
    DWORD securityDescRelativeSize = 0;
    PLSA_SECURITY_DESCRIPTOR_BUFFER pSecurityDescBuffer = NULL;

    pContext = (PLSA_GENERIC_CONTEXT)(hObject);

    switch (pContext->Type)
    {
    case LsaContextPolicy:
        ntStatus = LsaSrvQueryPolicySecurity(
                        (PPOLICY_CONTEXT)pContext,
                        SecurityInformation,
                        &pSecurityDescRelative,
                        &securityDescRelativeSize);
        break;

    case LsaContextAccount:
        ntStatus = LsaSrvQueryAccountSecurity(
                        (PLSAR_ACCOUNT_CONTEXT)pContext,
                        SecurityInformation,
                        &pSecurityDescRelative,
                        &securityDescRelativeSize);
        break;

    default:
        /* Something is seriously wrong if we get a context
           we haven't created */
        ntStatus = STATUS_INTERNAL_ERROR;
    }
    BAIL_ON_NTSTATUS_ERROR(ntStatus);

    ntStatus = LsaSrvAllocateMemory(
                        OUT_PPVOID(&pSecurityDescBuffer),
                        sizeof(*pSecurityDescBuffer));
    BAIL_ON_NT_STATUS(ntStatus);

    pSecurityDescBuffer->pBuffer   = (PBYTE)pSecurityDescRelative;
    pSecurityDescBuffer->BufferLen = securityDescRelativeSize;

    *ppSecurityDescBuffer = pSecurityDescBuffer;

error:
    if (err || ntStatus)
    {
        if (pSecurityDescBuffer)
        {
            if (pSecurityDescBuffer->pBuffer)
            {
                LsaSrvFreeMemory(pSecurityDescBuffer->pBuffer);
            }
            LsaSrvFreeMemory(pSecurityDescBuffer);
        }

        if (ppSecurityDescBuffer)
        {
            *ppSecurityDescBuffer = NULL;
        }
    }

    if (ntStatus == STATUS_SUCCESS &&
        err != ERROR_SUCCESS)
    {
        ntStatus = LwWin32ErrorToNtStatus(err);
    }

    return ntStatus;
}


static
NTSTATUS
LsaSrvQueryPolicySecurity(
    PPOLICY_CONTEXT pAccountContext,
    SECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR_RELATIVE *ppSecurityDescRelative,
    PDWORD pSecurityDescRelativeSize
    )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD err = ERROR_SUCCESS;
    PSECURITY_DESCRIPTOR_ABSOLUTE pSecDesc = gpLsaSecDesc;
    PSECURITY_DESCRIPTOR_ABSOLUTE pSecurityDesc = NULL;
    PSECURITY_DESCRIPTOR_RELATIVE pSecurityDescRelative = NULL;
    DWORD securityDescRelativeSize = 0;

    err = LwAllocateMemory(
                        SECURITY_DESCRIPTOR_ABSOLUTE_MIN_SIZE,
                        OUT_PPVOID(&pSecurityDesc));
    BAIL_ON_LSA_ERROR(err);

    ntStatus = RtlCreateSecurityDescriptorAbsolute(
                        pSecurityDesc,
                        SECURITY_DESCRIPTOR_REVISION);
    BAIL_ON_NT_STATUS(ntStatus);

    if (SecurityInformation & OWNER_SECURITY_INFORMATION)
    {
        PSID owner = NULL;
        BOOLEAN defaulted = FALSE;

        ntStatus = RtlGetOwnerSecurityDescriptor(
                            pSecDesc,
                            &owner,
                            &defaulted);
        BAIL_ON_NT_STATUS(ntStatus);

        ntStatus = RtlSetOwnerSecurityDescriptor(
                            pSecurityDesc,
                            owner,
                            defaulted);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    if (SecurityInformation & GROUP_SECURITY_INFORMATION)
    {
        PSID group = NULL;
        BOOLEAN defaulted = FALSE;

        ntStatus = RtlGetGroupSecurityDescriptor(
                            pSecDesc,
                            &group,
                            &defaulted);
        BAIL_ON_NT_STATUS(ntStatus);

        ntStatus = RtlSetGroupSecurityDescriptor(
                            pSecurityDesc,
                            group,
                            defaulted);
        BAIL_ON_NT_STATUS(ntStatus);
    }

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
    {
        PACL pDacl = NULL;
        BOOLEAN daclPresent = FALSE;
        BOOLEAN defaulted = FALSE;

        ntStatus = RtlGetDaclSecurityDescriptor(
                            pSecDesc,
                            &daclPresent,
                            &pDacl,
                            &defaulted);
        BAIL_ON_NT_STATUS(ntStatus);

        if (daclPresent)
        {
            ntStatus = RtlSetDaclSecurityDescriptor(
                                pSecurityDesc,
                                daclPresent,
                                pDacl,
                                defaulted);
            BAIL_ON_NT_STATUS(ntStatus);
        }
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
    {
        PACL pSacl = NULL;
        BOOLEAN saclPresent = FALSE;
        BOOLEAN defaulted = FALSE;

        ntStatus = RtlGetSaclSecurityDescriptor(
                            pSecDesc,
                            &saclPresent,
                            &pSacl,
                            &defaulted);
        BAIL_ON_NT_STATUS(ntStatus);

        if (saclPresent)
        {
            ntStatus = RtlSetSaclSecurityDescriptor(
                                pSecurityDesc,
                                saclPresent,
                                pSacl,
                                defaulted);
            BAIL_ON_NT_STATUS(ntStatus);
        }
    }

    ntStatus = RtlAbsoluteToSelfRelativeSD(
                        pSecurityDesc,
                        pSecurityDescRelative,
                        &securityDescRelativeSize);
    if (ntStatus == STATUS_BUFFER_TOO_SMALL)
    {
        ntStatus = STATUS_SUCCESS;
    }
    else if (ntStatus != STATUS_SUCCESS)
    {
        BAIL_ON_NT_STATUS(ntStatus);
    }

    ntStatus = LsaSrvAllocateMemory(
                        OUT_PPVOID(&pSecurityDescRelative),
                        securityDescRelativeSize);
    BAIL_ON_NT_STATUS(ntStatus);

    ntStatus = RtlAbsoluteToSelfRelativeSD(
                        pSecurityDesc,
                        pSecurityDescRelative,
                        &securityDescRelativeSize);
    BAIL_ON_NT_STATUS(ntStatus);

    *ppSecurityDescRelative    = pSecurityDescRelative;
    *pSecurityDescRelativeSize = securityDescRelativeSize;

error:
    if (err || ntStatus)
    {
        if (pSecurityDescRelative)
        {
            LsaSrvFreeMemory(pSecurityDescRelative);
        }

        *ppSecurityDescRelative    = NULL;
        *pSecurityDescRelativeSize = 0;
    }

    LW_SAFE_FREE_MEMORY(pSecurityDesc);

    if (ntStatus == STATUS_SUCCESS &&
        err != ERROR_SUCCESS)
    {
        ntStatus = LwWin32ErrorToNtStatus(err);
    }

    return ntStatus;
}


static
NTSTATUS
LsaSrvQueryAccountSecurity(
    PLSAR_ACCOUNT_CONTEXT pAccountContext,
    SECURITY_INFORMATION SecurityInformation,
    PSECURITY_DESCRIPTOR_RELATIVE *ppSecurityDescRelative,
    PDWORD pSecurityDescRelativeSize
    )
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    DWORD err = ERROR_SUCCESS;
    PSECURITY_DESCRIPTOR_RELATIVE pSecDescRelative = NULL;
    DWORD secDescRelativeSize = 0;
    PSECURITY_DESCRIPTOR_RELATIVE pSecurityDescRelative = NULL;

    err = LsaSrvPrivsGetAccountSecurity(
                        NULL,
                        pAccountContext->pPolicyCtx->pUserToken,
                        pAccountContext->pAccountContext,
                        SecurityInformation,
                        &pSecDescRelative,
                        &secDescRelativeSize);
    BAIL_ON_LSA_ERROR(err);

    ntStatus = LsaSrvAllocateMemory(
                        OUT_PPVOID(&pSecurityDescRelative),
                        secDescRelativeSize);
    BAIL_ON_NT_STATUS(ntStatus);

    memcpy(pSecurityDescRelative,
           pSecDescRelative,
           secDescRelativeSize);

    *ppSecurityDescRelative    = pSecurityDescRelative;
    *pSecurityDescRelativeSize = secDescRelativeSize;

error:
    if (err || ntStatus)
    {
        if (pSecurityDescRelative)
        {
            LsaSrvFreeMemory(pSecurityDescRelative);
        }

        *ppSecurityDescRelative    = NULL;
        *pSecurityDescRelativeSize = 0;
    }

    LW_SAFE_FREE_MEMORY(pSecDescRelative);

    if (ntStatus == STATUS_SUCCESS &&
        err != ERROR_SUCCESS)
    {
        ntStatus = LwWin32ErrorToNtStatus(err);
    }

    return ntStatus;
}


/*
local variables:
mode: c
c-basic-offset: 4
indent-tabs-mode: nil
tab-width: 4
end:
*/
