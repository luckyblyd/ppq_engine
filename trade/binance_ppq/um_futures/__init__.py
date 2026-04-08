from binance_ppq.api import API


class UMFutures(API):
    def __init__(self, key=None, secret=None, **kwargs):
        if "base_url" not in kwargs:
            kwargs["base_url"] = "https://fapi.binance.com"
        super().__init__(key, secret, **kwargs)

    # MARKETS
    from binance_ppq.um_futures.market import ping
    from binance_ppq.um_futures.market import time
    from binance_ppq.um_futures.market import exchange_info
    from binance_ppq.um_futures.market import depth
    from binance_ppq.um_futures.market import trades
    from binance_ppq.um_futures.market import historical_trades
    from binance_ppq.um_futures.market import agg_trades
    from binance_ppq.um_futures.market import klines
    from binance_ppq.um_futures.market import continuous_klines
    from binance_ppq.um_futures.market import index_price_klines
    from binance_ppq.um_futures.market import mark_price_klines
    from binance_ppq.um_futures.market import mark_price
    from binance_ppq.um_futures.market import funding_rate
    from binance_ppq.um_futures.market import funding_info
    from binance_ppq.um_futures.market import ticker_24hr_price_change
    from binance_ppq.um_futures.market import ticker_price
    from binance_ppq.um_futures.market import book_ticker
    from binance_ppq.um_futures.market import quarterly_contract_settlement_price
    from binance_ppq.um_futures.market import open_interest
    from binance_ppq.um_futures.market import open_interest_hist
    from binance_ppq.um_futures.market import top_long_short_position_ratio
    from binance_ppq.um_futures.market import long_short_account_ratio
    from binance_ppq.um_futures.market import top_long_short_account_ratio
    from binance_ppq.um_futures.market import taker_long_short_ratio
    from binance_ppq.um_futures.market import blvt_kline
    from binance_ppq.um_futures.market import index_info
    from binance_ppq.um_futures.market import asset_Index
    from binance_ppq.um_futures.market import index_price_constituents

    # ACCOUNT(including orders and trades)
    from binance_ppq.um_futures.account import change_position_mode
    from binance_ppq.um_futures.account import get_position_mode
    from binance_ppq.um_futures.account import change_multi_asset_mode
    from binance_ppq.um_futures.account import get_multi_asset_mode
    from binance_ppq.um_futures.account import new_order
    from binance_ppq.um_futures.account import new_order_test
    from binance_ppq.um_futures.account import modify_order
    from binance_ppq.um_futures.account import new_batch_order
    from binance_ppq.um_futures.account import query_order
    from binance_ppq.um_futures.account import cancel_order
    from binance_ppq.um_futures.account import cancel_open_orders
    from binance_ppq.um_futures.account import cancel_batch_order
    from binance_ppq.um_futures.account import countdown_cancel_order
    from binance_ppq.um_futures.account import get_open_orders
    from binance_ppq.um_futures.account import get_orders
    from binance_ppq.um_futures.account import get_all_orders
    from binance_ppq.um_futures.account import balance
    from binance_ppq.um_futures.account import account
    from binance_ppq.um_futures.account import change_leverage
    from binance_ppq.um_futures.account import change_margin_type
    from binance_ppq.um_futures.account import modify_isolated_position_margin
    from binance_ppq.um_futures.account import get_position_margin_history
    from binance_ppq.um_futures.account import get_position_risk
    from binance_ppq.um_futures.account import get_account_trades
    from binance_ppq.um_futures.account import get_income_history
    from binance_ppq.um_futures.account import leverage_brackets
    from binance_ppq.um_futures.account import adl_quantile
    from binance_ppq.um_futures.account import force_orders
    from binance_ppq.um_futures.account import api_trading_status
    from binance_ppq.um_futures.account import commission_rate
    from binance_ppq.um_futures.account import futures_account_configuration
    from binance_ppq.um_futures.account import symbol_configuration
    from binance_ppq.um_futures.account import query_user_rate_limit
    from binance_ppq.um_futures.account import download_transactions_asyn
    from binance_ppq.um_futures.account import aysnc_download_info
    from binance_ppq.um_futures.account import download_order_asyn
    from binance_ppq.um_futures.account import async_download_order_id
    from binance_ppq.um_futures.account import download_trade_asyn
    from binance_ppq.um_futures.account import async_download_trade_id
    from binance_ppq.um_futures.account import toggle_bnb_burn
    from binance_ppq.um_futures.account import get_bnb_burn
    from binance_ppq.um_futures.account import new_algo_order
    from binance_ppq.um_futures.account import cancel_algo_order
    from binance_ppq.um_futures.account import get_open_algo_orders

    # CONVERT
    from binance_ppq.um_futures.convert import list_all_convert_pairs
    from binance_ppq.um_futures.convert import send_quote_request
    from binance_ppq.um_futures.convert import accept_offered_quote
    from binance_ppq.um_futures.convert import order_status

    # STREAMS
    from binance_ppq.um_futures.data_stream import new_listen_key
    from binance_ppq.um_futures.data_stream import renew_listen_key
    from binance_ppq.um_futures.data_stream import close_listen_key
