#include "pch.h"

#include "OsgjsFileCache.h"
#include "OsgjsParserHelper.h"


using namespace osgJSONParser;
using namespace osg;

using json = nlohmann::json;

constexpr auto IMPLICIT_HEADER_LENGTH = 3;
constexpr auto IMPLICIT_HEADER_MASK_LENGTH = 1;
constexpr auto IMPLICIT_HEADER_PRIMITIVE_LENGTH = 0;
constexpr auto IMPLICIT_HEADER_EXPECTED_INDEX = 2;
constexpr auto HIGH_WATERMARK = 2;

const std::unordered_map<std::string, PrimitiveSet::Type> primitiveTypeMap 
{
	{"DrawElementsUShort", PrimitiveSet::DrawArraysPrimitiveType},
	{"DrawArrays", PrimitiveSet::DrawArraysPrimitiveType},
	{"DrawElementsUInt", PrimitiveSet::DrawElementsUIntPrimitiveType},
	{"DrawElementsUShort", PrimitiveSet::DrawElementsUShortPrimitiveType},
	{"DrawElementsUByte", PrimitiveSet::DrawElementsUBytePrimitiveType},
	{"DrawArrayLengths", PrimitiveSet::DrawArrayLengthsPrimitiveType},
};

// PUBLIC METHODS

bool ParserHelper::getSafeInteger(const std::string& in, int& outValue)
{
	if (in.empty())
		return false;

	try
	{
		char* endPtr = nullptr;
		outValue = std::strtol(in.c_str(), &endPtr, 10);

		return *endPtr == '\0' && endPtr != in.c_str();
	}
	catch (const std::invalid_argument&)
	{
		return false;
	}
	catch (const std::out_of_range&)
	{
		OSG_WARN << "Warning, integer parameter out of range" << std::endl;
		return false;
	}
}

bool ParserHelper::getSafeDouble(const std::string& in, double& outValue)
{
	if (in.empty())
		return false;

	try
	{
		char* endPtr = nullptr;
		outValue = std::strtod(in.c_str(), &endPtr);

		return *endPtr == '\0' && endPtr != in.c_str();
	}
	catch (const std::exception&)
	{
		return false;
	}
	return true;
}

