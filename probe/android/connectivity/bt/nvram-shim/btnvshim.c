/*
 * btnvshim - a two-function stand-in for libnvram.so, for the CM container's
 * Bluetooth stack only.  DRAFT: compiled, host-logic-tested, never run on the
 * device.  See devlog/2026-10-15-bt-consys-groundwork.md ("NVRAM").
 *
 * Why it exists.  libbluetooth_mtk.so (the CM image's MTK BT vendor code) imports
 * exactly NVM_GetFileDesc and NVM_CloseFileDesc from libnvram.so, to read and
 * write one 64-byte record (the BT_Addr file, LID 1).  The real libnvram wants
 * a whole tree under /data/nvram (FILE_VER, check numbers, a RestoreFlag, write
 * access for uid bluetooth) and, when a file is missing and RestoreFlag exists,
 * restores it from the NVRAM eMMC partition, which this project does not touch.
 * This shim serves the record from a plain file instead and never opens a
 * partition.
 *
 * Install: the host script binds the .so read-only over
 * <container>/system/lib/libnvram.so (only libbluetooth_mtk*.so and, in the CM
 * image, the stock audio HAL and a few MTK tools import it; none of those run
 * in the container).
 *
 * ABI, recovered from libnvram.so / libbluetooth_mtk.so disassembly:
 *   F_ID NVM_GetFileDesc(int lid, int *rec_size, int *rec_num, bool is_read)
 *     - F_ID is 12 bytes, returned through a hidden pointer (AAPCS):
 *         word 0 = fd (or -1), word 1 = lid, byte at +8 = is_read.
 *   bool NVM_CloseFileDesc(F_ID id)   - F_ID passed by value in r0..r2.
 *   BT_Addr is LID 1, one record of 0x40 bytes.
 *
 * Behaviour:
 *   read  : open BTNV_SEED (64 bytes + optional 2-byte trailer) read-only.
 *           If it does not exist the call fails (fd -1) and libbluetooth_mtk
 *           falls back to its built-in 0x8127 defaults, exactly as it does when
 *           the real libnvram cannot open the file.
 *   write : libbluetooth_mtk calls this when the address in use is a default or
 *           zero, then writes the 6 address bytes at offset 0.  They go to
 *           BTNV_WRITTEN, never to the seed, so the file is a readable record
 *           of the address the stack settled on (eFUSE or default).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#ifdef BTNV_HOST_TEST
#include <stdio.h>
#define BTNV_LOG(...) (fprintf(stderr, "btnvshim: " __VA_ARGS__), fputc('\n', stderr))
#else
#include <android/log.h>
#define BTNV_LOG(...) __android_log_print(ANDROID_LOG_INFO, "btnvshim", __VA_ARGS__)
#endif

#ifndef BTNV_SEED
#define BTNV_SEED "/data/nw/bt_nvram.bin"
#endif
#ifndef BTNV_WRITTEN
#define BTNV_WRITTEN "/data/nw/bt_nvram.written"
#endif

#define BT_ADDR_LID 1
#define BT_REC_SIZE 0x40
#define BT_REC_NUM 1

typedef struct {
    int iFileDesc;
    int iLID;
    unsigned char bIsRead;
} F_ID; /* sizeof == 12 on ARM, as libnvram lays it out */

#define EXPORT __attribute__((visibility("default")))

EXPORT F_ID NVM_GetFileDesc(int lid, int *rec_size, int *rec_num, bool is_read)
{
    F_ID id;
    id.iFileDesc = -1;
    id.iLID = lid;
    id.bIsRead = is_read;

    if (lid != BT_ADDR_LID) {
        BTNV_LOG("lid %d refused (only BT_Addr, lid %d, is served)", lid, BT_ADDR_LID);
        errno = ENOENT;
        return id;
    }
    if (rec_size)
        *rec_size = BT_REC_SIZE;
    if (rec_num)
        *rec_num = BT_REC_NUM;

    if (is_read)
        id.iFileDesc = open(BTNV_SEED, O_RDONLY);
    else
        id.iFileDesc = open(BTNV_WRITTEN, O_RDWR | O_CREAT, 0666);
    BTNV_LOG("lid %d %s -> fd %d (errno %d)", lid, is_read ? "read " BTNV_SEED : "write " BTNV_WRITTEN,
             id.iFileDesc, id.iFileDesc < 0 ? errno : 0);
    return id;
}

EXPORT bool NVM_CloseFileDesc(F_ID id)
{
    if (id.iFileDesc < 0)
        return false;
    return close(id.iFileDesc) == 0;
}
