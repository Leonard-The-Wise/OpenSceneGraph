
#include "pch.h"

#include "MViewFile.h"
#include "json.hpp"

using namespace MViewFile;
using json = nlohmann::json;

Archive::Archive(const std::vector<uint8_t>& data)
{
    ByteStream a(data);
    while (!a.empty()) {
        ArchiveFile file;
        file.name = a.readCString();
        file.type = a.readCString();
        uint32_t b = a.readUint32();
        uint32_t d = a.readUint32();
        uint32_t e = a.readUint32();
        file.data = a.readBytes(d);
        if (file.data.size() >= d) {
            if (b & 1) {
                file.data = decompress(file.data, e);
                if (file.data.empty()) {
                    continue;
                }
            }
            files[file.name] = file;
        }
    }
}

ArchiveFile Archive::get(const std::string& name) const
{
    auto it = files.find(name);
    if (it != files.end()) {
        return it->second;
    }
    return ArchiveFile();
}

ArchiveFile Archive::extract(const std::string& name)
{
    ArchiveFile file = get(name);
    files.erase(name);
    return file;
}

bool Archive::checkSignature(const ArchiveFile& a) const {
    if (a.name.empty()) {
        return false;
    }

    std::vector<uint8_t> c = get(a.name + ".sig").data;
    if (c.empty()) {
        return false;
    }

    json signatureData = json::parse(std::string(c.begin(), c.end()));
    if (signatureData.is_null()) {
        return false;
    }

    int32_t b = 5381;
    for (size_t d = 0; d < a.data.size(); ++d) {
        b = (33 * b + a.data[d]) & 4294967295;
    }

    BigInt aBigInt;
    aBigInt.setBytes({ 0, 233, 33, 170, 116, 86, 29, 195, 228, 46, 189, 3, 185, 31, 245, 19, 159, 105, 73, 
        190, 158, 80, 175, 38, 210, 116, 221, 229, 171, 134, 104, 144, 140, 5, 99, 255, 208, 78, 248, 215, 
        172, 44, 79, 83, 5, 244, 152, 19, 92, 137, 112, 10, 101, 142, 209, 100, 244, 92, 190, 125, 28,  0, 
        185, 54, 143,  247, 49, 37, 15, 254, 142, 180, 185, 232, 50, 219, 11, 186,  106, 116, 78, 212, 10, 
        105, 53, 26, 14, 181, 80, 47, 87, 213, 182, 19, 126, 151, 86, 109, 182, 224, 37,  135, 80, 59, 22, 
        93, 125, 68, 214, 106, 209, 152, 235, 157, 249, 245, 48, 76, 203, 0, 0, 95, 200, 246, 243, 229, 85, 79, 169 }, true);

    BigInt dBigInt;
    dBigInt.setBytes(signatureData[0].get<std::vector<uint8_t>>());

    return dBigInt.powmod(65537, aBigInt).toInt32() == b;
}

std::vector<std::string> Archive::getTextures() const
{
    std::vector<std::string> returnVector;
    for (auto& file : files)
    {
        if (file.second.type.compare(0, 6, "image/") == 0 && file.second.type != "image/derp")
        {
            returnVector.push_back(file.first);
        }
    }

    return returnVector;
}

