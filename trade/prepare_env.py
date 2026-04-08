#!/usr/bin/env python
import os
import pathlib

def get_api_key(config=None):
    with open(config.get("filepath", {}).get("path1"), 'rb') as f:
        config["api_key"] = f.read()
    with open(config.get("filepath", {}).get("path2"), 'rb') as f:
        #config["private_key"] = load_pem_private_key(f.read(), password=None)
        config["private_key"] = f.read()
    return config["api_key"], config["private_key"]


def get_api_key_read(config):
    return config.get("keys", {}).get("api_key"), config.get("keys", {}).get("api_secret")

proxies_env = { 'http': '',
            'https': '',}

private_key_pass_env = "hhjr"