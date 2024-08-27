#pragma once

#include <unordered_map>

namespace MViewFile
{

    struct ArchiveFile {
        std::string name;
        std::string type;
        std::vector<uint8_t> data;
    };

    class Archive {
    public:
        Archive(const std::vector<uint8_t>& data);

        ArchiveFile get(const std::string& name) const;

        ArchiveFile extract(const std::string& name);

        bool checkSignature(const ArchiveFile& file) const;

        std::vector<std::string> getTextures() const;

    private:

        std::unordered_map<std::string, ArchiveFile> files;

        std::vector<uint8_t> decompress(const std::vector<uint8_t>& input, uint32_t decompressedSize);
    };

    class BigInt {
    public:
        BigInt(const std::vector<uint16_t>& a = {}) : digits(a) {};

        void setBytes(const std::vector<uint8_t>& a, bool c = false);
        int32_t toInt32() const;
        bool lessThan(const BigInt& a) const;
        void shiftRight();
        BigInt shiftLeft(int a) const;
        int bitCount() const;
        void sub(const BigInt& a);
        BigInt mul(const BigInt& a) const;
        BigInt mod(const BigInt& a) const;
        BigInt powmod(uint32_t a, const BigInt& c) const;
        void trim();

    private:
        std::vector<uint16_t> digits;
    };

    class ByteStream {
    public:
        ByteStream(const std::vector<uint8_t>& data) : bytes(data) {}

        bool empty() const 
        {
            return bytes.empty();
        }

        std::string readCString();
        
        std::string asString() const;

        std::vector<uint8_t> readBytes(size_t length);

        uint32_t readUint32();

        uint8_t readUint8();

        uint16_t readUint16();
        
        float readFloat32();
        
        uint32_t seekUint32(size_t index) const;

        float seekFloat32(size_t index) const;

        std::vector<float> getMatrix(size_t index) const;

    private:
        std::vector<uint8_t> bytes;
    };

}