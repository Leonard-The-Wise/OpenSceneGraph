
#include "pch.h"
#include "ParserHelper.h"

using namespace osg;


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
				Vec2i newVec((*converted)[2 * i], (*converted)[2 * i + 1]);
				dynamic_cast<Vec2iArray*>(returnArray.get())->push_back(newVec);
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
osg::ref_ptr<T> ParserHelper::transformArray(osg::ref_ptr<T>& array, osg::Matrix& transform, bool normalize)
{
	osg::ref_ptr<osg::Array> returnArray;
	osg::Matrix transposeInverse = transform;
	transposeInverse.transpose(transposeInverse);
	transposeInverse = osg::Matrix::inverse(transposeInverse);

	switch (array->getType())
	{
	case osg::Array::Vec4ArrayType:
	{
		returnArray = new osg::Vec4Array();
		returnArray->reserveArray(array->getNumElements());
		for (auto& vec : *osg::dynamic_pointer_cast<const osg::Vec4Array>(array))
		{
			osg::Vec4 v;
			if (normalize)
			{
				osg::Vec3 tangentVec3;
				if (vec.x() == 0.0f && vec.y() == 0.0f && vec.z() == 0.0f) // Fix non-direction vectors (paliative)
					tangentVec3 = osg::Vec3(1.0f, 0.0f, 0.0f);
				else
					tangentVec3 = osg::Vec3(vec.x(), vec.y(), vec.z());
				tangentVec3 = tangentVec3 * transposeInverse;
				tangentVec3.normalize();
				v = osg::Vec4(tangentVec3.x(), tangentVec3.y(), tangentVec3.z(), vec.w());
			}
			else
				v = vec * transform;
			osg::dynamic_pointer_cast<osg::Vec4Array>(returnArray)->push_back(v);
		}
		break;
	}
	case osg::Array::Vec3ArrayType:
	{
		returnArray = new osg::Vec3Array();
		returnArray->reserveArray(array->getNumElements());
		for (auto& vec : *osg::dynamic_pointer_cast<const osg::Vec3Array>(array))
		{
			osg::Vec3 v;
			if (normalize)
			{
				v = vec * transposeInverse;
				if (v.x() == 0.0f && v.y() == 0.0f && v.z() == 0.0f)  // Fix non-direction vector (paliative)
					v = osg::Vec3(1.0f, 0.0f, 0.0f);
				v.normalize();
			}
			else
				v = vec * transform;

			osg::dynamic_pointer_cast<osg::Vec3Array>(returnArray)->push_back(v);
		}
		break;
	}
	default:
		OSG_WARN << "WARNING: Unsuported array to transform." << std::endl;
	}

	return osg::dynamic_pointer_cast<T>(returnArray);
}
