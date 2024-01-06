#pragma once

#include "OsgjsFileCache.h"

namespace osgJSONParser
{
	enum class DesiredVectorSize {
		Array=1, Vec2, Vec3, Vec4
	};

	class ParserHelper
	{
	public:

		enum class KeyDecodeMode {
			Vec2Compressed, Vec3Compressed, QuatCompressed
		};

		using json = nlohmann::json;

		ParserHelper() = default;

		static bool getSafeInteger(const std::string& in, int& outValue);

		static bool getSafeDouble(const std::string& in, double& outValue);

		static osg::ref_ptr<osg::Array> parseJSONArray(const json& currentJSONNode, int itemSize, const FileCache& fileCache, 
			uint32_t& magic, bool needDecodeIndices = false, GLenum drawMode = 0);

		static void makeInfluenceMap(osgAnimation::RigGeometry* rigGeometry, const osg::ref_ptr<osg::Array>& bones, const osg::ref_ptr<osg::Array>& weights,
			const std::map<int, std::string>& boneIndexes);

		static osg::ref_ptr<osg::Array> decodeVertices(const osg::ref_ptr<osg::Array>& indices, const osg::ref_ptr<osg::Array>& vertices,
			const std::vector<double>& vtx_bbl, const std::vector<double>& vtx_h);

		static osg::ref_ptr<osg::Array> decompressArray(const osg::ref_ptr<osg::Array>& keys, const osg::UserDataContainer* udc,
			KeyDecodeMode mode);

		static bool getShapeAttribute(const osg::ref_ptr<osgSim::ShapeAttributeList>& shapeAttrList, const std::string& name, double& value);
		static bool getShapeAttribute(const osg::ref_ptr<osgSim::ShapeAttributeList>& shapeAttrList, const std::string& name, int& value);
		static bool getShapeAttribute(const osg::ref_ptr<osgSim::ShapeAttributeList>& shapeAttrList, const std::string& name, std::string& value);

		static GLenum getModeFromString(const std::string& mode);

		static osg::BlendFunc::BlendFuncMode getBlendFuncFromString(const std::string& blendfunc);

		static osg::Texture::FilterMode getFilterModeFromString(const std::string& blendfunc);

		static osg::Texture::WrapMode getWrapModeFromString(const std::string& filterMode);

		static osg::CullFace::Mode getCullFaceModeFromString(const std::string& cullFaceMode);

		static osgText::Text::AlignmentType getTextAlignmentFromString(const std::string& textAlignment);

		static std::string stripAllExtensions(const std::string& filename);

	private:
		static bool getPrimitiveType(const json& currentJSONNode, osg::PrimitiveSet::Type& outPrimitiveType);
		static inline int64_t varintSigned(uint64_t input);
		template <typename T>
		static void copyIntToByteVector(T value, std::vector<uint8_t>& vec);
		static uint64_t decodeVarInt(const uint8_t* const data, int& decoded_bytes);
		static std::vector<uint8_t>* decodeVarintVector(const std::vector<uint8_t>& input, osg::Array::Type inputType, size_t itemCount, size_t offSet);
		static osg::Array* getVertexAttribArray(osgAnimation::RigGeometry& rigGeometry, const std::string arrayName);

		static osg::ref_ptr<osg::Array> recastArray(const osg::ref_ptr<osg::Array>& toRecast, DesiredVectorSize vecSize);
		static osg::ref_ptr<osg::Array> decastVector(const osg::ref_ptr<osg::Array>& toRecast);

		template <typename T>
		static std::vector<T> decodeDelta(const std::vector<T>& input, int e);
		template <typename T>
		static std::vector<T> decodeImplicit(const std::vector<T>& t, int n);
		template <typename T>
		static std::vector<T> decodeWatermark(const std::vector<T>& t, uint32_t& magic);
		template<typename T, typename U>
		static osg::ref_ptr<osg::Array> decodePredict(const osg::ref_ptr<T>& indices, const osg::ref_ptr<U>& vertices, int itemSize);
		template <typename T>
		static osg::ref_ptr<osg::Array> decodeQuantize(const osg::ref_ptr<T>& vertices, const std::vector<double>& vtx_bbl,
			const std::vector<double>& vtx_h, int elementSize);

		template <typename T>
		static osg::ref_ptr<T> deInterleaveKeys(const osg::ref_ptr<T>& input, unsigned int itemSize);

		template <typename T>
		static osg::ref_ptr<osg::DoubleArray> inflateKeys1(const osg::ref_ptr<T>& input, unsigned int itemSize,
			const std::vector<double>& attrB, const std::vector<double>& attrH);

		static osg::ref_ptr<osg::DoubleArray> inflateKeys2(const osg::ref_ptr<osg::DoubleArray>& input, unsigned int itemSize);

		template <typename T>
		static osg::ref_ptr<osg::DoubleArray> inflateKeysQuat(const osg::ref_ptr<T>& input);

		template <typename T>
		static osg::ref_ptr<osg::DoubleArray> int3ToFloat4(const osg::ref_ptr<T>& input, double epsilon, double nphi, int itemSize);

		template<typename T>
		osg::ref_ptr<osg::DoubleArray> int2ToFloat3(const osg::ref_ptr<T>& input, double epsilon, double nphi, int itemSize);
	};
}