ref_ptr<Array> ParserHelper::parseJSONArray(const json& currentJSONNode, int elementsPerItem, const FileCache& fileCache,
	uint32_t& magic, bool needDecodeIndices, GLenum drawMode)
{
#ifdef DEBUG
	std::string CurrentNode = currentJSONNode.dump();
#endif
	ref_ptr<Array> returnArray;
	Array::Type arrayType{};
	const json* elementsNode = nullptr;
	int elementTypeSize = 0;

	if (elementsPerItem < 1 || elementsPerItem > 4)
	{
		OSG_WARN << "WARNING: Error importing array. Field 'ItemSize' not between 1 and 4. Ignoring..." << std::endl;
		return nullptr;
	}

	// 1) Determine Array Elements type
	if (currentJSONNode.contains("Float64Array") && currentJSONNode["Float64Array"].is_object())
	{
		returnArray = new DoubleArray;
		arrayType = Array::DoubleArrayType;
		elementTypeSize = sizeof(double);
		elementsNode = &currentJSONNode["Float64Array"];
	}
	else if (currentJSONNode.contains("Float32Array") && currentJSONNode["Float32Array"].is_object())
	{ 
		returnArray = new FloatArray;
		arrayType = Array::FloatArrayType;
		elementTypeSize = sizeof(float);
		elementsNode = &currentJSONNode["Float32Array"];
	}
	else if (currentJSONNode.contains("Uint8Array") && currentJSONNode["Uint8Array"].is_object())
	{
		returnArray = new UByteArray;
		arrayType = Array::UByteArrayType;
		elementTypeSize = sizeof(uint8_t);
		elementsNode = &currentJSONNode["Uint8Array"];
	}
	else if (currentJSONNode.contains("Uint8ClampedArray") && currentJSONNode["Uint8ClampedArray"].is_object())
	{
		returnArray = new UByteArray;
		arrayType = Array::UByteArrayType;
		elementTypeSize = sizeof(uint8_t);
		elementsNode = &currentJSONNode["Uint8ClampedArray"];
	}
	else if (currentJSONNode.contains("Uint16Array") && currentJSONNode["Uint16Array"].is_object())
	{
		returnArray = new UShortArray;
		arrayType = Array::UShortArrayType;
		elementTypeSize = sizeof(uint16_t);
		elementsNode = &currentJSONNode["Uint16Array"];
	}
	else if (currentJSONNode.contains("Uint32Array") && currentJSONNode["Uint32Array"].is_object())
	{
		returnArray = new UIntArray;
		arrayType = Array::UIntArrayType;
		elementTypeSize = sizeof(uint32_t);
		elementsNode = &currentJSONNode["Uint32Array"];
	}
	else if (currentJSONNode.contains("Uint64Array") && currentJSONNode["Uint64Array"].is_object())
	{
		returnArray = new UInt64Array;
		arrayType = Array::UInt64ArrayType;
		elementTypeSize = sizeof(uint64_t);
		elementsNode = &currentJSONNode["Uint64Array"];
	}
	else if (currentJSONNode.contains("Int8Array") && currentJSONNode["Int8Array"].is_object())
	{
		returnArray = new ByteArray;
		arrayType = Array::ByteArrayType;
		elementTypeSize = sizeof(int8_t);
		elementsNode = &currentJSONNode["Int8Array"];
	}
	else if (currentJSONNode.contains("Int16Array") && currentJSONNode["Int16Array"].is_array())
	{
		returnArray = new ShortArray;
		arrayType = Array::ShortArrayType;
		elementTypeSize = sizeof(int16_t);
		elementsNode = &currentJSONNode["Int16Array"];
	}
	else if (currentJSONNode.contains("Int32Array") && currentJSONNode["Int32Array"].is_object())
	{
		returnArray = new IntArray;
		arrayType = Array::IntArrayType;
		elementTypeSize = sizeof(int32_t);
		elementsNode = &currentJSONNode["Int32Array"];
	}
	else if (currentJSONNode.contains("Int64Array") && currentJSONNode["Int64Array"].is_object())
	{
		returnArray = new Int64Array;
		arrayType = Array::Int64ArrayType;
		elementTypeSize = sizeof(int64_t);
		elementsNode = &currentJSONNode["Int64Array"];
	}


	// 2) Determine Write Mode: inline or file

	// 2.1) Inline arrays
	if (elementsNode && (*elementsNode).contains("Elements") && (*elementsNode)["Elements"].is_array())
	{
		returnArray->reserveArray((*elementsNode)["Elements"].size());

		switch (arrayType)
		{
		case Array::FloatArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<FloatArray*>(returnArray.get())->push_back(element.get<float>());
			break;
		case Array::UByteArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<UByteArray*>(returnArray.get())->push_back(element.get<uint8_t>());
			break;
		case Array::UShortArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<UShortArray*>(returnArray.get())->push_back(element.get<uint16_t>());
			break;
		case Array::UIntArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<UIntArray*>(returnArray.get())->push_back(element.get<uint32_t>());
			break;
		case Array::ByteArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<ByteArray*>(returnArray.get())->push_back(element.get<int8_t>());
			break;
		case Array::ShortArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<ShortArray*>(returnArray.get())->push_back(element.get<int16_t>());
			break;
		case Array::IntArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<IntArray*>(returnArray.get())->push_back(element.get<int32_t>());
			break;
		default:
			OSG_WARN << "WARNING: Unknown Array Type." << std::endl;
			return nullptr;
		}
	}

	// 2.2) File Mode
	else if (elementsNode && (*elementsNode).contains("File") && (*elementsNode)["File"].get<std::string>() != "")
	{
		std::string fileName = (*elementsNode)["File"].get<std::string>();
		int itemCount = (*elementsNode).contains("Size") ? (*elementsNode)["Size"].get<int>() : 0;
		if (itemCount == 0)
			return nullptr;

		int readOffset = (*elementsNode).contains("Offset") ? (*elementsNode)["Offset"].get<int>() : 0;
		int totalElements = itemCount * elementsPerItem;
		size_t totalBytesSize = static_cast<size_t>(totalElements * elementTypeSize + readOffset);
		const std::vector<uint8_t>* elementsBytes = fileCache.getFileBuffer(fileName);
		const std::vector<uint8_t>* elementsBytesConverted = nullptr;
		
		if (elementsBytes && !elementsBytes->empty())
		{
			// Verify size - only valid for non-compressed items
			if ((elementsBytes->size() < totalBytesSize) && !(*elementsNode).contains("Encoding"))
			{
				OSG_WARN << "WARNING: Error reading " << fileName << ". " <<
					"File has incorrect size. [expected = " << totalBytesSize << ", found = " << elementsBytes->size() << "]" << std::endl;
				return nullptr;
			}

			// Decode array if necessary
			if ((*elementsNode).contains("Encoding") && (*elementsNode)["File"].get<std::string>() != "varint")
			{
				elementsBytesConverted = decodeVarintVector(*elementsBytes, arrayType, static_cast<size_t>(itemCount * elementsPerItem), readOffset);
				elementsBytes = elementsBytesConverted;
				readOffset = 0;
			}

			// Decode indexes when necessary
			if (needDecodeIndices && !elementsBytes->empty())
			{
				int k = 0;
				bool decodeFail = false;

				switch (drawMode)
				{
				case GL_LINES:
				{
					break;
				}
				case GL_TRIANGLE_STRIP:
				{
					switch (arrayType) // We only deal with 3 types of arrays for indexes: UByteArray, UShortArray, UIntArray.
					{
					case Array::UByteArrayType:
					{
						std::vector<uint8_t> elementsDecodeCopy = std::vector<uint8_t>(elementsBytes->begin() + readOffset, elementsBytes->end());
						k = IMPLICIT_HEADER_LENGTH + static_cast<int>(elementsDecodeCopy[IMPLICIT_HEADER_MASK_LENGTH]);
						elementsDecodeCopy = decodeDelta<uint8_t>(elementsDecodeCopy, k);
						elementsDecodeCopy = decodeImplicit<uint8_t>(elementsDecodeCopy, k);
						if (elementsDecodeCopy.empty())
						{
							decodeFail = true;
							break;
						}
						elementsDecodeCopy = decodeWatermark<uint8_t>(elementsDecodeCopy, magic);

						for (auto& element : elementsDecodeCopy)
							dynamic_cast<UByteArray*>(returnArray.get())->push_back(element);

						break;
					}

					case Array::UShortArrayType:
					{
						const uint16_t* shortData = reinterpret_cast<const uint16_t*>(elementsBytes->data());
						std::vector<uint16_t> elementsDecodeCopy;
						for (size_t i = 0; i < totalElements; ++i)
							elementsDecodeCopy.push_back(shortData[i]);

						k = IMPLICIT_HEADER_LENGTH + static_cast<int>(elementsDecodeCopy[IMPLICIT_HEADER_MASK_LENGTH]);
						elementsDecodeCopy = decodeDelta<uint16_t>(elementsDecodeCopy, k);
						elementsDecodeCopy = decodeImplicit<uint16_t>(elementsDecodeCopy, k);
						if (elementsDecodeCopy.empty())
						{
							decodeFail = true;
							break;
						}
						elementsDecodeCopy = decodeWatermark<uint16_t>(elementsDecodeCopy, magic);

						for (auto& element : elementsDecodeCopy)
							dynamic_cast<UShortArray*>(returnArray.get())->push_back(element);

						break;
					}
					case Array::UIntArrayType:
					{
						const uint32_t* intData = reinterpret_cast<const uint32_t*>(elementsBytes->data());
						std::vector<uint32_t> elementsDecodeCopy;
						for (size_t i = 0; i < totalElements; ++i)
							elementsDecodeCopy.push_back(intData[i]);

						k = IMPLICIT_HEADER_LENGTH + static_cast<int>(elementsDecodeCopy[IMPLICIT_HEADER_MASK_LENGTH]);
						elementsDecodeCopy = decodeDelta<uint32_t>(elementsDecodeCopy, k); 
						elementsDecodeCopy = decodeImplicit<uint32_t>(elementsDecodeCopy, k);
						if (elementsDecodeCopy.empty())
						{
							decodeFail = true;
							break;
						}
						elementsDecodeCopy = decodeWatermark<uint32_t>(elementsDecodeCopy, magic);

						for (auto& element : elementsDecodeCopy)
							dynamic_cast<UIntArray*>(returnArray.get())->push_back(element);

						break;
					}
					default:
					{
						OSG_WARN << "WARNING: Unsuported indices array!" << std::endl;
						return nullptr;
					}
					}
					break;
				}
				case GL_TRIANGLES:
				{
					switch (arrayType) // We only deal with 3 types of arrays for indexes
					{
					case Array::UByteArrayType:
					{
						const uint8_t* shortData = reinterpret_cast<const uint8_t*>(elementsBytes->data());
						std::vector<uint8_t> elementsDecodeCopy;
						for (size_t i = 0; i < totalElements; ++i)
							elementsDecodeCopy.push_back(shortData[i]);

						elementsDecodeCopy = decodeDelta<uint8_t>(elementsDecodeCopy, k);
						elementsDecodeCopy = decodeWatermark<uint8_t>(elementsDecodeCopy, magic);

						for (auto& element : elementsDecodeCopy)
							dynamic_cast<UByteArray*>(returnArray.get())->push_back(element);

						break;
					}
					case Array::UShortArrayType:
					{
						const uint16_t* shortData = reinterpret_cast<const uint16_t*>(elementsBytes->data());
						std::vector<uint16_t> elementsDecodeCopy;
						for (size_t i = 0; i < totalElements; ++i)
							elementsDecodeCopy.push_back(shortData[i]);

						elementsDecodeCopy = decodeDelta<uint16_t>(elementsDecodeCopy, k);
						elementsDecodeCopy = decodeWatermark<uint16_t>(elementsDecodeCopy, magic);

						for (auto& element : elementsDecodeCopy)
							dynamic_cast<UShortArray*>(returnArray.get())->push_back(element);

						break;
					}
					case Array::UIntArrayType:
					{
						const uint32_t* intData = reinterpret_cast<const uint32_t*>(elementsBytes->data());
						std::vector<uint32_t> elementsDecodeCopy;
						for (size_t i = 0; i < totalElements; ++i)
							elementsDecodeCopy.push_back(intData[i]);

						elementsDecodeCopy = decodeDelta<uint32_t>(elementsDecodeCopy, k);
						elementsDecodeCopy = decodeWatermark<uint32_t>(elementsDecodeCopy, magic);

						for (auto& element : elementsDecodeCopy)
							dynamic_cast<UIntArray*>(returnArray.get())->push_back(element);

						break;
					}
					}
					break;
				}
				default:
				{
					OSG_WARN << "WARNING: Incompatible draw mode for indices decoding." << std::endl;
					return nullptr;
				}
				}

				if (!decodeFail)
					return recastArray(returnArray, static_cast<DesiredVectorSize>(elementsPerItem));
			}

			// Read and Copy bytes
			bool readFail = false;
			returnArray->reserveArray(totalElements);
			switch (arrayType)
			{
			case Array::FloatArrayType:
			{
				const float* floatData = reinterpret_cast<const float*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<FloatArray*>(returnArray.get())->push_back(floatData[i]);
				}
				break;
			}
			case Array::ByteArrayType:
			{
				const int8_t* byteData = reinterpret_cast<const int8_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<ByteArray*>(returnArray.get())->push_back(byteData[i]);
				}
				break;
			}
			case Array::UByteArrayType:
			{
				const uint8_t* ubyteData = reinterpret_cast<const uint8_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<UByteArray*>(returnArray.get())->push_back(ubyteData[i]);
				}
				break;
			}
			case Array::ShortArrayType:
			{
				const int16_t* shortData = reinterpret_cast<const int16_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<ShortArray*>(returnArray.get())->push_back(shortData[i]);
				}
				break;
			}
			case Array::UShortArrayType:
			{
				const uint16_t* ushortData = reinterpret_cast<const uint16_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<UShortArray*>(returnArray.get())->push_back(ushortData[i]);
				}
				break;
			}
			case Array::IntArrayType:
			{
				const int32_t* intData = reinterpret_cast<const int32_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<IntArray*>(returnArray.get())->push_back(intData[i]);
				}
				break;
			}
			case Array::UIntArrayType:
			{
				const uint16_t* uintData = reinterpret_cast<const uint16_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<UIntArray*>(returnArray.get())->push_back(uintData[i]);
				}
				break;
			}
			default:
				OSG_WARN << "WARNING: Unknown Array Type." << std::endl;
				return nullptr;
			}

			if (elementsBytesConverted)
				delete elementsBytesConverted;

			if (readFail)
			{
				OSG_WARN << "WARNING: File Array have incorrect size." << std::endl;
				return nullptr;
			}
		}
	}

	// 3) Convert Element nodes to Vectors if it applies.
	if (returnArray && elementsPerItem > 1)
	{
		returnArray = recastArray(returnArray, static_cast<DesiredVectorSize>(elementsPerItem));
	}

	return returnArray;
}

