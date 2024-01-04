//    copyright: 'Cedric Pinson cedric@plopbyte.com'

#include "pch.h"

#include <osg/Image>
#include <osg/Notify>
#include <osg/Geode>
#include <osg/GL>
#include <osg/Version>
#include <osg/Endian>
#include <osg/Projection>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>

#include <osgUtil/UpdateVisitor>
#include <osgDB/ReaderWriter>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>

#include <osgDB/Registry>
#include <osgDB/FileUtils>
#include <osgDB/FileNameUtils>

#include <osgAnimation/UpdateMatrixTransform>
#include <osgAnimation/AnimationManagerBase>
#include <osgAnimation/BasicAnimationManager>

#include <vector>

#include "json_stream.h"
#include "JSON_Objects.h"
#include "Animation.h"
#include "CompactBufferVisitor.h"
#include "WriteVisitor.h"

#include "ReaderWriterJSON.h"

#include "OsgjsFileCache.h"
#include "OsgjsParser.h"

using json = nlohmann::json;

osgDB::ReaderWriter::WriteResult ReaderWriterJSON::writeNode(const osg::Node& node, const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
{
    std::string ext = osgDB::getFileExtension(fileName);
    if (!acceptsExtension(ext)) return WriteResult::FILE_NOT_HANDLED;

    OptionsStruct _options = parseOptions(options);
    json_stream fout(fileName, _options.strictJson);

    if (fout) {
        WriteResult res = writeNodeModel(node, fout, osgDB::getNameLessExtension(fileName), _options);
        return res;
    }
    return WriteResult("Unable to open file for output");

}

osgDB::ReaderWriter::WriteResult ReaderWriterJSON::writeNode(const osg::Node& node, json_stream& fout, const osgDB::ReaderWriter::Options* options) const
{
    if (!fout) {
        return WriteResult("Unable to write to output stream");
    }

    OptionsStruct _options;
    _options = parseOptions(options);
    return writeNodeModel(node, fout, "stream", _options);
}

osgDB::ReaderWriter::WriteResult ReaderWriterJSON::writeNodeModel(const osg::Node& node, json_stream& fout, const std::string& basename, const OptionsStruct& options) const
{
    // process regular model
    osg::ref_ptr<osg::Node> model = osg::clone(&node);

    if (!options.disableCompactBuffer) {
        CompactBufferVisitor compact;
        model->accept(compact);
    }

    WriteVisitor writer;
    try {
        //osgDB::writeNodeFile(*model, "/tmp/debug_osgjs.osg");
        writer.setBaseName(basename);
        writer.useExternalBinaryArray(options.useExternalBinaryArray);
        writer.mergeAllBinaryFiles(options.mergeAllBinaryFiles);
        writer.setInlineImages(options.inlineImages);
        writer.setMaxTextureDimension(options.resizeTextureUpToPowerOf2);
        writer.setVarint(options.varint);
        writer.setBaseLodURL(options.baseLodURL);
        for (std::vector<std::string>::const_iterator specificBuffer = options.useSpecificBuffer.begin();
            specificBuffer != options.useSpecificBuffer.end(); ++specificBuffer) {
            writer.addSpecificBuffer(*specificBuffer);
        }
        model->accept(writer);
        if (writer._root.valid()) {
            writer.write(fout);
            return WriteResult::FILE_SAVED;
        }
    }
    catch (...) {
        osg::notify(osg::FATAL) << "can't save osgjs file" << std::endl;
        return WriteResult("Unable to write to output stream");
    }
    return WriteResult("Unable to write to output stream");
}

ReaderWriterJSON::OptionsStruct ReaderWriterJSON::parseOptions(const osgDB::ReaderWriter::Options* options) const
{
    OptionsStruct localOptions;

    if (options)
    {
        if (!options->getOptionString().empty())
            osg::notify(osg::NOTICE) << "Parsing options: " << options->getOptionString() << std::endl;
        std::istringstream iss(options->getOptionString());
        std::string opt;
        while (iss >> opt)
        {
            if (options->getDatabasePathList().size() > 0)
                localOptions.baseFilePath = options->getDatabasePathList()[0];

            // split opt into pre= and post=
            std::string pre_equals;
            std::string post_equals;

            size_t found = opt.find("=");
            if (found != std::string::npos)
            {
                pre_equals = opt.substr(0, found);
                post_equals = opt.substr(found + 1);
            }
            else
            {
                pre_equals = opt;
            }

            if (pre_equals == "useExternalBinaryArray")
            {
                localOptions.useExternalBinaryArray = true;
            }
            if (pre_equals == "mergeAllBinaryFiles")
            {
                localOptions.mergeAllBinaryFiles = true;
            }
            if (pre_equals == "disableCompactBuffer")
            {
                localOptions.disableCompactBuffer = true;
            }
            if (pre_equals == "disableStrictJson")
            {
                localOptions.strictJson = false;
            }


            if (pre_equals == "inlineImages")
            {
                localOptions.inlineImages = true;
            }
            if (pre_equals == "varint")
            {
                localOptions.varint = true;
            }

            if (pre_equals == "disableIndexDecompress")
            {
                localOptions.disableIndexDecompress = true;
            }

            if (pre_equals == "rebuildMaterials")
            {
                localOptions.rebuildMaterials = true;
            }

            if (pre_equals == "ignoreGzExtension")
            {
                localOptions.ignoreGzExtension = true;
            }

            if (pre_equals == "additionalSourceDir")
            {
                std::string path = post_equals;
                path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
                localOptions.additionalSourceDirs.emplace(path);
            }

            if (pre_equals == "resizeTextureUpToPowerOf2" && post_equals.length() > 0)
            {
                int value = atoi(post_equals.c_str());
                localOptions.resizeTextureUpToPowerOf2 = osg::Image::computeNearestPowerOfTwo(value);
            }

            if (pre_equals == "useSpecificBuffer" && !post_equals.empty())
            {
                size_t stop_pos = 0, start_pos = 0;
                while ((stop_pos = post_equals.find(",", start_pos)) != std::string::npos) {
                    localOptions.useSpecificBuffer.push_back(post_equals.substr(start_pos,
                        stop_pos - start_pos));
                    start_pos = stop_pos + 1;
                    ++stop_pos;
                }
                localOptions.useSpecificBuffer.push_back(post_equals.substr(start_pos,
                    post_equals.length() - start_pos));
            }

        }
        if (!options->getPluginStringData(std::string("baseLodURL")).empty())
        {
            localOptions.baseLodURL = options->getPluginStringData(std::string("baseLodURL"));
        }
    }
    return localOptions;
}

void ReaderWriterJSON::getModelFiles(const json& value, std::set<std::string>& FileNames) const
{
    if (value.is_object()) {
        for (auto itr = value.begin(); itr != value.end(); ++itr) {
            if (itr.key() == "File") 
            {
                std::string ext = osgDB::getLowerCaseFileExtension(itr.value());
                if (ext == "bin" || ext == "bin.gz" || ext == "binz")
                {
                    osg::notify(osg::DEBUG_INFO) << "Found Model Dependency: " << itr.value() << std::endl;
                    FileNames.insert(itr.value().get<std::string>());
                }
            }

            getModelFiles(itr.value(), FileNames);
        }
    }
    else if (value.is_array()) {
        for (auto& v : value) {
            getModelFiles(v, FileNames);
        }
    }
}

osg::ref_ptr<osg::Node> ReaderWriterJSON::parseOsgjs(const json& input, const OptionsStruct& options) const
{

#ifdef DEBUG
    std::string debugCurrentJSONNode = input.dump();
#endif

    if (input.is_object() && input.contains("osg.Node"))
    {
        if (!input["osg.Node"].is_object())
        {
            osg::notify(osg::FATAL) << "Can't parse file. Root node is invalid!" << std::endl;
            return nullptr;
        }

        osg::ref_ptr<osg::Group> rootNode;
        osgJSONParser::OsgjsParser nodeParser;

        if (input.contains("Generator"))
        {
            osg::notify(osg::ALWAYS) << "Generator: " << input["Generator"].get<std::string>();
            if (input["Generator"].get<std::string>() == "OpenSceneGraph 3.7.0")
                nodeParser.setNeedDecodeIndices(false);
        }
        if (input.contains("Generator") && input.contains("Version"))
            osg::notify(osg::ALWAYS) << " [Version " << input["Version"] << "]";
        if (input.contains("Generator"))
            osg::notify(osg::ALWAYS) << std::endl;

        // Get list of files inside the node to build file cache
        std::set<std::string> files;
        getModelFiles(input, files);

        // Build file cache
        if (files.size() > 0)
            osg::notify(osg::ALWAYS) << "[OSGJS] Building model's file cache..." << std::endl;

        osgJSONParser::FileCache fileCache(files, options.additionalSourceDirs);
        nodeParser.setFileCache(fileCache);
        nodeParser.setFileBasePath(options.baseFilePath);

        if (options.disableIndexDecompress)
            nodeParser.setNeedDecodeIndices(false);

        rootNode = nodeParser.parseObjectTree(input["osg.Node"]);

        if (rootNode)
            osg::notify(osg::ALWAYS) << "[OSGJS] Done importing!" << std::endl;
        else
            osg::notify(osg::FATAL) << "[OSGJS] Error importing model file!" << std::endl;

        return rootNode;
    }

    osg::notify(osg::FATAL) << "[OSGJS] Error importing model. File doesn't have a valid \"osg.Node\" object!" << std::endl;
    return nullptr;
}

osgDB::ReaderWriter::ReadResult ReaderWriterJSON::readObject(const std::string& filename, const osgDB::ReaderWriter::Options* options) const
{
    return readNode(filename, options);
}

osgDB::ReaderWriter::ReadResult ReaderWriterJSON::readNode(const std::string& file, const Options* options) const
{
    std::string ext = osgDB::getLowerCaseFileExtension(file);
    if (!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;

    std::string fileName = osgDB::findDataFile(file, options);
    if (fileName.empty()) return ReadResult::FILE_NOT_FOUND;

    osgDB::ifstream fin(fileName.c_str());
    if (fin)
    {
        osg::ref_ptr<Options> local_opt = options ? static_cast<Options*>(options->clone(osg::CopyOp::SHALLOW_COPY)) : new Options;
        std::string filepath = osgDB::getFilePath(fileName);
        local_opt->getDatabasePathList().push_front(osgDB::getFilePath(fileName));

        json doc;
        try {
            fin >> doc;
            fin.close();
        }
        catch (json::parse_error&) {
            osg::notify(osg::FATAL) << file << " has an invalid format!" << std::endl;
            return ReadResult::ERROR_IN_READING_FILE;
        }

        if (!doc.is_object())
        {
            osg::notify(osg::FATAL) << file << " does not contain a valid scene!" << std::endl;
            return ReadResult::ERROR_IN_READING_FILE;
        }

        osg::notify(osg::ALWAYS) << "[OSGJS] Reading \"" << fileName << "\"..." << std::endl;
        
        OptionsStruct _options = parseOptions(local_opt);

        return parseOsgjs(doc, _options);
    }

    return ReadResult::FILE_NOT_HANDLED;
}

// now register with Registry to instantiate the above
// reader/writer.
REGISTER_OSGPLUGIN(osgjs, ReaderWriterJSON)