std::vector<uint8_t> Archive::decompress(const std::vector<uint8_t>& input, uint32_t decompressedSize) {
    std::vector<uint8_t> output(decompressedSize);

    uint32_t d = 0;  // Índice do output
    std::vector<uint32_t> e(4096), f(4096);  // Dicionários de descompressão
    uint32_t g = 256;  // Tamanho do dicionário (começa em 256 e expande durante a descompressão)
    size_t h = input.size();  // Comprimento da entrada
    size_t k = 0;  // Índice anterior
    uint32_t n = 1;  // Comprimento da sequência
    uint32_t l = 1;  // Variável temporária

    // O primeiro byte é copiado diretamente para o output
    output[d++] = input[0];

    for (size_t p = 1;; p++) {
        // Calcula a posição atual do byte e assegura que está dentro dos limites
        l = p + (p >> 1);
        if (l + 1 >= h) {
            break;
        }

        // Recupera dois bytes da entrada
        uint32_t m = input[l + 1];
        uint32_t lByte = input[l];

        // Calcula o índice do dicionário
        uint32_t r = (p & 1) ? (m << 4 | lByte >> 4) : ((m & 15) << 8 | lByte);

        if (r < g) {
            if (r < 256) {
                // Copia diretamente o byte
                m = d;
                l = 1;
                output[d++] = r;
            }
            else {
                // Copia uma sequência do dicionário
                m = d;
                l = f[r];
                uint32_t start = e[r];
                uint32_t end = start + l;
                for (; start < end; start++) {
                    output[d++] = output[start];
                }
            }
        }
        else if (r == g) {
            // Copia e estende a sequência anterior
            m = d;
            l = n + 1;
            uint32_t start = k;
            uint32_t end = k + n;
            for (; start < end; start++) {
                output[d++] = output[start];
            }
            output[d++] = output[k];
        }
        else {
            break;  // Índice do dicionário inválido
        }

        // Atualiza o dicionário
        e[g] = k;
        f[g++] = n + 1;
        k = m;
        n = l;
        if (g >= 4096) {
            g = 256;  // Reinicia o dicionário quando ele se torna muito grande
        }
    }

    return (d == decompressedSize) ? output : std::vector<uint8_t>();  // Retorna vazio se a descompressão falhar
}


void BigInt::setBytes(const std::vector<uint8_t>& a, bool c) {
    int halfSize = (a.size() + 1) / 2;  
    digits.resize(halfSize);

    if (c) {
        for (int d = 0, index = static_cast<int>(a.size()) - 1; index >= 0; index -= 2) {  
            digits[d++] = a[index] + (index > 0 ? 256 * a[index - 1] : 0);
        }
    }
    else {
        for (int d = 0; d < halfSize; ++d) {  
            digits[d] = a[2 * d] + 256 * a[2 * d + 1];
        }
    }
    trim();
}

int32_t BigInt::toInt32() const {
    int32_t a = 0;
    if (!digits.empty()) {
        a = digits[0];
        if (digits.size() > 1) {
            a |= digits[1] << 16;
        }
    }
    return a;
}

bool BigInt::lessThan(const BigInt& a) const {
    if (digits.size() == a.digits.size()) {
        for (int c = digits.size() - 1; c >= 0; --c) {
            if (digits[c] != a.digits[c]) {
                return digits[c] < a.digits[c];
            }
        }
    }
    return digits.size() < a.digits.size();
}

void BigInt::shiftRight() {
    uint16_t carry = 0;
    for (int b = digits.size() - 1; b >= 0; --b) {
        uint16_t current = digits[b];
        digits[b] = (current >> 1) | (carry << 15);
        carry = current;
    }
    trim();
}

BigInt BigInt::shiftLeft(int shiftAmount) const {
    if (shiftAmount > 0) {
        size_t wholeShifts = shiftAmount / 16;
        int bitShifts = shiftAmount % 16;
        int inverseBitShifts = 16 - bitShifts;

        BigInt result(std::vector<uint16_t>(digits.size() + wholeShifts + 1));
        for (size_t i = 0; i < result.digits.size(); ++i) {
            result.digits[i] = ((i < wholeShifts || i >= digits.size() + wholeShifts ? 0 : digits[i - wholeShifts]) << bitShifts)
                | ((i < wholeShifts + 1 ? 0 : digits[i - wholeShifts - 1]) >> inverseBitShifts);
        }
        result.trim();
        return result;
    }
    return *this;
}

int BigInt::bitCount() const {
    int bitCount = 0;
    if (!digits.empty()) {
        bitCount = 16 * (digits.size() - 1);
        uint16_t lastDigit = digits.back();
        while (lastDigit) {
            lastDigit >>= 1;
            ++bitCount;
        }
    }
    return bitCount;
}

void BigInt::sub(const BigInt& a) {
    size_t maxSize = digits.size();
    size_t aSize = a.digits.size();
    int borrow = 0;
    for (size_t i = 0; i < maxSize; ++i) {
        int current = digits[i];
        int subtractValue = i < aSize ? a.digits[i] : 0;
        subtractValue += borrow;
        borrow = subtractValue > current ? 1 : 0;
        current += (borrow << 16);
        digits[i] = current - subtractValue & 65535;
    }
    trim();
}

