// -*-c++-*-

/*
 * FBX writer for Open Scene Graph
 *
 * Copyright (C) 2009
 *
 * Writing support added 2009 by Thibault Caporal and Sukender (Benoit Neil - http://sukender.free.fr)
 *
 * The Open Scene Graph (OSG) is a cross platform C++/OpenGL library for
 * real-time rendering of large 3D photo-realistic models.
 * The OSG homepage is http://www.openscenegraph.org/
 */

#ifndef _FBX_WRITER_NODE_VISITOR_HEADER__
#define _FBX_WRITER_NODE_VISITOR_HEADER__

#include <unordered_map>
#include <map>
#include <set>
#include <stack>
#include <osg/Geometry>
#include <osg/Material>
#include <osg/NodeVisitor>
#include <osg/PrimitiveSet>
#include <osgDB/FileNameUtils>
#include <osgDB/ReaderWriter>
#include <osgDB/ExternalFileWriter>
#include <osgAnimation/RigGeometry>
#include <osgAnimation/MorphGeometry>

#if defined(_MSC_VER)
#pragma warning( disable : 4505 )
#pragma warning( default : 4996 )
#endif
#include <fbxsdk.h>

struct Triangle
{
    unsigned int t1;
    unsigned int t2;
    unsigned int t3;
    unsigned int normalIndex1;        ///< Normal index for all bindings except BIND_PER_VERTEX and BIND_OFF.
    unsigned int normalIndex2;
    unsigned int normalIndex3;
    int material;
};

struct VertexIndex
{
    VertexIndex(unsigned int in_vertexIndex, unsigned int in_drawableIndex, unsigned int in_normalIndex)
        : vertexIndex(in_vertexIndex), drawableIndex(in_drawableIndex), normalIndex(in_normalIndex)
    {}
    VertexIndex(const VertexIndex & v) : vertexIndex(v.vertexIndex), drawableIndex(v.drawableIndex), normalIndex(v.normalIndex) {}

    unsigned int vertexIndex;        ///< Index of the vertice position in the vec3 array
    unsigned int drawableIndex;
    unsigned int normalIndex;        ///< Normal index for all bindings except BIND_PER_VERTEX and BIND_OFF.

    bool operator<(const VertexIndex & v) const {
        if (drawableIndex!=v.drawableIndex) return drawableIndex<v.drawableIndex;
        return vertexIndex<v.vertexIndex;
    }
};

typedef std::vector<std::pair<Triangle, int> > ListTriangle; //the int is the drawable of the triangle
typedef std::map<VertexIndex, unsigned int> MapIndices;        ///< Map OSG indices to FBX mesh indices
typedef std::vector<const osg::Geometry*> GeometryList; // a list of geometries to process in batch

namespace pluginfbx
{

    struct UpdateMatrixNodes
    {
        osg::ref_ptr<osg::MatrixTransform> matrixTransform;
        FbxNode* fbxNode;

        UpdateMatrixNodes() :
            fbxNode(nullptr)
        {};
    };

    ///\author Capo (Thibault Caporal), Sukender (Benoit Neil)
    class WriterNodeVisitor : public osg::NodeVisitor
    {
    public:
        WriterNodeVisitor(FbxScene* pScene,
            FbxManager* pSdkManager,
            const std::string& fileName,
            const osgDB::ReaderWriter::Options* options,
            const std::string& srcDirectory,
            bool ignoreBones,
            bool ignoreAnimations,
            double rotateXAxis,
            bool exportFullHierarchy,
            double scaleModel,
            bool flipUVs) :
            osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
            _pSdkManager(pSdkManager),
            _succeedLastApply(true),
            _pScene(pScene),
            _curFbxNode(pScene->GetRootNode()),
            _currentStateSet(new osg::StateSet()),
            _options(options),
            _externalWriter(srcDirectory, osgDB::getFilePath(fileName), true, 0),
            _texcoords(false),
            _drawableNum(0),
            _firstNodeProcessed(false),
            _ignoreBones(ignoreBones),
            _ignoreAnimations(ignoreAnimations),
            _MeshesRoot(nullptr),
            _rotateXAxis(rotateXAxis),
            _exportFullHierarchy(exportFullHierarchy),
            _scaleModel(scaleModel),
            _flipUVs(flipUVs)
        {}

