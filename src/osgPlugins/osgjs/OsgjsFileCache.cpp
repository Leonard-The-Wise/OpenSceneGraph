#include "pch.h"

#include "zlib.h"
#include "OsgjsFileCache.h"

using namespace osgJSONParser;

// Decompress buffer using ZLib library
static std::vector<uint8_t> decompressBuffer(const std::vector<uint8_t>& compressedData, const std::string& fileName)
{
    std::vector<uint8_t> decompressedData;

    // Queue decompress buffer size
    uLong decompressedSize = 0;
    if (uncompress(nullptr, &decompressedSize, compressedData.data(), compressedData.size()) != Z_OK) {
        OSG_WARN << "Error determining decompressed file size for [" << fileName << "]." << std::endl;
        return decompressedData; // Return empty on error
    }

    // Resize vector to decompressed size
    decompressedData.resize(decompressedSize);

    // Uncompress data
    if (uncompress(decompressedData.data(), &decompressedSize, compressedData.data(), compressedData.size()) != Z_OK) {
        OSG_WARN << "Error decompressing data for [" << fileName << "]." << std::endl;
        decompressedData.clear(); // Clear on error
    }

    return decompressedData;
}

// Decompress .gz Buffer
static std::vector<uint8_t> decompressGzBuffer(const std::vector<uint8_t>& gzBuffer, const std::string& fileName)
{
    std::vector<uint8_t> decompressedData;

    // Initializes decompress stream
    z_stream zStream{};
    zStream.zalloc = Z_NULL;
    zStream.zfree = Z_NULL;
    zStream.opaque = Z_NULL;
    zStream.avail_in = gzBuffer.size();
    zStream.next_in = const_cast<Bytef*>(gzBuffer.data());

    if (inflateInit2(&zStream, 16 + MAX_WBITS) != Z_OK) {
        OSG_WARN << "Error initializing decompress stream for '" << fileName << "'" << std::endl;
        return decompressedData;
    }

    // Loop to decompress buffer
    while (true) 
    {
        const int bufferSize = 1024;
        std::vector<uint8_t> tempBuffer(bufferSize);

        zStream.avail_out = bufferSize;
        zStream.next_out = reinterpret_cast<Bytef*>(tempBuffer.data());

        int result = inflate(&zStream, Z_NO_FLUSH);

        // File is not compressed
        if (result == Z_DATA_ERROR)
        {
            return gzBuffer;
        }

        if (result == Z_STREAM_END || zStream.avail_out > 0) 
        {
            decompressedData.insert(decompressedData.end(), tempBuffer.begin(), tempBuffer.begin() + (static_cast<size_t>(bufferSize) - zStream.avail_out));
            break;
        }
        else if (result != Z_OK) 
        {
            OSG_WARN << "Error decompressing data on '" << fileName << "'" << std::endl;
            inflateEnd(&zStream);
            return decompressedData;
        }

        decompressedData.insert(decompressedData.end(), tempBuffer.begin(), tempBuffer.end());
    }

    inflateEnd(&zStream);

    return decompressedData;
}

FileCache::FileCache(const std::set<std::string>& fileNames, const std::set<std::string>& extraDirSearch)
{
    _extraDirSearch = extraDirSearch;

    _extraDirSearch.emplace("animations/");
    _extraDirSearch.emplace("textures/");
    _extraDirSearch.emplace("background/");
    _extraDirSearch.emplace("environment/");
    setCache(fileNames);
}

bool FileCache::fileExistsInDirs(const std::string& filename, std::string& realFilePath)
{
    realFilePath = filename;

    if (osgDB::fileExists(filename))
        return true;

    for (const std::string& directory : _extraDirSearch)
    {
        std::string fullPath = osgDB::concatPaths(directory, filename);
        if (osgDB::fileExists(fullPath))
        {
            realFilePath = fullPath;
            return true;
        }
    }

    return false;
}

