#include "pch.h"

#include "OsgjsFileCache.h"

using namespace osgJSONParser;

FileCache::FileCache(const std::set<std::string>& fileNames, const std::set<std::string>& extraDirSearch)
{
    setCache(fileNames, extraDirSearch);
}

void FileCache::setCache(const std::set<std::string>& fileNames, const std::set<std::string>& extraDirSearch)
{
    _fileCacheInternal.clear();

    bool globalBroken = false;
    for (const std::string& fileName : fileNames)
    {
        bool found = false;
        std::vector<uint8_t> fileContent;

        // Primeiro, verificar no diretório atual
        if (osgDB::fileExists(fileName))
        {
            if (getFileContent(fileName, fileContent))
            {
                _fileCacheInternal.emplace(fileName, fileContent);
                found = true;
            }
        }
        else
        {
            for (const std::string& directory : extraDirSearch) {
                std::string fullPath = osgDB::concatPaths(directory, fileName);
                if (osgDB::fileExists(fullPath)) {
                    if (getFileContent(fileName, fileContent))
                    {
                        found = true;
                        _fileCacheInternal.emplace(fileName, fileContent);
                    }
                    break;
                }
            }
        }

        if (!found)
        {
            osg::notify(osg::WARN) << "WARNING: Resource file " << fileName << " not found." << std::endl;
            globalBroken = true;
        }
    }

    if (globalBroken)
        osg::notify(osg::ALWAYS) << "INFO: Consider locating missing files or your model will be incomplete." << std::endl;
}

const std::vector<uint8_t>* FileCache::getFileBuffer(const std::string& fileName) const
{
    if (_fileCacheInternal.find(fileName) == _fileCacheInternal.end())
        return nullptr;

    return &_fileCacheInternal.at(fileName);
}

bool FileCache::getBytes(const std::string& fileName, std::vector<uint8_t>& outBytes, size_t vecSize, size_t offSet) const
{
    if (_fileCacheInternal.find(fileName) == _fileCacheInternal.end())
        return false;

    if (vecSize < 0)
    {
        outBytes.resize(_fileCacheInternal.at(fileName).size());
        std::copy(_fileCacheInternal.at(fileName).begin(), _fileCacheInternal.at(fileName).end(), outBytes.begin());
        return true;
    }
    else
    {
        size_t trueSize = vecSize + offSet;
        if (_fileCacheInternal.at(fileName).size() < trueSize)
        {
            osg::notify(osg::WARN) << fileName << " has smaller size than requested buffer." << std::endl;
            return false;
        }

        outBytes.resize(vecSize);
        std::copy(_fileCacheInternal.at(fileName).begin() + offSet, _fileCacheInternal.at(fileName).begin() + offSet + vecSize, outBytes.begin());

        return true;
    }

    return false;
}


bool FileCache::getFileContent(const std::string& fileName, std::vector<uint8_t>& outFileContent)
{
    osgDB::ifstream ifs(fileName.c_str(), std::ios::binary);
    if (!ifs)
    {
        osg::notify(osg::FATAL) << "Could not read " << fileName << std::endl;
        return false;
    }

    ifs.seekg(0, std::ios::end);
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    outFileContent.resize(static_cast<size_t>(size));
    if (ifs.read(reinterpret_cast<char*>(outFileContent.data()), size)) {
        return true;
    }

    outFileContent.clear();
    return false;
}

