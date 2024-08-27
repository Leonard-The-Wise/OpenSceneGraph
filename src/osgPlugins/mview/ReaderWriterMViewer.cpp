/* -*-c++-*- */
/* 
* This is free software; you can redistribute it and/or modify
* it under the terms of the GNU Lesser General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
* IN THE SOFTWARE.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*/

#include "pch.h"

#include <osg/Notify>

#include <osgDB/FileNameUtils>
#include <osgDB/Registry>

#include "MViewReader.h"

class MVIEWReaderWriter : public osgDB::ReaderWriter
{
public:
    MVIEWReaderWriter()
    {
        supportsExtension("mview", "marmoset viewer loader");
    }

    virtual const char* className() const { return "MViewer plugin"; }

    ReadResult readObject(const std::string& location, const osgDB::Options* options) const
    {
        return readNode(location, options);
    }

    ReadResult readNode(const std::string& location, const osgDB::Options* options) const
    {
        std::string ext = osgDB::getLowerCaseFileExtension(location);
        if (!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;

        std::string fileName = osgDB::findDataFile(location, options);
        if (fileName.empty()) return ReadResult::FILE_NOT_FOUND;

        MViewParser::MViewReader mviewReader;

        return mviewReader.readMViewFile(fileName);
    }

    //! Read from a stream:
    ReadResult readNode(std::istream& inputStream, const osgDB::Options* options) const
    {
        OSG_FATAL << "This plugin does not support reading MVIEW files, only writting." << std::endl;
        return ReadResult::FILE_NOT_HANDLED;
    }

    //! Writes a node to GLTF.
    WriteResult writeNode(const osg::Node& node, const std::string& location, const osgDB::Options* options) const
    {
        OSG_FATAL << "This plugin does not support writting MVIEW files, only reading." << std::endl;
            return WriteResult::FILE_NOT_HANDLED;

    }
    
};

REGISTER_OSGPLUGIN(mview, MVIEWReaderWriter)