void FileCache::setCache(const std::set<std::string>& fileNames)
{
    _fileCacheInternal.clear();

    bool globalBroken = false;
    for (const std::string& fileName : fileNames)
    {
        bool found(false), error(false);
        std::vector<uint8_t> fileContent;

        // First, try search raw (stripped) file name, because this one we can read
        std::string fileNameStripped = stripAllExtensions(fileName) + ".bin";
        std::string realFileName;
        if (fileExistsInDirs(fileNameStripped, realFileName))
        {
            if (getFileContent(realFileName, fileContent))
            {
                found = true;
                _fileCacheInternal.emplace(fileNameStripped, fileContent);
            }
            else
                error = true;
        }

        // Second attempt. This time, try original file.
        if (!found)
        {
            if (fileExistsInDirs(fileName, realFileName))
            {
                if (getFileContent(realFileName, fileContent))
                {
                    found = true;
                    _fileCacheInternal.emplace(fileName, fileContent);
                }
                else
                    error = true;
            }
        }

        if (!found)
        {
            OSG_WARN << "WARNING: Resource file " << fileNameStripped << " not found." << std::endl;
            globalBroken = true;
        }
        else if (error)
        {
            OSG_WARN << "WARNING: Could not read " << fileNameStripped << ". Check if file is compressed or you have permissions." << std::endl;
        }
    }

    if (globalBroken)
        OSG_ALWAYS << "INFO: Consider locating missing files or your model will be incomplete." << std::endl;
}

const std::vector<uint8_t>* FileCache::getFileBuffer(const std::string& fileName) const
{
    // Look for filename with .bin extension
    std::string fileSearch = stripAllExtensions(fileName) + ".bin";
    if (_fileCacheInternal.find(fileSearch) != _fileCacheInternal.end())
        return &_fileCacheInternal.at(fileSearch);

    // Look for filename with .bin extension in extra dirs
    for (const std::string& directory : _extraDirSearch)
    {
        std::string fullPath = osgDB::concatPaths(directory, fileSearch);
        if (_fileCacheInternal.find(fullPath) != _fileCacheInternal.end())
            return &_fileCacheInternal.at(fullPath);
    }

    // Look for original name
    if (_fileCacheInternal.find(fileName) != _fileCacheInternal.end())
        return &_fileCacheInternal.at(fileName);

    // Look for original name in extra dirs
    for (const std::string& directory : _extraDirSearch)
    {
        std::string fullPath = osgDB::concatPaths(directory, fileName);
        if (_fileCacheInternal.find(fullPath) != _fileCacheInternal.end())
            return &_fileCacheInternal.at(fullPath);
    }

    // Finally, not found.
    return nullptr;
}

bool FileCache::getFileContent(const std::string& fileName, std::vector<uint8_t>& outFileContent)
{
    // Temporary: don't read compressed-encrypted binaries
    std::string ext = osgDB::getFileExtension(fileName);
    if (ext == "binz")
        return false;

    osgDB::ifstream ifs(fileName.c_str(), std::ios::binary);
    if (!ifs)
        return false;

    ifs.seekg(0, std::ios::end);
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    std::vector<uint8_t> outFileTemp(static_cast<size_t>(size));
    if (ifs.read(reinterpret_cast<char*>(outFileTemp.data()), size))
    {
        // Decompress contents if necessary
        if (ext == "gz")
            outFileContent = decompressGzBuffer(outFileTemp, fileName);
        else if (ext == "zip")
            outFileContent = decompressBuffer(outFileTemp, fileName);
        else
            outFileContent = outFileTemp;

        return true;
    }

    outFileContent.clear();
    return false;
}


std::string FileCache::stripAllExtensions(const std::string& filename)
{
    std::string finalName = filename;
    while (!osgDB::getFileExtension(finalName).empty())
    {
        std::string ext = osgDB::getFileExtension(finalName);

        // Only remove known extensions
        if (ext != "png" && ext != "gz" && ext != "bin" && ext != "binz" && ext != "zip" && ext != "bmp" && ext != "tiff" && ext != "tga" && ext != "jpg" && ext != "jpeg"
            && ext != "gif" && ext != "tgz" && ext != "pic" && ext != "pnm" && ext != "dds")
            break;

        finalName = osgDB::getStrippedName(finalName);
    }

    return finalName;
}
