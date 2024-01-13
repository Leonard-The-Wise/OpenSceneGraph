#include "pch.h"

#include "OsgjsFileCache.h"

using namespace osgJSONParser;

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
