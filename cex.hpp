/*
 *  CEX.IO exchange
 */
//test

#ifndef CEX_H
#define CEX_H
#include <boost/algorithm/string.hpp>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <elle/reactor/http/Request.hh>
#include <elle/cryptography/hmac.hh>
#include <elle/format/hexadecimal.hh>
#include <elle/Duration.hh>
#include <ctpl.h>
#include <stdexcept>
#include "exchanges.hpp"
#include "mexfw.hpp"
#include "arbitrage.hpp"

namespace mexfw {
using namespace utils;
using namespace elle::reactor;
using namespace elle::cryptography;
using namespace elle::format::hexadecimal;
using namespace elle::chrono_literals;

template<>
class rest_api<CEX>: public rest_api_base<CEX> {
    std::string username;
    ctpl::thread_pool tp;
    bool proxy_flood;
public:
    rest_api(const std::string& username = "", bool proxy_flood = true): tp(std::thread::hardware_concurrency()), proxy_flood(proxy_flood), username(username) {}
    std::string trade(auto& arb, const std::string& pair, const std::string& rate, const std::string& amt) {
        std::vector<std::string> coin(2);
        boost::split(coin, pair, boost::is_any_of("-:"));
        auto p = arb.as_pair(arb.pair(coin[0]+"-"+coin[1]).canonical);
        auto& c1 = p.first, c2 = p.second;
        std::string url = "https://cex.io/api/place_order/" + c1 + "/" + c2;
        std::string answer;
        produce_consume({{}}, [&, this](auto) {
            http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
            http::Request r(url, http::Method::POST, "application/x-www-form-urlencoded", conf);
            auto key_pair = this->get_keypair();
            std::string nonce = this->get_nonce(key_pair.first);
            std::string signature = nonce + username + key_pair.first;
            signature = encode(hmac::sign(signature, key_pair.second, Oneway::sha256).string());
            boost::algorithm::to_upper(signature);
            std::string body = "key=" + key_pair.first + "&signature=" + signature + "&nonce=" + nonce+"&type="+(arb(c1+"-"+c2).ord_type=="buylimit"?"buy":"sell")+"&amount="+amt+"&price="+rate;
            r << body;
            r.finalize();

            if(r.status() != http::StatusCode::OK) {
                throw std::runtime_error("status code is not ok");
            }
            answer = r.response().string();
        });
        auto response = parse_str(answer);
        std::string result = response["id"].GetString();
        active_orders.push_back(result);

        return result; 
    }

    void cancel_all() {
        produce_consume(active_orders, [&, this](std::string id){
            std::string answer = this->first_wins([&, id, this]{
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://cex.io/api/cancel_order/", http::Method::POST, "application/x-www-form-urlencoded", conf);
                auto key_pair = this->get_keypair();
                std::string nonce = this->get_nonce(key_pair.first);
                std::string signature = nonce + username + key_pair.first;
                signature = encode(hmac::sign(signature, key_pair.second, Oneway::sha256).string());
                boost::algorithm::to_upper(signature);
                std::string body = "key=" + key_pair.first + "&signature=" + signature + "&nonce=" + nonce + "&id=" + id;
                r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();

            }, proxy_flood ? 4 : 1);
            std::cout<<id<<" is canceled: "<<answer<<"\n";
        });
    }

    void update_balance(auto& balance) {
        produce_consume({{}}, [&, this](auto) {
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://cex.io/api/balance/", http::Method::POST, "application/x-www-form-urlencoded", conf);
                auto key_pair = this->get_keypair();
                std::string nonce = this->get_nonce(key_pair.first);
                std::string signature = nonce + username + key_pair.first;
                signature = encode(hmac::sign(signature, key_pair.second, Oneway::sha256).string());
                boost::algorithm::to_upper(signature);
                std::string body = "key=" + key_pair.first + "&signature=" + signature + "&nonce=" + nonce;
                r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();
            }, proxy_flood ? 4 : 1);
            auto resp = parse_str(response);
            if(resp.HasParseError()) {
                std::cout<<"Failed to parse: "<<response<<'\n';
                return;
            }

            balance.clear();

            for(const auto& entry : resp.GetObject()) {
                std::string k = entry.name.GetString();

                if(k == "timestamp" || k == "username") continue;

                double val = std::stod(entry.value.GetObject()["available"].GetString());

                if(val != 0.) balance[k] = val;
            }
        });
    }
    auto get_all_pairs() {
        std::vector<std::string> all_pairs;
        produce_consume({{}}, [&, this](auto) {
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://cex.io/api/currency_limits", http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();
            }, proxy_flood ? 10 : 1);
            //std::cout<<response<<'\n';
            auto resp = parse_str(response);
            if(resp.HasParseError()) {
                std::cout<<"Failed to parse: "<<response<<'\n';
                return;
            }
            if(resp.HasMember("error")) {
                throw std::runtime_error(resp["error"].GetString());
            }

            for(const auto& k : resp["data"].GetObject()["pairs"].GetArray()) {
                all_pairs.push_back(std::string(k.GetObject()["symbol1"].GetString()) + CEX::delimeter + k.GetObject()["symbol2"].GetString());
            }
        }, 200);
        return all_pairs;
    }

    void get_ob(const std::vector<std::string>& pairs, arbtools::arbitrage& arb, bool best = false) {
        produce_consume(pairs, [&, this](auto p) {
            boost::replace_all(p, ":", "/");
            auto url = "https://cex.io/api/order_book/" + p;
            std::string response = this->first_wins([&, this, url = std::move(url)] {
                auto proxy = this->get_proxy();
                http::Request::Configuration conf(3s, {}, http::Version::v11, true, proxy);
                http::Request r(url, http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();
            }, proxy_flood ? 10 : 1);
            auto ft = tp.push([response = std::move(response)](int) {
                return parse_str(response);
            });

            while(ft.wait_for(0s) != std::future_status::ready)
                sleep(100ms);

            Document doms = ft.get();
            if(doms.HasParseError()) {
                std::cout<<"Failed to parse: "<<response<<'\n';
                return;
            }
            std::string c1, c2;
            std::tie(c1, c2) = arb.as_pair(doms["pair"].GetString(), ":");

            if(doms.HasMember("asks")) {
                arb.add_pair(c2, c1, 0, "buylimit");
                arb.pair(c2, c1).ob.clear();

                for(const auto& ask : doms["asks"].GetArray()) {
                    arb.pair(c2, c1).ob[json_to_str(ask.GetArray()[0])] += ask.GetArray()[1].GetDouble();
                }

                arb.recalc_rates(c2 + "-" + c1, best);
            }

            if(doms.HasMember("bids")) {
                arb.add_pair(c1, c2, 0, "selllimit");
                arb.pair(c1, c2).ob.clear();

                for(const auto& bid : doms["bids"].GetArray()) {
                    arb.pair(c1, c2).ob[json_to_str(bid.GetArray()[0])] += bid.GetArray()[1].GetDouble();
                }

                arb.recalc_rates(c1 + "-" + c2, best);
            }
        }, 15);
    }


};

}

#endif  /*CEX_H*/
