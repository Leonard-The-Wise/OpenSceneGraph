#pragma once

#include <stdlib.h>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include <osg/Array>

#include <osg/MatrixTransform>
#include <osg/Geode>
#include <osg/Vec3f>

#include <osg/Geometry>
#include <osg/CullFace>
#include <osg/StateSet>
#include <osg/TexGen>
#include <osg/TexMat>

#include <osg/TextureRectangle>
#include <osg/Texture2D>
#include <osg/Texture1D>
#include <osg/Types>
#include <osg/Material>
#include <osg/BlendFunc>

#include <osgText/Text>

#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>
#include <osgDB/WriteFile>
#include <osgDB/ReadFile>

#include <osg/UserDataContainer>

#include <osg/Image>
#include <sstream>

#include <osgAnimation/Animation>
#include <osgAnimation/Channel>
#include <osgAnimation/CubicBezier>
#include <osgAnimation/RigGeometry>
#include <osgAnimation/MorphGeometry>
#include <osgAnimation/Sampler>
#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/StackedTranslateElement>
#include <osgAnimation/StackedQuaternionElement>
#include <osgAnimation/StackedRotateAxisElement>
#include <osgAnimation/StackedMatrixElement>
#include <osgAnimation/StackedScaleElement>

#include <osgAnimation/RigTransformHardware>
#include <osgAnimation/RigTransformSoftware>
#include <osgAnimation/MorphTransformSoftware>
#include <osgAnimation/MorphTransformHardware>


#include <osgSim/ShapeAttribute>

#include "json.hpp"