        virtual void apply(osg::Geometry& node);
        virtual void apply(osg::Group& node);
        virtual void apply(osg::MatrixTransform& node);

        void traverse(osg::Node& node)
        {
            osg::NodeVisitor::traverse(node);
        }

        typedef std::map<const osg::Image*, std::string> ImageSet;
        typedef std::set<std::string> ImageFilenameSet;        // Sub-optimal because strings are doubled (in ImageSet). Moreover, an unordered_set (= hashset) would be more efficient (Waiting for unordered_set to be included in C++ standard ;) ).


        class MaterialParser
        {
        public:

            enum class MaterialSurfaceLayer {
                None, Ambient, Diffuse, DisplacementColor, Emissive, NormalMap, Reflection, Specular, Shininess, Transparency
            };

            ///Create a KfbxMaterial and KfbxTexture from osg::Texture and osg::Material.
            MaterialParser(WriterNodeVisitor& writerNodeVisitor,
                osgDB::ExternalFileWriter& externalWriter,
                const osg::StateSet* stateset,
                const osg::Material* mat,
                const std::vector<const osg::Texture*>& texArray,
                FbxManager* pSdkManager,
                const osgDB::ReaderWriter::Options* options,
                int                  index = -1);

            FbxSurfacePhong* getFbxMaterial() const
            {
                return _fbxMaterial;
            }

        private:
            FbxSurfacePhong* _fbxMaterial;

            std::set<std::string> KnownLayerNames =
            {
                "AOPBR",
                "Sheen",
                "Matcap",
                "BumpMap",
                "Opacity",
                "AlbedoPBR",
                "AlphaMask",
                "CavityPBR",
                "ClearCoat",
                "EmitColor",
                "NormalMap",
                "Anisotropy",
                "DiffusePBR",
                "SpecularF0",
                "SpecularPBR",
                "DiffuseColor",
                "Displacement",
                "MetalnessPBR",
                "RoughnessPBR",
                "GlossinessPBR",
                "SpecularColor",
                "SheenRoughness",
                "DiffuseIntensity",
                "SpecularHardness",
                "ClearCoatNormalMap",
                "ClearCoatRoughness",
                "SubsurfaceScattering",
                "SubsurfaceTranslucency",
            };

            MaterialSurfaceLayer getTexMaterialLayer(const osg::Material* material, const osg::Texture* texture);
        };

    protected:
        /// Compares StateSets.
        ///\todo It may be useful to compare stack of pointers (see pushStateset()) in order to keep the same number of FBX materials when doing reading and then writing without further processing.
        struct CompareStateSet
        {
            bool operator () (const osg::ref_ptr<const osg::StateSet>& ss1, const osg::ref_ptr<const osg::StateSet>& ss2) const
            {
                return *ss1 < *ss2;
            }
        };

    private:

        void createMorphTargets(const osgAnimation::MorphGeometry* morphGeom, FbxMesh* mesh, const osg::Matrix& rotateMatrix);

        static bool hasSkeletonParent(const osg::Node& object);

        static osg::Matrix buildParentMatrices(const osg::Node& object);

        void applyGlobalTransforms(FbxNode* RootNode);

        static void snapMeshToParent(const osg::Geometry& geometry, FbxNode* meshNode);

        static osg::Matrix getMatrixFromSkeletonToNode(const osg::Node& node);

        /**
        *  Fill the faces field of the mesh and call buildMesh().
        *  \param name the name to assign to the Fbx Mesh
        *  \param geometryList is the list of geometries which contains the vertices and faces.
        *  \param listTriangles contain all the mesh's faces.
        *  \param texcoords tell us if we have to handle texture coordinates.
        *  \return the new mesh node 
        */ 
        FbxNode* buildMesh(const osg::Geometry& geometry,
            const MaterialParser* materialParser);

        void applySkinning(const osgAnimation::VertexInfluenceMap& vim, FbxMesh* fbxMesh, std::set<std::string>& emptyBoneNames);

        void buildBindPose();

        void buildMeshSkin();

        /// Set Vertices, normals, and UVs
        void setControlPointAndNormalsAndUV(MapIndices& index_vert, FbxMesh* fbxMesh, osg::Matrix& rotateMatrix);

