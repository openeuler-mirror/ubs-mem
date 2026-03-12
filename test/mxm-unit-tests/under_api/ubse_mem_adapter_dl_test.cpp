/*
Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 */
#include "gtest/gtest.h"
#include "mockcpp/mockcpp.hpp"
#include "region_repository.h"
#include "ubse_mem_adapter.h"
#include "ubs_error.h"
#include "region_manager.h"
#include "ubs_common_types.h"
#include "system_adapter.h"
#include "numa_cpu_utils.h"

extern "C" {
extern void *dlopen(const char *file, int mode);
extern int dlclose(void *handle);
extern void *dlsym(void *__restrict handle, const char *__restrict name);
}

using namespace ock::mxm;
using namespace ock::ubsm;
namespace UT {
class UbseMemAdapterDlTest : public ::testing::Test {
protected:
    void SetUp() override {}

    void TearDown() override
    {
        GlobalMockObject::verify();
    }
};

TEST_F(UbseMemAdapterDlTest, UbseMemAdapterLoadLibrary_WhenUbseMemAdapterInitSuccess)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);
    UbseMemAdapter::Initialize();
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapterLoadLibrary_WhenDlopenFail)
{
    void *nullPtr = nullptr;
    MOCKER(SystemAdapter::DlOpen).stubs().will(returnValue(nullPtr));
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, ock::common::MXM_ERR_UBSE_LIB);
    GlobalMockObject::reset();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapterLoadLibrary_WhenDlsymFailed)
{
    int ubseFuncCount = 26;
    void *nullPtr = nullptr;
    void *notNullPtr = reinterpret_cast<void *>(1);
    for (int i = 0; i < ubseFuncCount; i++) {
        MOCKER(SystemAdapter::DlSym).expects(exactly(i + 1)).will(repeat(notNullPtr, i)).then(returnValue(nullPtr));
        DBG_LOGINFO("UbseMemAdapter::DlopenLibUbse ubseFuncCount." << i);
        EXPECT_EQ(UbseMemAdapter::Initialize(), ock::common::MXM_ERR_UBSE_LIB);
        GlobalMockObject::reset();
    }
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LookupRegionListSuccess)
{
    SHMRegions regions;
    auto  ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    EXPECT_NE(regions.num, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_NumaLeaseMalloc)
{
    ock::mxm::LeaseMallocParam param;
    param.name = "UbseMemAdapter_NumaLeaseMalloc";
    param.regionName = "UbseMemAdapter_NumaLeaseMalloc_region";
    param.size = 128 * 1024 * 1024;
    param.isNuma = true;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    SHMRegions regions;
    auto  ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    ock::share::service::RegionInfo regionInfo{param.regionName, 1, regions.region[0]};
    regionInfo.region.affinity[0] = true;
    ret = ock::share::service::RegionManager::GetInstance().CreateRegionInfo(regionInfo);
    EXPECT_EQ(ret, true);

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, result);
    DBG_LOGINFO("Malloc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId
                                 << ", size is " << result.unitSize);
    EXPECT_EQ(hr, 0);
    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);
    ret = ock::share::service::RegionManager::GetInstance().DeleteRegionInfo(param.regionName);
    EXPECT_EQ(ret, true);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_FdLeaseMallocSuccess)
{
    ock::mxm::LeaseMallocParam param;
    param.name = "UbseMemAdapter_FdLeaseMallocSuccess";
    param.regionName = "default";
    param.size = 128 * 1024 * 1024;
    param.isNuma = false;
    param.distance = ock::mxm::MEM_DISTANCE_L0;
    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMalloc(param, result);
    DBG_LOGINFO("Malloc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId
                                 << ", size is " << result.unitSize);
    EXPECT_EQ(hr, 0);

    mode_t mode = S_IRUSR | S_IWUSR;
    hr = ock::mxm::UbseMemAdapter::FdPermissionChange(param.name, param.appContext, mode);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_FdLeaseMallocWithLocSuccess)
{
    ock::mxm::LeaseMallocWithLocParam param;
    param.name = "UbseMemAdapter_FdLeaseMallocWithLocSuccess";
    param.regionName = "default";
    param.size = 128 * 1024 * 1024;
    param.isNuma = false;

    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    DBG_LOGINFO("Malloc with loc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId
                                 << ", size is " << result.unitSize);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);

    // error branch
    param.size = 1024;
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    param.size = 0; // time out
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, UBS_ERR_TIMED_OUT);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_NumaLeaseMallocWithLocSuccess)
{
    ock::mxm::LeaseMallocWithLocParam param;
    param.name = "UbseMemAdapter_NumaLeaseMallocWithLocSuccess";
    param.regionName = "default";
    param.size = 128 * 1024 * 1024;
    param.isNuma = true;

    param.appContext.uid = 0;
    param.appContext.gid = 0;
    param.appContext.pid = 0;
    param.slotId = 1;
    param.socketId = 1;
    param.numaId = 0;
    param.portId = 1;

    ock::ubsm::LeaseMallocResult result{};
    auto hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    DBG_LOGINFO("Malloc with loc res is " << hr << ", name is " << param.name << ", numa is " << result.numaId
                                          << ", size is " << result.unitSize);
    EXPECT_EQ(hr, 0);

    hr = ock::mxm::UbseMemAdapter::LeaseFree(param.name, param.isNuma);
    EXPECT_EQ(hr, 0);

    // error branch
    param.size = 1024;
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    param.size = 0; // time out
    hr = ock::mxm::UbseMemAdapter::LeaseMallocWithLoc(param, result);
    EXPECT_EQ(hr, UBS_ERR_TIMED_OUT);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateSuccess)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmCreateSuccess";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    SHMRegions regions;
    auto  ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    // 创建
    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = "UbseMemAdapter_ShmCreateSuccess";
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // mmap
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(importOwnerParam.name,  ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    // unmap
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateWithAffinity)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmCreateWithAffinity";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    SHMRegions regions;
    auto  ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    MOCKER(NumaCpuUtils::IsAppBoundToOneNumaNode).stubs().will(returnValue(0));

    // 创建
    createParam.size = 1024; // 构造 size 小于4MB 失败
    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 0; // 构造 TIME_OUT 失败
    hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 128 * 1024 * 1024; // 正常创建
    hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = "UbseMemAdapter_ShmCreateWithAffinity";
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // mmap
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(importOwnerParam.name,  ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    // unmap
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_LookUpClusterStatistic)
{
    int ret = UbseMemAdapter::Initialize();
    EXPECT_EQ(ret, 0);
    ock::ubsm::ubsmemClusterInfo clusterInfo;
    auto hr = ock::mxm::UbseMemAdapter::LookUpClusterStatistic(clusterInfo);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmAttach)
{
    ock::mxm::CreateShmParam createParam;
    createParam.privider.slot_ids[0] = 1;
    createParam.privider.node_cnt = 1;
    createParam.name = "UbseMemAdapter_ShmAttach";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    SHMRegions regions;
    auto  ret = ock::mxm::UbseMemAdapter::LookupRegionList(regions);
    EXPECT_EQ(ret, 0);
    createParam.desc = regions.region[0];

    // 创建
    auto hr = ock::mxm::UbseMemAdapter::ShmCreate(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = createParam.name;
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // attach
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(createParam.name, ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    ubsmem_shmem_info_t shmInfo;
    hr = ock::mxm::UbseMemAdapter::ShmLookup(createParam.name, shmInfo);
    EXPECT_EQ(hr, 0);

    std::vector<ubsmem_shmem_desc_t> nameList;
    hr = ock::mxm::UbseMemAdapter::ShmListLookup(createParam.name, nameList);
    EXPECT_EQ(hr, 0);

    // detach
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}

TEST_F(UbseMemAdapterDlTest, UbseMemAdapter_ShmCreateWithProvider)
{
    ock::mxm::CreateShmWithProviderParam createParam("node2", 1, 1, UINT32_MAX);
    createParam.name = "UbseMemAdapter_ShmCreateWithProvider";
    createParam.size = 128 * 1024 * 1024;
    createParam.appContext.uid = 0;
    createParam.appContext.gid = 0;
    createParam.appContext.pid = 0;

    createParam.flag = UBSM_FLAG_CACHE;
    createParam.mode = 0600;

    // 创建
    createParam.size = 1024; // 构造 size 小于4MB 失败
    auto hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 0; // 构造 TIME_OUT 失败
    hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, MXM_ERR_UBSE_INNER);

    createParam.size = 128 * 1024 * 1024; // 正常创建
    hr = ock::mxm::UbseMemAdapter::ShmCreateWithProvider(createParam);
    EXPECT_EQ(hr, 0);

    ock::mxm::ImportShmParam importOwnerParam;
    importOwnerParam.name = "UbseMemAdapter_ShmCreateWithProvider";
    importOwnerParam.length = 128 * 1024 * 1024;
    importOwnerParam.appContext.uid = 0;
    importOwnerParam.appContext.gid = 0;
    importOwnerParam.appContext.pid = 0;
    importOwnerParam.mode = 0600;

    // get
    ock::mxm::ubse_user_info_t ubsUserInfo{};
    hr = ock::mxm::UbseMemAdapter::ShmGetUserData(importOwnerParam.name, ubsUserInfo);
    EXPECT_EQ(hr, 0);

    // mmap
    ock::mxm::AttachShmResult result;
    hr = ock::mxm::UbseMemAdapter::ShmAttach(importOwnerParam.name,  ubsUserInfo, result);
    EXPECT_EQ(hr, 0);

    // unmap
    hr = ock::mxm::UbseMemAdapter::ShmDetach(importOwnerParam.name);
    EXPECT_EQ(hr, 0);

    // delete
    ock::ubsm::AppContext appContext;
    appContext.uid = 0;
    appContext.gid = 0;
    appContext.pid = 0;
    hr = ock::mxm::UbseMemAdapter::ShmDelete(createParam.name, appContext);
    EXPECT_EQ(hr, 0);
    UbseMemAdapter::Destroy();
}
}  // namespace UT