void ParserHelper::makeInfluenceMap(osgAnimation::RigGeometry* rigGeometry, const ref_ptr<Array>& bones, const ref_ptr<Array>& weights,
	const std::map<int, std::string>& boneIndexes)
{
	ref_ptr<osgAnimation::VertexInfluenceMap> influenceMap = new osgAnimation::VertexInfluenceMap;

	// The most common type [not sure if it has others]
	ref_ptr<Vec4usArray> bonesVec = dynamic_pointer_cast<Vec4usArray>(bones);
	ref_ptr<Vec4Array> weightsVec = dynamic_pointer_cast<Vec4Array>(weights);

	if (bones && !bonesVec)
	{
		OSG_WARN << "WARNING: Unsuported bones array for RigGeometry. Must be Vec4usArray type. " << rigGeometry->getName() << std::endl;
		return;
	}

	if (weights && !weightsVec)
	{
		OSG_WARN << "WARNING: Unsuported weights for RigGeometry. Must be Vec4Array type. " << rigGeometry->getName() << std::endl;
		return;
	}


	if (!bonesVec && !weightsVec)
		return;

	if (!bonesVec || !weightsVec)
	{
		OSG_WARN << "WARNING: Missing either bones or weights array for RigGeometry " << rigGeometry->getName() << std::endl;
		return;
	}

	if (bonesVec->getNumElements() != weightsVec->getNumElements())
	{
		OSG_WARN << "WARNING: Number of bone indices don't match number of weight indices for RigGeometry " << rigGeometry->getName() << std::endl;
		return;
	}

	// Build influence map
	int elementSize = bones->getDataSize();
	for (unsigned int vertexIndex = 0; vertexIndex < bonesVec->getNumElements(); ++vertexIndex)
	{
		const Vec4us& boneIndices = (*bonesVec)[vertexIndex];
		const Vec4& boneWeights = (*weightsVec)[vertexIndex];

		for (int boneIndex = 0; boneIndex < elementSize; ++boneIndex)
		{
			uint16_t boneID = boneIndices[boneIndex];
			float weight = boneWeights[boneIndex];

			if (weight > 0.0f)
			{
				if (boneIndexes.find(boneID) == boneIndexes.end())
				{
					OSG_WARN << "WARNING: Bone index " << boneID << " not found! [" << rigGeometry->getName() << "]" << std::endl;
					continue;
				}
				std::string boneName = boneIndexes.at(boneID);

				(*influenceMap)[boneName].push_back(std::make_pair(vertexIndex, weight));
			}
		}
	}

	rigGeometry->setInfluenceMap(influenceMap);
}

osg::ref_ptr<osg::Array> ParserHelper::decodeVertices(const osg::ref_ptr<osg::Array>& indices, const osg::ref_ptr<osg::Array>& vertices,
	const std::vector<double>& vtx_bbl, const std::vector<double>& vtx_h)
{
	// Decast vertices to array.
	osg::ref_ptr<osg::Array> verticesConverted = ParserHelper::recastArray(vertices, DesiredVectorSize::Array);
	int elementSize = vertices->getDataSize();

	// Decode Predict
	switch (indices->getType())
	{
	case Array::UIntArrayType:
		switch (verticesConverted->getType())
		{
		case Array::UIntArrayType:
			verticesConverted = decodePredict<UIntArray, UIntArray>(dynamic_pointer_cast<UIntArray>(indices), dynamic_pointer_cast<UIntArray>(verticesConverted), elementSize);
			break;
		case Array::UShortArrayType:
			verticesConverted = decodePredict<UIntArray, UShortArray>(dynamic_pointer_cast<UIntArray>(indices), dynamic_pointer_cast<UShortArray>(verticesConverted), elementSize);
			break;
		case Array::UByteArrayType:
			verticesConverted = decodePredict<UIntArray, UByteArray>(dynamic_pointer_cast<UIntArray>(indices), dynamic_pointer_cast<UByteArray>(verticesConverted), elementSize);
			break;
		case Array::IntArrayType:
			verticesConverted = decodePredict<UIntArray, IntArray>(dynamic_pointer_cast<UIntArray>(indices), dynamic_pointer_cast<IntArray>(verticesConverted), elementSize);
			break;
		case Array::ShortArrayType:
			verticesConverted = decodePredict<UIntArray, ShortArray>(dynamic_pointer_cast<UIntArray>(indices), dynamic_pointer_cast<ShortArray>(verticesConverted), elementSize);
			break;
		case Array::ByteArrayType:
			verticesConverted = decodePredict<UIntArray, ByteArray>(dynamic_pointer_cast<UIntArray>(indices), dynamic_pointer_cast<ByteArray>(verticesConverted), elementSize);
			break;
		default:
			return nullptr;
		}
		break;
	case Array::UShortArrayType:
		switch (verticesConverted->getType())
		{
		case Array::UIntArrayType:
			verticesConverted = decodePredict<UShortArray, UIntArray>(dynamic_pointer_cast<UShortArray>(indices), dynamic_pointer_cast<UIntArray>(verticesConverted), elementSize);
			break;
		case Array::UShortArrayType:
			verticesConverted = decodePredict<UShortArray, UShortArray>(dynamic_pointer_cast<UShortArray>(indices), dynamic_pointer_cast<UShortArray>(verticesConverted), elementSize);
			break;
		case Array::UByteArrayType:
			verticesConverted = decodePredict<UShortArray, UByteArray>(dynamic_pointer_cast<UShortArray>(indices), dynamic_pointer_cast<UByteArray>(verticesConverted), elementSize);
			break;
		case Array::IntArrayType:
			verticesConverted = decodePredict<UShortArray, IntArray>(dynamic_pointer_cast<UShortArray>(indices), dynamic_pointer_cast<IntArray>(verticesConverted), elementSize);
			break;
		case Array::ShortArrayType:
			verticesConverted = decodePredict<UShortArray, ShortArray>(dynamic_pointer_cast<UShortArray>(indices), dynamic_pointer_cast<ShortArray>(verticesConverted), elementSize);
			break;
		case Array::ByteArrayType:
			verticesConverted = decodePredict<UShortArray, ByteArray>(dynamic_pointer_cast<UShortArray>(indices), dynamic_pointer_cast<ByteArray>(verticesConverted), elementSize);
			break;
		default:
			return nullptr;
		}
		break;
	case Array::UByteArrayType:
		switch (verticesConverted->getType())
		{
		case Array::UIntArrayType:
			verticesConverted = decodePredict<UByteArray, UIntArray>(dynamic_pointer_cast<UByteArray>(indices), dynamic_pointer_cast<UIntArray>(verticesConverted), elementSize);
			break;
		case Array::UShortArrayType:
			verticesConverted = decodePredict<UByteArray, UShortArray>(dynamic_pointer_cast<UByteArray>(indices), dynamic_pointer_cast<UShortArray>(verticesConverted), elementSize);
			break;
		case Array::UByteArrayType:
			verticesConverted = decodePredict<UByteArray, UByteArray>(dynamic_pointer_cast<UByteArray>(indices), dynamic_pointer_cast<UByteArray>(verticesConverted), elementSize);
			break;
		case Array::IntArrayType:
			verticesConverted = decodePredict<UByteArray, IntArray>(dynamic_pointer_cast<UByteArray>(indices), dynamic_pointer_cast<IntArray>(verticesConverted), elementSize);
			break;
		case Array::ShortArrayType:
			verticesConverted = decodePredict<UByteArray, ShortArray>(dynamic_pointer_cast<UByteArray>(indices), dynamic_pointer_cast<ShortArray>(verticesConverted), elementSize);
			break;
		case Array::ByteArrayType:
			verticesConverted = decodePredict<UByteArray, ByteArray>(dynamic_pointer_cast<UByteArray>(indices), dynamic_pointer_cast<ByteArray>(verticesConverted), elementSize);
			break;
		default:
			return nullptr;
		}
		break;
	default:
		return nullptr;
	}

	if (!verticesConverted)
		return nullptr;

	// Decode Quantize
	switch (verticesConverted->getType())
	{
	case Array::UIntArrayType:
		verticesConverted = decodeQuantize<UIntArray>(dynamic_pointer_cast<UIntArray>(verticesConverted), vtx_bbl, vtx_h, elementSize);
		break;
	case Array::UShortArrayType:
		verticesConverted = decodeQuantize<UShortArray>(dynamic_pointer_cast<UShortArray>(verticesConverted), vtx_bbl, vtx_h, elementSize);
		break;
	case Array::UByteArrayType:
		verticesConverted = decodeQuantize<UByteArray>(dynamic_pointer_cast<UByteArray>(verticesConverted), vtx_bbl, vtx_h, elementSize);
		break;
	case Array::IntArrayType:
		verticesConverted = decodeQuantize<IntArray>(dynamic_pointer_cast<IntArray>(verticesConverted), vtx_bbl, vtx_h, elementSize);
		break;
	case Array::ShortArrayType:
		verticesConverted = decodeQuantize<ShortArray>(dynamic_pointer_cast<ShortArray>(verticesConverted), vtx_bbl, vtx_h, elementSize);
		break;
	case Array::ByteArrayType:
		verticesConverted = decodeQuantize<ByteArray>(dynamic_pointer_cast<ByteArray>(verticesConverted), vtx_bbl, vtx_h, elementSize);
		break;
	default:
		return nullptr;
	}

	// Recast array
	verticesConverted = recastArray(verticesConverted, static_cast<DesiredVectorSize>(elementSize));

	return verticesConverted;
}

