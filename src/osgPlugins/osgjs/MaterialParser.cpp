
#include "pch.h"

#include "MaterialParser.h"
#include "jpcre2.hpp"

const std::string COMMENT = R"(^\/\/.+)";
const std::string MESHNAME = R"(^Mesh \"(?'MeshName'\w+)\" uses material \"(?'MaterialName'\w+)\" and has UniqueID \"(?'UniqueID'\d+)\")";
const std::string MATERIALNAME = R"(^Material \"(?'MaterialName'\w+)\" has ID (?'ID'[\w-]+))";
const std::string MATERIALLINE = R"(^\t(?'TextureLayerName'[\w\s]*?)(\s*+(\((?'TexCoord'UV\d+)\)))?(\s*+(\((?'Parameter'[\w\s\d=,]*)\)))*+:\s(?'FileOrParam'[\w.,+-|]*)";

const jpcre2::select<char>::Regex commentRegEx(COMMENT, PCRE2_FIRSTLINE, jpcre2::JIT_COMPILE);
const jpcre2::select<char>::Regex meshNameRegEx(MESHNAME, PCRE2_FIRSTLINE, jpcre2::JIT_COMPILE);
const jpcre2::select<char>::Regex materialNameRegEx(MATERIALNAME, PCRE2_FIRSTLINE, jpcre2::JIT_COMPILE);
const jpcre2::select<char>::Regex materialLineRegEx(MATERIALLINE, PCRE2_FIRSTLINE, jpcre2::JIT_COMPILE);

typedef jpcre2::select<char> pcre2;

using namespace osgJSONParser;

bool MaterialFile::readMaterialFile(const std::string& filePath)
{
	std::string fileName = osgDB::findDataFile(filePath);
	pcre2::VecNas captureGroup;

	pcre2::RegexMatch regexMatch;
	regexMatch.addModifier("gm");
	regexMatch.setNamedSubstringVector(&captureGroup);

	osgDB::ifstream ifs(fileName.c_str());

	if (ifs.is_open())
	{
		std::string line;
		while (std::getline(ifs, line))
		{
			if (line.empty())
				continue;

			regexMatch.setSubject(line);

			// First try parse as comment
			regexMatch.setRegexObject(&commentRegEx);
			size_t count = regexMatch.match();

			if (count > 0)
				continue;

			// Try parse as mesh
			regexMatch.setRegexObject(&meshNameRegEx);
			count = regexMatch.match();

			if (count > 0)
			{
				MeshInfo newMesh;
				newMesh.MeshName = captureGroup[0]["MeshName"];
				newMesh.MaterialName = captureGroup[0]["MaterialName"];
				newMesh.UniqueID = stoi(captureGroup[0]["UniqueID"]);

				continue;
			}

			// Try parse as Material
			regexMatch.setRegexObject(&materialNameRegEx);
			count = regexMatch.match();

			if (count > 0)
			{
				MaterialInfo newMaterial;
				newMaterial.ID = captureGroup[0]["ID"];
				newMaterial.Name = captureGroup[0]["MaterialName"];

				// Try to build materialList
				regexMatch.setRegexObject(&materialLineRegEx);

				// Doing double read, but since file has an empty line between objects this is not a problem.
				while (count > 0 && std::getline(ifs, line))
				{
					regexMatch.setSubject(line);
					count = regexMatch.match();

					if (count > 0)
					{
						// Try to find parameter in parameters map.
						if (newMaterial.KnownLayerNames.find(captureGroup[0]["TextureLayerName"]) != newMaterial.KnownLayerNames.end())
						{
							if (!captureGroup[0]["TexCoord"].empty())
							{
								newMaterial.KnownLayerNames.at(captureGroup[0]["TextureLayerName"]) = captureGroup[0]["FileOrParam"];
							}
						}
						else
						{
							OSG_DEBUG << "Found unknown texture parameter: " << captureGroup[0]["TextureLayerName"] << std::endl;
						}
					}
				}
			}
		}

		ifs.close();
	}
	else
	{
		OSG_WARN << "WARNING: Could not open " << filePath << std::endl;
		return false;
	}

	return true;
}
