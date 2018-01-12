/*
 * Base interface class for all exchanges.
 */

#ifndef MEXFW_H
#define MEXFW_H
#include <elle/reactor/Scope.hh>
#include <elle/reactor/Channel.hh>
#include <elle/reactor/Barrier.hh>
#include <elle/reactor/Thread.hh>
#include <elle/reactor/network/proxy.hh>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include "goodies.hpp"

namespace mexfw {

using namespace elle::reactor;
using namespace rapidjson;
using namespace utils;

template <typename EXCHANGE>
class rest_api;

template <typename EXCHANGE>
class rest_api_base {
protected:
    // Common fields and vars
    std::unordered_map<std::string, std::string> apikeys;
    std::unordered_map<std::string, unsigned long int> nonces;
    std::unordered_map<std::string, std::string> cookies;
    std::vector<network::Proxy> proxies;
    std::unordered_map<std::string, size_t> proxy_requests;
    std::unordered_map<std::string, size_t> ok_proxy_requests;

public:
    // API keys and nonces
    //void load_keys(const auto& filename) {}
    //void add_key(){}
    //void rm_key(){}
    //std::string get_priv_key(const auto& key, bool switch = true) {}


    // Proxy tools
    void load_proxies(const std::string& filename) {
        proxies.clear();
        if(!file_exists(filename)) {
            proxies.push_back({network::ProxyType::None, {}, {}});
            return;
        }
        auto proxy_file = parse_file(filename);
//       Document xx;
//       xx["test"].GetArray()[0].GetUint();
        for(const auto& p: proxy_file.GetArray()) {
            network::ProxyType proxy_type = p.GetObject()["types"].GetArray()[0].GetObject()["type"].GetString() == "HTTP" ? network::ProxyType::HTTP :
                                            p.GetObject()["types"].GetArray()[0].GetObject()["type"].GetString() == "HTTPS" ? network::ProxyType::HTTPS :
                                            p.GetObject()["types"].GetArray()[0].GetObject()["type"].GetString() == "SOCKS5" ? network::ProxyType::SOCKS : network::ProxyType::None;
            proxies.push_back(network::Proxy(proxy_type, p.GetObject()["host"].GetString(), p.GetObject()["port"].GetUint()));
        }
    }

    void save_proxies(const std::string& filename) {
        /*        json proxies2save;*/
        //std::ofstream ofs(filename);

        //for(auto p: proxies) {
        //std::string proxy_type = p.type() == network::ProxyType::HTTP ? "HTTP" :
        //p.type() == network::ProxyType::HTTPS ? "HTTPS" :
        //p.type() == network::ProxyType::SOCKS ? "SOCKS5" : "None";
        //proxies2save.push_back({{"host", p.host()}, {"port", p.port()}, {{"type", proxy_type}} });
        //}
        /*proxies2save >> ofs;*/
    }

    network::Proxy get_proxy(bool switch_ = true) {
        if(proxies.size() == 0) {
            return network::Proxy(network::ProxyType::None, {}, {});
        } else {
            if(switch_) {
                std::rotate(proxies.begin(), proxies.begin()+1, proxies.end());
            }
            auto proxy = proxies[0];
            proxy_requests[proxy.host()]++;
            return proxy;
        }
    }

    // Cookie routines
    //void load_cookies() {}
    //void save_cookies() {}

    // Patterns and routines to do async work
protected:
    template<class F>
    auto first_wins(F worker = {}, size_t n_wrks = 4, size_t n_fail = 30) {
        decltype(worker()) result;
        elle::With<Scope>() << [&] (Scope& scope) {
            Channel<size_t> chan;

            std::unordered_map<size_t, size_t> failures;
            Barrier work_ready, work_done;
            bool finish = false;

            for(size_t i = 0; i < n_wrks; ++i) {
                scope.run_background("worker" + std::to_string(i), [&, i] {
                    work_ready.wait();

                    while(!chan.empty()) {
                        try {
                            if(finish) break;
                            result = worker();
                            failures[i] = 0;
                            if(finish) break;
                            //std::cout<<"worker "<<i<<" wins\n";
                            chan.clear();
                            finish = true;
                            break;
                        } catch(...) {
                            if(finish) break;
                            if(failures[i]++ <= n_fail) {
                                chan.put(i);
                            }
                            else {
                                std::cout<<"failed job="<<i<<" failures="<<failures[i]<<'\n';
                                //failures[i] = 0;
                                throw;
                            }
                        }
                    }
                    work_done.open();
                });
            }
            scope.run_background("term", [&] {
                work_done.wait();
                chan.clear();
                finish = true;
                scope.terminate_now();
            });
            scope.run_background("prod", [&] {
                for(size_t i = 0; i < n_wrks; ++i) {
                    chan.put(i);
                }
                work_ready.open();
            });
            scope.wait();
        };
        return result;
    }

    template<class F> void produce_consume(const std::vector<std::string>& work, F worker, size_t n_fail = 30) {
        elle::With<Scope>() << [&] (Scope& scope) {
            Channel<std::string> chan;
            std::unordered_map<std::string, size_t> failures;
            Barrier work_ready;

            for(size_t i = 0; i < work.size(); ++i) {
                scope.run_background("worker" + std::to_string(i), [&, i] {
                    work_ready.wait();

                    while(!chan.empty()) {
                        auto job = chan.get();
                        try {
                            worker(job);
                            failures[job] = 0;
                        } catch(...) {
                            if(failures[job]++ <= n_fail) {
                                chan.put(job);
                            }
                            else {
                                std::cout<<"failed job="<<i<<'\n';
                                //failures[job] = 0;
                                throw;
                            }

                        }

                    }
                });
            }
            scope.run_background("prod", [&] {
                for(const auto& j: work) {
                    chan.put(j);
                }
                work_ready.open();
            });
            scope.wait();
        };
    }
};

}

#endif  /*MEXFW_H*/
