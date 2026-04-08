#!/usr/bin/env python
# 当前文件需要支持单个文件测试，所以需要引入全局配置
import os
import sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))



from .binance_ppq.spot import Spot as Client # pip install binance-connector
from .binance_ppq.lib.utils import config_logging
from trade.utils.prepare_env import get_api_key_read,proxies_env

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

api_key, api_secret = get_api_key_read(config)

def parase_api_key(api_key):
    # 提取第 45 到第 49 个字母
    sub_string = api_key[44:50]
    reversed_sub_string = sub_string[::-1]
    api_key = api_key[:44] + reversed_sub_string + api_key[50:]
    return api_key
# 本地client = Client(parase_api_key(api_key), parase_api_key(api_secret), base_url="https://api.binance.com",proxies=proxies_env)
# 服务器client = Client(parase_api_key(api_key), bytes(parase_api_key(api_secret),'utf-8'), base_url="https://api.binance.com",proxies=proxies_env)
client = Client(parase_api_key(api_key), bytes(parase_api_key(api_secret),'utf-8'), base_url="https://api.binance.com",proxies=proxies_env)
#查询现货账户信息
def account():
    try:
        params = {"omitZeroBalances": "true"}
        logger.info(client.account(**params))
    except Exception as e:
        logger.error(f"Error: {e}")
# 查询杠杆账户资产
def margin_account():
    try:  
        response = client.margin_account()
        '''
        过滤为零的资产"userAssets": [
        '''
        response['userAssets'] = [
            asset for asset in response['userAssets']
            if float(asset['netAsset']) != 0
        ]
        logger.info(response)

    except Exception as e:
        logger.error(f"Error: {e}")

# 查询杠杆可转margin_max_transferable
def margin_max_transferable():
    try:
        params = {"asset": "AR"}
        logger.info(client.margin_max_transferable(**params))
    except Exception as e:
        logger.error(f"Error: {e}")

# 查询杠杆可借margin_max_borrowable
def margin_max_borrowable():
    try:
        params = {"asset": "AR"}
        logger.info(client.margin_max_borrowable(**params))
    except Exception as e:
        logger.error(f"Error: {e}")

# 查询杠杆账户挂单记录
def margin_open_orders():
    try:
        params = {"symbol": "ARUSDT"}
        logger.info(client.margin_open_orders())
    except Exception as e:
        logger.error(f"Error: {e}")

# 查询杠杆账户委托（按币种）
def margin_all_orders():
    try:
        params = {"symbol": "ARUSDT",
                   #"orderId": 12345678
                   }
        #logging.info(client.margin_order(**params))  #Either orderId or origClientOrderId must be sent.
        logger.info(client.margin_all_orders(**params))
    except Exception as e:
        logger.error(f"Error: {e}")

# 通过orderid查询杠杆账户委托（按币种）
def margin_one_orders(params):
    try:
        #logging.info(client.margin_order(**params))  #Either orderId or origClientOrderId must be sent.
        response = client.margin_order(**params)
        logger.info(response)
        return response
    except Exception as e:
        logger.error(f"Error: {e}")
        # {'symbol': 'ARUSDT', 'orderId': 2121162486, 'clientOrderId': 'nTgjRFnFZUI80H6STkTOED', 'price': '6.42', 'origQty': '7.7', 'executedQty': '7.7', 'cummulativeQuoteQty': '49.665', 'status': 'FILLED', 'timeInForce': 'GTC', 'type': 'LIMIT', 'side': 'SELL', 'stopPrice': '0', 'icebergQty': '0', 'time': 1749088654141, 'updateTime': 1749088654141, 'isWorking': True, 'isIsolated': False, 'selfTradePreventionMode': 'EXPIRE_MAKER'}
 # NEW	订单被交易引擎接  PARTIALLY_FILLED	部分订单被成交  FILLED	订单完全成交  CANCELED	用户撤销了订单 REJECTED	订单没有被交易引擎接受，也没被处理

#获取杠杆账户划转历史
def margin_transfer_history():
    try:
        params = {
            "asset": "USDT",
        }
        logger.info(client.margin_transfer_history(**params))
    except Exception as e:
        logger.error(f"Error: {e}")
#获取杠杆账户借和还历史
def borrow_repay_record():
    try:
        #type	STRING	YES	操作类型：BORROW、REPAY
        params = {
            "type": "BORROW",
        }
        logger.info(client.borrow_repay_record(**params))
    except Exception as e:
        logger.error(f"Error: {e}")




if __name__ == "__main__":
    params = {"symbol": "UNIUSDT",
                   "orderId": 4029755587
                   }
    margin_one_orders(params)

