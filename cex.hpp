/*
 *  CEX.IO exchange
 */

#ifndef CEX_H
#define CEX_H
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <elle/reactor/http/Request.hh>
#include <elle/Duration.hh>
#include <ctpl.h>
#include <stdexcept>
#include "exchanges.hpp"
#include "mexfw.hpp"
#include "arbitrage.hpp"

namespace mexfw {
using namespace utils;
using namespace elle::reactor;
using namespace elle::chrono_literals;

template<>
class rest_api<CEX>: public rest_api_base<CEX> {
    ctpl::thread_pool tp;
public:
    rest_api(): tp(std::thread::hardware_concurrency()) {}
    auto get_all_pairs() {
        std::vector<std::string> all_pairs;
        produce_consume({{}}, [&, this](auto) {
            http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
            http::Request r("https://cex.io/api/currency_limits", http::Method::GET, "application/json", conf);
            r.finalize();

            if(r.status() != http::StatusCode::OK) {
                std::cout << "fuck ! it's not ok" << '\n';
                //return;
                throw std::runtime_error("status code is not ok");
            }

            auto resp = parse_str(r.response().string());

            for(const auto& k : resp["data"].GetObject()["pairs"].GetArray()) {
                all_pairs.push_back(std::string(k.GetObject()["symbol1"].GetString()) + CEX::delimeter + k.GetObject()["symbol2"].GetString());
            }
        }, 200);
        return all_pairs;
    }

    void get_ob(const std::vector<std::string>& pairs, arbtools::arbitrage& arb) {
        produce_consume(pairs, [&, this](auto p) {
            //Document doms = this->first_wins([&, this, p] {
            //std::string _p(p);
            boost::replace_all(p, ":", "/");
            auto url = "https://cex.io/api/order_book/" + p;
            auto proxy = this->get_proxy();
            http::Request::Configuration conf(3s, {}, http::Version::v11, true, proxy);
            http::Request r(url, http::Method::GET, "application/json", conf);
            r.finalize();

            if(r.status() != http::StatusCode::OK) {
                std::cout << "fuck ! it's not ok" << '\n';
                //return;
                throw std::runtime_error("status code is not ok");
            }

            auto ft = tp.push([response = r.response().string()](int) {
                return parse_str(response);
            });

            while(ft.wait_for(0s) != std::future_status::ready)
                sleep(100ms);

            //return ft.get();
            //}, 4, 50);
            Document doms = ft.get();
            std::string c1, c2;
            std::tie(c1, c2) = arb.as_pair(doms["pair"].GetString(), ":");

            if(doms.HasMember("asks")) {
                arb.add_pair(c2, c1, 0, "buylimit");
                arb.pair(c2, c1).ob.clear();

                for(const auto& ask : doms["asks"].GetArray()) {
                    arb.pair(c2, c1).ob[json_to_str(ask.GetArray()[0])] += ask.GetArray()[1].GetDouble();
                }

                arb.recalc_rates(c2 + "-" + c1);
            }

            if(doms.HasMember("bids")) {
                arb.add_pair(c1, c2, 0, "selllimit");
                arb.pair(c1, c2).ob.clear();

                for(const auto& bid : doms["bids"].GetArray()) {
                    arb.pair(c1, c2).ob[json_to_str(bid.GetArray()[0])] += bid.GetArray()[1].GetDouble();
                }

                arb.recalc_rates(c1 + "-" + c2);
            }

        }, 15);
    }


};

}

#endif  /*CEX_H*/