osg::ref_ptr<osg::Array> ParserHelper::decompressArray(const osg::ref_ptr<osg::Array>& keys, const UserDataContainer* udc,
	KeyDecodeMode mode)
{
	std::vector<double> o(4), b(3), h(3);
	std::vector<std::string> valuesO(4), valuesB(3), valuesH(3);
	double epsilon(0.0), nphi(0.0);

	std::ignore = udc->getUserValue("ox", valuesO[0]);
	std::ignore = udc->getUserValue("oy", valuesO[1]);
	std::ignore = udc->getUserValue("oz", valuesO[2]);
	std::ignore = udc->getUserValue("ow", valuesO[3]);

	std::ignore = udc->getUserValue("bx", valuesB[0]);
	std::ignore = udc->getUserValue("by", valuesB[1]);
	std::ignore = udc->getUserValue("bz", valuesB[2]);

	std::ignore = udc->getUserValue("hx", valuesH[0]);
	std::ignore = udc->getUserValue("hy", valuesH[1]);
	std::ignore = udc->getUserValue("hz", valuesH[2]);

	std::ignore = udc->getUserValue("epsilon", epsilon);
	std::ignore = udc->getUserValue("nphi", nphi);


	if (!valuesO[0].empty())
		getSafeDouble(valuesO[0], o[0]);
	if (!valuesO[1].empty())
		getSafeDouble(valuesO[1], o[1]);
	if (!valuesO[2].empty())
		getSafeDouble(valuesO[2], o[2]);
	if (!valuesO[3].empty())
		getSafeDouble(valuesO[3], o[3]);

	if (!valuesB[0].empty())
		getSafeDouble(valuesB[0], b[0]);
	if (!valuesB[1].empty())
		getSafeDouble(valuesB[1], b[1]);
	if (!valuesB[2].empty())
		getSafeDouble(valuesB[2], b[2]);

	if (!valuesH[0].empty())
		getSafeDouble(valuesH[0], h[0]);
	if (!valuesH[1].empty())
		getSafeDouble(valuesH[1], h[1]);
	if (!valuesH[2].empty())
		getSafeDouble(valuesH[2], h[2]);

	osg::ref_ptr<osg::Array> keysConverted = ParserHelper::recastArray(keys, DesiredVectorSize::Array);
	osg::ref_ptr<osg::DoubleArray> keysInflated1;
	osg::ref_ptr<osg::DoubleArray> keysInflated2 = new DoubleArray;
	osg::ref_ptr<osg::DoubleArray> keysFloat;
	unsigned int elementSize = keys->getDataSize();

	if (mode == KeyDecodeMode::DirectionCompressed)
	{
		switch (keysConverted->getType())
		{
		case Array::UIntArrayType:
			keysConverted = decodeVectorOctahedral<UIntArray>(dynamic_pointer_cast<UIntArray>(keysConverted));
			break;
		case Array::UShortArrayType:
			keysConverted = decodeVectorOctahedral<UShortArray>(dynamic_pointer_cast<UShortArray>(keysConverted));
			break;
		case Array::UByteArrayType:
			keysConverted = decodeVectorOctahedral<UByteArray>(dynamic_pointer_cast<UByteArray>(keysConverted));
			break;
		case Array::IntArrayType:
			keysConverted = decodeVectorOctahedral<IntArray>(dynamic_pointer_cast<IntArray>(keysConverted));
			break;
		case Array::ShortArrayType:
			keysConverted = decodeVectorOctahedral<ShortArray>(dynamic_pointer_cast<ShortArray>(keysConverted));
			break;
		case Array::ByteArrayType:
			keysConverted = decodeVectorOctahedral<ByteArray>(dynamic_pointer_cast<ByteArray>(keysConverted));
			break;
		}

		return keysConverted;
	}

	switch (keysConverted->getType())
	{
	case Array::UIntArrayType:
		keysConverted = deInterleaveKeys(dynamic_pointer_cast<UIntArray>(keysConverted), elementSize);
		break;
	case Array::UShortArrayType:
		keysConverted = deInterleaveKeys(dynamic_pointer_cast<UShortArray>(keysConverted), elementSize);
		break;
	case Array::UByteArrayType:
		keysConverted = deInterleaveKeys(dynamic_pointer_cast<UByteArray>(keysConverted), elementSize);
		break;
	case Array::IntArrayType:
		keysConverted = deInterleaveKeys(dynamic_pointer_cast<IntArray>(keysConverted), elementSize);
		break;
	case Array::ShortArrayType:
		keysConverted = deInterleaveKeys(dynamic_pointer_cast<ShortArray>(keysConverted), elementSize);
		break;
	case Array::ByteArrayType:
		keysConverted = deInterleaveKeys(dynamic_pointer_cast<ByteArray>(keysConverted), elementSize);
		break;
	default:
		OSG_WARN << "WARNING: Unsupported Array to decompress." << std::endl;
		return nullptr;
	}

	if (mode == KeyDecodeMode::Vec3Compressed)
	{
		switch (keysConverted->getType())
		{
		case Array::UIntArrayType:
			keysInflated1 = inflateKeys1<UIntArray>(dynamic_pointer_cast<UIntArray>(keysConverted), elementSize, b, h);
			break;
		case Array::UShortArrayType:
			keysInflated1 = inflateKeys1<UShortArray>(dynamic_pointer_cast<UShortArray>(keysConverted), elementSize, b, h);
			break;
		case Array::UByteArrayType:
			keysInflated1 = inflateKeys1<UByteArray>(dynamic_pointer_cast<UByteArray>(keysConverted), elementSize, b, h);
			break;
		case Array::IntArrayType:
			keysInflated1 = inflateKeys1<IntArray>(dynamic_pointer_cast<IntArray>(keysConverted), elementSize, b, h);
			break;
		case Array::ShortArrayType:
			keysInflated1 = inflateKeys1<ShortArray>(dynamic_pointer_cast<ShortArray>(keysConverted), elementSize, b, h);
			break;
		case Array::ByteArrayType:
			keysInflated1 = inflateKeys1<ByteArray>(dynamic_pointer_cast<ByteArray>(keysConverted), elementSize, b, h);
			break;
		}

		keysInflated2->reserveArray(keysInflated1->getNumElements() + 3);
		keysInflated2->push_back(o[0]);
		keysInflated2->push_back(o[1]);
		keysInflated2->push_back(o[2]);
		keysInflated2->insert(keysInflated2->begin() + 3, keysInflated1->begin(), keysInflated1->end());

		keysConverted = inflateKeys2(keysInflated2, elementSize);
	
		// Recast Array
		keysConverted = recastArray(keysConverted, static_cast<DesiredVectorSize>(elementSize));
	}
	else if (mode == KeyDecodeMode::QuatCompressed)
	{
		switch (keysConverted->getType())
		{
		case Array::UIntArrayType:
			keysInflated1 = int3ToFloat4<UIntArray>(dynamic_pointer_cast<UIntArray>(keysConverted), epsilon, nphi, elementSize);
			break;
		case Array::UShortArrayType:
			keysInflated1 = int3ToFloat4<UShortArray>(dynamic_pointer_cast<UShortArray>(keysConverted), epsilon, nphi, elementSize);
			break;
		case Array::UByteArrayType:
			keysInflated1 = int3ToFloat4<UByteArray>(dynamic_pointer_cast<UByteArray>(keysConverted), epsilon, nphi, elementSize);
			break;
		case Array::IntArrayType:
			keysInflated1 = int3ToFloat4<IntArray>(dynamic_pointer_cast<IntArray>(keysConverted), epsilon, nphi, elementSize);
			break;
		case Array::ShortArrayType:
			keysInflated1 = int3ToFloat4<ShortArray>(dynamic_pointer_cast<ShortArray>(keysConverted), epsilon, nphi, elementSize);
			break;
		case Array::ByteArrayType:
			keysInflated1 = int3ToFloat4<ByteArray>(dynamic_pointer_cast<ByteArray>(keysConverted), epsilon, nphi, elementSize);
			break;
		}

		keysInflated2->reserveArray(keysInflated1->getNumElements() + 4);
		keysInflated2->push_back(o[0]);
		keysInflated2->push_back(o[1]);
		keysInflated2->push_back(o[2]);
		keysInflated2->push_back(o[3]);
		keysInflated2->insert(keysInflated2->begin() + 4, keysInflated1->begin(), keysInflated1->end());

		keysConverted = inflateKeysQuat(keysInflated2);

		// Recast Array
		keysConverted = recastArray(keysConverted, static_cast<DesiredVectorSize>(elementSize + 1));
	}

	return keysConverted;
}

bool ParserHelper::getShapeAttribute(const osg::ref_ptr<osgSim::ShapeAttributeList>& shapeAttrList, const std::string& name, double& value)
{
	for (const osgSim::ShapeAttribute& attr : *shapeAttrList) 
	{
		if (attr.getName() == name && attr.getType() == osgSim::ShapeAttribute::Type::DOUBLE)
		{
			value = attr.getDouble();
			return true;
		}
		else if (attr.getName() == name && attr.getType() == osgSim::ShapeAttribute::Type::INTEGER)
		{
			value = static_cast<double>(attr.getInt());
			return true;
		}
	}
	return false;
}

bool ParserHelper::getShapeAttribute(const osg::ref_ptr<osgSim::ShapeAttributeList>& shapeAttrList, const std::string& name, int& value)
{
	for (const osgSim::ShapeAttribute& attr : *shapeAttrList)
	{
		if (attr.getName() == name && attr.getType() == osgSim::ShapeAttribute::Type::INTEGER)
		{
			value = attr.getInt();
			return true;
		}
	}
	return false;
}

