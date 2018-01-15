/*
 * Class for testing proxy servers
 */

#ifndef TEST_PROXY_H
#define TEST_PROXY_H
#include <vector>
#include <string>
#include <rapidjson/document.h>
#include <elle/reactor/http/Request.hh>
#include <elle/Duration.hh>
#include <stdexcept>
#include "mexfw.hpp"


namespace mexfw {

using namespace elle::reactor;
using namespace elle::chrono_literals;

struct TEST_PROXY {};

template <>
class rest_api<TEST_PROXY>: public rest_api_base<TEST_PROXY> {
  public:
    void reset_counters() {
        ok_proxy_requests.clear();
        proxy_requests.clear();
    }

    void test_proxies(const std::string& url) {
        std::vector<std::string> work(proxies.size(), url);
        produce_consume(work, [this](auto _url) {
            auto proxy = this->get_proxy();
            http::Request::Configuration conf(5s, {}, http::Version::v11, true, proxy);
            http::Request r(_url, http::Method::GET, "application/json", conf);
            r.finalize();

            if(r.status() != http::StatusCode::OK)
                throw std::runtime_error("status code is not ok");

            auto resp = parse_str(r.response().string());
            this->ok_proxy_requests[proxy.host()]++;
        }, 1, true);
    }

    void filter_proxies() {
        decltype(proxies) good_proxies;

        for(const auto& p: proxies) {
            if(ok_proxy_requests[p.host()] > 0)
                good_proxies.push_back(p);
        }

        proxies = good_proxies;
    }

    void print_proxy_stats() {
        size_t ok_proxies_sz = 0;

        for(const auto& p : proxies) {
            if(proxy_requests[p.host()] != 0 && ok_proxy_requests[p.host()] != 0) {
                std::cout<<p<<" all_reqs="<<proxy_requests[p.host()]<<" ok_reqs="<<ok_proxy_requests[p.host()]<<'\n';
                ok_proxies_sz++;
            }
        }

        std::cout<<"Good proxies: "<<ok_proxies_sz<<" out of "<<proxy_requests.size()<<'\n';
    }

};
}

#endif  /*TEST_PROXY_H*/
