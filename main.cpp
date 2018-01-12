#include <iostream>
#include <fstream>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <elle/reactor/scheduler.hh>
#include <elle/reactor/Thread.hh>
#include <elle/reactor/http/Request.hh>
#include "exchanges.hpp"
#include "mexfw.hpp"
#include "goodies.hpp"

using namespace elle::reactor;
using namespace rapidjson;
using namespace mexfw;
using namespace mexfw::utils;

int main(void) {
    Scheduler sched;
    rest_api_base<bool> x;
    x.load_proxies("proxies.json");

    for(size_t i = 0; i < 5; ++i) {
        auto proxy = x.get_proxy();
        std::cout << proxy.host() << '\n';
    }

    Thread main_thread(sched, "main thread", [] {
        http::Request r("https://yobit.net/api/3/info", http::Method::GET, "application/json");
        Document d;
        d.Parse(r.response().string().c_str());
        for(const auto& i : d["pairs"].GetObject())
            std::cout << i.name.GetString() << '\n';
    });
    sched.run();
    return 0;
}