bool ParserHelper::getShapeAttribute(const osg::ref_ptr<osgSim::ShapeAttributeList>& shapeAttrList, const std::string& name, std::string& value)
{
	for (const osgSim::ShapeAttribute& attr : *shapeAttrList)
	{
		if (attr.getName() == name && attr.getType() == osgSim::ShapeAttribute::Type::STRING)
		{
			value = attr.getString();
			return true;
		}
	}
	return false;
}

GLenum ParserHelper::getModeFromString(const std::string& mode)
{
	if (mode == "POINTS") return GL_POINTS;
	if (mode == "LINES") return GL_LINES;
	if (mode == "LINE_LOOP") return GL_LINE_LOOP;
	if (mode == "LINE_STRIP") return GL_LINE_STRIP;
	if (mode == "TRIANGLES") return GL_TRIANGLES;
	if (mode == "TRIANGLE_STRIP") return GL_TRIANGLE_STRIP;
	if (mode == "TRIANGLE_FAN") return GL_TRIANGLE_FAN;

	return GL_POINTS;
}

BlendFunc::BlendFuncMode ParserHelper::getBlendFuncFromString(const std::string& blendFunc)
{
	if (blendFunc == "DST_ALPHA") return BlendFunc::DST_ALPHA;
	if (blendFunc == "DST_COLOR") return BlendFunc::DST_COLOR;
	if (blendFunc == "ONE") return BlendFunc::ONE;
	if (blendFunc == "ONE_MINUS_DST_ALPHA") return BlendFunc::ONE_MINUS_DST_ALPHA;
	if (blendFunc == "ONE_MINUS_DST_COLOR") return BlendFunc::ONE_MINUS_DST_COLOR;
	if (blendFunc == "ONE_MINUS_SRC_ALPHA") return BlendFunc::ONE_MINUS_SRC_ALPHA;
	if (blendFunc == "ONE_MINUS_SRC_COLOR") return BlendFunc::ONE_MINUS_SRC_COLOR;
	if (blendFunc == "SRC_ALPHA") return BlendFunc::SRC_ALPHA;
	if (blendFunc == "SRC_ALPHA_SATURATE") return BlendFunc::SRC_ALPHA_SATURATE;
	if (blendFunc == "SRC_COLOR") return BlendFunc::SRC_COLOR;
	if (blendFunc == "CONSTANT_COLOR") return BlendFunc::CONSTANT_COLOR;
	if (blendFunc == "ONE_MINUS_CONSTANT_COLOR") return BlendFunc::ONE_MINUS_CONSTANT_COLOR;
	if (blendFunc == "CONSTANT_ALPHA") return BlendFunc::CONSTANT_ALPHA;
	if (blendFunc == "ONE_MINUS_CONSTANT_ALPHA") return BlendFunc::ONE_MINUS_CONSTANT_ALPHA;
	if (blendFunc == "ZERO") return BlendFunc::ZERO;

	return BlendFunc::ONE;
}

Texture::FilterMode ParserHelper::getFilterModeFromString(const std::string& filterMode)
{
	if (filterMode == "LINEAR") return Texture::FilterMode::LINEAR;
	if (filterMode == "LINEAR_MIPMAP_LINEAR") return Texture::FilterMode::LINEAR_MIPMAP_LINEAR;
	if (filterMode == "LINEAR_MIPMAP_NEAREST") return Texture::FilterMode::LINEAR_MIPMAP_NEAREST;
	if (filterMode == "NEAREST") return Texture::FilterMode::NEAREST;
	if (filterMode == "NEAREST_MIPMAP_LINEAR") return Texture::FilterMode::NEAREST_MIPMAP_LINEAR;
	if (filterMode == "NEAREST_MIPMAP_NEAREST") return Texture::FilterMode::NEAREST_MIPMAP_NEAREST;

	return Texture::FilterMode::LINEAR;
}

Texture::WrapMode ParserHelper::getWrapModeFromString(const std::string& wrapMode)
{
	if (wrapMode == "CLAMP_TO_EDGE") return Texture::WrapMode::CLAMP_TO_EDGE;
	if (wrapMode == "CLAMP_TO_BORDER") return Texture::WrapMode::CLAMP_TO_BORDER;
	if (wrapMode == "REPEAT") return Texture::WrapMode::REPEAT;
	if (wrapMode == "MIRROR") return Texture::WrapMode::MIRROR;

	return Texture::WrapMode::REPEAT;
}

CullFace::Mode ParserHelper::getCullFaceModeFromString(const std::string& cullFaceMode)
{
	if (cullFaceMode == "FRONT") return CullFace::Mode::FRONT;
	if (cullFaceMode == "BACK") return CullFace::Mode::BACK;
	if (cullFaceMode == "FRONT_AND_BACK") return CullFace::Mode::FRONT_AND_BACK;

	return CullFace::Mode::FRONT_AND_BACK;
}

osgText::Text::AlignmentType ParserHelper::getTextAlignmentFromString(const std::string& textAlignment)
{
	if (textAlignment == "LEFT_TOP") return osgText::Text::AlignmentType::LEFT_TOP;
	if (textAlignment == "LEFT_CENTER") return osgText::Text::AlignmentType::LEFT_CENTER;
	if (textAlignment == "LEFT_BOTTOM") return osgText::Text::AlignmentType::LEFT_BOTTOM;
	if (textAlignment == "CENTER_TOP") return osgText::Text::AlignmentType::CENTER_TOP;
	if (textAlignment == "CENTER_CENTER") return osgText::Text::AlignmentType::CENTER_CENTER;
	if (textAlignment == "CENTER_BOTTOM") return osgText::Text::AlignmentType::CENTER_BOTTOM;
	if (textAlignment == "RIGHT_TOP") return osgText::Text::AlignmentType::RIGHT_TOP;
	if (textAlignment == "RIGHT_CENTER") return osgText::Text::AlignmentType::RIGHT_CENTER;
	if (textAlignment == "RIGHT_BOTTOM") return osgText::Text::AlignmentType::RIGHT_BOTTOM;
	if (textAlignment == "LEFT_BASE_LINE") return osgText::Text::AlignmentType::LEFT_BASE_LINE;
	if (textAlignment == "CENTER_BASE_LINE") return osgText::Text::AlignmentType::CENTER_BASE_LINE;
	if (textAlignment == "RIGHT_BASE_LINE") return osgText::Text::AlignmentType::RIGHT_BASE_LINE;
	if (textAlignment == "LEFT_BOTTOM_BASE_LINE") return osgText::Text::AlignmentType::LEFT_BOTTOM_BASE_LINE;
	if (textAlignment == "CENTER_BOTTOM_BASE_LINE") return osgText::Text::AlignmentType::CENTER_BOTTOM_BASE_LINE;
	if (textAlignment == "RIGHT_BOTTOM_BASE_LINE") return osgText::Text::AlignmentType::RIGHT_BOTTOM_BASE_LINE;

	return osgText::Text::AlignmentType::LEFT_TOP;
}




// PRIVATE METHODS

bool ParserHelper::getPrimitiveType(const json& currentJSONNode, PrimitiveSet::Type& outPrimitiveType)
{
	for (auto& nodeElement : currentJSONNode)
	{
		for (auto& elementString : primitiveTypeMap)
		{
			if (nodeElement.contains(elementString.first))
			{
				outPrimitiveType = elementString.second;
				return true;
			}
		}
	}

	return false;
}

inline int64_t ParserHelper::varintSigned(uint64_t input)
{
	return static_cast<int64_t>(input & 1 ? ~(input >> 1) : (input >> 1));
}

template <typename T>
void ParserHelper::copyIntToByteVector(T value, std::vector<uint8_t>& vec)
{
	static_assert(std::is_integral<T>::value, "T must be of integral type.");
	uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);

	for (size_t i = 0; i < sizeof(T); ++i) {
		vec.push_back(bytes[i]);
	}
}

uint64_t ParserHelper::decodeVarInt(const uint8_t* const data, int& decoded_bytes)
{
	int i = 0;
	uint64_t decoded_value = 0;
	int shift_amount = 0;

	do
	{
		decoded_value |= (uint64_t)(data[i] & 0x7F) << shift_amount;
		shift_amount += 7;
	} while ((data[i++] & 0x80) != 0);

	decoded_bytes = i;
	return decoded_value;
}

std::vector<uint8_t>* ParserHelper::decodeVarintVector(const std::vector<uint8_t>& input, Array::Type inputType, size_t itemCount, size_t offSet)
{
	std::vector<uint8_t>* parsedVector = new std::vector<uint8_t>;

	size_t parsedSize = 0;
	int parsedItemCount = 0;
	while (parsedItemCount < itemCount)
	{
		int decodedBytes = 0;
		uint64_t decodedValue = 0;
		try
		{
			decodedValue = decodeVarInt(input.data() + offSet + parsedSize, decodedBytes);
		}
		catch (std::exception)
		{
			OSG_WARN << "WARNING: Error while decoding input vector!" << std::endl;
			return nullptr;
		}

		switch (inputType)
		{
		case Array::ByteArrayType:
			copyIntToByteVector(static_cast<int8_t>(varintSigned(decodedValue)), (*parsedVector));
			break;
		case Array::UByteArrayType:
			copyIntToByteVector(static_cast<uint8_t>(decodedValue), (*parsedVector));
			break;
		case Array::ShortArrayType:
			copyIntToByteVector(static_cast<int16_t>(varintSigned(decodedValue)), (*parsedVector));
			break;
		case Array::UShortArrayType:
			copyIntToByteVector(static_cast<uint16_t>(decodedValue), (*parsedVector));
			break;
		case Array::IntArrayType:
			copyIntToByteVector(static_cast<int32_t>(varintSigned(decodedValue)), (*parsedVector));
			break;
		case Array::UIntArrayType:
			copyIntToByteVector(static_cast<uint32_t>(decodedValue), (*parsedVector));
			break;
		case Array::Int64ArrayType:
			copyIntToByteVector(static_cast<int64_t>(varintSigned(decodedValue)), (*parsedVector));
			break;
		case Array::UInt64ArrayType:
			copyIntToByteVector(static_cast<uint64_t>(decodedValue), (*parsedVector));
			break;
		}

		parsedItemCount++;
		parsedSize += decodedBytes;
	}

	return parsedVector;
}

