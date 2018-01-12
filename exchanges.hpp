/*
 * All exchanges and their parameters
 */

#ifndef EXCHANGES_H
#define EXCHANGES_H
#include <initializer_list>

struct CCEX {
    /*c-cex.com*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"btc", "doge", "ltc", "usd"};
    static constexpr auto delimeter = "-";
};

struct CEX {
    /*cex.io*/
    static constexpr auto fee = 0.0025;
    static constexpr auto bases = {"USD", "EUR", "GBP", "RUB", "BTC"};
    static constexpr auto delimeter = ":";
};

struct YOBIT {
    /*yobit.net*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"btc", "doge", "usd", "rur", "eth", "waves"};
    static constexpr auto delimeter = "_";
};

struct CRYPTOPIA {
    /*cryptopia.co.nz*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"BTC", "DOGE", "USDT", "NZDT", "LTC"};
    static constexpr auto delimeter = "/";
};

struct HITBTC {
    /*hitbtc.com*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"BTC", "ETH", "USD"};
    static constexpr auto delimeter = "";
};

struct EXMO {
    /*exmo.com*/
    static constexpr auto fee = 0.002;
    static constexpr auto bases = {"BTC", "ETH", "USD", "USDT", "RUB", "EUR", "UAH", "PLN", "LTC"};
    static constexpr auto delimeter = "_";
};

struct BITFINEX {
    /*bitfinex.com*/
};

struct LIVECOIN {
    /*livecoin.net*/
};

#endif  /*EXCHANGES_H*/
