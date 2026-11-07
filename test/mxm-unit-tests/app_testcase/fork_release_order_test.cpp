/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 * ubs-mem is licensed under the Mulan PSL v2.
 */
#include <gtest/gtest.h>
#include <cerrno>
#include <mockcpp/mockcpp.hpp>
#include "BorrowAppMetaMgr.h"
#include "RackMem.h"
#include "RackMemShm.h"
#include "ShmMetaDataMgr.h"
#include "ipc_command.h"
#include "shm_ipc_command.h"

using namespace ock::mxmd;
using namespace ock::common;
using namespace ock::ubsm;
static const char *releaseName = "fork_release_order";

class ForkReleaseOrderTest : public testing::TestWithParam<bool> {
protected:
    void *address{MAP_FAILED};
    size_t size{0};
    void SetUp() override
    {
        size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        address = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        ASSERT_NE(address, MAP_FAILED);
        if (GetParam()) {
            AppBorrowMetaDesc desc(size);
            desc.UpdateBorrowMetaDesc({1}, size, false, releaseName);
            ASSERT_EQ(BorrowAppMetaMgr::GetInstance().AddMeta(address, desc), UBSM_OK);
        } else {
            ShmAppMetaData meta(releaseName, {1}, address, size, READWRITE, UBSM_FLAG_CACHE);
            meta.AddAddr(address, size);
            meta.slices.push_back({1, address, size, 0, 0, O_RDWR});
            ASSERT_EQ(ShmMetaDataMgr::GetInstance().AddMetaData(releaseName, address, meta), UBSM_OK);
        }
    }
    void TearDown() override
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
        if (GetParam()) {
            BorrowAppMetaMgr::GetInstance().RemoveMeta(address);
        } else {
            ShmMetaDataMgr::GetInstance().RemoveMetaData(address, releaseName);
        }
        if (address != MAP_FAILED) {
            EXPECT_EQ(munmap(address, size), 0);
        }
    }
    uint32_t Release()
    {
        return GetParam() ? RackMem::GetInstance().UbsMemFree(address) :
                            RackMemShm::GetInstance().UbsMemShmUnmmap(address, size);
    }
    void Remote(unsigned int count, uint32_t first = UBSM_OK)
    {
        if (GetParam()) {
            MOCKER(IpcCommand::AppFreeMemory)
                .expects(exactly(count))
                .will(returnValue(first))
                .then(returnValue(static_cast<uint32_t>(UBSM_OK)));
        } else {
            MOCKER(ShmIpcCommand::IpcCallShmUnMap)
                .expects(exactly(count))
                .will(returnValue(first))
                .then(returnValue(static_cast<uint32_t>(UBSM_OK)));
        }
    }
};

TEST_P(ForkReleaseOrderTest, ReservationFailureDoesNotReleaseRemote)
{
    MOCKER(SystemAdapter::MemoryMap).expects(once()).will(returnValue(MAP_FAILED));
    MOCKER(SystemAdapter::MemoryUnMap).expects(never());
    Remote(0);
    EXPECT_EQ(Release(), MXM_ERR_MMAP_VA_FAILED);
    unsigned char resident = 0;
    EXPECT_EQ(mincore(address, size, &resident), 0);
}

TEST_P(ForkReleaseOrderTest, RemoteFailureKeepsAddressAndSuccessUnmapsIt)
{
    Remote(2, MXM_ERR_IPC_SYNC_CALL);
    EXPECT_EQ(Release(), MXM_ERR_IPC_SYNC_CALL);
    unsigned char resident = 0;
    EXPECT_EQ(mincore(address, size, &resident), 0);
    errno = 0;
    EXPECT_EQ(mmap(address, size, PROT_READ, MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0), MAP_FAILED);
    EXPECT_EQ(errno, EEXIST);
    // Retry follows the original full release path, including replacing the reservation again.
    EXPECT_EQ(Release(), UBSM_OK);
    errno = 0;
    EXPECT_EQ(mincore(address, size, &resident), -1);
    EXPECT_EQ(errno, ENOMEM);
}

INSTANTIATE_TEST_CASE_P(SharedAndObmmLease, ForkReleaseOrderTest, testing::Values(false, true));
