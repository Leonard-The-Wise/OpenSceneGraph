#pragma once

#include "json.hpp"
#include <osg/MatrixTransform>

enum class DesiredVectorSize {
	Array = 1, Vec2, Vec3, Vec4
};


class ParserHelper
{
public:

	ParserHelper() = default;

	static bool getSafeInteger(const std::string& in, int& outValue);

	static bool getSafeDouble(const std::string& in, double& outValue);

	static bool getBooleanValue(const nlohmann::json& json, const std::string& key, bool defaultValue = false);

	static osg::ref_ptr<osg::Array> recastArray(const osg::ref_ptr<osg::Array>& toRecast, DesiredVectorSize vecSize);

	static osg::ref_ptr<osg::Array> decastVector(const osg::ref_ptr<osg::Array>& toRecast);

	template<typename T>
	static osg::ref_ptr<T> transformArray(osg::ref_ptr<T>& array, osg::Matrix& transform, bool normalize);

};

