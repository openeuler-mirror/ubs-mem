#include "mxm_msg_packer.h"
#include <gtest/gtest.h>
#include <mockcpp/mockcpp.hpp>
#include "mxm_msg.h"
#include "rpc_config.h"

namespace UT {
using namespace ock::mxmd;
using namespace ock::rpc;

class MxmMsgPackerTest : public testing::Test {
public:
    static void TearDownTestCase()
    {
        mockcpp::GlobalMockObject::verify();
        mockcpp::GlobalMockObject::reset();
    }
};

TEST_F(MxmMsgPackerTest, SerializePod)
{
    NetMsgPacker packer;
    int32_t val = 42;
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    int32_t result = 0;
    unpacker.Deserialize(result);
    EXPECT_EQ(result, 42);
}

TEST_F(MxmMsgPackerTest, SerializeMultiplePod)
{
    NetMsgPacker packer;
    int32_t a = 1;
    uint64_t b = 2;
    int16_t c = 3;
    packer.Serialize(a);
    packer.Serialize(b);
    packer.Serialize(c);
    NetMsgUnpacker unpacker(packer.String());
    int32_t ra = 0;
    uint64_t rb = 0;
    int16_t rc = 0;
    unpacker.Deserialize(ra);
    unpacker.Deserialize(rb);
    unpacker.Deserialize(rc);
    EXPECT_EQ(ra, 1);
    EXPECT_EQ(rb, 2);
    EXPECT_EQ(rc, 3);
}

TEST_F(MxmMsgPackerTest, SerializeString)
{
    NetMsgPacker packer;
    std::string val = "hello";
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::string result;
    unpacker.Deserialize(result);
    EXPECT_EQ(result, "hello");
}

TEST_F(MxmMsgPackerTest, SerializeEmptyString)
{
    NetMsgPacker packer;
    std::string val;
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::string result = "not_empty";
    unpacker.Deserialize(result);
    EXPECT_EQ(result, "");
}

TEST_F(MxmMsgPackerTest, SerializeStringWithSpecialChars)
{
    NetMsgPacker packer;
    std::string val;
    val += 'a';
    val += '\0';
    val += 'b';
    val += '\x01';
    val += 'c';
    EXPECT_EQ(val.size(), 5);
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::string result;
    unpacker.Deserialize(result);
    EXPECT_EQ(result.size(), 5);
    EXPECT_EQ(result[0], 'a');
    EXPECT_EQ(result[1], '\0');
    EXPECT_EQ(result[2], 'b');
}

TEST_F(MxmMsgPackerTest, SerializeVectorInt)
{
    NetMsgPacker packer;
    std::vector<int32_t> val = {10, 20, 30};
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::vector<int32_t> result;
    unpacker.Deserialize(result);
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], 10);
    EXPECT_EQ(result[1], 20);
    EXPECT_EQ(result[2], 30);
}

TEST_F(MxmMsgPackerTest, SerializeEmptyVector)
{
    NetMsgPacker packer;
    std::vector<int32_t> val;
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::vector<int32_t> result;
    unpacker.Deserialize(result);
    ASSERT_EQ(result.size(), 0);
}

TEST_F(MxmMsgPackerTest, SerializeVectorString)
{
    NetMsgPacker packer;
    std::vector<std::string> val = {"abc", "def", "ghi"};
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::vector<std::string> result;
    unpacker.Deserialize(result);
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "abc");
    EXPECT_EQ(result[1], "def");
    EXPECT_EQ(result[2], "ghi");
}

TEST_F(MxmMsgPackerTest, SerializeMapStringInt)
{
    NetMsgPacker packer;
    std::map<std::string, int32_t> val = {{"one", 1}, {"two", 2}};
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::map<std::string, int32_t> result;
    unpacker.Deserialize(result);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result["one"], 1);
    EXPECT_EQ(result["two"], 2);
}

TEST_F(MxmMsgPackerTest, SerializeEmptyMap)
{
    NetMsgPacker packer;
    std::map<std::string, int32_t> val;
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::map<std::string, int32_t> result;
    unpacker.Deserialize(result);
    ASSERT_EQ(result.size(), 0);
}

TEST_F(MxmMsgPackerTest, SerializeMapIntInt)
{
    NetMsgPacker packer;
    std::map<int32_t, int32_t> val = {{1, 100}, {2, 200}};
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    std::map<int32_t, int32_t> result;
    unpacker.Deserialize(result);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[1], 100);
    EXPECT_EQ(result[2], 200);
}