Array* ParserHelper::getVertexAttribArray(osgAnimation::RigGeometry& rigGeometry, const std::string arrayName) {
	for (unsigned int i = 0; i < rigGeometry.getNumVertexAttribArrays(); ++i) {
		Array* attribute = rigGeometry.getVertexAttribArray(i);
		bool isBones = false;
		if (attribute && attribute->getUserValue(arrayName, isBones) && isBones) {
			return attribute;
		}
	}
	return 0;
}


ref_ptr<Array> ParserHelper::recastArray(const ref_ptr<Array>& toRecast, DesiredVectorSize vecSize)
{
	if (!toRecast)
		return nullptr;

	ref_ptr<Array> returnArray;

	if (vecSize == DesiredVectorSize::Array)
		return decastVector(toRecast);

	switch (vecSize)
	{
	case DesiredVectorSize::Vec2:
	{
		// Certify the array contains appropriate number of elements
		if (toRecast->getNumElements() % 2 != 0)
		{
			OSG_WARN << "WARNING: Array has incorrect size. Ignoring!" << std::endl;
			return nullptr;
		}

		int totalElements = toRecast->getNumElements() / 2;
		switch (toRecast->getType())
		{
		case Array::DoubleArrayType:
		{
			returnArray = new Vec2dArray;
			returnArray->reserveArray(totalElements);
			DoubleArray* converted = dynamic_cast<DoubleArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2d newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2dArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::FloatArrayType:
		{
			returnArray = new Vec2Array;
			returnArray->reserveArray(totalElements);
			FloatArray* converted = dynamic_cast<FloatArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2 newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2Array*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UByteArrayType:
		{
			returnArray = new Vec2ubArray;
			returnArray->reserveArray(totalElements);
			UByteArray* converted = dynamic_cast<UByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2ub newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2ubArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UShortArrayType:
		{
			returnArray = new Vec2usArray;
			returnArray->reserveArray(totalElements);
			UShortArray* converted = dynamic_cast<UShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2us newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2usArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UIntArrayType:
		{
			returnArray = new Vec2uiArray;
			returnArray->reserveArray(totalElements);
			UIntArray* converted = dynamic_cast<UIntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2ui newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UInt64ArrayType:
		{
			OSG_WARN << "WARNING: Uint64Array don't have a proper vector implemented. Data may be lost." << std::endl;
			returnArray = new Vec2uiArray;
			returnArray->reserveArray(totalElements);
			UInt64Array* converted = dynamic_cast<UInt64Array*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2ui newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::ByteArrayType:
		{
			returnArray = new Vec2bArray;
			returnArray->reserveArray(totalElements);
			ByteArray* converted = dynamic_cast<ByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2b newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2bArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::ShortArrayType:
		{
			returnArray = new Vec2sArray;
			returnArray->reserveArray(totalElements);
			ShortArray* converted = dynamic_cast<ShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2s newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2sArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::IntArrayType:
		{
			returnArray = new Vec2iArray;
			returnArray->reserveArray(totalElements);
			IntArray* converted = dynamic_cast<IntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2i newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2iArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::Int64ArrayType:
		{
			OSG_WARN << "WARNING: Int64Array don't have a proper vector implemented. Data may be lost." << std::endl;
			returnArray = new Vec2iArray;
			returnArray->reserveArray(totalElements);
			Int64Array* converted = dynamic_cast<Int64Array*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec2ui newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		}
		break;
	}
	case DesiredVectorSize::Vec3:
	{
		// Certify the array contains appropriate number of elements
		if (toRecast->getNumElements() % 3 != 0)
		{
			OSG_WARN << "WARNING: Array has incorrect size. Ignoring!" << std::endl;
			return nullptr;
		}

		int totalElements = toRecast->getNumElements() / 3;
		switch (toRecast->getType())
		{
		case Array::DoubleArrayType:
		{
			returnArray = new Vec3dArray;
			returnArray->reserveArray(totalElements);
			DoubleArray* converted = dynamic_cast<DoubleArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3d newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3dArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::FloatArrayType:
		{
			returnArray = new Vec3Array;
			returnArray->reserveArray(totalElements);
			FloatArray* converted = dynamic_cast<FloatArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3 newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3Array*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UByteArrayType:
		{
			returnArray = new Vec3ubArray;
			returnArray->reserveArray(totalElements);
			UByteArray* converted = dynamic_cast<UByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3ub newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3ubArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UShortArrayType:
		{
			returnArray = new Vec3usArray;
			returnArray->reserveArray(totalElements);
			UShortArray* converted = dynamic_cast<UShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3us newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3usArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UIntArrayType:
		{
			returnArray = new Vec3uiArray;
			returnArray->reserveArray(totalElements);
			UIntArray* converted = dynamic_cast<UIntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3ui newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UInt64ArrayType:
		{
			OSG_WARN << "WARNING: Uint64Array don't have a proper vector implemented. Data may be lost." << std::endl;
			returnArray = new Vec3uiArray;
			returnArray->reserveArray(totalElements);
			UInt64Array* converted = dynamic_cast<UInt64Array*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3ui newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::ByteArrayType:
		{
			returnArray = new Vec3bArray;
			returnArray->reserveArray(totalElements);
			ByteArray* converted = dynamic_cast<ByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3b newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3bArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::ShortArrayType:
		{
			returnArray = new Vec3sArray;
			returnArray->reserveArray(totalElements);
			ShortArray* converted = dynamic_cast<ShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3s newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3sArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::IntArrayType:
		{
			returnArray = new Vec3iArray;
			returnArray->reserveArray(totalElements);
			IntArray* converted = dynamic_cast<IntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3i newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3iArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::Int64ArrayType:
		{
			OSG_WARN << "WARNING: Int64Array don't have a proper vector implemented. Data may be lost." << std::endl;
			returnArray = new Vec3iArray;
			returnArray->reserveArray(totalElements);
			Int64Array* converted = dynamic_cast<Int64Array*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec3i newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<Vec3iArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		}
		break;
	}
	case DesiredVectorSize::Vec4:
	{
		// Certify the array contains appropriate number of elements
		if (toRecast->getNumElements() % 4 != 0)
		{
			OSG_WARN << "WARNING: Array has incorrect size. Ignoring!" << std::endl;
			return nullptr;
		}

		int totalElements = toRecast->getNumElements() / 4;
		switch (toRecast->getType())
		{
		case Array::DoubleArrayType:
		{
			returnArray = new Vec4dArray;
			returnArray->reserveArray(totalElements);
			DoubleArray* converted = dynamic_cast<DoubleArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4d newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4dArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::FloatArrayType:
		{
			returnArray = new Vec4Array;
			returnArray->reserveArray(totalElements);
			FloatArray* converted = dynamic_cast<FloatArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4 newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4Array*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UByteArrayType:
		{
			returnArray = new Vec4ubArray;
			returnArray->reserveArray(totalElements);
			UByteArray* converted = dynamic_cast<UByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4ub newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4ubArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UShortArrayType:
		{
			returnArray = new Vec4usArray;
			returnArray->reserveArray(totalElements);
			UShortArray* converted = dynamic_cast<UShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4us newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4usArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UIntArrayType:
		{
			returnArray = new Vec4uiArray;
			returnArray->reserveArray(totalElements);
			UIntArray* converted = dynamic_cast<UIntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4ui newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::UInt64ArrayType:
		{
			OSG_WARN << "WARNING: Uint64Array don't have a proper vector implemented. Data may be lost." << std::endl;
			returnArray = new Vec4uiArray;
			returnArray->reserveArray(totalElements);
			UInt64Array* converted = dynamic_cast<UInt64Array*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4ui newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::ByteArrayType:
		{
			returnArray = new Vec4bArray;
			returnArray->reserveArray(totalElements);
			ByteArray* converted = dynamic_cast<ByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4b newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4bArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::ShortArrayType:
		{
			returnArray = new Vec4sArray;
			returnArray->reserveArray(totalElements);
			ShortArray* converted = dynamic_cast<ShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4s newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4sArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::IntArrayType:
		{
			returnArray = new Vec4iArray;
			returnArray->reserveArray(totalElements);
			IntArray* converted = dynamic_cast<IntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4i newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4iArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case Array::Int64ArrayType:
		{
			OSG_WARN << "WARNING: Int64Array don't have a proper vector implemented. Data may be lost." << std::endl;
			returnArray = new Vec4iArray;
			returnArray->reserveArray(totalElements);
			Int64Array* converted = dynamic_cast<Int64Array*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				Vec4i newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<Vec4iArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		}
		break;
	}
	}

	return returnArray;
}

ref_ptr<Array> ParserHelper::decastVector(const ref_ptr<Array>& toRecast)
{
	if (!toRecast)
		return nullptr;

	ref_ptr<Array> returnArray;

	switch (toRecast->getType())
	{
	case Array::ByteArrayType:
		returnArray = new ByteArray(*dynamic_pointer_cast<ByteArray>(toRecast));
		break;
	case Array::ShortArrayType:
		returnArray = new ShortArray(*dynamic_pointer_cast<ShortArray>(toRecast));
		break;
	case Array::IntArrayType:
		returnArray = new IntArray(*dynamic_pointer_cast<IntArray>(toRecast));
		break;
	case Array::Int64ArrayType:
		returnArray = new Int64Array(*dynamic_pointer_cast<Int64Array>(toRecast));
		break;
	case Array::UByteArrayType:
		returnArray = new UByteArray(*dynamic_pointer_cast<UByteArray>(toRecast));
		break;
	case Array::UShortArrayType:
		returnArray = new UShortArray(*dynamic_pointer_cast<UShortArray>(toRecast));
		break;
	case Array::UIntArrayType:
		returnArray = new UIntArray(*dynamic_pointer_cast<UIntArray>(toRecast));
		break;
	case Array::UInt64ArrayType:
		returnArray = new UInt64Array(*dynamic_pointer_cast<UInt64Array>(toRecast));
		break;
	case Array::FloatArrayType:
		returnArray = new FloatArray(*dynamic_pointer_cast<FloatArray>(toRecast));
		break;
	case Array::DoubleArrayType:
		returnArray = new DoubleArray(*dynamic_pointer_cast<DoubleArray>(toRecast));
		break;

	case Array::Vec4dArrayType:
		returnArray = new DoubleArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4dArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4dArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4ArrayType:
		returnArray = new FloatArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4Array>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4Array>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4ubArrayType:
		returnArray = new UByteArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4ubArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4ubArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4usArrayType:
		returnArray = new UShortArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4usArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4usArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4uiArrayType:
		returnArray = new UIntArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4uiArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4uiArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4bArrayType:
		returnArray = new ByteArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4bArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4bArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4sArrayType:
		returnArray = new ShortArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4sArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4sArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->w());
		}
		break;
	case Array::Vec4iArrayType:
		returnArray = new IntArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec4iArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec4iArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->z());
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->w());
		}
		break;


	case Array::Vec3dArrayType:
		returnArray = new DoubleArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3dArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3dArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3ArrayType:
		returnArray = new FloatArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3Array>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3Array>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3ubArrayType:
		returnArray = new UByteArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3ubArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3ubArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3usArrayType:
		returnArray = new UShortArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3usArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3usArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3uiArrayType:
		returnArray = new UIntArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3uiArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3uiArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3bArrayType:
		returnArray = new ByteArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3bArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3bArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3sArrayType:
		returnArray = new ShortArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3sArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3sArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->z());
		}
		break;
	case Array::Vec3iArrayType:
		returnArray = new IntArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec3iArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec3iArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->y());
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->z());
		}
		break;


	case Array::Vec2dArrayType:
		returnArray = new DoubleArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2dArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2dArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<DoubleArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2ArrayType:
		returnArray = new FloatArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2Array>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2Array>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<FloatArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2ubArrayType:
		returnArray = new UByteArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2ubArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2ubArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UByteArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2usArrayType:
		returnArray = new UShortArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2usArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2usArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UShortArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2uiArrayType:
		returnArray = new UIntArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2uiArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2uiArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<UIntArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2bArrayType:
		returnArray = new ByteArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2bArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2bArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<ByteArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2sArrayType:
		returnArray = new ShortArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2sArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2sArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<ShortArray>(returnArray)->push_back(it->y());
		}
		break;
	case Array::Vec2iArrayType:
		returnArray = new IntArray();
		returnArray->reserveArray(toRecast->getNumElements() * toRecast->getDataSize());
		for (auto it = dynamic_pointer_cast<Vec2iArray>(toRecast)->begin(); it != dynamic_pointer_cast<Vec2iArray>(toRecast)->end(); ++it)
		{
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->x());
			dynamic_pointer_cast<IntArray>(returnArray)->push_back(it->y());
		}
		break;
	}

	return returnArray;
}


template <typename T>
std::vector<T> ParserHelper::decodeDelta(const std::vector<T>& input, int e)
{
	std::vector<T> t = input;
	std::vector<T> tEmpty;
	if (t.empty() || e >= t.size()) 
		return tEmpty;

	uint32_t r = t[e];
	for (int a = e + 1; a < t.size(); ++a) {
		r = t[a] = r + (t[a] >> 1 ^ -static_cast<int>(1 & t[a]));
	}

	return t;
}

template <typename T>
std::vector<T> ParserHelper::decodeImplicit(const std::vector<T>& t, int n)
{
	std::vector<T> vempty;
	uint32_t eSize = t[IMPLICIT_HEADER_PRIMITIVE_LENGTH];
	std::vector<T> e(eSize, 0);
	uint32_t a = t[IMPLICIT_HEADER_EXPECTED_INDEX];
	uint32_t s = t[IMPLICIT_HEADER_MASK_LENGTH];
	uint32_t r = HIGH_WATERMARK;
	uint32_t u = 32 * s - e.size();
	uint32_t l = static_cast<uint32_t>(1 << 31);
	uint32_t h = 0;

	while (h < s) 
	{
		uint32_t c = t[h + IMPLICIT_HEADER_LENGTH];
		uint32_t d = 32;
		uint32_t p = h * d;

		if (p >= e.size())
			return vempty;

		uint32_t f = (h == s - 1) ? u : 0;
		uint32_t g1 = f;

		while (g1 < d) 
		{
			if (c & (l >> g1)) 
			{
				e[p] = t[n];
				n++;
			}
			else 
			{
				e[p] = (r) ? a : a++;
			}
			++g1;
			++p;
		}
		++h;
	}

	return e;
}

template <typename T>
std::vector<T> ParserHelper::decodeWatermark(const std::vector<T>& t, uint32_t& magic)
{
	std::vector<T> e = t;
	uint32_t n = magic;
	uint32_t r = t.size();

	for (uint32_t a = 0; a < r; ++a) {
		uint32_t s = n - static_cast<uint32_t>(t[a]);
		e[a] = static_cast<T>(s);
		if (n <= s) {
			n = s + 1;
		}
	}

	magic = n;

	return e;
}

template <typename T, typename U>
osg::ref_ptr<osg::Array> ParserHelper::decodePredict(const osg::ref_ptr<T>& indices, const osg::ref_ptr<U>& vertices, int itemSize)
{
	osg::ref_ptr<U> t = new U(*vertices);
	if (!indices->empty())
	{
		int n = t->size() / itemSize;
		std::vector<int> r(n, 0);
		int a = indices->size() - 1;

		if ((*indices)[0] > r.size() || (*indices)[1] > r.size() || (*indices)[2] > r.size())
			return nullptr;

		r[(*indices)[0]] = 1;
		r[(*indices)[1]] = 1;
		r[(*indices)[2]] = 1;

		for (int s = 2; s < a; ++s)
		{
			int o = s - 2;
			unsigned int u = (*indices)[o];
			unsigned int l = (*indices)[o + 1];
			unsigned int h = (*indices)[o + 2];
			unsigned int c = (*indices)[o + 3];

			if (c > r.size())
				return nullptr;

			if (1 != r[c])
			{
				r[c] = 1;
				u *= itemSize;
				l *= itemSize;
				h *= itemSize;
				c *= itemSize;

				for (int d = 0; d < itemSize; ++d)
				{
					(*t)[c + d] = (*t)[c + d] + (*t)[l + d] + (*t)[h + d] - (*t)[u + d];
				}
			}
		}
	}
	return t;
}

template <typename T>
osg::ref_ptr<osg::Array> ParserHelper::decodeQuantize(const osg::ref_ptr<T>& vertices, const std::vector<double>& vtx_bbl,
	const std::vector<double>& vtx_h, int elementSize)
{
	ref_ptr<DoubleArray> x = new DoubleArray;
	x->resize(vertices->getNumElements());

	int id = 0;
	for (unsigned int r = 0; r < vertices->getNumElements() / elementSize; ++r)
	{
		for (int l = 0; l < elementSize; ++l)
		{
			(*x)[id] = static_cast<double>(vtx_bbl[l] + static_cast<double>((*vertices)[id]) * vtx_h[l]);
			id++;
		}
	}
	return x;
}

// Etap1
template <typename T>
osg::ref_ptr<T> ParserHelper::deInterleaveKeys(const osg::ref_ptr<T>& input, unsigned int itemSize)
{
	unsigned int n = input->getNumElements() / itemSize;
	unsigned int r = 0;
	osg::ref_ptr<T> output = new T(input->getNumElements());

	while (r < n) 
	{
		unsigned int a = r * itemSize;
		unsigned int s = 0;
		while (s < itemSize) 
		{
			(*output)[static_cast<size_t>(a) + s] = (*input)[r + static_cast<size_t>(n) * s];
			s += 1;
		}
		r += 1;
	}
	return output;
}

// Etap2
template <typename T>
osg::ref_ptr<osg::DoubleArray> ParserHelper::inflateKeys1(const osg::ref_ptr<T>& input, unsigned int itemSize,
	const std::vector<double>& attrB, const std::vector<double>& attrH)
{
	std::vector<double> i = { attrB[0], attrB[1], attrB[2] }; // bx, by, bz
	std::vector<double> n = { attrH[0], attrH[1], attrH[2] }; // hx, hy, hz

	unsigned int a = input->getNumElements() / itemSize;
	osg::ref_ptr<osg::DoubleArray> output = new osg::DoubleArray(input->getNumElements());
	unsigned int s = 0;

	while (s < a) {
		unsigned int o = s * itemSize;
		unsigned int u = 0;
		while (u < itemSize) {
			(*output)[static_cast<size_t>(o) + u] = static_cast<float>(i[u] + (*input)[static_cast<size_t>(o) + u] * n[u]);
			u += 1;
		}
		s += 1;
	}

	return output;
}

// Etap3
osg::ref_ptr<osg::DoubleArray> ParserHelper::inflateKeys2(const osg::ref_ptr<osg::DoubleArray>& input, unsigned int itemSize)
{
	osg::ref_ptr<osg::DoubleArray> output = new DoubleArray(*input);
	unsigned int i = itemSize | 1; 
	unsigned int n = 1;
	unsigned int r = output->getNumElements() / i;

	while (n < r) {
		unsigned int a = (n - 1) * i;
		unsigned int s = n * i;
		unsigned int o = 0;
		while (o < i) {
			(*output)[static_cast<size_t>(s) + o] += (*output)[static_cast<size_t>(a) + o];
			o += 1;
		}
		n += 1;
	}

	return output;
}

// Etap4
template <typename T>
osg::ref_ptr<osg::DoubleArray> ParserHelper::inflateKeysQuat(const osg::ref_ptr<T>& input)
{
	osg::ref_ptr<osg::DoubleArray> output = new DoubleArray(input->begin(), input->end());

	unsigned int e = 1;
	unsigned int i = output->getNumElements() / 4;

	while (e < i) {
		unsigned int n = 4 * (e - 1);
		unsigned int r = 4 * e;

		double a = (*output)[n];
		double s = (*output)[static_cast<size_t>(n) + 1];
		double o = (*output)[static_cast<size_t>(n) + 2];
		double u = (*output)[static_cast<size_t>(n) + 3];
		double l = (*output)[r];
		double h = (*output)[static_cast<size_t>(r) + 1];
		double c = (*output)[static_cast<size_t>(r) + 2];
		double d = (*output)[static_cast<size_t>(r) + 3];

		(*output)[r] = a * d + s * c - o * h + u * l;
		(*output)[static_cast<size_t>(r) + 1] = -a * c + s * d + o * l + u * h;
		(*output)[static_cast<size_t>(r) + 2] = a * h - s * l + o * d + u * c;
		(*output)[static_cast<size_t>(r) + 3] = -a * l - s * h - o * c + u * d;

		++e;
	}
	
	return output;
}

template <typename T>
osg::ref_ptr<osg::DoubleArray> ParserHelper::int3ToFloat4(const osg::ref_ptr<T>& input, double epsilon, double nphi, int itemSize)
{
	int c = 4;
	double d = epsilon != 0 ? epsilon : 0.25;
	int p = static_cast<int>(nphi != 0 ? nphi : 720);
	osg::ref_ptr<osg::DoubleArray> e = new osg::DoubleArray();
	e->resizeArray(input->getNumElements() * 4 / itemSize);

	double i = 1.57079632679;
	double n = 6.28318530718;
	double r = 3.14159265359;
	double a = 0.01745329251;
	double l = 47938362584151635e-21;
	std::map<int, double> _;

	double b = r / static_cast<double>(p - 1);
	double x = i / static_cast<double>(p - 1);

	int y = 3;

	int m = 0;
	int v_length = input->getNumElements() / y;
	while (m < v_length) 
	{
		int A = m * c;
		int S = m * y;
		int C = (*input)[S];
		int w = (*input)[S + 1];
		double M(0.0), T(0.0), E(0.0);
		int I = 3 * (C + p * w);

		M = _[I];
		if (M == 0) 
		{  
			double N = C * b;
			double k = std::cos(N);
			double F = std::sin(N);
			N += x;
			double D = (std::cos(d * a) - k * std::cos(N)) / std::max(1e-5, F * std::sin(N));
			D = std::max(-1.0, std::min(D, 1.0));

			double P = w * n / std::ceil(r / std::max(1e-5, std::acos(D)));
			M = _[I] = F * std::cos(P);
			T = _[static_cast<size_t>(I) + 1] = F * std::sin(P);
			E = _[static_cast<size_t>(I) + 2] = k;
		}
		else 
		{
			T = _[static_cast<size_t>(I) + 1];
			E = _[static_cast<size_t>(I) + 2];
		}

		double R = (*input)[S + 2] * l;
		double O = std::sin(R);
		(*e)[A] = O * M;
		(*e)[static_cast<size_t>(A) + 1] = O * T;
		(*e)[static_cast<size_t>(A) + 2] = O * E;
		(*e)[static_cast<size_t>(A) + 3] = std::cos(R);

		++m;
	}

	return e;
}


template <typename T>
osg::ref_ptr<osg::DoubleArray> ParserHelper::inflateKeysVec3(const osg::ref_ptr<T>& input)
{
	osg::ref_ptr<osg::DoubleArray> output = new DoubleArray(input->begin(), input->end());

	unsigned int e = 1;
	unsigned int i = output->getNumElements() / 3;

	while (e < i) {
		unsigned int n = 3 * (e - 1);
		unsigned int r = 3 * e;

		double a = (*output)[n];
		double s = (*output)[static_cast<size_t>(n) + 1];
		double o = (*output)[static_cast<size_t>(n) + 2];
		double u = 0.0; //(*output)[static_cast<size_t>(n) + 3];
		double l = (*output)[r];
		double h = (*output)[static_cast<size_t>(r) + 1];
		double c = (*output)[static_cast<size_t>(r) + 2];
		double d = 0.0; //(*output)[static_cast<size_t>(r) + 3];

		(*output)[r] = a * d + s * c - o * h + u * l;
		(*output)[static_cast<size_t>(r) + 1] = -a * c + s * d + o * l + u * h;
		(*output)[static_cast<size_t>(r) + 2] = a * h - s * l + o * d + u * c;
		//(*output)[static_cast<size_t>(r) + 3] = -a * l - s * h - o * c + u * d;

		++e;
	}

	return output;
}


// For references, see: 
// https://cesium.com/blog/2015/05/18/vertex-compression/
// https://github.com/CesiumGS/cesium/blob/master/Source/Core/AttributeCompression.js
// https://jcgt.org/published/0003/02/01/


static osg::Vec3 decodeOctahedral(const osg::Vec2ui& encodedInt, float maxRange)
{
	// Normalizar os valores inteiros para o intervalo de -1 a 1
	float x = (static_cast<float>(encodedInt.x()) / (maxRange / 2)) - 1.0f;
	float y = (static_cast<float>(encodedInt.y()) / (maxRange / 2)) - 1.0f;

	// Passo 1: Reconstruir z
	float z = 1.0f - fabs(x) - fabs(y);

	// Passo 2: Ajustar se o ponto está na parte inferior do octaedro
	if (z < 0.0f) {
		float oldX = x;
		x = copysign(1.0f - fabs(y), x);
		y = copysign(1.0f - fabs(oldX), y);
	}

	// Passo 3: Normalizar o vetor
	osg::Vec3 normal(x, y, z);
	normal.normalize();

	return normal;
}

// TODO: Figure out which encoding for normals and tangents. This encoding here seems to NOT be the case.
template <typename T>
osg::ref_ptr<osg::Vec3Array> ParserHelper::decodeVectorOctahedral(const osg::ref_ptr<T>& input)
{
	ref_ptr<osg::UIntArray> e = new UIntArray(input->begin(), input->end());
	ref_ptr<Vec3Array> returnVec = new Vec3Array;

	unsigned int maxRange = *std::max_element(e->begin(), e->end());

	returnVec->reserveArray(e->getNumElements() / 2);
	for (unsigned int i = 0; i < e->getNumElements() / 2; i++)
	{
		unsigned int x = (*e)[i * 2];
		unsigned int y = (*e)[i * 2 + 1];
		Vec3 v = decodeOctahedral(Vec2ui(x, y), static_cast<float>(maxRange));
		returnVec->push_back(v);
	}

	return returnVec;
}