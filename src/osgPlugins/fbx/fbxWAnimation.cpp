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


	inline FbxAnimStack* WriterNodeVisitor::getCurrentAnimStack()
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

	// Call this only after all node's children are already processed
	void WriterNodeVisitor::applyAnimations(const osg::ref_ptr<osg::Callback> callback)
	{

		//if (_MorphedMeshMap.size() > 0)
		//	applyMorphTargets();

		//if (_updateMatrixMap.size() > 0)

	}

	void WriterNodeVisitor::applyUpdateMatrix(const osg::ref_ptr<osg::Callback> callback,
		FbxNode* currentNode)
	{
		const ref_ptr<UpdateMatrixTransform> umt = dynamic_pointer_cast<UpdateMatrixTransform>(callback);
		const ref_ptr<UpdateBone> ub = dynamic_pointer_cast<UpdateBone>(callback);

		if (!umt && !ub)
			return;

		if (!_matrixAndBonesAnimLayer)
			_matrixAndBonesAnimLayer = FbxAnimLayer::Create(_pSdkManager, "Skeleton Animations");

		FbxAnimCurveNode* matrixAnimCurve = FbxAnimCurveNode::Create(_pSdkManager, currentNode->GetName());

		auto& stackedTransforms = (ub) ? ub->getStackedTransforms() : umt->getStackedTransforms();

		// Iterate through bone StackedTransformElements
		for (auto& stackedTransform : stackedTransforms)
		{
			if (dynamic_pointer_cast<StackedTranslateElement>(stackedTransform))
			{
				FbxAnimCurve* transCurveX = currentNode->LclTranslation.GetCurve(_matrixAndBonesAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
				FbxAnimCurve* transCurveY = currentNode->LclTranslation.GetCurve(_matrixAndBonesAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
				FbxAnimCurve* transCurveZ = currentNode->LclTranslation.GetCurve(_matrixAndBonesAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

				int keyIndex = transCurveX->KeyAdd(FbxTime(0));
				transCurveX->KeySetValue(keyIndex, dynamic_pointer_cast<StackedTranslateElement>(stackedTransform)->getTranslate().x());
				transCurveX->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				keyIndex = transCurveY->KeyAdd(FbxTime(0));
				transCurveY->KeySetValue(keyIndex, dynamic_pointer_cast<StackedTranslateElement>(stackedTransform)->getTranslate().y());
				transCurveY->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				keyIndex = transCurveZ->KeyAdd(FbxTime(0));
				transCurveZ->KeySetValue(keyIndex, dynamic_pointer_cast<StackedTranslateElement>(stackedTransform)->getTranslate().z());
				transCurveZ->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				matrixAnimCurve->ConnectToChannel(transCurveX, FBXSDK_CURVENODE_COMPONENT_X);
				matrixAnimCurve->ConnectToChannel(transCurveY, FBXSDK_CURVENODE_COMPONENT_Y);
				matrixAnimCurve->ConnectToChannel(transCurveZ, FBXSDK_CURVENODE_COMPONENT_Z);
			}
			else if (dynamic_pointer_cast<StackedQuaternionElement>(stackedTransform))
			{
				double angle(0.0);
				FbxVector4 axis;
				osg::Quat quat = dynamic_pointer_cast<StackedQuaternionElement>(stackedTransform)->getQuaternion();

				angle = 2 * acos(quat.w());
				double s = sqrt(1 - quat.w() * quat.w());

				axis;
				if (s < 0.001) // test to avoid divide by zero, s is always positive due to sqrt
				{
					// If s close to zero then direction of axis not important
					axis = FbxVector4(quat.x(), quat.y(), quat.z()); // if it is important that axis is normalized then replace with x=1; y=z=0;
				}
				else
				{
					axis = FbxVector4(quat.x() / s, quat.y() / s, quat.z() / s); // normalise axis
				}

				// Create a curve for rotation
				FbxAnimCurve* rotCurveX = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_X);
				FbxAnimCurve* rotCurveY = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Y);
				FbxAnimCurve* rotCurveZ = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Z);

				int keyIndexX = rotCurveX->KeyAdd(FbxTime(0));
				rotCurveX->KeySetValue(keyIndexX, axis[0] * angle);
				rotCurveX->KeySetInterpolation(keyIndexX, FbxAnimCurveDef::eInterpolationLinear);

				int keyIndexY = rotCurveY->KeyAdd(FbxTime(0));
				rotCurveY->KeySetValue(keyIndexY, axis[1] * angle);
				rotCurveY->KeySetInterpolation(keyIndexY, FbxAnimCurveDef::eInterpolationLinear);

				int keyIndexZ = rotCurveZ->KeyAdd(FbxTime(0));
				rotCurveZ->KeySetValue(keyIndexZ, axis[2] * angle);
				rotCurveZ->KeySetInterpolation(keyIndexZ, FbxAnimCurveDef::eInterpolationLinear);

				// Connect curves to the curve node
				matrixAnimCurve->ConnectToChannel(rotCurveX, FBXSDK_CURVENODE_COMPONENT_X);
				matrixAnimCurve->ConnectToChannel(rotCurveY, FBXSDK_CURVENODE_COMPONENT_Y);
				matrixAnimCurve->ConnectToChannel(rotCurveZ, FBXSDK_CURVENODE_COMPONENT_Z);
			}
			else if (dynamic_pointer_cast<StackedRotateAxisElement>(stackedTransform))
			{
				osg::Vec3 axis = dynamic_pointer_cast<StackedRotateAxisElement>(stackedTransform)->getAxis();
				double angle = dynamic_pointer_cast<StackedRotateAxisElement>(stackedTransform)->getAngle();

				angle = osg::RadiansToDegrees(angle);

				FbxAnimCurve* rotCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_ROTATION);

				int keyIndex = rotCurve->KeyAdd(FbxTime(0));
				rotCurve->KeySetValue(keyIndex, angle); // Set the angle of rotation
				rotCurve->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				if (axis == osg::Vec3(1, 0, 0))
					matrixAnimCurve->ConnectToChannel(rotCurve, FBXSDK_CURVENODE_COMPONENT_X);
				else if (axis == osg::Vec3(0, 1, 0))
					matrixAnimCurve->ConnectToChannel(rotCurve, FBXSDK_CURVENODE_COMPONENT_Y);
				else if (axis == osg::Vec3(0, 0, 1))
					matrixAnimCurve->ConnectToChannel(rotCurve, FBXSDK_CURVENODE_COMPONENT_Z);
			}
			else if (dynamic_pointer_cast<StackedScaleElement>(stackedTransform))
			{
				osg::Vec3 scale = dynamic_pointer_cast<StackedScaleElement>(stackedTransform)->getScale();

				FbxAnimCurve* scaleXCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_X);
				FbxAnimCurve* scaleYCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Y);
				FbxAnimCurve* scaleZCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Z);

				int keyIndexX = scaleXCurve->KeyAdd(FbxTime(0));
				scaleXCurve->KeySetValue(keyIndexX, scale.x());
				scaleXCurve->KeySetInterpolation(keyIndexX, FbxAnimCurveDef::eInterpolationLinear);

				int keyIndexY = scaleYCurve->KeyAdd(FbxTime(0));
				scaleYCurve->KeySetValue(keyIndexY, scale.y());
				scaleYCurve->KeySetInterpolation(keyIndexY, FbxAnimCurveDef::eInterpolationLinear);

				int keyIndexZ = scaleZCurve->KeyAdd(FbxTime(0));
				scaleZCurve->KeySetValue(keyIndexZ, scale.z());
				scaleZCurve->KeySetInterpolation(keyIndexZ, FbxAnimCurveDef::eInterpolationLinear);

				matrixAnimCurve->ConnectToChannel(scaleXCurve, FBXSDK_CURVENODE_COMPONENT_X);
				matrixAnimCurve->ConnectToChannel(scaleYCurve, FBXSDK_CURVENODE_COMPONENT_Y);
				matrixAnimCurve->ConnectToChannel(scaleZCurve, FBXSDK_CURVENODE_COMPONENT_Z);
			}
			else if (dynamic_pointer_cast<StackedMatrixElement>(stackedTransform))
			{
				osg::Matrixf matrix = dynamic_pointer_cast<StackedMatrixElement>(stackedTransform)->getMatrix();

				// Decompose the matrix into translation, rotation (quaternion), and scale
				osg::Vec3f translation, scale;
				osg::Quat rotation, so;
				matrix.decompose(translation, rotation, scale, so);

				// Decompose quaternion
				double angle = 2 * acos(rotation.w());
				double s = sqrt(1 - rotation.w() * rotation.w());
				FbxVector4 axis;
				if (s < 0.001)
					axis = FbxVector4(rotation.x(), rotation.y(), rotation.z());
				else
					axis = FbxVector4(rotation.x() / s, rotation.y() / s, rotation.z() / s);

				// Add translation
				FbxAnimCurve* transCurveX = currentNode->LclTranslation.GetCurve(_matrixAndBonesAnimLayer, FBXSDK_CURVENODE_COMPONENT_X, true);
				FbxAnimCurve* transCurveY = currentNode->LclTranslation.GetCurve(_matrixAndBonesAnimLayer, FBXSDK_CURVENODE_COMPONENT_Y, true);
				FbxAnimCurve* transCurveZ = currentNode->LclTranslation.GetCurve(_matrixAndBonesAnimLayer, FBXSDK_CURVENODE_COMPONENT_Z, true);

				int keyIndex = transCurveX->KeyAdd(FbxTime(0));
				transCurveX->KeySetValue(keyIndex, translation.x());
				transCurveX->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				keyIndex = transCurveY->KeyAdd(FbxTime(0));
				transCurveY->KeySetValue(keyIndex, translation.y());
				transCurveY->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				keyIndex = transCurveZ->KeyAdd(FbxTime(0));
				transCurveZ->KeySetValue(keyIndex, translation.z());
				transCurveZ->KeySetInterpolation(keyIndex, FbxAnimCurveDef::eInterpolationLinear);

				// Add rotation
				FbxAnimCurve* rotCurveX = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_X);
				FbxAnimCurve* rotCurveY = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Y);
				FbxAnimCurve* rotCurveZ = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Z);

				int keyIndexX = rotCurveX->KeyAdd(FbxTime(0));
				rotCurveX->KeySetValue(keyIndexX, axis[0] * angle);
				rotCurveX->KeySetInterpolation(keyIndexX, FbxAnimCurveDef::eInterpolationLinear);

				int keyIndexY = rotCurveY->KeyAdd(FbxTime(0));
				rotCurveY->KeySetValue(keyIndexY, axis[1] * angle);
				rotCurveY->KeySetInterpolation(keyIndexY, FbxAnimCurveDef::eInterpolationLinear);

				int keyIndexZ = rotCurveZ->KeyAdd(FbxTime(0));
				rotCurveZ->KeySetValue(keyIndexZ, axis[2] * angle);
				rotCurveZ->KeySetInterpolation(keyIndexZ, FbxAnimCurveDef::eInterpolationLinear);

				// Add Scale
				FbxAnimCurve* scaleXCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_X);
				FbxAnimCurve* scaleYCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Y);
				FbxAnimCurve* scaleZCurve = matrixAnimCurve->CreateCurve(currentNode->GetName(), FBXSDK_CURVENODE_COMPONENT_Z);

				keyIndexX = scaleXCurve->KeyAdd(FbxTime(0));
				scaleXCurve->KeySetValue(keyIndexX, scale.x());
				scaleXCurve->KeySetInterpolation(keyIndexX, FbxAnimCurveDef::eInterpolationLinear);

				keyIndexY = scaleYCurve->KeyAdd(FbxTime(0));
				scaleYCurve->KeySetValue(keyIndexY, scale.y());
				scaleYCurve->KeySetInterpolation(keyIndexY, FbxAnimCurveDef::eInterpolationLinear);

				keyIndexZ = scaleZCurve->KeyAdd(FbxTime(0));
				scaleZCurve->KeySetValue(keyIndexZ, scale.z());
				scaleZCurve->KeySetInterpolation(keyIndexZ, FbxAnimCurveDef::eInterpolationLinear);

				// Connect curves to the curve node
				matrixAnimCurve->ConnectToChannel(transCurveX, FBXSDK_CURVENODE_COMPONENT_X);
				matrixAnimCurve->ConnectToChannel(transCurveY, FBXSDK_CURVENODE_COMPONENT_Y);
				matrixAnimCurve->ConnectToChannel(transCurveZ, FBXSDK_CURVENODE_COMPONENT_Z);

				matrixAnimCurve->ConnectToChannel(rotCurveX, FBXSDK_CURVENODE_COMPONENT_X);
				matrixAnimCurve->ConnectToChannel(rotCurveY, FBXSDK_CURVENODE_COMPONENT_Y);
				matrixAnimCurve->ConnectToChannel(rotCurveZ, FBXSDK_CURVENODE_COMPONENT_Z);

				matrixAnimCurve->ConnectToChannel(scaleXCurve, FBXSDK_CURVENODE_COMPONENT_X);
				matrixAnimCurve->ConnectToChannel(scaleYCurve, FBXSDK_CURVENODE_COMPONENT_Y);
				matrixAnimCurve->ConnectToChannel(scaleZCurve, FBXSDK_CURVENODE_COMPONENT_Z);
			}
		}

		_matrixAndBonesAnimLayer->AddMember(matrixAnimCurve);
		getCurrentAnimStack()->AddMember(_matrixAndBonesAnimLayer);
	}

	void WriterNodeVisitor::applyMorphTargets()
	{

	}



}


