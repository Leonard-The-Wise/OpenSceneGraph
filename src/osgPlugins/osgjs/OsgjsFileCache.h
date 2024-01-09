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

		const std::vector<uint8_t>* getFileBuffer(const std::string& fileName) const;

		static std::string stripAllExtensions(const std::string& filename);

		bool fileExistsInDirs(const std::string& filename, std::string& realFilePath);

	private:

		std::set<std::string> _extraDirSearch;

		// Create a new file cache.
		void setCache(const std::set<std::string>& fileNames);

		bool getFileContent(const std::string& fileName, std::vector<uint8_t>& fileBuffer);
		std::map<std::string, std::vector<uint8_t>> _fileCacheInternal;
	};
}
