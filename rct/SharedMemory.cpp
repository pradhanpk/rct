#include "SharedMemory.h"
#include "Log.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <assert.h>
#include <errno.h>

#define PROJID 3946

SharedMemory::SharedMemory(key_t key, int size, CreateMode mode)
    : mShm(-1), mOwner(false), mAddr(0), mKey(-1), mSize(0)
{
    init(key, size, mode);
}

SharedMemory::SharedMemory(const Path& filename, int size, CreateMode mode)
    : mShm(-1), mOwner(false), mAddr(0), mKey(-1), mSize(0)
{
    init(ftok(filename.nullTerminated(), PROJID), size, mode);
}

bool SharedMemory::init(key_t key, int size, CreateMode mode)
{
    if (key == -1)
        return false;

    mShm = shmget(key, size, (mode == None ? 0 : (IPC_CREAT | IPC_EXCL)));
    if (mShm == -1 && mode == Recreate) {
        mShm = shmget(key, size, 0);
        if (mShm != -1) {
            shmctl(mShm, IPC_RMID, 0);
            mShm = shmget(key, size, IPC_CREAT | IPC_EXCL);
        }
    }
    if (mShm == -1)
        return false;

    if (mode != None) {
        shmid_ds ds;
        memset(&ds, 0, sizeof(ds));
        ds.shm_perm.uid = getuid();
        ds.shm_perm.mode = 0600;
#ifndef OS_Darwin
        ds.shm_perm.mode |= SHM_DEST;
#endif
        const int ret = shmctl(mShm, IPC_SET, &ds);
        if (ret == -1) {
            shmctl(mShm, IPC_RMID, 0);
            mShm = -1;
            return false;
        }
    }
    mKey = key;
    mOwner = (mode != None);
    mSize = size;

    return true;
}

SharedMemory::~SharedMemory()
{
    cleanup();
}

void* SharedMemory::attach(AttachFlag flag, void* address)
{
    if (mAddr)
        return mAddr;

    int flg = address ? SHM_RND : 0;
    if (!(flag & Write))
        flg |= SHM_RDONLY;
    mAddr = shmat(mShm, address, flg);
    if (mAddr == reinterpret_cast<void*>(-1)) {
        error() << strerror(errno) << errno;
        mAddr = 0;
    }
    return mAddr;
}

void SharedMemory::detach()
{
    if (!mAddr)
        return;

    shmdt(mAddr);
    mAddr = 0;
}

void SharedMemory::cleanup()
{
    detach();
    if (mShm != -1 && mOwner)
        shmctl(mShm, IPC_RMID, 0);
}
