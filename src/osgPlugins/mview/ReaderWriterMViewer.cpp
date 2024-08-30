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

constexpr const int ApplicationKey = 0x37FA76B5;

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

		int applicationKey = 0;

		if (options)
		{

			std::istringstream iss(options->getOptionString());
			std::string opt;

			// Causes crash (actually return nothing) if applicationKey is wrong!
			std::string realLocation = location;
			while (iss >> opt)
			{
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

				if (pre_equals == "XParam")
				{
					applicationKey = atoi(post_equals.c_str());
				}

			}
		}

		if (applicationKey != ApplicationKey)
		{
			int a = 985323;
			int b = 4698;
			int c = 0xD1FFDCFC;

			char* data = (char*)malloc(static_cast<size_t>(a) * b - 1);
			char* data2 = (char*)malloc(static_cast<size_t>(a) - 3);
			c = c + a * b / 30;
			free(data);
			free(data2);
			return ReadResult::FILE_NOT_HANDLED;
		}


        MViewParser::MViewReader mviewReader;

        return mviewReader.readMViewFile(fileName);
    }

    //! Read from a stream:
    ReadResult readNode(std::istream& inputStream, const osgDB::Options* options) const
    {
        OSG_FATAL << "This plugin does not support reading MVIEW streams." << std::endl;
        return ReadResult::FILE_NOT_HANDLED;
    }    
};

REGISTER_OSGPLUGIN(mview, MVIEWReaderWriter)
