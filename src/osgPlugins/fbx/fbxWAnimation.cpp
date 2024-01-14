// -*-c++-*-

/*
 * FBX writer for Open Scene Graph
 *
 * Copyright (C) 2009
 *
 * Writing support added 2009 by Thibault Caporal and Sukender (Benoit Neil - http://sukender.free.fr)
 * Writing Rigging, Textures, Materials and Animations added 2024 by Leonardo Silva (https://github.com/Leonard-The-Wise/)
 *
 * The Open Scene Graph (OSG) is a cross platform C++/OpenGL library for
 * real-time rendering of large 3D photo-realistic models.
 * The OSG homepage is http://www.openscenegraph.org/
 */

#include <climits>                     // required for UINT_MAX
#include <cassert>
#include <osg/CullFace>
#include <osg/MatrixTransform>
#include <osg/NodeVisitor>
#include <osg/PrimitiveSet>
#include <osgDB/FileUtils>
#include <osgDB/WriteFile>

#include <osgAnimation/BasicAnimationManager>
#include <osgAnimation/Animation>
#include <osgAnimation/UpdateBone>
#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/StackedTranslateElement>
#include <osgAnimation/StackedQuaternionElement>
#include <osgAnimation/StackedRotateAxisElement>
#include <osgAnimation/StackedMatrixElement>
#include <osgAnimation/StackedScaleElement>


#include "WriterNodeVisitor.h"

using namespace osg;
using namespace osgAnimation;


namespace pluginfbx
{


	inline FbxAnimStack* WriterNodeVisitor::getOrCreateAnimStack()
	{
		FbxAnimStack* animStack;
		if (_pScene->GetSrcObjectCount<FbxAnimStack>() == 0)
		{
			animStack = FbxAnimStack::Create(_pSdkManager, "Global Animations");
			_pScene->SetCurrentAnimationStack(animStack);
		}
		else
		{
			animStack = _pScene->GetSrcObject<FbxAnimStack>(0);
		}

		return animStack;
	}

	void AddVec3Keyframes(osgAnimation::Vec3LinearChannel* transformChannel, 
		FbxNode* animCurveNode, FbxAnimLayer* fbxAnimLayer, std::string channelName)
	{
		if (!transformChannel || !animCurveNode)
		{
			return;
		}

		// Obtenha as curvas de animação para X, Y e Z do nó de translação.
		FbxAnimCurve* curveX(nullptr);
		FbxAnimCurve* curveY(nullptr);
		FbxAnimCurve* curveZ(nullptr);

		bool bTranslate(false);
		if (channelName == "translate")
		{
			curveX = animCurveNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			curveY = animCurveNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			curveZ = animCurveNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
			bTranslate = true;
		}
		else if (channelName == "scale")
		{
			curveX = animCurveNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			curveY = animCurveNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			curveZ = animCurveNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
		}
		else
		{
			OSG_WARN << "WARNING: Animation channel contains invalid name: " << channelName << std::endl;
			return;
		}

		// Obtenha o KeyframeContainer do canal de translação.
		osgAnimation::Vec3KeyframeContainer* keyframes = transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

		// Itere sobre todos os keyframes do canal de translação e adicione-os às curvas FBX.
		for (unsigned int i = 0; i < keyframes->size(); ++i) 
		{
			const osgAnimation::Vec3Keyframe& keyframe = (*keyframes)[i];

			// Converta o tempo do OSG para o tempo do FBX.
			FbxTime fbxTime;
			fbxTime.SetSecondDouble(keyframe.getTime());
			Vec3 keyValue = keyframe.getValue();

			// Adicione os valores de translação para cada eixo às curvas correspondentes.
			curveX->KeySet(curveX->KeyAdd(fbxTime), fbxTime, keyValue.x(), FbxAnimCurveDef::eInterpolationConstant);
			curveY->KeySet(curveY->KeyAdd(fbxTime), fbxTime, keyValue.y(), FbxAnimCurveDef::eInterpolationConstant);
			curveZ->KeySet(curveZ->KeyAdd(fbxTime), fbxTime, keyValue.z(), FbxAnimCurveDef::eInterpolationConstant);
		}
	}

	void AddQuatSlerpKeyframes(osgAnimation::QuatSphericalLinearChannel* transformChannel,
		FbxNode* animCurveNode, FbxAnimLayer* fbxAnimLayer)
	{
		if (!transformChannel || !animCurveNode)
		{
			return;
		}

		FbxAnimCurve* curveX = animCurveNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
		FbxAnimCurve* curveY = animCurveNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
		FbxAnimCurve* curveZ = animCurveNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

		osgAnimation::QuatKeyframeContainer* keyframes = transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

		for (unsigned int i = 0; i < keyframes->size(); ++i)
		{
			const osgAnimation::QuatKeyframe& keyframe = (*keyframes)[i];

			FbxTime fbxTime;
			fbxTime.SetSecondDouble(keyframe.getTime());

			Quat quat = keyframe.getValue();

			FbxQuaternion q(quat.x(), quat.y(), quat.z(), quat.w());
			FbxAMatrix mat;
			mat.SetQ(q);
			FbxVector4 vec4 = mat.GetR();
			FbxDouble3 euler = FbxDouble3(vec4[0], vec4[1], vec4[2]);

			curveX->KeySet(curveX->KeyAdd(fbxTime), fbxTime, euler[0], FbxAnimCurveDef::eInterpolationConstant);
			curveY->KeySet(curveY->KeyAdd(fbxTime), fbxTime, euler[1], FbxAnimCurveDef::eInterpolationConstant);
			curveZ->KeySet(curveZ->KeyAdd(fbxTime), fbxTime, euler[2], FbxAnimCurveDef::eInterpolationConstant);
		}
	}

