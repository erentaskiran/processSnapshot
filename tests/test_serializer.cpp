#include <gtest/gtest.h>
#include "core/serializer.hpp"
#include <cstring>

using namespace checkpoint;

class SerializerTest : public ::testing::Test {
protected:
    BinarySerializer binarySerializer;
    JsonSerializer jsonSerializer;
};

// Binary Serializer Tests
TEST_F(SerializerTest, BinarySerializeDeserialize) {
    int original = 42;
    auto data = binarySerializer.serialize(&original, sizeof(original));
    
    int result = 0;
    ASSERT_TRUE(binarySerializer.deserialize(data, &result, sizeof(result)));
    EXPECT_EQ(original, result);
}

TEST_F(SerializerTest, BinarySerializeObject) {
    struct TestStruct {
        int a;
        double b;
        char c;
    };
    
    TestStruct original{42, 3.14, 'X'};
    auto data = binarySerializer.serializeObject(original);
    auto result = binarySerializer.deserializeObject<TestStruct>(data);
    
    EXPECT_EQ(original.a, result.a);
    EXPECT_DOUBLE_EQ(original.b, result.b);
    EXPECT_EQ(original.c, result.c);
}

TEST_F(SerializerTest, ChecksumCalculation) {
    StateData data1 = {0x01, 0x02, 0x03, 0x04};
    StateData data2 = {0x01, 0x02, 0x03, 0x04};
    StateData data3 = {0x01, 0x02, 0x03, 0x05};  // Farklı
    
    auto checksum1 = binarySerializer.calculateChecksum(data1);
    auto checksum2 = binarySerializer.calculateChecksum(data2);
    auto checksum3 = binarySerializer.calculateChecksum(data3);
    
    EXPECT_EQ(checksum1, checksum2);
    EXPECT_NE(checksum1, checksum3);
}

TEST_F(SerializerTest, ChecksumVerification) {
    StateData data = {0x48, 0x65, 0x6C, 0x6C, 0x6F};  // "Hello"
    auto checksum = binarySerializer.calculateChecksum(data);
    
    EXPECT_TRUE(binarySerializer.verifyChecksum(data, checksum));
    EXPECT_FALSE(binarySerializer.verifyChecksum(data, checksum + 1));
}

TEST_F(SerializerTest, CompressDecompress) {
    // Tekrarlayan veriler sıkıştırılabilir olmalı
    StateData data(100, 0xAA);  // 100 adet 0xAA
    
    auto compressed = binarySerializer.compress(data);
    auto decompressed = binarySerializer.decompress(compressed);
    
    EXPECT_EQ(data, decompressed);
}

TEST_F(SerializerTest, EmptyDataHandling) {
    StateData empty;
    
    auto checksum = binarySerializer.calculateChecksum(empty);
    EXPECT_TRUE(binarySerializer.verifyChecksum(empty, checksum));
    
    auto compressed = binarySerializer.compress(empty);
    auto decompressed = binarySerializer.decompress(compressed);
    EXPECT_TRUE(decompressed.empty());
}

// JSON Serializer Tests
TEST_F(SerializerTest, JsonSerializeDeserialize) {
    int original = 12345;
    auto data = jsonSerializer.serialize(&original, sizeof(original));
    
    // JSON çıktısını kontrol et
    std::string jsonStr(data.begin(), data.end());
    EXPECT_TRUE(jsonStr.find("\"data\":") != std::string::npos);
    EXPECT_TRUE(jsonStr.find("\"size\":") != std::string::npos);
    
    int result = 0;
    ASSERT_TRUE(jsonSerializer.deserialize(data, &result, sizeof(result)));
    EXPECT_EQ(original, result);
}

TEST_F(SerializerTest, JsonToString) {
    StateData data = {'T', 'e', 's', 't'};
    auto jsonStr = jsonSerializer.toJsonString(data);
    auto backToData = jsonSerializer.fromJsonString(jsonStr);
    
    EXPECT_EQ(data, backToData);
}

// Edge Cases
TEST_F(SerializerTest, LargeDataSerialization) {
    // 1MB veri
    StateData largeData(1024 * 1024);
    for (size_t i = 0; i < largeData.size(); ++i) {
        largeData[i] = static_cast<uint8_t>(i % 256);
    }
    
    auto checksum = binarySerializer.calculateChecksum(largeData);
    EXPECT_TRUE(binarySerializer.verifyChecksum(largeData, checksum));
}

TEST_F(SerializerTest, DeserializeInsufficientBuffer) {
    StateData smallData = {0x01, 0x02};
    int result = 0;
    
    // Daha büyük boyut isteniyor
    EXPECT_FALSE(binarySerializer.deserialize(smallData, &result, sizeof(result)));
}
