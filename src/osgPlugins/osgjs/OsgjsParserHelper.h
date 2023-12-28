#pragma once


namespace osgJSONParser
{
	enum class DesiredVectorSize {
		Array=1, Vec2, Vec3, Vec4
	};

	class ParserHelper
	{
	public:

		using json = nlohmann::json;

		ParserHelper() = default;

		static bool getSafeInteger(const std::string& in, int& outValue);

		static bool getSafeDouble(const std::string& in, double& outValue);

		static osg::ref_ptr<osg::Array> recastArray(const osg::ref_ptr<osg::Array> toRecast, osg::Array::Type arrayType, DesiredVectorSize vecSize);

		static osg::ref_ptr<osg::Array> parseJSONArray(const json& currentJSONNode, int itemSize, FileCache& fileCache);

		static GLenum getModeFromString(const std::string& mode);

		static osg::BlendFunc::BlendFuncMode getBlendFuncFromString(const std::string& blendfunc);

		static osg::Texture::FilterMode getFilterModeFromString(const std::string& blendfunc);

		osg::Texture::WrapMode getWrapModeFromString(const std::string& filterMode);

		osgText::Text::AlignmentType getTextAlignmentFromString(const std::string& textAlignment);

		static void makeInfluenceMap(osgAnimation::RigGeometry* rigGeometry, const osg::ref_ptr<osg::Array> bones, const osg::ref_ptr<osg::Array> weights, 
			const std::map<int, std::string>& boneIndexes);

	private:
		static bool getPrimitiveType(const json& currentJSONNode, osg::PrimitiveSet::Type& outPrimitiveType);
		template <typename T>
		static void copyIntToByteVector(T value, std::vector<uint8_t>& vec);
		static uint32_t ParserHelper::decodeVarInt(const uint8_t* const data, int& decoded_bytes);
		static std::vector<uint8_t>* decodeVarintVector(const std::vector<uint8_t>& input, osg::Array::Type inputType, size_t itemCount, size_t offSet);
		static osg::Array* getVertexAttribArray(osgAnimation::RigGeometry& rigGeometry, const std::string arrayName);
	};
}