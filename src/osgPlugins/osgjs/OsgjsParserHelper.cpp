#include "pch.h"

#include "OsgjsFileCache.h"
#include "OsgjsParserHelper.h"


using namespace osgJSONParser;
using json = nlohmann::json;


const std::unordered_map<std::string, osg::PrimitiveSet::Type> primitiveTypeMap {
	{"DrawElementsUShort", osg::PrimitiveSet::DrawArraysPrimitiveType},
	{"DrawArrays", osg::PrimitiveSet::DrawArraysPrimitiveType},
	{"DrawElementsUInt", osg::PrimitiveSet::DrawElementsUIntPrimitiveType},
	{"DrawElementsUShort", osg::PrimitiveSet::DrawElementsUShortPrimitiveType},
	{"DrawElementsUByte", osg::PrimitiveSet::DrawElementsUBytePrimitiveType},
	{"DrawArrayLengths", osg::PrimitiveSet::DrawArrayLengthsPrimitiveType},
};

// PUBLIC METHODS

bool ParserHelper::getSafeInteger(const std::string& in, int& outValue)
{
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
		osg::notify(osg::WARN) << "Warning, integer parameter out of range" << std::endl;
		return false;
	}
}

bool ParserHelper::getSafeDouble(const std::string& in, double& outValue)
{
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

osg::ref_ptr<osg::Array> ParserHelper::recastArray(const osg::ref_ptr<osg::Array> toRecast, osg::Array::Type arrayType, DesiredVectorSize vecSize)
{
	osg::ref_ptr<osg::Array> returnArray;

	if (vecSize == DesiredVectorSize::Array)
		return nullptr;

	switch (vecSize)
	{
	case DesiredVectorSize::Vec2:
	{
		// Certify the array contains appropriate number of elements
		if (toRecast->getNumElements() % 2 != 0)
		{
			osg::notify(osg::WARN) << "WARNING: Array has incorrect size. Ignoring!" << std::endl;
			return nullptr;
		}

		int totalElements = toRecast->getNumElements() / 2;
		switch (arrayType)
		{
		case osg::Array::FloatArrayType:
		{
			returnArray = new osg::Vec2Array;
			returnArray->reserveArray(totalElements);
			osg::FloatArray* converted = dynamic_cast<osg::FloatArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2 newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2Array*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UByteArrayType:
		{
			returnArray = new osg::Vec2ubArray;
			returnArray->reserveArray(totalElements);
			osg::UByteArray* converted = dynamic_cast<osg::UByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2ub newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2ubArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UShortArrayType:
		{
			returnArray = new osg::Vec2usArray;
			returnArray->reserveArray(totalElements);
			osg::UShortArray* converted = dynamic_cast<osg::UShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2us newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2usArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UIntArrayType:
		{
			returnArray = new osg::Vec2uiArray;
			returnArray->reserveArray(totalElements);
			osg::UIntArray* converted = dynamic_cast<osg::UIntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2ui newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::ByteArrayType:
		{
			returnArray = new osg::Vec2bArray;
			returnArray->reserveArray(totalElements);
			osg::ByteArray* converted = dynamic_cast<osg::ByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2b newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2bArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::ShortArrayType:
		{
			returnArray = new osg::Vec2sArray;
			returnArray->reserveArray(totalElements);
			osg::ShortArray* converted = dynamic_cast<osg::ShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2s newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2sArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::IntArrayType:
		{
			returnArray = new osg::Vec2iArray;
			returnArray->reserveArray(totalElements);
			osg::IntArray* converted = dynamic_cast<osg::IntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec2i newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<osg::Vec2iArray*>(returnArray.get())->push_back(newVec);
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
			osg::notify(osg::WARN) << "WARNING: Array has incorrect size. Ignoring!" << std::endl;
			return nullptr;
		}

		int totalElements = toRecast->getNumElements() / 3;
		switch (arrayType)
		{
		case osg::Array::FloatArrayType:
		{
			returnArray = new osg::Vec3Array;
			returnArray->reserveArray(totalElements);
			osg::FloatArray* converted = dynamic_cast<osg::FloatArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3 newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3Array*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UByteArrayType:
		{
			returnArray = new osg::Vec3ubArray;
			returnArray->reserveArray(totalElements);
			osg::UByteArray* converted = dynamic_cast<osg::UByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3ub newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3ubArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UShortArrayType:
		{
			returnArray = new osg::Vec3usArray;
			returnArray->reserveArray(totalElements);
			osg::UShortArray* converted = dynamic_cast<osg::UShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3us newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3usArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UIntArrayType:
		{
			returnArray = new osg::Vec3uiArray;
			returnArray->reserveArray(totalElements);
			osg::UIntArray* converted = dynamic_cast<osg::UIntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3ui newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::ByteArrayType:
		{
			returnArray = new osg::Vec3bArray;
			returnArray->reserveArray(totalElements);
			osg::ByteArray* converted = dynamic_cast<osg::ByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3b newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3bArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::ShortArrayType:
		{
			returnArray = new osg::Vec3sArray;
			returnArray->reserveArray(totalElements);
			osg::ShortArray* converted = dynamic_cast<osg::ShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3s newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3sArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::IntArrayType:
		{
			returnArray = new osg::Vec3iArray;
			returnArray->reserveArray(totalElements);
			osg::IntArray* converted = dynamic_cast<osg::IntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec3i newVec((*converted)[3 * i], (*converted)[3 * i + 1], (*converted)[3 * i + 2]);
				dynamic_cast<osg::Vec3iArray*>(returnArray.get())->push_back(newVec);
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
			osg::notify(osg::WARN) << "WARNING: Array has incorrect size. Ignoring!" << std::endl;
			return nullptr;
		}

		int totalElements = toRecast->getNumElements() / 4;
		switch (arrayType)
		{
		case osg::Array::FloatArrayType:
		{
			returnArray = new osg::Vec4Array;
			returnArray->reserveArray(totalElements);
			osg::FloatArray* converted = dynamic_cast<osg::FloatArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4 newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4Array*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UByteArrayType:
		{
			returnArray = new osg::Vec4ubArray;
			returnArray->reserveArray(totalElements);
			osg::UByteArray* converted = dynamic_cast<osg::UByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4ub newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4ubArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UShortArrayType:
		{
			returnArray = new osg::Vec4usArray;
			returnArray->reserveArray(totalElements);
			osg::UShortArray* converted = dynamic_cast<osg::UShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4us newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4usArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::UIntArrayType:
		{
			returnArray = new osg::Vec4uiArray;
			returnArray->reserveArray(totalElements);
			osg::UIntArray* converted = dynamic_cast<osg::UIntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4ui newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4uiArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::ByteArrayType:
		{
			returnArray = new osg::Vec4bArray;
			returnArray->reserveArray(totalElements);
			osg::ByteArray* converted = dynamic_cast<osg::ByteArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4b newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4bArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::ShortArrayType:
		{
			returnArray = new osg::Vec4sArray;
			returnArray->reserveArray(totalElements);
			osg::ShortArray* converted = dynamic_cast<osg::ShortArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4s newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4sArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		case osg::Array::IntArrayType:
		{
			returnArray = new osg::Vec4iArray;
			returnArray->reserveArray(totalElements);
			osg::IntArray* converted = dynamic_cast<osg::IntArray*>(toRecast.get());
			if (!converted)
				return nullptr;
			for (int i = 0; i < totalElements; i++)
			{
				osg::Vec4i newVec((*converted)[4 * i], (*converted)[4 * i + 1], (*converted)[4 * i + 2], (*converted)[4 * i + 3]);
				dynamic_cast<osg::Vec4iArray*>(returnArray.get())->push_back(newVec);
			}
			break;
		}
		}
		break;
	}
	}

	return returnArray;
}

osg::ref_ptr<osg::Array> ParserHelper::parseJSONArray(const json& currentJSONNode, int elementsPerItem, FileCache& fileCache)
{
#ifdef DEBUG
	std::string CurrentNode = currentJSONNode.dump();
#endif
	osg::ref_ptr<osg::Array> returnArray;
	osg::Array::Type arrayType{};
	const json* elementsNode = nullptr;
	int elementTypeSize = 0;

	if (elementsPerItem < 1 || elementsPerItem > 4)
	{
		osg::notify(osg::WARN) << "WARNING: Error importing array. Field 'ItemSize' not between 1 and 4. Ignoring..." << std::endl;
		return nullptr;
	}

	// 1) Determine Array Elements type
	if (currentJSONNode.contains("Float32Array") && currentJSONNode["Float32Array"].is_object())
	{ 
		returnArray = new osg::FloatArray;
		arrayType = osg::Array::FloatArrayType;
		elementTypeSize = sizeof(float);
		elementsNode = &currentJSONNode["Float32Array"];
	}
	else if (currentJSONNode.contains("Uint8Array") && currentJSONNode["Uint8Array"].is_object())
	{
		returnArray = new osg::UByteArray;
		arrayType = osg::Array::UByteArrayType;
		elementTypeSize = sizeof(uint8_t);
		elementsNode = &currentJSONNode["Uint8Array"];
	}
	else if (currentJSONNode.contains("Uint16Array") && currentJSONNode["Uint16Array"].is_object())
	{
		returnArray = new osg::UShortArray;
		arrayType = osg::Array::UShortArrayType;
		elementTypeSize = sizeof(uint16_t);
		elementsNode = &currentJSONNode["Uint16Array"];
	}
	else if (currentJSONNode.contains("Uint32Array") && currentJSONNode["Uint32Array"].is_object())
	{
		returnArray = new osg::UIntArray;
		arrayType = osg::Array::UIntArrayType;
		elementTypeSize = sizeof(uint32_t);
		elementsNode = &currentJSONNode["Uint32Array"];
	}
	else if (currentJSONNode.contains("Int8Array") && currentJSONNode["Int8Array"].is_object())
	{
		returnArray = new osg::ByteArray;
		arrayType = osg::Array::ByteArrayType;
		elementTypeSize = sizeof(int8_t);
		elementsNode = &currentJSONNode["Int8Array"];
	}
	else if (currentJSONNode.contains("Int16Array") && currentJSONNode["Int16Array"].is_array())
	{
		returnArray = new osg::ShortArray;
		arrayType = osg::Array::ShortArrayType;
		elementTypeSize = sizeof(int16_t);
		elementsNode = &currentJSONNode["Int16Array"];
	}
	else if (currentJSONNode.contains("Int32Array") && currentJSONNode["Int32Array"].is_object())
	{
		returnArray = new osg::IntArray;
		arrayType = osg::Array::IntArrayType;
		elementTypeSize = sizeof(int32_t);
		elementsNode = &currentJSONNode["Int32Array"];
	}

	// 2) Determine Write Mode: inline or file

	// 2.1) Inline arrays
	if (elementsNode && (*elementsNode).contains("Elements") && (*elementsNode)["Elements"].is_array())
	{
		returnArray->reserveArray((*elementsNode)["Elements"].size());

		switch (arrayType)
		{
		case osg::Array::FloatArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::FloatArray*>(returnArray.get())->push_back(element.get<float>());
			break;
		case osg::Array::UByteArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::UByteArray*>(returnArray.get())->push_back(element.get<uint8_t>());
			break;
		case osg::Array::UShortArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::UShortArray*>(returnArray.get())->push_back(element.get<uint16_t>());
			break;
		case osg::Array::UIntArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::UIntArray*>(returnArray.get())->push_back(element.get<uint32_t>());
			break;
		case osg::Array::ByteArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::ByteArray*>(returnArray.get())->push_back(element.get<int8_t>());
			break;
		case osg::Array::ShortArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::ShortArray*>(returnArray.get())->push_back(element.get<int16_t>());
			break;
		case osg::Array::IntArrayType:
			for (auto& element : (*elementsNode)["Elements"])
				dynamic_cast<osg::IntArray*>(returnArray.get())->push_back(element.get<int32_t>());
			break;
		default:
			osg::notify(osg::WARN) << "WARNING: Unknown Array Type." << std::endl;
			return nullptr;
		}
	}

	// 2.2) File Mode
	else if (elementsNode && (*elementsNode).contains("File") && (*elementsNode)["File"].get<std::string>() != "")
	{
		std::string fileName = (*elementsNode)["File"].get<std::string>();
		int itemCount = (*elementsNode).contains("Size") ? (*elementsNode)["Size"].get<int>() : 0;
		int readOffset = (*elementsNode).contains("Offset") ? (*elementsNode)["Offset"].get<int>() : 0;
		int totalElements = itemCount * elementsPerItem;
		size_t totalBytesSize = static_cast<size_t>(totalElements * elementTypeSize + readOffset);
		std::vector<uint8_t>* elementsBytes = fileCache.getFileBuffer(fileName);
		std::vector<uint8_t>* elementsBytesConverted = nullptr;
		
		if (elementsBytes)
		{
			// Verify size - only valid for non-compressed items
			if ((elementsBytes->size() < totalBytesSize) && !(*elementsNode).contains("Encoding"))
			{
				osg::notify(osg::WARN) << "WARNING: Error reading " << fileName << ". " <<
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

			// Read and Copy bytes
			bool readFail = false;
			returnArray->reserveArray(totalElements);
			switch (arrayType)
			{
			case osg::Array::FloatArrayType:
			{
				const float* floatData = reinterpret_cast<const float*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::FloatArray*>(returnArray.get())->push_back(floatData[i]);
				}
				break;
			}
			case osg::Array::ByteArrayType:
			{
				const int8_t* byteData = reinterpret_cast<const int8_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::ByteArray*>(returnArray.get())->push_back(byteData[i]);
				}
				break;
			}
			case osg::Array::UByteArrayType:
			{
				const uint8_t* ubyteData = reinterpret_cast<const uint8_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::UByteArray*>(returnArray.get())->push_back(ubyteData[i]);
				}
				break;
			}
			case osg::Array::ShortArrayType:
			{
				const int16_t* shortData = reinterpret_cast<const int16_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::ShortArray*>(returnArray.get())->push_back(shortData[i]);
				}
				break;
			}
			case osg::Array::UShortArrayType:
			{
				const uint16_t* ushortData = reinterpret_cast<const uint16_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::UShortArray*>(returnArray.get())->push_back(ushortData[i]);
				}
				break;
			}
			case osg::Array::IntArrayType:
			{
				const int32_t* intData = reinterpret_cast<const int32_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::IntArray*>(returnArray.get())->push_back(intData[i]);
				}
				break;
			}
			case osg::Array::UIntArrayType:
			{
				const uint16_t* uintData = reinterpret_cast<const uint16_t*>(elementsBytes->data() + readOffset);
				for (size_t i = 0; i < totalElements; ++i) {
					dynamic_cast<osg::UIntArray*>(returnArray.get())->push_back(uintData[i]);
				}
				break;
			}
			default:
				osg::notify(osg::WARN) << "WARNING: Unknown Array Type." << std::endl;
				return nullptr;
			}

			if (elementsBytesConverted)
				delete elementsBytesConverted;

			if (readFail)
			{
				osg::notify(osg::WARN) << "WARNING: File Array have incorrect size." << std::endl;
				return nullptr;
			}
		}
	}

	// 3) Convert Element nodes to Vectors if it applies.
	if (returnArray && elementsPerItem > 1)
	{
		returnArray = recastArray(returnArray, arrayType, static_cast<DesiredVectorSize>(elementsPerItem));
		
	}

	return returnArray;
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

osg::BlendFunc::BlendFuncMode ParserHelper::getBlendFuncFromString(const std::string& blendFunc)
{
	if (blendFunc == "DST_ALPHA") return osg::BlendFunc::DST_ALPHA;
	if (blendFunc == "DST_COLOR") return osg::BlendFunc::DST_COLOR;
	if (blendFunc == "ONE") return osg::BlendFunc::ONE;
	if (blendFunc == "ONE_MINUS_DST_ALPHA") return osg::BlendFunc::ONE_MINUS_DST_ALPHA;
	if (blendFunc == "ONE_MINUS_DST_COLOR") return osg::BlendFunc::ONE_MINUS_DST_COLOR;
	if (blendFunc == "ONE_MINUS_SRC_ALPHA") return osg::BlendFunc::ONE_MINUS_SRC_ALPHA;
	if (blendFunc == "ONE_MINUS_SRC_COLOR") return osg::BlendFunc::ONE_MINUS_SRC_COLOR;
	if (blendFunc == "SRC_ALPHA") return osg::BlendFunc::SRC_ALPHA;
	if (blendFunc == "SRC_ALPHA_SATURATE") return osg::BlendFunc::SRC_ALPHA_SATURATE;
	if (blendFunc == "SRC_COLOR") return osg::BlendFunc::SRC_COLOR;
	if (blendFunc == "CONSTANT_COLOR") return osg::BlendFunc::CONSTANT_COLOR;
	if (blendFunc == "ONE_MINUS_CONSTANT_COLOR") return osg::BlendFunc::ONE_MINUS_CONSTANT_COLOR;
	if (blendFunc == "CONSTANT_ALPHA") return osg::BlendFunc::CONSTANT_ALPHA;
	if (blendFunc == "ONE_MINUS_CONSTANT_ALPHA") return osg::BlendFunc::ONE_MINUS_CONSTANT_ALPHA;
	if (blendFunc == "ZERO") return osg::BlendFunc::ZERO;

	return osg::BlendFunc::ONE;
}

osg::Texture::FilterMode ParserHelper::getFilterModeFromString(const std::string& filterMode)
{
	if (filterMode == "LINEAR") return osg::Texture::FilterMode::LINEAR;
	if (filterMode == "LINEAR_MIPMAP_LINEAR") return osg::Texture::FilterMode::LINEAR_MIPMAP_LINEAR;
	if (filterMode == "LINEAR_MIPMAP_NEAREST") return osg::Texture::FilterMode::LINEAR_MIPMAP_NEAREST;
	if (filterMode == "NEAREST") return osg::Texture::FilterMode::NEAREST;
	if (filterMode == "NEAREST_MIPMAP_LINEAR") return osg::Texture::FilterMode::NEAREST_MIPMAP_LINEAR;
	if (filterMode == "NEAREST_MIPMAP_NEAREST") return osg::Texture::FilterMode::NEAREST_MIPMAP_NEAREST;

	return osg::Texture::FilterMode::LINEAR;
}

osg::Texture::WrapMode ParserHelper::getWrapModeFromString(const std::string& wrapMode)
{
	if (wrapMode == "CLAMP_TO_EDGE") return osg::Texture::WrapMode::CLAMP_TO_EDGE;
	if (wrapMode == "CLAMP_TO_BORDER") return osg::Texture::WrapMode::CLAMP_TO_BORDER;
	if (wrapMode == "REPEAT") return osg::Texture::WrapMode::REPEAT;
	if (wrapMode == "MIRROR") return osg::Texture::WrapMode::MIRROR;

	return osg::Texture::WrapMode::CLAMP_TO_EDGE;
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

void ParserHelper::makeInfluenceMap(osgAnimation::RigGeometry* rigGeometry, const osg::ref_ptr<osg::Array> bones, const osg::ref_ptr<osg::Array> weights,
	const std::map<int, std::string>& boneIndexes)
{
	osg::ref_ptr<osgAnimation::VertexInfluenceMap> influenceMap = new osgAnimation::VertexInfluenceMap;

	// The most common type [not sure if it has others]
	osg::ref_ptr<osg::Vec4usArray> bonesVec = osg::dynamic_pointer_cast<osg::Vec4usArray>(bones);
	osg::ref_ptr<osg::Vec4Array> weightsVec = osg::dynamic_pointer_cast<osg::Vec4Array>(weights);

	if (bones && !bonesVec)
	{
		osg::notify(osg::WARN) << "WARNING: Unsuported bones array for RigGeometry. Must be Vec4usArray type. " << rigGeometry->getName() << std::endl;
		return;
	}

	if (weights && !weightsVec)
	{
		osg::notify(osg::WARN) << "WARNING: Unsuported weights for RigGeometry. Must be Vec4Array type. " << rigGeometry->getName() << std::endl;
		return;
	}


	if (!bonesVec && !weightsVec)
		return;

	if (!bonesVec || !weightsVec)
	{
		osg::notify(osg::WARN) << "WARNING: Missing either bones or weights array for RigGeometry " << rigGeometry->getName() << std::endl;
		return;
	}

	if (bonesVec->getNumElements() != weightsVec->getNumElements())
	{
		osg::notify(osg::WARN) << "WARNING: Number of bone indices don't match number of weight indices for RigGeometry " << rigGeometry->getName() << std::endl;
		return;
	}

	// Build influence map
	int elementSize = bones->getDataSize();
	for (unsigned int vertexIndex = 0; vertexIndex < bonesVec->getNumElements(); ++vertexIndex)
	{
		const osg::Vec4us& boneIndices = (*bonesVec)[vertexIndex];
		const osg::Vec4& boneWeights = (*weightsVec)[vertexIndex];

		for (int boneIndex = 0; boneIndex < elementSize; ++boneIndex)
		{
			uint16_t boneID = boneIndices[boneIndex];
			float weight = boneWeights[boneIndex];

			if (weight > 0.0f)
			{
				if (boneIndexes.find(boneID) == boneIndexes.end())
				{
					osg::notify(osg::WARN) << "WARNING: Bone index " << boneID << " not found! [" << rigGeometry->getName() << "]" << std::endl;
					continue;
				}
				std::string boneName = boneIndexes.at(boneID);

				(*influenceMap)[boneName].push_back(std::make_pair(vertexIndex, weight));
			}
		}
	}

	rigGeometry->setInfluenceMap(influenceMap);
}



// PRIVATE METHODS

bool ParserHelper::getPrimitiveType(const json& currentJSONNode, osg::PrimitiveSet::Type& outPrimitiveType)
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

template <typename T>
void ParserHelper::copyIntToByteVector(T value, std::vector<uint8_t>& vec)
{
	static_assert(std::is_integral<T>::value, "T must be of integral type.");
	uint8_t* bytes = reinterpret_cast<uint8_t*>(&value);

	for (size_t i = 0; i < sizeof(T); ++i) {
		vec.push_back(bytes[i]);
	}
}

uint32_t ParserHelper::decodeVarInt(const uint8_t* const data, int& decoded_bytes)
{
	int i = 0;
	uint32_t decoded_value = 0;
	int shift_amount = 0;

	do
	{
		decoded_value |= (uint32_t)(data[i] & 0x7F) << shift_amount;
		shift_amount += 7;
	} while ((data[i++] & 0x80) != 0);

	decoded_bytes = i;
	return decoded_value;
}

std::vector<uint8_t>* ParserHelper::decodeVarintVector(const std::vector<uint8_t>& input, osg::Array::Type inputType, size_t itemCount, size_t offSet)
{
	std::vector<uint8_t>* parsedVector = new std::vector<uint8_t>;

	size_t parsedSize = 0;
	int parsedItemCount = 0;
	while (parsedItemCount < itemCount)
	{
		int decodedBytes = 0;
		uint32_t decodedValue = 0;
		try
		{
			decodedValue = decodeVarInt(input.data() + offSet + parsedSize, decodedBytes);
		}
		catch (std::exception)
		{
			osg::notify(osg::WARN) << "WARNING: Error while decoding input vector!" << std::endl;
			return nullptr;
		}

		switch (inputType)
		{
		case osg::Array::ByteArrayType:
		case osg::Array::UByteArrayType:
			copyIntToByteVector(static_cast<uint8_t>(decodedValue), (*parsedVector));
			break;
		case osg::Array::ShortArrayType:
		case osg::Array::UShortArrayType:
			copyIntToByteVector(static_cast<uint16_t>(decodedValue), (*parsedVector));
			break;
		case osg::Array::IntArrayType:
		case osg::Array::UIntArrayType:
			copyIntToByteVector(static_cast<uint32_t>(decodedValue), (*parsedVector));
			break;
		}

		parsedItemCount++;
		parsedSize += decodedBytes;
	}

	return parsedVector;
}


osg::Array* ParserHelper::getVertexAttribArray(osgAnimation::RigGeometry& rigGeometry, const std::string arrayName) {
	for (unsigned int i = 0; i < rigGeometry.getNumVertexAttribArrays(); ++i) {
		osg::Array* attribute = rigGeometry.getVertexAttribArray(i);
		bool isBones = false;
		if (attribute && attribute->getUserValue(arrayName, isBones) && isBones) {
			return attribute;
		}
	}
	return 0;
}

