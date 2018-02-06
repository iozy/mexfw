/*
 *  CEX.IO exchange
 *  TODO:
 *  add trade({pairs, sizes})
 *  add multi-index-container for orders with fields: id, pair-name, time, price, amt *
 */

#ifndef CEX_H
#define CEX_H
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
class rest_api<CEX>: public rest_api_base<CEX> {
    std::string username;
    std::unordered_map<std::string, double> min_sizes;
    //ctpl::thread_pool tp;
    bool proxy_flood;

    std::string signed_payload(const std::unordered_map<std::string, std::string>& other_params = {}) {
        auto key_pair = this->get_keypair();
        std::string nonce = this->get_nonce(key_pair.first);
        std::string signature = nonce + username + key_pair.first;
        signature = encode(hmac::sign(signature, key_pair.second, Oneway::sha256).string());
        boost::algorithm::to_upper(signature);
        std::string body = "key=" + key_pair.first + "&signature=" + signature + "&nonce=" + nonce;

        if(other_params.size() > 0) {
            for(auto p : other_params) {
                body += "&" + p.first + "=" + p.second;
            }
        }

        return body;
    }
public:
    rest_api(const std::string& username = "", bool proxy_flood = true): /*tp(std::thread::hardware_concurrency()),*/ proxy_flood(proxy_flood), username(username) {}

    auto get_open_orders() {
        produce_consume({{}}, [&, this](auto) {
            auto body = this->signed_payload();
            std::string response = this->first_wins([&, this, body] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://cex.io/api/open_orders/", http::Method::POST, "application/x-www-form-urlencoded", conf);
                r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();
            }, proxy_flood ? 4 : 1);
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }

            if(resp.IsObject() && resp.HasMember("error")) {
                std::string error = resp["error"].GetString();
                std::cout<<"get_open_orders failed: "<<error<<'\n';
                throw std::runtime_error(error);
            }

            active_orders.clear();

            for(const auto& entry : resp.GetArray()) {
                auto ord = entry.GetObject();
                active_orders.push_back(ord["id"].GetString());
            }
        });
        return active_orders;
    }

    std::string trade(auto& arb, const std::string& pair, const std::string& rate, const std::string& amt) {
        std::string result;
        std::vector<std::string> coin(2);
        boost::split(coin, pair, boost::is_any_of("-:"));
        auto p = arb.as_pair(arb.pair(coin[0] + "-" + coin[1]).canonical);
        auto& c1 = p.first, c2 = p.second;
        std::string url = "https://cex.io/api/place_order/" + c1 + "/" + c2;
        produce_consume({{}}, [&, this](auto) {
            std::string body = this->signed_payload({{"type", (arb(c1 + "-" + c2).ord_type == "buylimit" ? "buy" : "sell")}, {"amount", amt}, {"price", rate}});
            std::string answer = this->first_wins([&, this, body] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r(url, http::Method::POST, "application/x-www-form-urlencoded", conf);
                r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }

                return r.response().string();
            }, proxy_flood ? 4 : 1);
            auto response = parse_str(answer);
            result = response["id"].GetString();
        });
        active_orders.push_back(result);
        return result;
    }

    auto cancel(const std::vector<std::string>& ids) {
        std::unordered_map<std::string, bool> result;
        produce_consume(ids, [&, this](std::string id) {
            std::string body = this->signed_payload({{"id", id}});
            result[id] = this->first_wins([&, id, this, body] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://cex.io/api/cancel_order/", http::Method::POST, "application/x-www-form-urlencoded", conf);

                r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                auto resp = r.response().string();

                if(resp != "true" && resp != "false") {
                    std::cout << "cancel failed: " << resp << '\n';
                    throw std::runtime_error("cancel failed");
                }
                bool res = false;
                std::istringstream(resp) >> std::boolalpha >> res;
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

    void update_balance(auto& balance) {
        produce_consume({{}}, [&, this](auto) {
            std::string body = this->signed_payload();
            std::string response = this->first_wins([&, this, body] {
                http::Request::Configuration conf(5s, {}, http::Version::v11, true, this->get_proxy());
                http::Request r("https://cex.io/api/balance/", http::Method::POST, "application/x-www-form-urlencoded", conf);
                r << body;
                r.finalize();

                if(r.status() != http::StatusCode::OK) {
                    throw std::runtime_error("status code is not ok");
                }
                return r.response().string();
            }, proxy_flood ? 4 : 1);
            auto resp = parse_str(response);

            if(resp.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }

            if(resp.IsObject() && resp.HasMember("error")) {
                std::string error = resp["error"].GetString();
                std::cout<<"get_open_orders failed: "<<error<<'\n';
                throw std::runtime_error(error);
            }

            balance.clear();

            for(const auto& entry : resp.GetObject()) {
                std::string k = entry.name.GetString();

                if(k == "timestamp" || k == "username") continue;

                double val = std::stod(entry.value.GetObject()["available"].GetString());
                val -= std::stod(entry.value.GetObject()["orders"].GetString());

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
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }

            if(resp.IsObject() && resp.HasMember("error")) {
                std::string error = resp["error"].GetString();
                std::cout<<"get_open_orders failed: "<<error<<'\n';
                throw std::runtime_error(error);
            }

            for(const auto& k : resp["data"].GetObject()["pairs"].GetArray()) {
                std::string c1 = std::string(k.GetObject()["symbol1"].GetString()),
                            c2 = std::string(k.GetObject()["symbol2"].GetString());
                all_pairs.push_back(c1 + "-" + c2);
                min_sizes[c1 + "-" + c2] = k.GetObject()["minLotSize"].GetDouble();
            }
        }, 200);
        return all_pairs;
    }

    void get_ob(const std::vector<std::string>& pairs, arbtools::arbitrage& arb, bool best = false) {
        produce_consume(pairs, [&, this](auto p) {
            std::vector<std::string> coin(2);
            boost::split(coin, p, boost::is_any_of("-:"));
            std::string& c1 = coin[0], c2 = coin[1];
            auto url = "https://cex.io/api/order_book/" + c1 + "/" + c2;
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
            /*auto ft = tp.push([response = std::move(response)](int) {
                return parse_str(response);
            });

            while(ft.wait_for(0s) != std::future_status::ready)
                sleep(100ms);

            Document doms = ft.get();
            */
            auto doms = parse_str(response);

            if(doms.HasParseError()) {
                std::cout << "Failed to parse: " << response << '\n';
                return;
            }

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
