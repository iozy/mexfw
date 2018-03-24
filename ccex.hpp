/*
 *  C-CEX.COM exchange
 */

#ifndef CCEX_H
#define CCEX_H
#include <boost/algorithm/string.hpp>
#include <boost/range/algorithm/replace_if.hpp>
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
class rest_api<CCEX>: public rest_api_base<CCEX> {    
    friend class CCEX;
    std::unordered_map<std::string, double> min_sizes;
    bool proxy_flood;

    std::string signed_payload(std::string& url) {
        auto key_pair = this->get_keypair();
        std::string nonce = this->get_nonce(key_pair.first);
        url += "&apikey="+key_pair.first+"&nonce="+nonce;
        return encode(hmac::sign(url, key_pair.second, Oneway::sha512).string());
    }

public:
    rest_api(bool proxy_flood = true): /*tp(std::thread::hardware_concurrency()),*/ proxy_flood(proxy_flood) {}

    auto get_trade_history(const std::vector<std::string>& pairs) {
        std::unordered_map<std::string, double> historic_rates;
        produce_consume(pairs, [&, this](auto pair) {
            std::string url = "https://c-cex.com/t/api.html?a=mytrades&marketid="+pair;
            std::string apisign = this->signed_payload(url);
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                conf.header_add("apisign", apisign);
                http::Request r(url, http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, proxy_flood ? 4 : 1);
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }
            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }
            
            for(const auto& entry: resp["return"].GetArray()) {
                std::string pair = entry["marketid"].GetString();
                boost::to_lower(pair);
                if(entry["tradetype"] == "Buy") {
                    pair = arbtools::misc::reverse_pair(pair);
                    if(historic_rates.find(pair) != historic_rates.end()) continue;
                    historic_rates[pair] = 1/std::stod(entry["tradeprice"].GetString());
                } else {
                    if(historic_rates.find(pair) != historic_rates.end()) continue;
                    historic_rates[pair] = std::stod(entry["tradeprice"].GetString());
                }
            }
        });
        return historic_rates;
    }

    auto get_order_history() {
        std::unordered_map<std::string, double> historic_rates;
        produce_consume({{}}, [&, this](auto) {
            std::string url = "https://c-cex.com/t/api.html?a=getorderhistory";
            std::string apisign = this->signed_payload(url);
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                conf.header_add("apisign", apisign);
                http::Request r(url, http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, proxy_flood ? 4 : 1);
            //std::cout<<response<<'\n';
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }
            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }
            
            for(const auto& entry: resp["result"].GetArray()) {
                std::string pair = entry["Exchange"].GetString();
                boost::to_lower(pair);
                pair = arbtools::misc::reverse_pair(pair);
                if(historic_rates.find(pair) != historic_rates.end()) continue;
                historic_rates[pair] = entry["OrderType"] == "LIMIT_BUY" ? 1/entry["PricePerUnit"].GetDouble() : entry["PricePerUnit"].GetDouble();
                //std::cout<<pair<<": "<<historic_rates[pair]<<"\n";
            }
        });
        return historic_rates;
    }

    auto get_open_orders() {
        produce_consume({{}}, [&, this](auto) {
            std::string url = "https://c-cex.com/t/api.html?a=getopenorders";
            std::string apisign = this->signed_payload(url);
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                conf.header_add("apisign", apisign);
                http::Request r(url, http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, proxy_flood ? 4 : 1);
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }
            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }

            active_orders.clear();

            for(const auto& entry : resp["result"].GetArray()) {
                active_orders.push_back(entry.GetObject()["OrderUuid"].GetString());
            }
        });
        return active_orders;
    }

    template<class arb_t>
    std::string trade(arb_t& arb, const std::string& p, const std::pair<double, double>& szp) {
        std::string result;
        std::string rate, quantity, market, type;
        market = arb(p).canonical;
        type = arb(p).ord_type;
        if(type == "buylimit"){
            double amt = szp.second / (1 - CCEX::fee);
            rate = arb.get_rate2(p, amt);
            quantity = arbtools::double2string(amt);
        }
        else {
            double amt = szp.first;
            rate = arb.get_rate1(p, amt);
            quantity = arbtools::double2string(amt);
        }
        produce_consume({{}}, [&, this](auto) {
            std::string url = "https://c-cex.com/t/api.html?a="+type+"&market="+market+"&rate="+rate+"&quantity="+quantity;
            std::cout<<"doing trade url="<<url<<'\n';
            std::string apisign = this->signed_payload(url);
            /*std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(3s, {}, http::Version::v11, true, this->get_proxy());
                conf.header_add("apisign", apisign);
                http::Request r(url, http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, 1);
            std::cout<<"response="<<response<<'\n';
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }
            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }
            result = resp["result"]["uuid"].GetString();
            */

        });

        return result;
    }

    auto cancel(const std::vector<std::string>& ids) {
        std::unordered_map<std::string, bool> result;
        produce_consume(ids, [&, this](std::string id) {
            result[id] = this->first_wins([&, id, this] {
                bool res = false;
                return  res;
            }, proxy_flood ? 4 : 1);
        });
        for(auto it = active_orders.begin(); it != active_orders.end();) {
            if(result[*it]) it = active_orders.erase(it);
            else ++it;
        }
        return result;
    }

    void cancel_all() {
        cancel(active_orders);
    }

    template<class T>
    void update_balance(T& balance) {
        produce_consume({{}}, [&, this](auto) {
            std::string url = "https://c-cex.com/t/api.html?a=getbalances";
            std::string apisign = this->signed_payload(url);
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                conf.header_add("apisign", apisign);
                http::Request r(url, http::Method::GET, "application/x-www-form-urlencoded", conf);
                //r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, proxy_flood ? 4 : 1);
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }

            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }

            balance.clear();

            for(const auto& entry : resp["result"].GetArray()) {
                double val = entry.GetObject()["Available"].GetDouble();
                std::string coin = entry.GetObject()["Currency"].GetString();
                boost::to_lower(coin);
                if(val != 0.) balance[coin] = val;
            }
        });
    }

    auto get_balance() {
        std::unordered_map<std::string, double> retval;
        update_balance(retval);
        return retval;
    }

    void get_full_ob(arbtools::arbitrage& arb) {
        produce_consume({{}}, [&, this](auto) {
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(10s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://c-cex.com/t/api_pub.html?a=getfullorderbook&depth=20", http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, proxy_flood ? 10 : 1);
            //std::cout<<response<<'\n';
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }
            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }
            const auto& doms = resp["result"].GetObject();
            std::stringstream s;
            //for(auto p: arb.all_pairs()) arb(p).ob.clear();
            //for(auto p: arb.all_pairs()) {
            //    arb(p).ob.clear();
            //}

            std::unordered_map<std::string, decltype(arbtools::EdgeProperties::ob)> obs;
            if(doms.HasMember("buy")) {
                for(const auto& item: doms["buy"].GetArray()) {
                    std::string p = item["Market"].GetString();
                    if(std::find(std::begin(CCEX::base_rates), std::end(CCEX::base_rates), p) != std::end(CCEX::base_rates)) {
                        arb.add_pair(p, 0, "selllimit");
                        //arb(p).ob[json_to_str(item["Rate"])] += item["Quantity"].GetDouble();
                        obs[p][json_to_str(item["Rate"])] += item["Quantity"].GetDouble();
                        continue;
                    }
                    if(std::find(std::begin(CCEX::rev_base_rates), std::end(CCEX::rev_base_rates), p) != std::end(CCEX::rev_base_rates))
                        continue;

                    s<<std::fixed<<std::setprecision(8)<<1./item["Rate"].GetDouble();//+0.0017025646;
                    arb.add_pair(p, 0, "buylimit");
                    //arb(p).ob[s.str()] += item["Rate"].GetDouble() * item["Quantity"].GetDouble();
                    obs[p][s.str()] += item["Rate"].GetDouble() * item["Quantity"].GetDouble();
                    s.str({});
                }
            }
            if(doms.HasMember("sell")) {
                for(const auto& item: doms["sell"].GetArray()) {
                    std::string p = item["Market"].GetString();
                    if(std::find(std::begin(CCEX::rev_base_rates), std::end(CCEX::rev_base_rates), p) != std::end(CCEX::rev_base_rates)) {
                        arb.add_pair(p, 0, "buylimit");
                        //arb(p).ob[json_to_str(item["Rate"])] += item["Quantity"].GetDouble();
                        obs[p][json_to_str(item["Rate"])] += item["Quantity"].GetDouble();
                        continue;
                    }
                    if(std::find(std::begin(CCEX::base_rates), std::end(CCEX::base_rates), p) != std::end(CCEX::base_rates))
                        continue;

                    s<<std::fixed<<std::setprecision(8)<<1./item["Rate"].GetDouble();//+0.0017025646;
                    arb.add_pair(p, 0, "selllimit");
                    //arb(p).ob[s.str()] += item["Rate"].GetDouble() * item["Quantity"].GetDouble();
                    obs[p][s.str()] += item["Rate"].GetDouble() * item["Quantity"].GetDouble();
                    s.str({});
                }
            }
            for(auto itm: obs) {
                arb(itm.first).ob = itm.second;
                arb.recalc_rates(itm.first);
            }

        }, 10);
    }

    auto get_all_pairs() {
        std::vector<std::string> all_pairs;
        produce_consume({{}}, [&, this](auto) {
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://c-cex.com/t/pairs.json", http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();
            }, proxy_flood ? 10 : 1);
            //std::cout<<response<<'\n';
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }

            for(const auto& k : resp["pairs"].GetArray()) {
                all_pairs.push_back(k.GetString());
            }
        }, 10);
        return all_pairs;
    }

    void get_ob(const std::vector<std::string>& pairs, arbtools::arbitrage& arb, bool best = false) {
        /*std::set<std::string> pairs_set;
        for(auto& p: pairs) pairs_set.insert(arb(p).canonical);
        pairs.assign(pairs_set.begin(), pairs_set.end());*/
        produce_consume(pairs, [&, this](auto pair) {
            std::string response = this->first_wins([&, this] {
                http::Request::Configuration conf(10s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://c-cex.com/t/api_pub.html?a=getorderbook&type=both&depth=20&market="+pair, http::Method::GET, "application/json", conf);
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                std::string answer = r.response().string();
                if(answer.length() == 0) throw std::runtime_error("empty response");
                return answer;
            }, proxy_flood ? 10 : 1);
            //std::cout<<response<<'\n';
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }
            if(resp.IsObject() && !resp["success"].GetBool()) {
                std::cout<<"Error: "<<resp["message"].GetString()<<'\n';
                throw std::runtime_error(resp["message"].GetString());
            }
            const auto& doms = resp["result"].GetObject();

            if(doms.HasMember("buy")) {
                std::string& p = pair;
                arb.add_pair(p, 0, "selllimit");
                arb(p).ob.clear();
                for(const auto& item: doms["buy"].GetArray()) {
                    arb(p).ob[json_to_str(item["Rate"])] += item["Quantity"].GetDouble();
                }
                arb.recalc_rates(p);
            }
            if(doms.HasMember("sell")) {
                std::string p = arb.reverse(pair);
                arb.add_pair(p, 0, "buylimit");
                arb(p).ob.clear();
                for(const auto& item: doms["sell"].GetArray()) {
                    arb(p).ob[json_to_str(item["Rate"])] += item["Quantity"].GetDouble();
                }
                arb.recalc_rates(p);
            }

        }, 10);

    }

};

}

#endif  /*CCEX_H*/
