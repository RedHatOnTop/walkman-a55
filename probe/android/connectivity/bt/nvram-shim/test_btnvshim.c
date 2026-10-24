/* Host-side logic test for btnvshim.c (x86_64; the F_ID ABI is the target's
 * business, this only checks the file handling).  Build + run: ./build.sh test */
#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int iFileDesc;
    int iLID;
    unsigned char bIsRead;
} F_ID;
F_ID NVM_GetFileDesc(int lid, int *rec_size, int *rec_num, bool is_read);
bool NVM_CloseFileDesc(F_ID id);

int main(int argc, char **argv)
{
    int sz = 0, num = 0;
    unsigned char buf[80];
    F_ID id;

    (void)argc;
    /* seed file produced by gen-bt-nvram.py is argv[1] */
    id = NVM_GetFileDesc(1, &sz, &num, true);
    assert(id.iFileDesc >= 0 && sz == 0x40 && num == 1 && id.iLID == 1 && id.bIsRead == 1);
    assert(read(id.iFileDesc, buf, sizeof buf) == 66);
    assert(NVM_CloseFileDesc(id));

    FILE *f = fopen(argv[1], "rb");
    unsigned char want[80];
    assert(f && fread(want, 1, 66, f) == 66);
    fclose(f);
    assert(memcmp(buf, want, 66) == 0);

    /* another LID is refused */
    id = NVM_GetFileDesc(2, &sz, &num, true);
    assert(id.iFileDesc < 0);
    assert(!NVM_CloseFileDesc(id));

    /* write path: what libbluetooth_mtk does (lseek 0, write 6 address bytes) */
    id = NVM_GetFileDesc(1, &sz, &num, false);
    assert(id.iFileDesc >= 0 && sz == 0x40 && num == 1 && id.bIsRead == 0);
    assert(lseek(id.iFileDesc, 0, SEEK_SET) == 0);
    assert(write(id.iFileDesc, "\x02\x11\x22\x33\x44\x55", 6) == 6);
    assert(NVM_CloseFileDesc(id));

    /* the seed is untouched by the write */
    id = NVM_GetFileDesc(1, &sz, &num, true);
    unsigned char again[80];
    assert(read(id.iFileDesc, again, sizeof again) == 66 && memcmp(again, want, 66) == 0);
    NVM_CloseFileDesc(id);

    puts("btnvshim host test OK");
    return 0;
}
