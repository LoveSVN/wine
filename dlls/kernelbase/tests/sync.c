/*
 * Synchronization tests
 *
 * Copyright 2018 Daniel Lehman
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>
#include <windef.h>
#include <winbase.h>
#include <stdlib.h>
#include <winerror.h>

#include "wine/test.h"

static BOOL (WINAPI *pWaitOnAddress)(volatile void *, void *, SIZE_T, DWORD);
static void (WINAPI *pWakeByAddressAll)(void *);
static void (WINAPI *pWakeByAddressSingle)(void *);

static LONG64 address;
static LONG64 compare;
static DWORD WINAPI test_WaitOnAddress_func(void *arg)
{
    BOOL ret = FALSE;
    DWORD gle;
    while (address == compare)
    {
        SetLastError(0xdeadbeef);
        ret = pWaitOnAddress(&address, &compare, sizeof(compare), INFINITE);
        gle = GetLastError();
        ok(gle == 0xdeadbeef || broken(gle == ERROR_SUCCESS) /* Win 8 */, "got %d\n", gle);
    }
    ok(ret, "got %d\n", ret);
    return 0;
}

static void test_WaitOnAddress(void)
{
    DWORD gle, val, nthreads;
    HANDLE threads[8];
    BOOL ret;
    int i;

    if (!pWaitOnAddress)
    {
        win_skip("WaitOnAddress not supported, skipping test\n");
        return;
    }

    address = 0;
    compare = 0;
    if (0) /* crash on Windows */
    {
        ret = pWaitOnAddress(&address, NULL, 8, 0);
        ret = pWaitOnAddress(NULL, &compare, 8, 0);
    }

    /* invalid arguments */
    SetLastError(0xdeadbeef);
    pWakeByAddressSingle(NULL);
    gle = GetLastError();
    ok(gle == 0xdeadbeef, "got %d\n", gle);

    SetLastError(0xdeadbeef);
    pWakeByAddressAll(NULL);
    gle = GetLastError();
    ok(gle == 0xdeadbeef, "got %d\n", gle);

    SetLastError(0xdeadbeef);
    ret = pWaitOnAddress(NULL, NULL, 0, 0);
    gle = GetLastError();
    ok(gle == ERROR_INVALID_PARAMETER, "got %d\n", gle);
    ok(!ret, "got %d\n", ret);

    address = 0;
    compare = 0;
    SetLastError(0xdeadbeef);
    ret = pWaitOnAddress(&address, &compare, 5, 0);
    gle = GetLastError();
    ok(gle == ERROR_INVALID_PARAMETER, "got %d\n", gle);
    ok(!ret, "got %d\n", ret);
    ok(address == 0, "got %s\n", wine_dbgstr_longlong(address));
    ok(compare == 0, "got %s\n", wine_dbgstr_longlong(compare));

    /* no waiters */
    address = 0;
    SetLastError(0xdeadbeef);
    pWakeByAddressSingle(&address);
    gle = GetLastError();
    ok(gle == 0xdeadbeef, "got %d\n", gle);
    ok(address == 0, "got %s\n", wine_dbgstr_longlong(address));

    SetLastError(0xdeadbeef);
    pWakeByAddressAll(&address);
    gle = GetLastError();
    ok(gle == 0xdeadbeef, "got %d\n", gle);
    ok(address == 0, "got %s\n", wine_dbgstr_longlong(address));

    /* different address size */
    address = 0;
    compare = 0xffff0000;
    SetLastError(0xdeadbeef);
    ret = pWaitOnAddress(&address, &compare, 4, 0);
    gle = GetLastError();
    ok(gle == 0xdeadbeef || broken(gle == ERROR_SUCCESS) /* Win 8 */, "got %d\n", gle);
    ok(ret, "got %d\n", ret);

    SetLastError(0xdeadbeef);
    ret = pWaitOnAddress(&address, &compare, 2, 0);
    gle = GetLastError();
    ok(gle == ERROR_TIMEOUT, "got %d\n", gle);
    ok(!ret, "got %d\n", ret);

    /* simple wait case */
    address = 0;
    compare = 1;
    SetLastError(0xdeadbeef);
    ret = pWaitOnAddress(&address, &compare, 8, 0);
    gle = GetLastError();
    ok(gle == 0xdeadbeef || broken(gle == ERROR_SUCCESS) /* Win 8 */, "got %d\n", gle);
    ok(ret, "got %d\n", ret);

    /* WakeByAddressAll */
    address = 0;
    compare = 0;
    for (i = 0; i < ARRAY_SIZE(threads); i++)
        threads[i] = CreateThread(NULL, 0, test_WaitOnAddress_func, NULL, 0, NULL);

    Sleep(100);
    address = ~0;
    pWakeByAddressAll(&address);
    val = WaitForMultipleObjects(ARRAY_SIZE(threads), threads, TRUE, 5000);
    ok(val == WAIT_OBJECT_0, "got %d\n", val);
    for (i = 0; i < ARRAY_SIZE(threads); i++)
        CloseHandle(threads[i]);

    /* WakeByAddressSingle */
    address = 0;
    for (i = 0; i < ARRAY_SIZE(threads); i++)
        threads[i] = CreateThread(NULL, 0, test_WaitOnAddress_func, NULL, 0, NULL);

    Sleep(100);
    address = 1;
    nthreads = ARRAY_SIZE(threads);
    while (nthreads)
    {
        val = WaitForMultipleObjects(nthreads, threads, FALSE, 0);
        ok(val == STATUS_TIMEOUT, "got %u\n", val);

        pWakeByAddressSingle(&address);
        val = WaitForMultipleObjects(nthreads, threads, FALSE, 2000);
        ok(val < WAIT_OBJECT_0 + nthreads, "got %u\n", val);
        CloseHandle(threads[val]);
        memmove(&threads[val], &threads[val+1], (nthreads - val - 1) * sizeof(threads[0]));
        nthreads--;
    }

}

START_TEST(sync)
{
    HMODULE hmod;

    hmod = LoadLibraryA("kernel32.dll");
    pWaitOnAddress       = (void *)GetProcAddress(hmod, "WaitOnAddress");
    ok(!pWaitOnAddress, "expected only in kernelbase.dll\n");

    hmod = LoadLibraryA("kernelbase.dll");
    pWaitOnAddress       = (void *)GetProcAddress(hmod, "WaitOnAddress");
    pWakeByAddressAll    = (void *)GetProcAddress(hmod, "WakeByAddressAll");
    pWakeByAddressSingle = (void *)GetProcAddress(hmod, "WakeByAddressSingle");

    test_WaitOnAddress();
}