        /**
        *  Create the list of faces from the geode.
        *  \param geo is the geode to study.
        *  \param listTriangles is the list to fill.
        *  \param texcoords tell us if we have to treat texture coord.
        *  \param drawable_n tell us which drawable we are building.
        */
        void createListTriangle(const osg::Geometry* geo,
            ListTriangle& listTriangles,
            bool& texcoords,
            unsigned int         drawable_n);

        FbxAnimStack* getOrCreateAnimStack();

        void applyAnimations(const osg::ref_ptr<osg::Callback>& callback);

        void createAnimationLayer(const osg::ref_ptr<osgAnimation::Animation> osgAnimation);

         void applyUpdateMatrixTransform(const osg::ref_ptr<osg::Callback>& callback, FbxNode* fbxNode,
            osg::MatrixTransform& matrixTransform);

        ///Return a material from StateSet
        WriterNodeVisitor::MaterialParser* processStateSet(const osg::StateSet* stateset);

        typedef std::stack<osg::ref_ptr<osg::StateSet> > StateSetStack;
        typedef std::map<osg::ref_ptr<const osg::StateSet>, MaterialParser, CompareStateSet> MaterialMap;

        ///We need this for every new Node we create.
        FbxManager* _pSdkManager;

        ///Tell us if the last apply succeed, useful to stop going through the graph.
        bool _succeedLastApply;

        ///Marks if the first node is processed.
        bool _firstNodeProcessed;

        ///The current directory.
        std::string _directory;

        ///The Scene to save.
        FbxScene* _pScene;

        ///The current Fbx Node.
        FbxNode* _curFbxNode;
        FbxNode* _MeshesRoot;

        ///The current stateSet.
        osg::ref_ptr<osg::StateSet> _currentStateSet;

        const osgDB::ReaderWriter::Options* _options;
        osgDB::ExternalFileWriter           _externalWriter;

        ///Export options
        bool _ignoreBones;                      // Tell the export engine to ignore Rigging for the mesh
        bool _ignoreAnimations;                 // Tell the export engine to not process animations
        double _rotateXAxis;                    // Tell the export engine to rotate rigged and morphed geometry Nº in X Axis (default = 180.0º)
        bool _exportFullHierarchy;              // Tell the export engine to not bypass node hierarchy
        double _scaleModel;                     // Scales model by a given factor
        bool _flipUVs;                          // Flip UVs on Y Axis

        ///Maintain geode state between visits to the geometry
        GeometryList _geometryList;
        ListTriangle _listTriangles;
        bool _texcoords;
        unsigned int _drawableNum;

        ///Maintain rigged geometry information to latter processing
        typedef std::map<osg::ref_ptr<osgAnimation::RigGeometry>, FbxNode*> RiggedMeshMap;      // Maps OSG Rigged Geometry to FBX meshes
        typedef std::map<osg::ref_ptr<osgAnimation::MorphGeometry>, FbxNode*> MorphedMeshMap;   // Maps OSG Morphed Geometry to FBX meshes
        typedef std::pair<osg::ref_ptr<osgAnimation::Bone>, FbxNode*> BonePair;
        typedef std::unordered_map<std::string, BonePair> BoneNodeMap;                                     // Map Bone name to respective OSG Bone and FBX Bone Node (FbxSkeleton)
        typedef std::unordered_map<std::string, std::shared_ptr<UpdateMatrixNodes>> MatrixAnimCurveMap;    // Maps updateBone names to corresponding bones and FbxNode
        typedef std::map<std::string, FbxBlendShapeChannel*> BlendShapeAnimMap;

        RiggedMeshMap _riggedMeshMap;
        MorphedMeshMap _MorphedMeshMap;
        BoneNodeMap _boneNodeSkinMap;
        MatrixAnimCurveMap _matrixAnimCurveMap;
        BlendShapeAnimMap _blendShapeAnimations;
        FbxNode* _firstMatrix;

        // Keep track of created materials
        std::unordered_map<const osg::Material*, MaterialParser*> _materialMap;

        // Keep track of transform matrices
        std::deque<std::pair<std::string, osg::Matrix>> _matrixStack;

        // Keep track of created Skeletons
        std::set<FbxNode*> _skeletonNodes;

    };

// end namespace pluginfbx
}

#endif // _FBX_WRITER_NODE_VISITOR_HEADER__
