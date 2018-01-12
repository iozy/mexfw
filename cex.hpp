/*
 *  CEX.IO exchange
 */

#ifndef CEX_H
#define CEX_H
#include <rapidjson/document.h>
#include <elle/reactor/http/Request.hh>
#include <elle/Duration.hh>
#include "exchanges.hpp"
#include "mexfw.hpp"

namespace mexfw {
using namespace utils;
using namespace elle::reactor;
using namespace elle::chrono_literals;

template<>
class rest_api<CEX>: public rest_api_base<CEX> {
public:
    auto get_all_pairs() {
        std::vector<std::string> all_pairs;
        produce_consume({{}}, [&, this](auto) {
            http::Request::Configuration conf(5s, {}, http::Version::v20, true, this->get_proxy());
            http::Request r("https://cex.io/api/currency_limits", http::Method::GET, "application/json", conf);
            r.finalize();
            auto resp = parse_str(r.response().string());
            for(const auto& k : resp["data"].GetObject()["pairs"].GetArray()) {
                all_pairs.push_back(std::string(k.GetObject()["symbol1"].GetString()) + CEX::delimeter + k.GetObject()["symbol2"].GetString());
            }
        }, 10);
        return all_pairs;
    }
//    void get_ob(const std::vector<std::string>& pairs, arbtools::arbitrage& arb) {}

};

}

#endif  /*CEX_H*/
