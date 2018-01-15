/*
 * Utility functions
 */

#ifndef GOODIES_H
#define GOODIES_H
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace mexfw {
namespace utils {
using namespace rapidjson;

inline bool file_exists (const std::string& name) {
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

inline Document parse_file(const std::string& filename) {
    Document d;
    std::ifstream ifs(filename);
    IStreamWrapper isw(ifs);
    d.ParseStream(isw);
    return d;
}

inline Document parse_str(const std::string& s) {
    Document d;
    d.Parse(s.c_str());
    return d;
}

std::string json_to_str(auto& d) {
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    d.Accept(writer);
    return buffer.GetString();
}

void save_json(Document& d, const std::string& filename) {
    std::ofstream ofs;
    ofs.open(filename);
    if(ofs.is_open()) {
        auto json_str = json_to_str(d);
        ofs << json_str;
    }
    ofs.close();
}

}
}

#endif  /*GOODIES_H*/
