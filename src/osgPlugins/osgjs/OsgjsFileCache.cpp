#include "pch.h"

#include "OsgjsFileCache.h"

using namespace osgJSONParser;

FileCache::FileCache(const std::set<std::string>& fileNames, const std::set<std::string>& extraDirSearch)
{
    _extraDirSearch = extraDirSearch;

    _extraDirSearch.emplace("animations/");
    _extraDirSearch.emplace("textures/");
    setCache(fileNames);
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
        std::string fileSearch = stripAllExtensions(fileName) + ".bin";
        if (osgDB::fileExists(fileSearch))
        {
            if (getFileContent(fileSearch, fileContent))
            {
                found = true;
                _fileCacheInternal.emplace(fileSearch, fileContent);
            }
            else
                error = true;
        }
        else
        {
            for (const std::string& directory : _extraDirSearch) 
            {
                std::string fullPath = osgDB::concatPaths(directory, fileSearch);
                if (osgDB::fileExists(fullPath)) {
                    if (getFileContent(fullPath, fileContent))
                    {
                        found = true;
                        _fileCacheInternal.emplace(fileSearch, fileContent);
                    }
                    else
                        error = true;

                    break;
                }
            }
        }

        // Second attempt. This time, try original file.
        if (!found)
        {
            if (osgDB::fileExists(fileName))
            {
                if (getFileContent(fileName, fileContent))
                {
                    found = true;
                    _fileCacheInternal.emplace(fileName, fileContent);
                }
                else
                    error = true;
            }
            else
            {
                for (const std::string& directory : _extraDirSearch)
                {
                    std::string fullPath = osgDB::concatPaths(directory, fileName);
                    if (osgDB::fileExists(fullPath)) {
                        if (getFileContent(fullPath, fileContent))
                        {
                            found = true;
                            _fileCacheInternal.emplace(fileName, fileContent);
                        }
                        else
                            error = true;

                        break;
                    }
                }
            }
        }

        if (!found && !error)
        {
            OSG_WARN << "WARNING: Resource file " << fileName << " not found." << std::endl;
            globalBroken = true;
        }
        else if (error)
        {
            OSG_WARN << "WARNING: Could not read " << fileName << ". Check if file is compressed or you have permissions." << std::endl;
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

//bool FileCache::getBytes(const std::string& fileName, std::vector<uint8_t>& outBytes, size_t vecSize, size_t offSet) const
//{
//    if (_fileCacheInternal.find(fileName) == _fileCacheInternal.end())
//        return false;
//
//    if (vecSize < 0)
//    {
//        outBytes.resize(_fileCacheInternal.at(fileName).size());
//        std::copy(_fileCacheInternal.at(fileName).begin(), _fileCacheInternal.at(fileName).end(), outBytes.begin());
//        return true;
//    }
//    else
//    {
//        size_t trueSize = vecSize + offSet;
//        if (_fileCacheInternal.at(fileName).size() < trueSize)
//        {
//            osg::notify(osg::WARN) << fileName << " has smaller size than requested buffer." << std::endl;
//            return false;
//        }
//
//        outBytes.resize(vecSize);
//        std::copy(_fileCacheInternal.at(fileName).begin() + offSet, _fileCacheInternal.at(fileName).begin() + offSet + vecSize, outBytes.begin());
//
//        return true;
//    }
//
//    return false;
//}

bool FileCache::getFileContent(const std::string& fileName, std::vector<uint8_t>& outFileContent)
{
    // Temporary: don't read compressed binaries
    std::string ext = osgDB::getFileExtension(fileName);
    if (ext == "binz" || ext == "gz" || ext == "zip")
        return false;

    osgDB::ifstream ifs(fileName.c_str(), std::ios::binary);
    if (!ifs)
        return false;

    ifs.seekg(0, std::ios::end);
    std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    outFileContent.resize(static_cast<size_t>(size));
    if (ifs.read(reinterpret_cast<char*>(outFileContent.data()), size)) 
    {
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
        finalName = osgDB::getStrippedName(finalName);
    }

    return finalName;
}
