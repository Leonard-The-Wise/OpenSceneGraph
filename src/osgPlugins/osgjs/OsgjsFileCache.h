#pragma once


namespace osgJSONParser
{
	class FileCache
	{
	public:

		FileCache() {};

		FileCache(const std::set<std::string>& fileNames, const std::set<std::string>& extraDirSearch = {});

		~FileCache()
		{
			_fileCacheInternal.clear();
		}

		// Create a new file cache.
		void setCache(const std::set<std::string>& fileNames, const std::set<std::string>& extraDirSearch = {});

		std::vector<uint8_t>* getFileBuffer(const std::string& fileName);

		// Get bytes from cache. vecSize == -1 means to get the entire buffer.
		bool getBytes(const std::string& fileName, std::vector<uint8_t>& outBytes, size_t vecSize = -1, size_t offSet = 0) const;

	private:
		bool getFileContent(const std::string& fileName, std::vector<uint8_t>& fileBuffer);
		std::map<std::string, std::vector<uint8_t>> _fileCacheInternal;
	};
}
