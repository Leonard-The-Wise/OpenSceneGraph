#pragma once

/**
 * Assembles and returns an inline string using a stream-like << operator.
 * Example:
 *     std::string str = Stringify() << "Hello, world " << variable;
 */
struct Stringify
{
    operator std::string () const
    {
        std::string result;
        result = buf.str();
        return result;
    }

    template<typename T>
    Stringify& operator << (const T& val) { buf << val; return (*this); }

    Stringify& operator << (const Stringify& val) { buf << (std::string)val; return (*this); }

protected:
    std::stringstream buf;
};

template<> inline
Stringify& Stringify::operator << <bool>(const bool& val) { buf << (val ? "true" : "false"); return (*this); }

template<> inline
Stringify& Stringify::operator << <osg::Vec3f>(const osg::Vec3f& val) {
    buf << val.x() << " " << val.y() << " " << val.z(); return (*this); }

template<> inline
Stringify& Stringify::operator << <osg::Vec3d>(const osg::Vec3d& val ) {
    buf << val.x() << " " << val.y() << " " << val.z(); return (*this); }

template<> inline
Stringify& Stringify::operator << <osg::Vec4f>(const osg::Vec4f& val) {
    buf << val.r() << " " << val.g() << " " << val.b() << " " << val.a(); return (*this); }