TEST_F(MxmMsgPackerTest, SerializeClusterNode)
{
    NetMsgPacker packer;
    ClusterNode node;
    node.id = "node1";
    node.ip = "192.168.1.1";
    node.port = 8080;
    node.isMaster = true;
    node.lastSeen = 12345;
    node.isActive = false;
    node.type = NodeType::ELIGIBLE_NODE;
    packer.Serialize(node);
    NetMsgUnpacker unpacker(packer.String());
    ClusterNode result;
    unpacker.Deserialize(result);
    EXPECT_EQ(result.id, "node1");
    EXPECT_EQ(result.ip, "192.168.1.1");
    EXPECT_EQ(result.port, 8080);
    EXPECT_EQ(result.isMaster, true);
    EXPECT_EQ(result.lastSeen, 12345);
    EXPECT_EQ(result.isActive, false);
    EXPECT_EQ(result.type, NodeType::ELIGIBLE_NODE);
}

TEST_F(MxmMsgPackerTest, SerializeBool)
{
    NetMsgPacker packer;
    bool val = true;
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    bool result = false;
    unpacker.Deserialize(result);
    EXPECT_EQ(result, true);

    NetMsgPacker packer2;
    bool val2 = false;
    packer2.Serialize(val2);
    NetMsgUnpacker unpacker2(packer2.String());
    bool result2 = true;
    unpacker2.Deserialize(result2);
    EXPECT_EQ(result2, false);
}

TEST_F(MxmMsgPackerTest, SerializeUint16)
{
    NetMsgPacker packer;
    uint16_t val = 0xABCD;
    packer.Serialize(val);
    NetMsgUnpacker unpacker(packer.String());
    uint16_t result = 0;
    unpacker.Deserialize(result);
    EXPECT_EQ(result, 0xABCD);
}

TEST_F(MxmMsgPackerTest, StringRoundTrip)
{
    NetMsgPacker packer;
    int32_t a = 42;
    std::string b = "test_string";
    uint64_t c = 0xDEADBEEF;
    packer.Serialize(a);
    packer.Serialize(b);
    packer.Serialize(c);

    NetMsgUnpacker unpacker(packer.String());
    int32_t ra = 0;
    std::string rb;
    uint64_t rc = 0;
    unpacker.Deserialize(ra);
    unpacker.Deserialize(rb);
    unpacker.Deserialize(rc);
    EXPECT_EQ(ra, 42);
    EXPECT_EQ(rb, "test_string");
    EXPECT_EQ(rc, 0xDEADBEEF);
}

TEST_F(MxmMsgPackerTest, RejectsTruncatedPod)
{
    NetMsgUnpacker unpacker(std::string(1, '\0'));
    uint32_t value = 42;

    unpacker.Deserialize(value);

    EXPECT_FALSE(unpacker.IsValid());
    EXPECT_EQ(value, 0U);
}

TEST_F(MxmMsgPackerTest, RejectsTruncatedString)
{
    NetMsgPacker packer;
    packer.Serialize(static_cast<uint32_t>(4));
    NetMsgUnpacker unpacker(packer.String());
    std::string value = "unchanged";

    unpacker.Deserialize(value);

    EXPECT_FALSE(unpacker.IsValid());
    EXPECT_TRUE(value.empty());
}

TEST_F(MxmMsgPackerTest, RejectsContainerCountLargerThanPayload)
{
    NetMsgPacker packer;
    packer.Serialize(static_cast<size_t>(2));
    NetMsgUnpacker unpacker(packer.String());
    std::vector<uint64_t> values = {1};

    unpacker.Deserialize(values);

    EXPECT_FALSE(unpacker.IsValid());
    EXPECT_TRUE(values.empty());
}

TEST_F(MxmMsgPackerTest, MessageDeserializeReportsMalformedInput)
{
    ShmDeleteRequest request;
    NetMsgUnpacker unpacker(std::string(1, '\0'));

    EXPECT_EQ(request.Deserialize(unpacker), MXM_ERR_PARAM_INVALID);
}

TEST_F(MxmMsgPackerTest, RejectsTruncatedVectorElement)
{
    NetMsgPacker packer;
    packer.Serialize(static_cast<size_t>(2));
    packer.Serialize(static_cast<uint64_t>(1));
    NetMsgUnpacker unpacker(packer.String());
    std::vector<uint64_t> values;

    unpacker.Deserialize(values);

    EXPECT_FALSE(unpacker.IsValid());
    EXPECT_TRUE(values.empty());
}

TEST_F(MxmMsgPackerTest, RejectsTruncatedMapValue)
{
    NetMsgPacker packer;
    packer.Serialize(static_cast<size_t>(1));
    packer.Serialize(static_cast<uint64_t>(1));
    NetMsgUnpacker unpacker(packer.String());
    std::map<uint64_t, uint64_t> values;

    unpacker.Deserialize(values);

    EXPECT_FALSE(unpacker.IsValid());
    EXPECT_TRUE(values.empty());
}
} // namespace UT
