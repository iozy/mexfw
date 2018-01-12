/*
 * Utility functions
 */

#ifndef GOODIES_H
#define GOODIES_H
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

namespace mexfw {
namespace utils {
using namespace rapidjson;

inline bool file_exists (const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

Document parse_file(const std::string& filename) {
    Document d;
    std::ifstream ifs(filename);
    IStreamWrapper isw(ifs);
    d.ParseStream(isw);
    return d;
}

}
}

#endif  /*GOODIES_H*/
