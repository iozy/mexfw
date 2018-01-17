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
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>
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
    bool use_proxy;

public:
    rest_api_base(): use_proxy(true) {}
    // API keys and nonces
    //void load_keys(const auto& filename) {}
    //void add_key(){}
    //void rm_key(){}
    //std::string get_priv_key(const auto& key, bool switch = true) {}


    // Proxy tools
    void load_proxies(const std::string& filename = "proxies.json") {
        proxies.clear();

        if(!file_exists(filename)) {
            proxies.push_back({network::ProxyType::None, {}, {}});
            return;
        }

        auto proxy_file = parse_file(filename);

//       Document xx;
//       xx["test"].GetArray()[0].GetUint();
        for(const auto& p : proxy_file.GetArray()) {
            std::string ptype = p.GetObject()["types"].GetArray()[0].GetObject()["type"].GetString();
            network::ProxyType proxy_type = ptype == "HTTP" ? network::ProxyType::HTTP :
                                            ptype == "HTTPS" ? network::ProxyType::HTTPS :
                                            ptype == "SOCKS5" ? network::ProxyType::SOCKS : network::ProxyType::None;
            proxies.push_back(network::Proxy(proxy_type, p.GetObject()["host"].GetString(), p.GetObject()["port"].GetUint()));
        }
    }

    void save_proxies(const std::string& filename = "good_proxies.json") {
        Document d;
        auto& alloc = d.GetAllocator();
        d.SetArray();

        for(const auto& p : proxies) {
            //if(ok_proxy_requests[p.host()] == 0) continue;
            const char* proxy_type = p.type() == network::ProxyType::HTTP ? "HTTP" :
                                     p.type() == network::ProxyType::HTTPS ? "HTTPS" :
                                     p.type() == network::ProxyType::SOCKS ? "SOCKS5" : "None";
            Value v(kObjectType);
            v.AddMember("host", StringRef(p.host().c_str()), alloc);
            v.AddMember("port", Value().SetUint(p.port()), alloc);
            Value va(kArrayType);
            Value vt(kObjectType);
            vt.AddMember("type", StringRef(proxy_type), alloc);
            va.PushBack(vt, alloc);
            v.AddMember("types", va, alloc);
            d.PushBack(v, alloc);
        }

        save_json(d, filename);
    }

    void toggle_use_proxy() {
        use_proxy = !use_proxy;
    }

    network::Proxy get_proxy(bool switch_ = true) {
        if(proxies.size() == 0 || !use_proxy) {
            return network::Proxy(network::ProxyType::None, {}, {});
        } else {
            if(switch_) {
                std::rotate(proxies.begin(), std::next(proxies.begin(), 1), proxies.end());
            }

            auto proxy = *proxies.begin();
            proxy_requests[proxy.host()]++;
            return proxy;
        }
    }

    auto current_proxy() {
        return proxies.begin()->host();
    }

    // Cookie routines
    //void load_cookies() {}
    //void save_cookies() {}

    // Patterns and routines to do async work
protected:
    template<class F>
    auto first_wins2(F worker = {}, size_t n_wrks = 4, size_t n_fail = 30, bool no_throw = false) {
        decltype(worker()) result;
        elle::With<Scope>() << [&, this] (Scope & scope) {
            Channel<size_t> chan;
            std::unordered_map<size_t, size_t> failures;
            Barrier work_ready, work_done;
            bool finish = false;

            for(size_t i = 0; i < n_wrks; ++i) {
                scope.run_background("worker" + std::to_string(i), [&, i, this] {
                    work_ready.wait();

                    while(!chan.empty()) {
                        try {
                            result = worker();
                            work_done.open();

                            break;
                        } catch(...) {
                            if(failures[i]++ < n_fail - 1) {
                                chan.put(i);
                            }
                            else {
                                //std::cout << "failed job=" << i << " failures=" << failures[i] << '\n';
                                //failures[i] = 0;
                                if(!no_throw) throw;
                            }
                        }
                    }
                });
            }

            scope.run_background("prod", [&] {
                for(size_t i = 0; i < n_wrks; ++i) {
                    chan.put(i);
                }
                work_ready.open();
            });
            scope.run_background("term", [&] {
                work_done.wait();
                chan.clear();
                scope.terminate_now();
            });

            scope.wait();
        };
        return result;
    }

    template<class F>
    auto first_wins(F worker = {}, size_t n_wrks = 4, size_t n_fail = 30, bool no_throw = false) {
        decltype(worker()) result;
        elle::With<Scope>() << [&, this] (Scope & scope) {
            Channel<size_t> chan;
            std::unordered_map<size_t, size_t> failures;
            Barrier work_ready, work_done;
            bool finish = false;

            for(size_t i = 0; i < n_wrks; ++i) {
                scope.run_background("worker" + std::to_string(i), [&, i, this] {
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

                            if(failures[i]++ < n_fail - 1) {
                                chan.put(i);
                            }
                            else {
                                //std::cout << "failed job=" << i << " failures=" << failures[i] << '\n';
                                //failures[i] = 0;
                                if(!no_throw) throw;
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

    template<class F> void produce_consume(const std::vector<std::string>& work, F worker, size_t n_fail = 30, bool no_throw = false) {
        elle::With<Scope>() << [&, this] (Scope & scope) {
            Channel<std::string> chan;
            std::unordered_map<std::string, size_t> failures;
            Barrier work_ready;

            for(size_t i = 0; i < work.size(); ++i) {
                scope.run_background("worker" + std::to_string(i), [&, i, this] {
                    work_ready.wait();

                    while(!chan.empty()) {
                        auto job = chan.get();

                        try {
                            worker(job);
                            failures[job] = 0;
                        } catch(...) {
                            if(failures[job]++ < n_fail - 1) {
                                chan.put(job);
                            }
                            else {
                                //std::cout << "failed job=" << i << '\n';
                                //failures[job] = 0;
                                if(!no_throw) throw;
                            }
                        }
                    }
                });
            }

            scope.run_background("prod", [&] {
                for(const auto& j : work) {
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
