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
			animStack = FbxAnimStack::Create(_pScene, "Animations Stack");
			_pScene->SetCurrentAnimationStack(animStack);
		}
		else
		{
			animStack = _pScene->GetSrcObject<FbxAnimStack>(0);
		}

		return animStack;
	}

	/*
	//void AddVec3Keyframes(osgAnimation::Vec3CubicBezierChannel* transformChannel,
	//	FbxAnimCurveNode* animCurveNode, FbxAnimLayer* fbxAnimLayer)
	//{
	//	if (!transformChannel || !animCurveNode)
	//	{
	//		return;
	//	}

	//	// Obtenha as curvas de animação para X, Y e Z do nó de translação.
	//	FbxAnimCurve* curveX = animCurveNode->CreateCurve("X");
	//	FbxAnimCurve* curveY = animCurveNode->CreateCurve("Y");
	//	FbxAnimCurve* curveZ = animCurveNode->CreateCurve("Z");

	//	// Obtenha o KeyframeContainer do canal de translação.
	//	osgAnimation::Vec3CubicBezierKeyframeContainer* keyframes = transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

	//	// Itere sobre todos os keyframes do canal de translação e adicione-os às curvas FBX.
	//	for (unsigned int i = 0; i < keyframes->size(); ++i)
	//	{
	//		const osgAnimation::Vec3CubicBezierKeyframe& keyframe = (*keyframes)[i];

	//		// Converta o tempo do OSG para o tempo do FBX.
	//		FbxTime fbxTime;
	//		fbxTime.SetSecondDouble(keyframe.getTime());

	//		// FIXME
	//		// Adicione os valores de translação para cada eixo às curvas correspondentes.
	//		// curveX->KeySet(curveX->KeyAdd(fbxTime), fbxTime, keyframe.getValue().x(), fbxInterpolationMode);
	//		// curveY->KeySet(curveY->KeyAdd(fbxTime), fbxTime, keyframe.getValue().y(), fbxInterpolationMode);
	//		// curveZ->KeySet(curveZ->KeyAdd(fbxTime), fbxTime, keyframe.getValue().z(), fbxInterpolationMode);
	//	}
	//}
	*/

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

		if (channelName == "translate")
		{
			curveX = animCurveNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			curveY = animCurveNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			curveZ = animCurveNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
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

			// Adicione os valores de translação para cada eixo às curvas correspondentes.
			curveX->KeySet(curveX->KeyAdd(fbxTime), fbxTime, keyframe.getValue().x(), FbxAnimCurveDef::eInterpolationConstant);
			curveY->KeySet(curveY->KeyAdd(fbxTime), fbxTime, keyframe.getValue().y(), FbxAnimCurveDef::eInterpolationConstant);
			curveZ->KeySet(curveZ->KeyAdd(fbxTime), fbxTime, keyframe.getValue().z(), FbxAnimCurveDef::eInterpolationConstant);
		}
	}

	void AddQuatSlerpKeyframes(osgAnimation::QuatSphericalLinearChannel* transformChannel,
		FbxNode* animCurveNode, FbxAnimLayer* fbxAnimLayer, std::string targetBoneName)
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

	void WriterNodeVisitor::createAnimationLayer(const osg::ref_ptr<osgAnimation::Animation> osgAnimation)
	{
		std::string animationName = osgAnimation->getName().c_str();
		FbxAnimLayer* fbxAnimLayer = FbxAnimLayer::Create(_pSdkManager, animationName.c_str());
		getOrCreateAnimStack()->AddMember(fbxAnimLayer);

		for (auto& channel : osgAnimation->getChannels()) 
		{
			std::string targetBoneName = channel->getTargetName();

			// FIXME: Add Morph target
			auto boneAnimCurveNodeIter = _boneAnimCurveMap.find(targetBoneName);
			if (boneAnimCurveNodeIter == _boneAnimCurveMap.end())
			{
				continue;
			}

			auto& boneAnimCurveNodes = boneAnimCurveNodeIter->second;

			if (auto transformChannel = dynamic_pointer_cast<Vec3LinearChannel>(channel)) 
			{
				AddVec3Keyframes(transformChannel, boneAnimCurveNodes->fbxNode, fbxAnimLayer, transformChannel->getName());
			}
			else if (auto rotateChannel = dynamic_pointer_cast<QuatSphericalLinearChannel>(channel))
			{
				AddQuatSlerpKeyframes(rotateChannel, boneAnimCurveNodes->fbxNode, fbxAnimLayer, targetBoneName);
			}
		}
	}


	// Call this only after all node's children are already processed
	void WriterNodeVisitor::applyAnimations(const osg::ref_ptr<osg::Callback>& callback)
	{
		if (!callback)
			return;

		// Read animation takes
		auto bam = dynamic_pointer_cast<BasicAnimationManager>(callback);
		if (!bam)
			return;

		// Run through all animations
		for (auto& animation : bam->getAnimationList())
		{
			createAnimationLayer(animation);
		}
	}


}