BigInt BigInt::mul(const BigInt& a) const {
    BigInt result(std::vector<uint16_t>(digits.size() + a.digits.size()));
    auto& resultDigits = result.digits;
    for (size_t i = 0; i < digits.size(); ++i) {
        uint16_t multiplicand = digits[i];
        for (size_t j = 0; j < a.digits.size(); ++j) {
            uint32_t product = multiplicand * a.digits[j];
            size_t resultIndex = i + j;
            while (product) {
                uint32_t temp = (product & 65535) + resultDigits[resultIndex];
                resultDigits[resultIndex] = temp & 65535;
                product >>= 16;
                product += temp >> 16;
                ++resultIndex;
            }
        }
    }
    result.trim();
    return result;
}

BigInt BigInt::mod(const BigInt& a) const {
    if (digits.empty() || a.digits.empty()) {
        return BigInt(std::vector<uint16_t>{0});
    }
    BigInt result(*this);
    if (!lessThan(a)) {
        BigInt divisor(a);
        divisor = divisor.shiftLeft(result.bitCount() - divisor.bitCount());
        while (!result.lessThan(a)) {
            if (divisor.lessThan(result)) {
                result.sub(divisor);
            }
            divisor.shiftRight();
        }
        result.trim();
    }
    return result;
}

BigInt BigInt::powmod(uint32_t exponent, const BigInt& modValue) const {
    BigInt result(std::vector<uint16_t>{1});
    BigInt base = mod(modValue);
    while (exponent) {
        if (exponent & 1) {
            result = result.mul(base).mod(modValue);
        }
        exponent >>= 1;
        base = base.mul(base).mod(modValue);
    }
    return result;
}

void BigInt::trim() {
    while (!digits.empty() && digits.back() == 0) {
        digits.pop_back();
    }
}
std::string ByteStream::readCString()
{
    auto it = std::find(bytes.begin(), bytes.end(), 0);
    if (it != bytes.end()) {
        std::string result(bytes.begin(), it);
        bytes.erase(bytes.begin(), it + 1);
        return result;
    }
    return "";
}

std::string ByteStream::asString() const
{
    return std::string(bytes.begin(), bytes.end());
}

std::vector<uint8_t> ByteStream::readBytes(size_t length)
{
    std::vector<uint8_t> result(bytes.begin(), bytes.begin() + length);
    bytes.erase(bytes.begin(), bytes.begin() + length);
    return result;
}

uint32_t ByteStream::readUint32()
{
    uint32_t value = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
    bytes.erase(bytes.begin(), bytes.begin() + 4);
    return value;
}

uint8_t ByteStream::readUint8()
{
    uint8_t value = bytes[0];
    bytes.erase(bytes.begin());
    return value;
}

uint16_t ByteStream::readUint16()
{
    uint16_t value = bytes[0] | (bytes[1] << 8);
    bytes.erase(bytes.begin(), bytes.begin() + 2);
    return value;
}

float ByteStream::readFloat32()
{
    float value;
    std::memcpy(&value, bytes.data(), sizeof(float));
    bytes.erase(bytes.begin(), bytes.begin() + 4);
    return value;
}

uint32_t ByteStream::seekUint32(size_t index) const
{
    size_t offset = 4 * index;
    return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (bytes[offset + 3] << 24);
}

float ByteStream::seekFloat32(size_t index) const
{
    size_t offset = 4 * index;
    float value;
    std::memcpy(&value, bytes.data() + offset, sizeof(float));
    return value;
}

osg::Matrix ByteStream::getMatrix(size_t index) const {
    size_t offset = 64 * index;  

    if (offset + 16 * sizeof(float) > bytes.size()) 
    {
        OSG_FATAL << "FATAL ERROR: Insufficient data on matrix array." << std::endl;
        throw std::runtime_error("Insufficient data to load matrix.");
    }

    osg::Matrix matrix;
    const float* data = reinterpret_cast<const float*>(bytes.data() + offset);

    // Atribui os valores diretamente aos índices da matriz
    for (int i = 0; i < 16; ++i) {
        matrix.ptr()[i] = data[i];
    }

    return matrix;
}