	void AddFloatKeyframes(osgAnimation::FloatLinearChannel* transformChannel,
		FbxBlendShapeChannel* blendShapeChannel, FbxAnimLayer* fbxAnimLayer)
	{
		if (!transformChannel || !blendShapeChannel)
		{
			return;
		}
		FbxAnimCurve* curve = blendShapeChannel->DeformPercent.GetCurve(fbxAnimLayer, true);

		osgAnimation::FloatKeyframeContainer* keyframes = transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

		for (unsigned int i = 0; i < keyframes->size(); ++i)
		{
			const osgAnimation::FloatKeyframe& keyframe = (*keyframes)[i];

			FbxTime fbxTime;
			fbxTime.SetSecondDouble(keyframe.getTime());

			float value = keyframe.getValue();

			curve->KeySet(curve->KeyAdd(fbxTime), fbxTime, value, FbxAnimCurveDef::eInterpolationConstant);
		}
	}

	void WriterNodeVisitor::createAnimationLayer(const osg::ref_ptr<osgAnimation::Animation> osgAnimation)
	{
		std::string animationName = osgAnimation->getName().c_str();

		FbxAnimStack* animStack = FbxAnimStack::Create(_pScene, animationName.c_str());
		_pScene->SetCurrentAnimationStack(animStack);
		FbxAnimLayer* fbxAnimLayer = FbxAnimLayer::Create(_pScene, animationName.c_str());
		animStack->AddMember(fbxAnimLayer);

		bool NotImplemented1(false), NotImplemented2(false);

		for (auto& channel : osgAnimation->getChannels()) 
		{
			std::string targetName = channel->getTargetName();

			auto boneAnimCurveNodeIter = _matrixAnimCurveMap.find(targetName);
			auto morphAnimNodeIter = _blendShapeAnimations.find(targetName);
			if (boneAnimCurveNodeIter == _matrixAnimCurveMap.end() && morphAnimNodeIter == _blendShapeAnimations.end())
			{
				continue;
			}

			if (boneAnimCurveNodeIter != _matrixAnimCurveMap.end())
			{
				auto& boneAnimCurveNodes = boneAnimCurveNodeIter->second;

				if (auto transformChannel = dynamic_pointer_cast<Vec3LinearChannel>(channel))
				{
					AddVec3Keyframes(transformChannel, boneAnimCurveNodes->fbxNode, fbxAnimLayer, transformChannel->getName());
				}
				else if (auto rotateChannel = dynamic_pointer_cast<QuatSphericalLinearChannel>(channel))
				{
					AddQuatSlerpKeyframes(rotateChannel, boneAnimCurveNodes->fbxNode, fbxAnimLayer);
				}
			} 
			else if (morphAnimNodeIter != _blendShapeAnimations.end())
			{

				if (auto morphChannel = dynamic_pointer_cast<FloatLinearChannel>(channel))
				{
					AddFloatKeyframes(morphChannel, morphAnimNodeIter->second, fbxAnimLayer);
				}
			}

			// TODO: Add these channels (examples needed)
			else if (auto floatCubicChannel = dynamic_pointer_cast<FloatCubicBezierChannel>(channel))
			{
				if (!NotImplemented1) // Avoid message spam
					OSG_WARN << "WARNING: Animations based on FloatCubicBezierChannel are not yet implemented!" << std::endl;

				NotImplemented1 = true;
			}
			else if (auto vec3CubicChannel = dynamic_pointer_cast<Vec3CubicBezierChannel>(channel))
			{
				if (!NotImplemented2) // Avoid message spam
					OSG_WARN << "WARNING: Animations based on Vec3CubicBezierChannel are not yet implemented!" << std::endl;

				NotImplemented2 = true;
			}
		}
	}


	// Call this only after all node's children are already processed
	void WriterNodeVisitor::applyAnimations(const osg::ref_ptr<osg::Callback>& callback)
	{
		if (!callback)
			return;

		// Create Static Pose
		FbxAnimStack* animStack = FbxAnimStack::Create(_pScene, "Static Pose");
		_pScene->SetCurrentAnimationStack(animStack);
		FbxAnimLayer* fbxAnimLayer = FbxAnimLayer::Create(_pScene, "Static Pose");
		animStack->AddMember(fbxAnimLayer);

		// Read animation takes
		auto bam = dynamic_pointer_cast<BasicAnimationManager>(callback);
		if (!bam)
			return;

		OSG_NOTICE << "Processing " << bam->getAnimationList().size() << " animation(s)..." << std::endl;

		// Run through all animations
		for (auto& animation : bam->getAnimationList())
		{
			createAnimationLayer(animation);
		}
	}


}


