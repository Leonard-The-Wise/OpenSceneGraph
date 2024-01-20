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

	// Variable to avoid spamming warnings
	static std::set<std::string> missingTargets;

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

	static std::string getLastPart(const std::string& input) 
	{
		size_t pos = input.find_last_of('|');
		if (pos != std::string::npos) {
			return input.substr(pos + 1);
		}
		return input;
	}

	inline static Quat AddQuaternions(const Quat& q1, const Quat& q2) {
		return Quat(q1.x() + q2.x(), q1.y() + q2.y(), q1.z() + q2.z(), q1.w() + q2.w());
	}

	inline static Quat SubtractQuaternions(const Quat& q1, const Quat& q2) {
		return Quat(q1.x() - q2.x(), q1.y() - q2.y(), q1.z() - q2.z(), q1.w() - q2.w());
	}

	inline static Quat MultiplyQuaternionByScalar(const Quat& q, double scalar) {
		return Quat(q.x() * scalar, q.y() * scalar, q.z() * scalar, q.w() * scalar);
	}

	inline static double QuaternionDot(const Quat& q1, const Quat& q2) {
		return q1.x() * q2.x() + q1.y() * q2.y() + q1.z() * q2.z() + q1.w() * q2.w();
	}

	static Quat Slerp(const Quat& q1, const Quat& q2, double t) 
	{
		double cosTheta = QuaternionDot(q1, q2);
		if (cosTheta > 0.9995) 
		{
			return AddQuaternions(q1, MultiplyQuaternionByScalar(SubtractQuaternions(q2, q1), t));
		}
		else 
		{
			double theta = acos(std::max(-1.0, std::min(1.0, cosTheta)));
			double thetap = theta * t;

			Quat qperp = SubtractQuaternions(q2, MultiplyQuaternionByScalar(q1, cosTheta));
			qperp.asVec4().normalize();

			return AddQuaternions(MultiplyQuaternionByScalar(q1, cos(thetap)), MultiplyQuaternionByScalar(qperp, sin(thetap)));
		}
	}

	void WriterNodeVisitor::applyDummyKeyFrame(const FbxTime& fbxTime, FbxAnimLayer* fbxAnimLayer)
	{
		if (_matrixAnimCurveMap.size() > 0)
		{
			for (auto& animCurveNode : _matrixAnimCurveMap)
			{
				auto& dummyAnimNode = animCurveNode.second->fbxNode;

				std::string nodeName = dummyAnimNode->GetName(); // for debug

				FbxDouble3 staticTrans = dummyAnimNode->LclTranslation.Get();
				FbxAnimCurve* dummyCurveTransX = dummyAnimNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
				FbxAnimCurve* dummyCurveTransY = dummyAnimNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
				FbxAnimCurve* dummyCurveTransZ = dummyAnimNode->LclTranslation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
				dummyCurveTransX->KeySet(dummyCurveTransX->KeyAdd(fbxTime), fbxTime, staticTrans[0], FbxAnimCurveDef::eInterpolationConstant);
				dummyCurveTransY->KeySet(dummyCurveTransY->KeyAdd(fbxTime), fbxTime, staticTrans[1], FbxAnimCurveDef::eInterpolationConstant);
				dummyCurveTransZ->KeySet(dummyCurveTransZ->KeyAdd(fbxTime), fbxTime, staticTrans[2], FbxAnimCurveDef::eInterpolationConstant);

				FbxDouble3 staticScale = dummyAnimNode->LclScaling.Get();
				FbxAnimCurve* dummyCurveScaleX = dummyAnimNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
				FbxAnimCurve* dummyCurveScaleY = dummyAnimNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
				FbxAnimCurve* dummyCurveScaleZ = dummyAnimNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
				dummyCurveScaleX->KeySet(dummyCurveScaleX->KeyAdd(fbxTime), fbxTime, staticScale[0], FbxAnimCurveDef::eInterpolationConstant);
				dummyCurveScaleY->KeySet(dummyCurveScaleY->KeyAdd(fbxTime), fbxTime, staticScale[1], FbxAnimCurveDef::eInterpolationConstant);
				dummyCurveScaleZ->KeySet(dummyCurveScaleZ->KeyAdd(fbxTime), fbxTime, staticScale[2], FbxAnimCurveDef::eInterpolationConstant);

				FbxDouble3 staticRot = dummyAnimNode->LclRotation.Get();
				FbxAnimCurve* dummyCurveRotX = dummyAnimNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
				FbxAnimCurve* dummyCurveRotY = dummyAnimNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
				FbxAnimCurve* dummyCurveRotZ = dummyAnimNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
				dummyCurveRotX->KeySet(dummyCurveRotX->KeyAdd(fbxTime), fbxTime, staticRot[0], FbxAnimCurveDef::eInterpolationConstant);
				dummyCurveRotY->KeySet(dummyCurveRotY->KeyAdd(fbxTime), fbxTime, staticRot[1], FbxAnimCurveDef::eInterpolationConstant);
				dummyCurveRotZ->KeySet(dummyCurveRotZ->KeyAdd(fbxTime), fbxTime, staticRot[2], FbxAnimCurveDef::eInterpolationConstant);
			}
		}
	}

	FbxTime WriterNodeVisitor::AddVec3Keyframes(osgAnimation::Vec3LinearChannel* transformChannel,
		FbxNode* animCurveNode, FbxAnimLayer* fbxAnimLayer, std::string channelName)
	{
		if (!transformChannel || !animCurveNode)
		{
			return FbxTime(0);
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
		else if (channelName == "scale" || channelName == "ScalingCompensation")
		{
			curveX = animCurveNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
			curveY = animCurveNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
			curveZ = animCurveNode->LclScaling.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);
		}
		else
		{
			OSG_WARN << "WARNING: Animation channel contains invalid name: " << channelName << std::endl;
			return FbxTime(0);
		}

		// Obtenha o KeyframeContainer do canal de translação.
		osgAnimation::Vec3KeyframeContainer* keyframes = transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

		// Itere sobre todos os keyframes do canal de translação e adicione-os às curvas FBX.
		FbxTime fbxTime;

		// Aplicar uma dummy keyframe no começo
		//applyDummyKeyFrame(fbxTime, fbxAnimLayer);

		for (unsigned int i = 0; i < keyframes->size(); ++i)
		{
			const osgAnimation::Vec3Keyframe& keyframe = (*keyframes)[i];

			// Converta o tempo do OSG para o tempo do FBX.
			fbxTime.SetSecondDouble(keyframe.getTime());
			Vec3 keyValue = keyframe.getValue();

			// Adicione os valores de translação para cada eixo às curvas correspondentes.
			curveX->KeySet(curveX->KeyAdd(fbxTime), fbxTime, keyValue.x(), FbxAnimCurveDef::eInterpolationLinear);
			curveY->KeySet(curveY->KeyAdd(fbxTime), fbxTime, keyValue.y(), FbxAnimCurveDef::eInterpolationLinear);
			curveZ->KeySet(curveZ->KeyAdd(fbxTime), fbxTime, keyValue.z(), FbxAnimCurveDef::eInterpolationLinear);
		}

		// Aplicar uma dummy keyframe no fim da animação
		//applyDummyKeyFrame(fbxTime, fbxAnimLayer);

		return fbxTime;
	}

	FbxTime WriterNodeVisitor::AddQuatSlerpKeyframes(osgAnimation::QuatSphericalLinearChannel* transformChannel,
		FbxNode* animCurveNode, FbxAnimLayer* fbxAnimLayer)
	{
		if (!transformChannel || !animCurveNode)
		{
			return FbxTime(0);
		}

		FbxAnimCurve* curveX = animCurveNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
		FbxAnimCurve* curveY = animCurveNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
		FbxAnimCurve* curveZ = animCurveNode->LclRotation.GetCurve(fbxAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

		// Cria uma cópia
		osgAnimation::QuatKeyframeContainer keyframes = *transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

		if (keyframes.size() == 0)
			return FbxTime(0);

		FbxTime fbxTime;

		// Aplicar uma dummy keyframe no começo
		// applyDummyKeyFrame(fbxTime, fbxAnimLayer);

		// Pegar a rotação original (inicial) do objeto
		FbxDouble3 lastRotation = animCurveNode->LclRotation.Get();
		FbxAMatrix quatMatrix;
		quatMatrix.SetR(lastRotation);
		FbxQuaternion qLastRotation = quatMatrix.GetQ();
		Quat zeroQuatKey = Quat(qLastRotation[0], qLastRotation[1], qLastRotation[2], qLastRotation[3]);

		// Prepara os quaternions para interpolação
		Quat firstQuatKey = keyframes[0].getValue();
		//if (QuaternionDot(firstQuatKey, zeroQuatKey) < 0)
		//	keyframes[0].setValue(-firstQuatKey);

		//for (unsigned int i = 1; i < keyframes.size(); ++i) 
		//{
		//	Quat current = keyframes[i].getValue();
		//	Quat previous = keyframes[i - 1].getValue();

		//	if (QuaternionDot(current, previous) < 0) 
		//	{
		//		keyframes[i].setValue(-current); // Inverte o sinal do quaternion
		//	}
		//}
		
		//constexpr double t = 0.5;
		for (unsigned int i = 0; i < keyframes.size(); ++i)
		{
			osgAnimation::QuatKeyframe& keyframe = keyframes[i];

			fbxTime.SetSecondDouble(keyframe.getTime());

			Quat quat = keyframe.getValue();
			Quat interpolatedQuat = quat;

			//if (i == 0)
			//{
			//	interpolatedQuat = Slerp(zeroQuatKey, keyframes[i].getValue(), t);
			//}
			//else
			//{
			//	Quat quatStart = keyframes[i - 1].getValue();
			//	Quat quatEnd = keyframes[i].getValue();
			//								
			//	interpolatedQuat = Slerp(quatStart, quatEnd, t);
			//}

			FbxQuaternion q(interpolatedQuat.x(), interpolatedQuat.y(), interpolatedQuat.z(), interpolatedQuat.w());
			FbxAMatrix mat;
			mat.SetQ(q);
			FbxVector4 vec4 = mat.GetR();
			FbxDouble3 euler = FbxDouble3(vec4[0], vec4[1], vec4[2]);

			curveX->KeySet(curveX->KeyAdd(fbxTime), fbxTime, euler[0], FbxAnimCurveDef::eInterpolationLinear);
			curveY->KeySet(curveY->KeyAdd(fbxTime), fbxTime, euler[1], FbxAnimCurveDef::eInterpolationLinear);
			curveZ->KeySet(curveZ->KeyAdd(fbxTime), fbxTime, euler[2], FbxAnimCurveDef::eInterpolationLinear);
		}

		// Aplicar uma dummy keyframe no final
		//applyDummyKeyFrame(fbxTime, fbxAnimLayer);

		return fbxTime;
	}

	FbxTime AddFloatKeyframes(osgAnimation::FloatLinearChannel* transformChannel,
		FbxBlendShapeChannel* blendShapeChannel, FbxAnimLayer* fbxAnimLayer)
	{
		if (!transformChannel || !blendShapeChannel)
		{
			return FbxTime(0);
		}
		FbxAnimCurve* curve = blendShapeChannel->DeformPercent.GetCurve(fbxAnimLayer, true);

		osgAnimation::FloatKeyframeContainer* keyframes = transformChannel->getOrCreateSampler()->getOrCreateKeyframeContainer();

		FbxTime fbxTime;
		for (unsigned int i = 0; i < keyframes->size(); ++i)
		{
			const osgAnimation::FloatKeyframe& keyframe = (*keyframes)[i];

			fbxTime.SetSecondDouble(keyframe.getTime());

			float value = keyframe.getValue();

			curve->KeySet(curve->KeyAdd(fbxTime), fbxTime, value, FbxAnimCurveDef::eInterpolationConstant);
		}
		return fbxTime;
	}

	void WriterNodeVisitor::createAnimationStack(const osg::ref_ptr<osgAnimation::Animation> osgAnimation)
	{
		std::string animationName = osgAnimation->getName().c_str();
		animationName = getLastPart(animationName);

		FbxAnimStack* fbxAnimStack = FbxAnimStack::Create(_pScene, animationName.c_str());
		_pScene->SetCurrentAnimationStack(fbxAnimStack);
		FbxAnimLayer* fbxAnimLayer = FbxAnimLayer::Create(_pScene, animationName.c_str());
		fbxAnimStack->AddMember(fbxAnimLayer);

		bool NotImplemented1(false), NotImplemented2(false);

		FbxTime startTime, endTime;
		startTime.SetSecondDouble(0.0);
		for (auto& channel : osgAnimation->getChannels()) 
		{
			std::string targetName = channel->getTargetName();

			auto boneAnimCurveNodeIter = _matrixAnimCurveMap.find(targetName);
			auto morphAnimNodeIter = _blendShapeAnimations.find(targetName);

			FbxTime currentTime;
			if (boneAnimCurveNodeIter == _matrixAnimCurveMap.end() && morphAnimNodeIter == _blendShapeAnimations.end())
			{
				if (missingTargets.find(targetName) == missingTargets.end() && _discardedAnimationTargetNames.find(targetName) == _discardedAnimationTargetNames.end())
				{
					OSG_WARN << "WARNING: Found animation without target: " << targetName << std::endl;
					missingTargets.emplace(targetName);
					continue;
				}
			}

			if (boneAnimCurveNodeIter != _matrixAnimCurveMap.end())
			{
				auto& boneAnimCurveNodes = boneAnimCurveNodeIter->second;

				if (auto transformChannel = dynamic_pointer_cast<Vec3LinearChannel>(channel))
				{
					currentTime = AddVec3Keyframes(transformChannel, boneAnimCurveNodes->fbxNode, fbxAnimLayer, transformChannel->getName());
				}
				else if (auto rotateChannel = dynamic_pointer_cast<QuatSphericalLinearChannel>(channel))
				{
					currentTime = AddQuatSlerpKeyframes(rotateChannel, boneAnimCurveNodes->fbxNode, fbxAnimLayer);
				}
			} 
			else if (morphAnimNodeIter != _blendShapeAnimations.end())
			{

				if (auto morphChannel = dynamic_pointer_cast<FloatLinearChannel>(channel))
				{
					currentTime = AddFloatKeyframes(morphChannel, morphAnimNodeIter->second, fbxAnimLayer);
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

			if (currentTime.GetSecondDouble() > endTime.GetSecondDouble())
				endTime = currentTime;
		}
		
		std::string framerateStr;
		DefaultUserDataContainer* udc = dynamic_cast<DefaultUserDataContainer*>(osgAnimation->getUserDataContainer());
		if (udc && udc->getUserValue("framerate", framerateStr))
		{
			int framerate(0);
			framerate = std::stoi(framerateStr);
			switch (framerate)
			{
			case 24:
				startTime.SetGlobalTimeMode(FbxTime::eFrames24);
				endTime.SetGlobalTimeMode(FbxTime::eFrames24);
				break;
			case 30:
				startTime.SetGlobalTimeMode(FbxTime::eFrames30);
				endTime.SetGlobalTimeMode(FbxTime::eFrames30);
				break;
			case 48:
				startTime.SetGlobalTimeMode(FbxTime::eFrames48);
				endTime.SetGlobalTimeMode(FbxTime::eFrames48);
				break;
			case 50:
				startTime.SetGlobalTimeMode(FbxTime::eFrames50);
				endTime.SetGlobalTimeMode(FbxTime::eFrames50);
				break;
			case 60:
				startTime.SetGlobalTimeMode(FbxTime::eFrames60);
				endTime.SetGlobalTimeMode(FbxTime::eFrames60);
				break;
			case 72:
				startTime.SetGlobalTimeMode(FbxTime::eFrames72);
				endTime.SetGlobalTimeMode(FbxTime::eFrames72);
				break;
			case 96:
				startTime.SetGlobalTimeMode(FbxTime::eFrames96);
				endTime.SetGlobalTimeMode(FbxTime::eFrames96);
				break;
			case 100:
				startTime.SetGlobalTimeMode(FbxTime::eFrames100);
				endTime.SetGlobalTimeMode(FbxTime::eFrames100);
				break;
			case 120:
				startTime.SetGlobalTimeMode(FbxTime::eFrames120);
				endTime.SetGlobalTimeMode(FbxTime::eFrames120);
				break;
			}
		}

		fbxAnimStack->LocalStart = startTime;
		fbxAnimStack->LocalStop = endTime;
	}

	// Call this only after all node's children are already processed
	void WriterNodeVisitor::applyAnimations(const osg::ref_ptr<osg::Callback>& callback)
	{
		if (!callback)
			return;

		// Create Static Pose, add a dummy keyframe for every bone (so some applications won't give warnings about it)
		FbxAnimStack* fbxAnimStack = FbxAnimStack::Create(_pScene, "Static Pose");
		_pScene->SetCurrentAnimationStack(fbxAnimStack);
		FbxAnimLayer* fbxAnimLayer = FbxAnimLayer::Create(_pScene, "Static Pose");
		fbxAnimStack->AddMember(fbxAnimLayer);

		FbxTime fbxTime;
		fbxTime.SetSecondDouble(0);

		applyDummyKeyFrame(fbxTime, fbxAnimLayer);

		fbxAnimStack->LocalStart = fbxTime;
		fbxAnimStack->LocalStop = fbxTime;

		// Read animation takes
		auto bam = dynamic_pointer_cast<BasicAnimationManager>(callback);
		if (!bam)
			return;

		OSG_NOTICE << "Processing " << bam->getAnimationList().size() << " animation(s)..." << std::endl;

		// Run through all animations
		for (auto& animation : bam->getAnimationList())
		{
			createAnimationStack(animation);
		}
	}


	void WriterNodeVisitor::buildAnimationTargets(osg::Group* node)
	{
		// Only build this list once
		if (!node || _animationTargetNames.size() > 0)
			return;

		std::string nodeName = node->getName(); // for debug

		// Traverse hierarchy looking for basic animations manager
		ref_ptr<Callback> nodeCallback = const_cast<Callback*>(node->getUpdateCallback());
		ref_ptr<Callback> callback = getRealUpdateCallback(nodeCallback);

		auto bam = dynamic_pointer_cast<BasicAnimationManager>(callback);
		if (bam)
		{
			for (auto& animation : bam->getAnimationList())
			{
				for (auto& channel : animation->getChannels())
				{
					// Disconsider channels with 1 keyframe (non-animated). Mark them for reference
					if (channel->getSampler() && channel->getSampler()->getKeyframeContainer() && 
						channel->getSampler()->getKeyframeContainer()->size() > 1)
						_animationTargetNames.emplace(channel->getTargetName());
					else
						_discardedAnimationTargetNames.emplace(channel->getTargetName());
				}
			}
		}
		else
		{
			for (unsigned int i = 0; i < node->getNumChildren(); ++i)
			{
				buildAnimationTargets(dynamic_cast<Group*>(node->getChild(i)));
				if (_animationTargetNames.size() > 0)
					break;
			}
		}
	}

}


