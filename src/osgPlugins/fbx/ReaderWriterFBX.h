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
        // Read options
        supportsOption("LightmapTextures", "(Read option) Interpret texture maps as overriding the lighting. 3D Studio Max may export files that should be interpreted in this way.");
        supportsOption("UseFbxRoot", "(Read) If the source OSG root node is a simple group with no stateset, the reader will put its children directly under the FBX root");
        supportsOption("TessellatePolygons", "(Read option) Tessellate mesh polygons. If the model contains concave polygons this may be necessary, however tessellating can be very slow and may erroneously produce triangle shards.");
        // Write options
        supportsOption("Embedded", "(Write option) Embed textures in FBX file");
        supportsOption("FBXASCII", "(Write option) Export as FBX ASCII format.");
        supportsOption("FlipUVs", "(Write option) Flip textures UV's.");
        supportsOption("NoAnimations", "(Write option) Ignore animations.");
        supportsOption("NoRigging", "(Write option) Ignore model rigging. This option also disables animations exporting.");
        supportsOption("NoWeights", "(Write option) Export skeleton and animations without any vertex weights.");
        supportsOption("RotateXAxis", "(Write option) Rotate models on X axis. Use like: -O RotateXAxis=Angle (eg: RotateXAxis=-90.0).");
        supportsOption("ScaleModel", "(Write option) Scale model uniformly by given factor. Use like -O ScaleModel=Factor (eg: ScaleModel=100.0).");
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
