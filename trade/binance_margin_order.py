#!/usr/bin/env python


from .binance_ppq.spot import Spot as Client
from .binance_ppq.lib.utils import config_logging
from .binance_ppq.error import ClientError
from trade.utils.prepare_env import get_api_key,proxies_env,private_key_pass_env

from  utils.logger import get_logger
logger = get_logger(__name__)

import json
# 全局配置
file_path = 'config.json'
config = {}
try:
    with open(file_path, 'r', encoding='utf-8') as file:
        config = json.load(file)
except FileNotFoundError:
    print(f"交易模块初始化错误: 文件 {file_path} 未找到。")
except json.JSONDecodeError:
    print(f"交易模块初始化错误: 文件 {file_path} 不是有效的 JSON 格式。")


def parase_api_key(api_key):
    # 提取第 45 到第 49 个字母
    sub_string = api_key[44:50]
    reversed_sub_string = sub_string[::-1]
    api_key = api_key[:44] + reversed_sub_string + api_key[50:]
    return api_key


api_key, api_secret = get_api_key(config)
client = Client(api_key = parase_api_key(api_key), private_key = parase_api_key(api_secret),base_url="https://api.binance.com",proxies=proxies_env,private_key_pass=private_key_pass_env)

# 杠杆借和还币
def borrow_repay(params):
    '''
    入参
    params = {
    "asset": "USDT",
    "isIsolated": "FALSE",
    "symbol": "ARUSDT",
    "amount": 4,
    "type": "REPAY",
    }
    '''

    try:
        logger.info(params)
        response = client.borrow_repay(**params)
        logger.info(response)
    except ClientError as error:
        logger.error(
            "Found error. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )

# 杠杆委托下单
def new_margin_order(params):
    # 杠杆下委托买AR2U  #NOTIONAL小于最小买入量
    '''
    入参
    params = {
        "symbol": "ARUSDT",
        "side": "BUY",
        "type": "LIMIT",
        "quantity": 2,
        "timeInForce": "GTC",
        "price": 4,
        "recvWindow": 5000,
    }
    '''

    try:
        logger.info(params)
        response = client.new_margin_order(**params)
        logger.info(response)
        # 返回完整响应，方便外部根据 status / 成交价格等字段做进一步处理
        return response
    except ClientError as error:
        logger.error(
            "Found error. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )

    '''
    返回数据实例
    {'symbol': 'ARUSDT', 'orderId': 2067071755, 'clientOrderId': 'zMbb8nuwoJedVPvmKp3DGk', 'transactTime': 1744197247898, 'price': '4', 'origQty': '2', 'executedQty': '0', 'cummulativeQuoteQty': '0', 'status': 'NEW', 'timeInForce': 'GTC', 'type': 'LIMIT', 'side': 'BUY', 'fills': [], 'isIsolated': False, 'selfTradePreventionMode': 'EXPIRE_MAKER'}
    '''
# 杠杆委托撤单
def cancel_margin_order(params):
    '''入参
    params = {
        "symbol": "ARUSDT",
        "orderId": 2067071755,
    }
    '''
    try:
        logger.info(params)
        response = client.cancel_margin_order(**params)
        logger.info(response)
        return response.get("status")
    except ClientError as error:
        logger.error(
            "Found error. status: {}, error code: {}, error message: {}".format(
                error.status_code, error.error_code, error.error_message
            )
        )
    '''
    返回数据实例
    {'orderId': '2067071755', 'symbol': 'ARUSDT', 'origClientOrderId': 'zMbb8nuwoJedVPvmKp3DGk', 'clientOrderId': 'N9xsQgsNsXky4G4N3KpWeh', 'transactTime': 1744197403001, 'price': '4', 'origQty': '2', 'executedQty': '0', 'cummulativeQuoteQty': '0', 'status': 'CANCELED', 'timeInForce': 'GTC', 'type': 'LIMIT', 'side': 'BUY', 'isIsolated': False, 'selfTradePreventionMode': 'EXPIRE_MAKER'}
    '''

if __name__ == "__main__":
    # 杠杆下委托买AR2U  #NOTIONAL小于最小买入量
    params = {
        "symbol": "STXUSDT",
        "orderId": 2305037495,
    }
    response = cancel_margin_order(params)
