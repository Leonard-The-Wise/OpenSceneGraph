#ifndef READERWRITERFBX_H
#define READERWRITERFBX_H

#include <osgDB/ReaderWriter>
#include <fbxsdk/fbxsdk_def.h>


///////////////////////////////////////////////////////////////////////////
// OSG reader plugin for the ".fbx" format.
// See http://www.autodesk.com/fbx
// This plugin requires the FBX SDK version 2013.3 or 2014.1 or later

#if FBXSDK_VERSION_MAJOR < 2013 || (FBXSDK_VERSION_MAJOR == 2013 && FBXSDK_VERSION_MINOR < 3)
#error Wrong FBX SDK version
#endif

class ReaderWriterFBX : public osgDB::ReaderWriter
{
public:
    ReaderWriterFBX()
    {
        supportsExtension("fbx", "FBX format");
        supportsOption("Embedded", "(Write option) Embed textures in FBX file");
        supportsOption("UseFbxRoot", "(Read) If the source OSG root node is a simple group with no stateset, the reader will put its children directly under the FBX root");
        supportsOption("LightmapTextures", "(Read option) Interpret texture maps as overriding the lighting. 3D Studio Max may export files that should be interpreted in this way.");
        supportsOption("TessellatePolygons", "(Read option) Tessellate mesh polygons. If the model contains concave polygons this may be necessary, however tessellating can be very slow and may erroneously produce triangle shards.");
        supportsOption("IgnoreRigging", "(Write option) Ignore model rigging. This option also disables animations import.");
        supportsOption("IgnoreAnimations", "(Write option) Ignore animations.");
        supportsOption("SnapMeshesToParentGroup", "(Write option) Some models need to snap meshes transformations to parent Matrix groups. Use this option if pieces of your object looks mixed up.");
        supportsOption("RotateXAxis", "(Write option) Rotate models for Rigged and Morphed geometry to fix some models being upside down.");
    }

    const char* className() const { return "FBX reader/writer"; }

    virtual ReadResult readObject(const std::string& filename, const Options* options) const
    {
        return readNode(filename, options);
    }

    virtual WriteResult writeObject(const osg::Node& node, const std::string& filename, const Options* options) const
    {
        return writeNode(node, filename, options);
    }

    virtual ReadResult readNode(const std::string& filename, const Options*) const;
    virtual WriteResult writeNode(const osg::Node&, const std::string& filename, const Options*) const;
};

///////////////////////////////////////////////////////////////////////////

#endif
